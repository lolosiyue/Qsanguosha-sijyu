#include "android-zip-reader.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStorageInfo>

#include <limits>
#include <sys/stat.h>
#include <zlib.h>

namespace {
constexpr quint64 kBufferSize = 256 * 1024;
constexpr quint64 kSpoolReserve = 64 * 1024 * 1024;

bool addChecked(quint64 a, quint64 b, quint64 *out)
{
    if (b > std::numeric_limits<quint64>::max() - a) return false;
    *out = a + b; return true;
}
bool within(quint64 offset, quint64 length, quint64 total)
{ return offset <= total && length <= total - offset; }
quint16 u16(const QByteArray &b, qsizetype p)
{ return quint16(uchar(b[p])) | (quint16(uchar(b[p + 1])) << 8); }
quint32 u32(const QByteArray &b, qsizetype p)
{ return quint32(u16(b, p)) | (quint32(u16(b, p + 2)) << 16); }
quint64 u64(const QByteArray &b, qsizetype p)
{ return quint64(u32(b, p)) | (quint64(u32(b, p + 4)) << 32); }

bool readAt(QIODevice &device, quint64 offset, qsizetype size, QByteArray *out)
{
    if (offset > quint64(std::numeric_limits<qint64>::max()) || !device.seek(qint64(offset))) return false;
    out->clear(); out->reserve(size);
    while (out->size() < size) {
        const QByteArray chunk = device.read(size - out->size());
        if (chunk.isEmpty()) return false;
        out->append(chunk);
    }
    return true;
}

bool validUtf8(const QByteArray &value)
{
    const uchar *p = reinterpret_cast<const uchar *>(value.constData()), *end = p + value.size();
    while (p < end) {
        quint32 code = 0; int continuation = 0;
        if (*p < 0x80) { ++p; continue; }
        if (*p >= 0xc2 && *p <= 0xdf) { code = *p & 0x1f; continuation = 1; }
        else if (*p >= 0xe0 && *p <= 0xef) { code = *p & 0x0f; continuation = 2; }
        else if (*p >= 0xf0 && *p <= 0xf4) { code = *p & 0x07; continuation = 3; }
        else return false;
        if (end - p <= continuation) return false;
        for (int i = 1; i <= continuation; ++i) {
            if ((p[i] & 0xc0) != 0x80) return false;
            code = (code << 6) | (p[i] & 0x3f);
        }
        if ((continuation == 2 && code < 0x800) || (continuation == 3 && code < 0x10000)
            || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
        p += continuation + 1;
    }
    return true;
}

bool safePath(const QByteArray &raw, QString *normal)
{
    if (raw.isEmpty() || raw.size() > 4096 || raw.contains('\0') || raw.contains('\\') || !validUtf8(raw)) return false;
    const QString decoded = QString::fromUtf8(raw.constData(), raw.size());
    if (decoded.startsWith('/') || decoded.startsWith('\\') || decoded.contains(':')) return false;
    const bool directory = decoded.endsWith('/');
    QStringList parts = decoded.split('/', Qt::KeepEmptyParts);
    if (directory) parts.removeLast();
    if (parts.isEmpty()) return false;
    for (const QString &part : parts) {
        if (part.isEmpty() || part.toUtf8().size() > 255 || part == QStringLiteral(".")
            || part == QStringLiteral("..") || part.endsWith('.') || part.endsWith(' ')) return false;
        for (QChar c : part) if (c.unicode() < 32) return false;
    }
    *normal = decoded.normalized(QString::NormalizationForm_C); return true;
}

bool zip64Extra(const QByteArray &extra, bool needUncompressed, bool needCompressed, bool needOffset,
                quint64 *uncompressed, quint64 *compressed, quint64 *offset)
{
    qsizetype pos = 0; bool found = false;
    while (pos < extra.size()) {
        if (extra.size() - pos < 4) return false;
        const quint16 id = u16(extra, pos), length = u16(extra, pos + 2); pos += 4;
        if (length > extra.size() - pos) return false;
        if (id == 1) {
            if (found) return false;
            found = true; qsizetype cursor = pos;
            const qsizetype fieldEnd = pos + length;
            // ZIP64 values must fit this field, not borrow bytes from the next one.
            auto take = [&](bool needed, quint64 *value) { if (!needed) return true; if (fieldEnd - cursor < 8) return false; *value = u64(extra, cursor); cursor += 8; return true; };
            if (!take(needUncompressed, uncompressed) || !take(needCompressed, compressed) || !take(needOffset, offset)) return false;
        }
        pos += length;
    }
    return (!needUncompressed && !needCompressed && !needOffset) || found;
}
}

bool AndroidZipReader::fail(QString *error, const QString &message)
{ if (error) *error = message; return false; }

AndroidZipReader::AndroidZipReader(const QString &spoolDirectory)
{
    // Android imports must spool on the app's writable private filesystem.
    // Keep Qt's default temporary location for standalone desktop callers.
    if (!spoolDirectory.isEmpty())
        m_spool.setFileTemplate(spoolDirectory + QStringLiteral("/archive-XXXXXX.zip"));
}

bool AndroidZipReader::open(QIODevice &source, const AndroidContentStore::ImportLimits &limits,
                            std::atomic_bool *cancel, QString *error)
{
    m_entries.clear(); m_archiveSize = 0; m_centralOffset = 0; m_limits = limits;
    if (m_spool.isOpen()) m_spool.close();
    if (!m_spool.open() || !m_spool.resize(0))
        return fail(error, QStringLiteral("cannot create ZIP spool: ") + m_spool.errorString());
    QByteArray buffer(int(kBufferSize), Qt::Uninitialized);
    for (;;) {
        if (cancel && cancel->load()) return fail(error, QStringLiteral("import cancelled"));
        const qint64 n = source.read(buffer.data(), buffer.size());
        if (n < 0) return fail(error, source.errorString());
        if (!n) { if (!source.atEnd()) return fail(error, QStringLiteral("source made no progress")); break; }
        if (!addChecked(m_archiveSize, quint64(n), &m_archiveSize)) return fail(error, QStringLiteral("archive size overflow"));
        if (m_limits.maxArchiveBytes && m_archiveSize > m_limits.maxArchiveBytes) return fail(error, QStringLiteral("archive size limit exceeded"));
        const QStorageInfo storage(QFileInfo(m_spool.fileName()).absolutePath());
        if (storage.isValid() && storage.bytesAvailable() < qint64(kSpoolReserve)) return fail(error, QStringLiteral("insufficient spool storage"));
        qint64 written = 0;
        while (written < n) {
            const qint64 chunk = m_spool.write(buffer.constData() + written, n - written);
            if (chunk <= 0) return fail(error, m_spool.errorString());
            written += chunk;
        }
    }
    if (!m_spool.flush() || m_archiveSize < 22) return fail(error, QStringLiteral("invalid ZIP archive"));
    const quint64 tailSize = qMin<quint64>(m_archiveSize, 65557); QByteArray tail;
    if (!readAt(m_spool, m_archiveSize - tailSize, qsizetype(tailSize), &tail)) return fail(error, QStringLiteral("cannot read ZIP footer"));
    qsizetype eocd = -1;
    for (qsizetype p = tail.size() - 22; p >= 0; --p)
        if (tail.mid(p, 4) == QByteArray("PK\x05\x06", 4) && quint64(p + 22 + u16(tail, p + 20)) == quint64(tail.size())) { eocd = p; break; }
    if (eocd < 0) return fail(error, QStringLiteral("ZIP end record missing or trailing data"));
    const quint16 disk = u16(tail, eocd + 4), centralDisk = u16(tail, eocd + 6);
    const quint16 diskCount = u16(tail, eocd + 8), centralCount = u16(tail, eocd + 10);
    if (disk || centralDisk || diskCount != centralCount) return fail(error, QStringLiteral("multipart ZIP is unsupported"));
    quint64 count = centralCount, centralSize = u32(tail, eocd + 12), centralOffset = u32(tail, eocd + 16);
    quint64 footerOffset = m_archiveSize - tailSize + quint64(eocd);
    if (count == 0xffff || centralSize == 0xffffffffU || centralOffset == 0xffffffffU) {
        const qsizetype locator = tail.lastIndexOf(QByteArray("PK\x06\x07", 4), eocd - 1);
        if (locator < 0 || locator + 20 != eocd || u32(tail, locator + 4) != 0 || u32(tail, locator + 16) != 1) return fail(error, QStringLiteral("invalid ZIP64 locator"));
        QByteArray record; const quint64 recordOffset = u64(tail, locator + 8);
        if (!readAt(m_spool, recordOffset, 56, &record) || record.left(4) != QByteArray("PK\x06\x06", 4)) return fail(error, QStringLiteral("invalid ZIP64 end record"));
        const quint64 recordSize = u64(record, 4); quint64 recordEnd = 0, recordLength = 0;
        if (recordSize < 44 || !addChecked(recordSize, 12, &recordLength)
            || !addChecked(recordOffset, recordLength, &recordEnd)
            || recordEnd != m_archiveSize - tailSize + quint64(locator)) return fail(error, QStringLiteral("invalid ZIP64 end size"));
        footerOffset = recordOffset;
        if (u32(record, 16) || u32(record, 20) || u64(record, 24) != u64(record, 32)) return fail(error, QStringLiteral("multipart ZIP64 is unsupported"));
        count = u64(record, 32); centralSize = u64(record, 40); centralOffset = u64(record, 48);
    }
    if (!within(centralOffset, centralSize, footerOffset)) return fail(error, QStringLiteral("central directory exceeds archive"));
    m_centralOffset = centralOffset;
    const quint64 maxEntries = m_limits.maxEntries ? m_limits.maxEntries : 100000;
    if (count > maxEntries || count > quint64(std::numeric_limits<int>::max())) return fail(error, QStringLiteral("ZIP entry count limit exceeded"));
    if (!m_spool.seek(qint64(centralOffset))) return fail(error, QStringLiteral("cannot seek central directory"));
    QSet<QString> names, filePaths, directoryPaths;
    QHash<QString, QString> pathComponents;
    quint64 cursor = centralOffset, expandedTotal = 0, metadataBytes = 0;
    for (quint64 i = 0; i < count; ++i) {
        if (cancel && cancel->load()) return fail(error, QStringLiteral("import cancelled"));
        QByteArray header = m_spool.read(46);
        if (header.size() != 46 || header.left(4) != QByteArray("PK\x01\x02", 4)) return fail(error, QStringLiteral("invalid central entry"));
        const quint16 madeBy = u16(header, 4), flags = u16(header, 8), method = u16(header, 10);
        const quint16 nameLength = u16(header, 28), extraLength = u16(header, 30), commentLength = u16(header, 32);
        metadataBytes += quint64(nameLength) + extraLength + commentLength + 46;
        if (metadataBytes > 16 * 1024 * 1024) return fail(error, QStringLiteral("ZIP metadata memory limit exceeded"));
        if (u16(header, 34) != 0) return fail(error, QStringLiteral("multipart ZIP entry is unsupported"));
        if ((flags & 1) || (flags & 0x20) || (flags & 0x40) || method != 0 && method != 8) return fail(error, QStringLiteral("encrypted, multipart, or unsupported ZIP entry"));
        const QByteArray rawName = m_spool.read(nameLength), extra = m_spool.read(extraLength);
        if (rawName.size() != nameLength || extra.size() != extraLength || m_spool.read(commentLength).size() != commentLength) return fail(error, QStringLiteral("truncated central entry"));
        QString path; if (!safePath(rawName, &path)) return fail(error, QStringLiteral("unsafe or invalid UTF-8 ZIP path"));
        QString collisionPath = path; if (collisionPath.endsWith('/')) collisionPath.chop(1);
        const QString key = collisionPath.toCaseFolded(); if (names.contains(key)) return fail(error, QStringLiteral("ZIP path collision: %1").arg(path)); names.insert(key);
        QString prefix;
        const QStringList parts = collisionPath.split('/');
        for (qsizetype component = 0; component < parts.size(); ++component) {
            if (!prefix.isEmpty()) prefix += '/';
            prefix += parts[component];
            const QString folded = prefix.toCaseFolded();
            if (pathComponents.contains(folded) && pathComponents.value(folded) != prefix)
                return fail(error, QStringLiteral("ZIP directory case collision: %1").arg(path));
            pathComponents.insert(folded, prefix);
            const bool isDirectory = component + 1 < parts.size() || path.endsWith('/');
            if ((isDirectory && filePaths.contains(folded)) || (!isDirectory && directoryPaths.contains(folded)))
                return fail(error, QStringLiteral("ZIP file/directory collision: %1").arg(path));
            if (isDirectory) directoryPaths.insert(folded); else filePaths.insert(folded);
        }
        quint64 compressed = u32(header, 20), expanded = u32(header, 24), offset = u32(header, 42);
        const bool needExpanded = expanded == 0xffffffffU, needCompressed = compressed == 0xffffffffU, needOffset = offset == 0xffffffffU;
        if (!zip64Extra(extra, needExpanded, needCompressed, needOffset, &expanded, &compressed, &offset)) return fail(error, QStringLiteral("incomplete ZIP64 extra field"));
        // Central and local extra fields need not have the same lengths.
        // extract() reads and bounds the actual local fields before any write.
        if (!within(offset, 30, m_centralOffset)) return fail(error, QStringLiteral("local header outside archive"));
        const quint32 attributes = u32(header, 38); const bool directory = path.endsWith('/'); const quint8 host = quint8(madeBy >> 8);
        if (host == 3) { const quint32 mode = attributes >> 16; const quint32 type = mode & S_IFMT; if (type != 0 && type != S_IFREG && type != S_IFDIR) return fail(error, QStringLiteral("non-regular ZIP entry")); }
        if (m_limits.maxEntryBytes && expanded > m_limits.maxEntryBytes) return fail(error, QStringLiteral("entry size limit exceeded"));
        if (method == 0 && compressed != expanded) return fail(error, QStringLiteral("stored ZIP entry size mismatch"));
        if (m_limits.maxCompressionRatio && expanded > 0 && (compressed == 0 || (expanded > compressed && (expanded - 1) / compressed >= m_limits.maxCompressionRatio))) return fail(error, QStringLiteral("compression ratio limit exceeded"));
        if (!addChecked(expandedTotal, expanded, &expandedTotal) || (m_limits.maxExpandedBytes && expandedTotal > m_limits.maxExpandedBytes)) return fail(error, QStringLiteral("expanded size limit exceeded"));
        Entry entry; entry.path = path; entry.originalName = QString::fromUtf8(rawName); entry.rawName = rawName; entry.flags = flags; entry.method = method; entry.externalAttributes = attributes; entry.compressedSize = compressed; entry.uncompressedSize = expanded; entry.crcValue = u32(header, 16); entry.localHeaderOffset = offset; entry.directory = directory; m_entries.append(entry);
        quint64 next = 0; if (!addChecked(cursor, 46 + nameLength + extraLength + commentLength, &next) || next > centralOffset + centralSize) return fail(error, QStringLiteral("central entry exceeds central directory")); cursor = next;
    }
    if (cursor != centralOffset + centralSize) return fail(error, QStringLiteral("central directory size mismatch"));
    return true;
}

bool AndroidZipReader::extract(const Entry &entry, QIODevice &destination, std::atomic_bool *cancel,
                               const AndroidContentStore::Progress &progress, QString *error)
{
    bool knownEntry = false;
    for (const Entry &candidate : m_entries) {
        if (candidate.localHeaderOffset == entry.localHeaderOffset && candidate.rawName == entry.rawName
            && candidate.compressedSize == entry.compressedSize && candidate.uncompressedSize == entry.uncompressedSize) {
            knownEntry = true; break;
        }
    }
    if (!knownEntry) return fail(error, QStringLiteral("unknown ZIP entry"));
    if (entry.directory) return true;
    QByteArray local; if (!readAt(m_spool, entry.localHeaderOffset, 30, &local) || local.left(4) != QByteArray("PK\x03\x04", 4)) return fail(error, QStringLiteral("local header mismatch"));
    const quint16 localFlags = u16(local, 6), localMethod = u16(local, 8), nameLength = u16(local, 26), extraLength = u16(local, 28);
    QByteArray localName, localExtra; quint64 nameOffset = 0, dataOffset = 0;
    if (!addChecked(entry.localHeaderOffset, 30, &nameOffset) || !readAt(m_spool, nameOffset, nameLength, &localName) || !addChecked(nameOffset, nameLength, &dataOffset) || !readAt(m_spool, dataOffset, extraLength, &localExtra) || !addChecked(dataOffset, extraLength, &dataOffset) || !within(dataOffset, entry.compressedSize, m_centralOffset)) return fail(error, QStringLiteral("truncated local header or payload"));
    if (localName != entry.rawName || localFlags != entry.flags || localMethod != entry.method) return fail(error, QStringLiteral("local header does not match central entry"));
    const quint32 localCrc = u32(local, 14); quint64 localCompressed = u32(local, 18), localExpanded = u32(local, 22); const bool descriptor = (entry.flags & 8) != 0;
    const bool needLocalCompressed = localCompressed == 0xffffffffU, needLocalExpanded = localExpanded == 0xffffffffU;
    if ((needLocalCompressed || needLocalExpanded) && !zip64Extra(localExtra, needLocalExpanded, needLocalCompressed, false, &localExpanded, &localCompressed, nullptr)) return fail(error, QStringLiteral("incomplete local ZIP64 extra field"));
    if (!descriptor && (localCrc != entry.crcValue || localCompressed != entry.compressedSize || localExpanded != entry.uncompressedSize)) return fail(error, QStringLiteral("local header size or CRC mismatch"));
    if (descriptor && localCrc && localCrc != 0xffffffffU && localCrc != entry.crcValue) return fail(error, QStringLiteral("local header CRC mismatch"));
    if (descriptor && localCompressed && localCompressed != 0xffffffffU && localCompressed != entry.compressedSize) return fail(error, QStringLiteral("local header compressed size mismatch"));
    if (descriptor && localExpanded && localExpanded != 0xffffffffU && localExpanded != entry.uncompressedSize) return fail(error, QStringLiteral("local header expanded size mismatch"));
    if (!m_spool.seek(qint64(dataOffset))) return fail(error, QStringLiteral("cannot seek ZIP payload"));
    QByteArray input(int(kBufferSize), Qt::Uninitialized), output(int(kBufferSize), Qt::Uninitialized); quint64 left = entry.compressedSize, written = 0; uLong crc = crc32(0L, Z_NULL, 0); z_stream stream{}; bool ended = entry.method == 0;
    if (entry.method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK) return fail(error, QStringLiteral("cannot initialize deflate"));
    auto writeAll = [&](const char *data, qint64 size) { qint64 done = 0; while (done < size) { if (cancel && cancel->load()) return false; const qint64 n = destination.write(data + done, size - done); if (n <= 0) return false; done += n; } return true; };
    auto writeChunk = [&](qint64 size) { if (!size) return true; quint64 next = 0; if (!addChecked(written, quint64(size), &next) || next > entry.uncompressedSize) return false; crc = crc32(crc, reinterpret_cast<const Bytef *>(output.constData()), uInt(size)); if (!writeAll(output.constData(), size)) return false; written = next; if (progress) progress(written, entry.uncompressedSize); return true; };
    while (left || !ended) {
        if (cancel && cancel->load()) { if (entry.method == 8) inflateEnd(&stream); return fail(error, QStringLiteral("import cancelled")); }
        if (entry.method == 0) { const qint64 n = m_spool.read(output.data(), qMin<quint64>(left, kBufferSize)); if (n <= 0) { if (entry.method == 8) inflateEnd(&stream); return fail(error, QStringLiteral("truncated ZIP payload")); } left -= quint64(n); if (!writeChunk(n)) return fail(error, QStringLiteral("cannot write staged entry")); continue; }
        if (!stream.avail_in && left) { const qint64 n = m_spool.read(input.data(), qMin<quint64>(left, kBufferSize)); if (n <= 0) { inflateEnd(&stream); return fail(error, QStringLiteral("truncated ZIP payload")); } left -= quint64(n); stream.next_in = reinterpret_cast<Bytef *>(input.data()); stream.avail_in = uInt(n); }
        const uInt beforeIn = stream.avail_in; const quint64 beforeWritten = written; stream.next_out = reinterpret_cast<Bytef *>(output.data()); stream.avail_out = uInt(output.size()); const int result = inflate(&stream, Z_NO_FLUSH);
        if (!writeChunk(output.size() - stream.avail_out)) { inflateEnd(&stream); return fail(error, QStringLiteral("entry expansion limit or write failure")); }
        if (result == Z_STREAM_END) { ended = true; if (stream.avail_in || left) { inflateEnd(&stream); return fail(error, QStringLiteral("trailing compressed data")); } }
        else if (result != Z_OK) { inflateEnd(&stream); return fail(error, QStringLiteral("invalid deflate stream or early end")); }
        if (beforeIn == stream.avail_in && beforeWritten == written) { inflateEnd(&stream); return fail(error, QStringLiteral("deflate made no progress")); }
    }
    if (entry.method == 8) inflateEnd(&stream); if (written != entry.uncompressedSize || quint32(crc) != entry.crcValue) return fail(error, QStringLiteral("ZIP CRC or size mismatch")); return true;
}
