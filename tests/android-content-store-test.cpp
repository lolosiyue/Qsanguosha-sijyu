#include "android-content-store.h"
#include "android-zip-reader.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <functional>

#include <cstring>
#include <utility>
#include <zlib.h>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
class TemporaryZeroReadDevice final : public QIODevice
{
public:
    explicit TemporaryZeroReadDevice(QByteArray bytes, bool staleEof = false)
        : m_bytes(std::move(bytes)), m_staleEof(staleEof) {}
    bool isSequential() const override { return !m_staleEof; }
    bool atEnd() const override { return !m_staleEof && m_offset == m_bytes.size(); }
    qint64 size() const override { return m_bytes.size(); }
    // A provider may advertise a fixed size while refusing seek; exercise the
    // spool fallback instead of the direct-seek path covered separately below.
    bool seek(qint64) override { return false; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (!m_returnedTemporaryZero) {
            m_returnedTemporaryZero = true;
            return 0;
        }
        const qint64 count = qMin(maxSize, qint64(m_bytes.size()) - m_offset);
        if (count <= 0) return 0;
        memcpy(data, m_bytes.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QByteArray m_bytes;
    qint64 m_offset = 0;
    bool m_returnedTemporaryZero = false;
    bool m_staleEof = false;
};

struct ZipItem {
    QByteArray name;
    QByteArray data;
    quint16 method = 0;
    quint16 flags = 0;
    quint32 attributes = 0;
    quint16 madeBy = 20;
    bool badCrc = false;
};

void put16(QByteArray &out, quint16 value)
{ out.append(char(value)); out.append(char(value >> 8)); }
void put32(QByteArray &out, quint32 value)
{ put16(out, quint16(value)); put16(out, quint16(value >> 16)); }
void put64(QByteArray &out, quint64 value)
{ put32(out, quint32(value)); put32(out, quint32(value >> 32)); }

QByteArray zip(const QList<ZipItem> &items, bool zip64 = false, bool truncate = false)
{
    QByteArray out;
    struct Central { ZipItem item; quint32 crc; quint32 offset; };
    QList<Central> records;
    for (const ZipItem &item : items) {
        const quint32 crc = quint32(crc32(0L, reinterpret_cast<const Bytef *>(item.data.constData()),
                                          uInt(item.data.size())));
        const quint32 offset = quint32(out.size());
        put32(out, 0x04034b50); put16(out, 20); put16(out, item.flags); put16(out, item.method);
        put16(out, 0); put16(out, 0); put32(out, item.badCrc ? crc ^ 1U : crc);
        put32(out, quint32(item.data.size())); put32(out, quint32(item.data.size()));
        put16(out, quint16(item.name.size())); put16(out, 0); out.append(item.name); out.append(item.data);
        records.append({item, crc, offset});
    }
    const quint32 centralOffset = quint32(out.size());
    for (const Central &record : records) {
        const ZipItem &item = record.item;
        QByteArray extra;
        const bool wide = zip64;
        if (wide) { put16(extra, 1); put16(extra, 24); put64(extra, item.data.size()); put64(extra, item.data.size()); put64(extra, record.offset); }
        put32(out, 0x02014b50); put16(out, quint16((quint32(item.madeBy) << 8) | 3)); put16(out, 20);
        put16(out, item.flags); put16(out, item.method); put16(out, 0); put16(out, 0);
        put32(out, record.crc); put32(out, wide ? 0xffffffffU : quint32(item.data.size()));
        put32(out, wide ? 0xffffffffU : quint32(item.data.size())); put16(out, quint16(item.name.size()));
        put16(out, quint16(extra.size())); put16(out, 0); put16(out, 0); put16(out, 0);
        put32(out, item.attributes); put32(out, wide ? 0xffffffffU : record.offset);
        out.append(item.name); out.append(extra);
    }
    const quint32 centralSize = quint32(out.size()) - centralOffset;
    if (zip64) {
        const quint64 recordOffset = quint64(out.size());
        put32(out, 0x06064b50); put64(out, 44); put16(out, 45); put16(out, 45); put32(out, 0); put32(out, 0);
        put64(out, records.size()); put64(out, records.size()); put64(out, centralSize); put64(out, centralOffset);
        put32(out, 0x07064b50); put32(out, 0); put64(out, recordOffset); put32(out, 1);
        put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, 0xffff); put16(out, 0xffff);
        put32(out, 0xffffffffU); put32(out, 0xffffffffU); put16(out, 0);
    } else {
        put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, quint16(records.size()));
        put16(out, quint16(records.size())); put32(out, centralSize); put32(out, centralOffset); put16(out, 0);
    }
    if (truncate) out.chop(qMin(8, out.size()));
    return out;
}

bool check(bool condition, const QString &message)
{
    if (!condition) QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

bool readMap(const QString &path, QVariantMap *map)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return false;
    *map = document.object().toVariantMap();
    return true;
}

bool writeMap(const QString &path, const QVariantMap &map)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray bytes = QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size();
}

int versionCount(const QString &root)
{
    return QDir(root + "/content/versions").entryList(QDir::Dirs | QDir::NoDotAndDotDot).size();
}

bool readerRejects(const QByteArray &bytes, const QString &message)
{
    QBuffer source(const_cast<QByteArray *>(&bytes)); source.open(QIODevice::ReadOnly);
    AndroidZipReader reader; AndroidContentStore::ImportLimits limits; QString error;
    if (!reader.open(source, limits, nullptr, &error))
        return check(true, message + ": " + error);
    if (message != QStringLiteral("CRC"))
        return check(false, message + " was accepted");
    QBuffer destination; destination.open(QIODevice::WriteOnly);
    return check(!reader.extract(reader.entries().first(), destination, nullptr, {}, &error),
                 message + ": " + error);
}

QByteArray extensionZip(const QByteArray &body)
{
    const QByteArray descriptor = R"({"schema_version":2,"profile":"declared-v2","extensions":[{"name":"addon","script":"extensions/addon.lua","dependencies":[],"libs":[],"lang":[],"ai":[]}]})";
    return zip({{QByteArrayLiteral("runtime-content.json"), descriptor}, {QByteArrayLiteral("extensions/addon.lua"), body}});
}

QByteArray modularPackageZip(const QByteArray &body, bool corruptHash = false)
{
    const QByteArray relativePath = QByteArrayLiteral("lua/addon.lua");
    QByteArray hash = QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex();
    if (corruptHash) hash[0] = hash[0] == '0' ? '1' : '0';
    const QJsonObject file{{"path", QString::fromLatin1(relativePath)}, {"role", "rules"},
                           {"size", body.size()}, {"sha256", QString::fromLatin1(hash)}};
    const QJsonObject extension{{"name", "modular-addon"}, {"script", QString::fromLatin1(relativePath)},
        {"dependencies", QJsonArray{}}, {"libs", QJsonArray{}}, {"lang", QJsonArray{}}, {"ai", QJsonArray{}}};
    const QJsonObject optionalImage{{"path", "image/general/missing.png"}, {"role", "data"},
        {"size", 5}, {"sha256", QString(64, QLatin1Char('0'))}};
    const QJsonObject manifest{{"schema_version", 1}, {"id", "demo"}, {"version", "1.0.0"},
        {"engine_api", 1}, {"dependencies", QJsonArray{}}, {"extensions", QJsonArray{extension}},
        {"assets", QJsonObject{{"image/general/missing.png", "image/general/missing.png"}}},
        {"files", QJsonArray{file, optionalImage}}};
    return zip({{QByteArrayLiteral("manifest.json"), QJsonDocument(manifest).toJson(QJsonDocument::Compact)},
                {relativePath, body}});
}

QByteArray mediaZip(bool badHash = false)
{
    const QList<QPair<QByteArray, QByteArray>> payload{{"image/base.png", "image"}, {"audio/base.ogg", "audio"}, {"font/base.ttf", "font"}};
    QJsonArray records;
    QList<ZipItem> items;
    quint64 expanded = 0;
    for (const auto &entry : payload) {
        const QByteArray digest = badHash ? QByteArray(64, '0')
            : QCryptographicHash::hash(entry.second, QCryptographicHash::Sha256).toHex();
        records.append(QJsonObject{{"path", QString::fromLatin1(entry.first)}, {"size", entry.second.size()},
                                   {"sha256", QString::fromLatin1(digest)}});
        items.append({entry.first, entry.second}); expanded += quint64(entry.second.size());
    }
    const QJsonObject manifest{{"format", 1}, {"role", "media"}, {"expandedBytes", qlonglong(expanded)}, {"files", records}};
    items.append({QByteArrayLiteral("qsan-media.json"), QJsonDocument(manifest).toJson(QJsonDocument::Compact)});
    return zip(items);
}

QByteArray longPath(int length)
{
    QByteArray result;
    while (result.size() < length - 8) result += QByteArray(200, 'p') + '/';
    return result + "file.lua";
}

QByteArray metadataLimitZip()
{
    QList<ZipItem> items;
    for (int i = 0; i < 100000; ++i) {
        QByteArray name = QByteArray(180, 'm') + QByteArray::number(i) + ".lua";
        items.append({name, QByteArrayLiteral("x")});
    }
    return zip(items);
}

bool readerCases()
{
    const QByteArray normal = zip({{QByteArrayLiteral("ok.lua"), QByteArrayLiteral("x")}});
    if (!check(!normal.isEmpty(), "zip helper creates an archive")) return false;
    TemporaryZeroReadDevice intermittent(normal);
    intermittent.open(QIODevice::ReadOnly);
    AndroidZipReader intermittentReader; AndroidContentStore::ImportLimits intermittentLimits; QString intermittentError;
    if (!check(intermittentReader.open(intermittent, intermittentLimits, nullptr, &intermittentError),
               "reader retries a transient zero-byte source read: " + intermittentError)) return false;
    TemporaryZeroReadDevice staleEof(normal, true);
    staleEof.open(QIODevice::ReadOnly);
    AndroidZipReader staleEofReader;
    if (!check(staleEofReader.open(staleEof, intermittentLimits, nullptr, &intermittentError),
               "reader recognizes a fully copied fixed length with stale EOF: " + intermittentError)) return false;
    if (!readerRejects(zip({{QByteArrayLiteral("../escape.lua"), QByteArrayLiteral("x")}}), "traversal")) return false;
    if (!readerRejects(zip({{QByteArrayLiteral("A.lua"), QByteArrayLiteral("x")}, {QByteArrayLiteral("a.lua"), QByteArrayLiteral("y")}}), "case collision")) return false;
    if (!readerRejects(zip({{QByteArrayLiteral("A/x.lua"), QByteArrayLiteral("x")}, {QByteArrayLiteral("a/y.lua"), QByteArrayLiteral("y")}}), "case collision in directory component")) return false;
    if (!readerRejects(zip({{QByteArrayLiteral("a"), QByteArrayLiteral("x")}, {QByteArrayLiteral("a/x.lua"), QByteArrayLiteral("y")}}), "file versus child collision")) return false;
    if (!readerRejects(zip({{longPath(4097), QByteArrayLiteral("x")}}), "path length limit")) return false;
    if (!readerRejects(zip({{QByteArray(256, 's') + ".lua", QByteArrayLiteral("x")}}), "path segment limit")) return false;
    if (!readerRejects(zip({{QByteArrayLiteral("link.lua"), QByteArrayLiteral("x"), 0, 0, 0120777U << 16, 3}}), "symlink")) return false;
    if (!readerRejects(zip({{QByteArrayLiteral("bad.lua"), QByteArrayLiteral("x"), 0, 0, 0, 20, true}}), "CRC")) return false;
    if (!readerRejects(zip({{QByteArrayLiteral("short.lua"), QByteArrayLiteral("payload")}}, false, true), "truncated")) return false;
    if (!readerRejects(metadataLimitZip(), "metadata memory limit")) return false;
    QBuffer source; const QByteArray wide = zip({{QByteArrayLiteral("zip64.lua"), QByteArrayLiteral("x")}}, true); source.setData(wide); source.open(QIODevice::ReadOnly);
    // The offset bytes can resemble another extra field. They must not be
    // borrowed when the ZIP64 field itself declares only the two size values.
    QByteArray shortExtra = wide;
    const qsizetype central = shortExtra.indexOf(QByteArray("PK\x01\x02", 4));
    const qsizetype extraLengthOffset = central + 46 + QByteArrayLiteral("zip64.lua").size() + 2;
    shortExtra[extraLengthOffset] = char(16);
    shortExtra[extraLengthOffset + 1] = char(0);
    if (!readerRejects(shortExtra, "ZIP64 value outside its extra field")) return false;
    AndroidZipReader reader; AndroidContentStore::ImportLimits limits; QString error;
    return check(reader.open(source, limits, nullptr, &error), "ZIP64") && check(reader.entries().size() == 1, "ZIP64 entry count");
}

class SequentialArchive final : public QIODevice
{
public:
    explicit SequentialArchive(const QByteArray &bytes) : m_bytes(bytes) { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }
    bool atEnd() const override { return m_offset == m_bytes.size() && QIODevice::bytesAvailable() == 0; }
    qint64 bytesAvailable() const override { return m_bytes.size() - m_offset + QIODevice::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maximum) override {
        const qint64 count = qMin<qint64>(maximum, m_bytes.size() - m_offset);
        std::copy_n(m_bytes.constData() + m_offset, count, data);
        m_offset += count;
        return count;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    QByteArray m_bytes;
    qint64 m_offset = 0;
};

bool indexedReaderCases()
{
    QTemporaryDir root;
    if (!check(root.isValid(), "indexed reader root")) return false;
    AndroidContentStore::ImportLimits limits;
    QString error;
    QList<ZipItem> items;
    for (int i = 0; i < 256; ++i)
        items.append({QByteArray::number(i) + ".lua", QByteArray::number(i)});
    const QByteArray archive = zip(items);
    QBuffer source;
    source.setData(archive); source.open(QIODevice::ReadOnly);
    AndroidZipReader reader(root.path());
    if (!check(reader.open(source, limits, nullptr, &error), "open seekable ZIP: " + error)) return false;
    if (!check(QDir(root.path()).entryList(QDir::Files).isEmpty(), "seekable source has no spool copy")) return false;
    // Extract in reverse order: the lookup must bind the supplied entry, not
    // accidentally depend on extraction order or the source's current offset.
    for (int i = int(reader.entries().size()) - 1; i >= 0; --i) {
        QBuffer output; output.open(QIODevice::WriteOnly);
        if (!check(reader.extract(reader.entries().at(i), output, nullptr, {}, &error), "indexed extraction")) return false;
        if (!check(output.data() == QByteArray::number(i), "indexed payload")) return false;
    }
    auto forged = reader.entries().last();
    forged.crcValue ^= 1;
    QBuffer rejected; rejected.open(QIODevice::WriteOnly);
    if (!check(!reader.extract(forged, rejected, nullptr, {}, &error) && rejected.data().isEmpty(),
               "index rejects modified entry before writing")) return false;

    SequentialArchive sequential(archive);
    if (!check(reader.open(sequential, limits, nullptr, &error), "sequential provider spool: " + error)) return false;
    if (!check(QDir(root.path()).entryList({"archive-*.zip"}, QDir::Files).size() == 1, "one sequential spool")) return false;
    QBuffer sequentialOutput; sequentialOutput.open(QIODevice::WriteOnly);
    if (!check(reader.extract(reader.entries().last(), sequentialOutput, nullptr, {}, &error)
               && sequentialOutput.data() == "255", "sequential extraction")) return false;
    source.seek(0);
    if (!check(reader.open(source, limits, nullptr, &error)
               && QDir(root.path()).entryList(QDir::Files).isEmpty(), "reader reuse releases old spool")) return false;

    QByteArray duplicate = zip({{"first.lua", "a"}, {"second.lua", "b"}});
    const QByteArray central("PK\x01\x02", 4);
    const qsizetype second = duplicate.indexOf(central, duplicate.indexOf(central) + 4);
    for (int i = 0; i < 4; ++i) duplicate[second + 42 + i] = 0;
    return check(readerRejects(duplicate, "duplicate local header offset"), "duplicate indexed offset rejected");
}

bool spoolRecoveryCases()
{
    QTemporaryDir root;
    if (!check(root.isValid(), "spool recovery root")) return false;
    AndroidContentStore initial(root.path());
    QString error;
    if (!check(initial.prepareStartup(&error), "initialize spool recovery: " + error)) return false;
    const QString staging = root.path() + "/content/staging/";
    const QByteArray payload("partial ZIP from an interrupted import");
    const auto write = [&](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(payload) == payload.size();
    };
    // Leave ordinary files behind, as a killed process would: no RAII temp
    // object remains to remove them before the next store instance starts.
    const QString orphan = staging + "archive-aB123Z.zip";
    if (!check(write(orphan), "create abandoned spool")) return false;
    const QStringList preserved{staging + "archive-short.zip", staging + "archive-1234567.zip",
        staging + "archive-123456.zip.keep", staging + "archive-12345_.zip",
        staging + "unknown.zip", root.path() + "/archive-ABC123.zip"};
    for (const QString &path : preserved)
        if (!check(write(path), "create preserved file: " + path)) return false;
    const QString namedDirectory = staging + "archive-dir123.zip";
    if (!check(QDir().mkpath(namedDirectory) && write(namedDirectory + "/keep"),
               "create directory resembling spool")) return false;
    const QString target = root.path() + "/symlink-target";
    const QString link = staging + "archive-link12.zip";
    if (!check(write(target), "create symlink target")) return false;
#ifdef Q_OS_WIN
    // QFile::link creates a .lnk shortcut on Windows, not the filesystem
    // symlink needed here. Developer Mode or symlink privilege may be absent.
    bool linked = CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(link.utf16()),
        reinterpret_cast<LPCWSTR>(target.utf16()), 0x2) != 0;
    if (!linked && GetLastError() == ERROR_INVALID_PARAMETER)
        linked = CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(link.utf16()),
            reinterpret_cast<LPCWSTR>(target.utf16()), 0) != 0;
    if (!linked) QTextStream(stderr) << "SKIP: spool symlink fixture unavailable on Windows\n";
#else
    const bool linked = QFile::link(target, link);
    if (!check(linked, "create spool symlink")) return false;
#endif
    AndroidContentStore restarted(root.path());
    if (!check(restarted.prepareStartup(&error), "recover abandoned spool: " + error)) return false;
    if (!check(!QFileInfo::exists(orphan), "known abandoned spool removed")) return false;
    for (const QString &path : preserved + QStringList{target, namedDirectory + "/keep"}) {
        QFile file(path);
        if (!check(file.open(QIODevice::ReadOnly) && file.readAll() == payload,
                   "unknown or out-of-staging file preserved: " + path)) return false;
    }
    return !linked || check(QFileInfo(link).isSymLink()
        && QFileInfo(link).symLinkTarget() == target, "spool symlink preserved");
}

bool storeCases()
{
    QTemporaryDir root;
    if (!check(root.isValid(), "temporary app data root")) return false;
    AndroidContentStore store(root.path()); QString error;
    if (!check(store.prepareStartup(&error), "prepare startup: " + error)) return false;
    if (!check(!store.mediaReady() && store.packages().size() == 2, "base packages and incomplete media state")) return false;
    QBuffer cancelledSource; cancelledSource.setData(extensionZip("cancelled")); cancelledSource.open(QIODevice::ReadOnly);
    AndroidContentStore::Cancelled cancel(true);
    if (!check(!store.stageExtension(cancelledSource, "cancelled.zip", "cancelled", &cancel, {}, &error), "cancel import")) return false;
    if (!check(!store.hasPending() && QFileInfo::exists(store.runtimeRoot() + "/extensions/base.lua"), "cancel keeps active version")) return false;
    QBuffer progressSource; progressSource.setData(extensionZip("progress-cancelled")); progressSource.open(QIODevice::ReadOnly);
    AndroidContentStore::Cancelled progressCancel;
    const auto cancelFromProgress = [&progressCancel](quint64, quint64) { progressCancel.store(true); };
    if (!check(!store.stageExtension(progressSource, "progress.zip", "progress", &progressCancel,
                                     cancelFromProgress, &error), "progress callback cancellation")) return false;
    if (!check(!store.hasPending() && !QFileInfo::exists(store.runtimeRoot() + "/extensions/progress.lua"),
               "progress cancellation leaves active runtime")) return false;
    QBuffer first; first.setData(extensionZip("original")); first.open(QIODevice::ReadOnly);
    if (!check(store.stageExtension(first, "addon.zip", "addon", nullptr, {}, &error), "stage extension")) return false;
    QBuffer second; second.setData(extensionZip("replacement")); second.open(QIODevice::ReadOnly);
    if (!check(store.stageExtension(second, "addon.zip", "addon", nullptr, {}, &error), "stage replacement while pending")) return false;
    AndroidContentStore afterImport(root.path());
    if (!check(afterImport.prepareStartup(&error), "apply pending import: " + error)) return false;
    if (!check(QFile(afterImport.runtimeRoot() + "/extensions/addon.lua").open(QIODevice::ReadOnly), "replacement file published")) return false;
    if (!check(afterImport.setPackageEnabled("addon", false, &error), "disable package")) return false;
    AndroidContentStore disabled(root.path());
    if (!check(disabled.prepareStartup(&error), "apply disabled state")) return false;
    if (!check(!QFileInfo::exists(disabled.runtimeRoot() + "/extensions/addon.lua"), "disabled package removed from runtime")) return false;
    QFile disabledDescriptor(disabled.runtimeRoot() + "/runtime-content.json");
    if (!check(disabledDescriptor.open(QIODevice::ReadOnly)
               && !disabledDescriptor.readAll().contains("addon.lua"), "disabled descriptor excludes package")) return false;
    if (!check(disabled.removePackage("addon", &error), "remove package")) return false;
    AndroidContentStore removed(root.path());
    if (!check(removed.prepareStartup(&error), "apply remove")) return false;
    if (!check(!removed.hasPending() && removed.packages().size() == 2, "removed package descriptor state")) return false;
    QFile restoredAddon(removed.runtimeRoot() + "/extensions/addon.lua");
    if (!check(restoredAddon.open(QIODevice::ReadOnly)
               && restoredAddon.readAll().contains("bundled addon"), "replacement fallback restores original")) return false;
    QFile restoredDescriptor(removed.runtimeRoot() + "/runtime-content.json");
    if (!check(restoredDescriptor.open(QIODevice::ReadOnly)
               && restoredDescriptor.readAll().contains("addon.lua"), "fallback descriptor restores package")) return false;
    QBuffer media; media.setData(mediaZip()); media.open(QIODevice::ReadOnly);
    if (!check(removed.stageMedia(media, nullptr, {}, &error), "complete media import: " + error)) return false;
    AndroidContentStore ready(root.path());
    if (!check(ready.prepareStartup(&error) && ready.mediaReady(), "media completeness")) return false;
    if (!check(ready.beginBootAttempt(&error), "boot marker")) return false;
    AndroidContentStore recovering(root.path());
    if (!check(recovering.prepareStartup(&error) && recovering.needsRecovery(), "boot marker recovery detection")) return false;
    return check(recovering.recoverPrevious(&error), "recover previous version: " + error);
}

bool sharedMediaCases()
{
    QTemporaryDir root;
    if (!check(root.isValid(), "shared media root")) return false;
    QString error;
    AndroidContentStore initial(root.path());
    if (!check(initial.prepareStartup(&error), "shared media prepare")) return false;
    QBuffer media; media.setData(mediaZip()); media.open(QIODevice::ReadOnly);
    if (!check(initial.stageMedia(media, nullptr, {}, &error), "shared media import: " + error)) return false;
    AndroidContentStore ready(root.path());
    if (!check(ready.prepareStartup(&error) && ready.mediaReady(), "shared media activation")) return false;
    const QString originalRuntime = ready.runtimeRoot();
    const QString payload = QFileInfo(originalRuntime + "/image/base.png").canonicalFilePath();
#ifdef Q_OS_UNIX
    if (!check(QFileInfo(originalRuntime + "/image").isSymLink()
               && payload.startsWith(root.path() + "/content/blobs/"), "complete media uses shared directory")) return false;
#endif
    // Cancellation must not replace the active media blob.
    QBuffer cancelled; cancelled.setData(mediaZip()); cancelled.open(QIODevice::ReadOnly);
    AndroidContentStore::Cancelled cancel(false);
    if (!check(!ready.stageMedia(cancelled, &cancel, [&](quint64, quint64) { cancel.store(true); }, &error)
               && !ready.hasPending(), "cancelled media preserves active version")) return false;
    QBuffer extension; extension.setData(extensionZip("new Lua only")); extension.open(QIODevice::ReadOnly);
    if (!check(ready.stageExtension(extension, "addon.zip", "addon", nullptr, {}, &error), "Lua update with shared media")) return false;
    AndroidContentStore updated(root.path());
    if (!check(updated.prepareStartup(&error) && updated.mediaReady(), "apply Lua update")) return false;
#ifdef Q_OS_UNIX
    if (!check(QFileInfo(updated.runtimeRoot() + "/image/base.png").canonicalFilePath() == payload,
               "Lua update reuses exact media file")) return false;
#endif
    QVariantMap baseline;
    const QString baselinePath = root.path() + "/content/baseline/metadata.json";
    if (!check(readMap(baselinePath, &baseline), "read baseline for upgrade")) return false;
    baseline.insert("revision", "shared-media-baseline-upgrade");
    if (!check(writeMap(baselinePath, baseline), "advance baseline revision")) return false;
    AndroidContentStore upgraded(root.path());
    if (!check(upgraded.prepareStartup(&error) && upgraded.mediaReady(), "baseline migration: " + error)) return false;
#ifdef Q_OS_UNIX
    if (!check(QFileInfo(upgraded.runtimeRoot() + "/image/base.png").canonicalFilePath() == payload
               && !QFileInfo::exists(originalRuntime) && QFileInfo::exists(payload),
               "baseline migration and old-version GC preserve shared blob")) return false;
#endif
    if (!check(upgraded.rollback(&error), "stage shared media rollback: " + error)) return false;
    AndroidContentStore rolledBack(root.path());
    if (!check(rolledBack.prepareStartup(&error) && rolledBack.mediaReady(), "activate shared media rollback")) return false;
#ifdef Q_OS_UNIX
    if (!check(QFileInfo(rolledBack.runtimeRoot() + "/image/base.png").canonicalFilePath() == payload,
               "rollback reuses original media")) return false;
    // A forged directory link cannot redirect the runtime outside the blob
    // recorded by this package, even if the external file has the same size.
    QTemporaryDir outside;
    if (!check(outside.isValid(), "outside media-link fixture root")) return false;
    QFile outsideFile(outside.path() + "/base.png");
    if (!outsideFile.open(QIODevice::WriteOnly) || outsideFile.write("image") != 5) return false;
    outsideFile.close();
    const QString link = rolledBack.runtimeRoot() + "/image";
    if (!check(QFile::remove(link) && QFile::link(outside.path(), link), "forge media root link")) return false;
    QVariantMap state;
    const QString statePath = root.path() + "/content/state.json";
    if (!readMap(statePath, &state)) return false;
    state.remove("startup_validation");
    if (!writeMap(statePath, state)) return false;
    AndroidContentStore forged(root.path());
    if (!check(forged.prepareStartup(&error) && !forged.mediaReady(), "reject forged media root")) return false;
    if (!check(QDir(root.path()).removeRecursively() && QFileInfo::exists(outsideFile.fileName()),
               "snapshot cleanup never traverses media directory links")) return false;
#endif
    return true;
}

bool mixedMediaCases()
{
#ifdef Q_OS_UNIX
    QTemporaryDir root;
    if (!check(root.isValid(), "mixed media root")) return false;
    QString error;
    AndroidContentStore store(root.path());
    if (!store.prepareStartup(&error)) return false;
    QBuffer media; media.setData(mediaZip()); media.open(QIODevice::ReadOnly);
    if (!store.stageMedia(media, nullptr, {}, &error)) return false;
    AndroidContentStore ready(root.path());
    if (!ready.prepareStartup(&error) || !ready.mediaReady()) return false;
    const QString payload = QFileInfo(ready.runtimeRoot() + "/image/base.png").canonicalFilePath();
    QBuffer mixed;
    mixed.setData(zip({{"extensions/addon.lua", "mixed Lua"}, {"image/addon.png", "extra image"}}));
    mixed.open(QIODevice::ReadOnly);
    if (!check(ready.stageExtension(mixed, "addon.zip", "addon", nullptr, {}, &error), "mixed media import: " + error)) return false;
    AndroidContentStore applied(root.path());
    if (!check(applied.prepareStartup(&error) && applied.mediaReady(), "mixed media activation")) return false;
    if (!check(!QFileInfo(applied.runtimeRoot() + "/image").isSymLink()
               && QFileInfo(applied.runtimeRoot() + "/image/base.png").isSymLink()
               && QFileInfo(applied.runtimeRoot() + "/image/addon.png").isSymLink()
               && QFileInfo(applied.runtimeRoot() + "/image/base.png").canonicalFilePath() == payload,
               "mixed root uses exact per-file references without copying media")) return false;
    if (!applied.setPackageEnabled("addon", false, &error)) return false;
    AndroidContentStore disabled(root.path());
    if (!check(disabled.prepareStartup(&error) && disabled.mediaReady()
               && QFileInfo(disabled.runtimeRoot() + "/image").isSymLink()
               && !QFileInfo::exists(disabled.runtimeRoot() + "/image/addon.png"),
               "disabled extension media cannot leak through whole-directory sharing")) return false;
#endif
    return true;
}

bool modularPackageCases()
{
    QTemporaryDir root;
    if (!check(root.isValid(), "modular package root")) return false;
    QString error;
    AndroidContentStore store(root.path());
    if (!check(store.prepareStartup(&error), "prepare modular package store: " + error)) return false;

    // Android ignores old SHA values and missing optional images for modular
    // packages too; required Lua and descriptor validation remain in force.
    QBuffer source;
    source.setData(modularPackageZip("return true", true));
    source.open(QIODevice::ReadOnly);
    const bool staged = store.stageModularPackage(source, "demo.zip", nullptr, {}, &error);
    if (!check(staged,
               "stage modular package without checksum/media completeness gate: " + error)) return false;
    AndroidContentStore installed(root.path());
    if (!check(installed.prepareStartup(&error), "apply modular package: " + error)) return false;
    const QString runtime = installed.runtimeRoot();
    QFile manifest(runtime + "/packages/demo/manifest.json");
    QFile payload(runtime + "/packages/demo/lua/addon.lua");
    if (!check(manifest.open(QIODevice::ReadOnly) && payload.open(QIODevice::ReadOnly),
               "package manifest and payload are installed under packages/<id>")) return false;
    if (!check(!installed.needsRecovery()
               && !QFileInfo::exists(runtime + "/packages/demo/image/general/missing.png"),
               "missing optional modular image does not block startup")) return false;
    QVariantMap descriptor;
    if (!check(readMap(runtime + "/runtime-content.json", &descriptor)
               && descriptor.value("schema_version").toInt() == 3
               && descriptor.value("profile").toString() == "packages-v1",
               "modular package publishes runtime descriptor v3")) return false;
    const QVariantList packages = descriptor.value("packages").toList();
    const QVariantList extensions = descriptor.value("extensions").toList();
    if (!check(packages.size() == 1 && packages.first().toMap().value("id").toString() == "demo"
               && extensions.size() == 3
               && extensions.at(0).toMap().value("name").toString() == "base"
               && extensions.at(0).toMap().value("script").toString() == "extensions/base.lua"
               && extensions.at(1).toMap().value("name").toString() == "addon"
               && extensions.at(1).toMap().value("script").toString() == "extensions/addon.lua"
               && extensions.at(2).toMap().value("name").toString() == "modular-addon"
               && extensions.at(2).toMap().value("script").toString() == "packages/demo/lua/addon.lua",
               "descriptor preserves bundled extension order and appends package extension")) return false;

    if (!check(installed.removePackage("demo", &error), "stage modular package removal: " + error)) return false;
    AndroidContentStore removed(root.path());
    if (!check(removed.prepareStartup(&error), "apply modular package removal: " + error)) return false;
    QFile removedPayload(removed.runtimeRoot() + "/packages/demo/lua/addon.lua");
    if (!check(!removedPayload.exists(), "removed modular package is absent from effective runtime")) return false;
    QVariantMap restored;
    return check(readMap(removed.runtimeRoot() + "/runtime-content.json", &restored)
                 && restored.value("schema_version").toInt() == 2,
                 "removing the last modular package restores the v2 legacy descriptor");
}

bool bootstrapUpgradeCases(bool sameApkRevision)
{
    QTemporaryDir root;
    if (!check(root.isValid(), "bootstrap upgrade root")) return false;
    const auto writeBytes = [](const QString &path, const QByteArray &bytes) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    const auto readBytes = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const QByteArray overrideBytes("-- preserved user addon\n");
    if (!writeBytes(root.path() + "/runtime/extensions/addon.lua", overrideBytes)) return false;
    QString error;
    AndroidContentStore initial(root.path());
    if (!initial.prepareStartup(&error)) return false;
    QBuffer media; media.setData(mediaZip()); media.open(QIODevice::ReadOnly);
    if (!initial.stageMedia(media, nullptr, {}, &error)) return false;
    AndroidContentStore installed(root.path());
    if (!installed.prepareStartup(&error)) return false;
    const QString oldRuntime = installed.runtimeRoot();
    const QString oldAudio = QFileInfo(oldRuntime + "/audio/base.ogg").canonicalFilePath();
    const QByteArray legacyLoader("-- old APK bootstrap\n");
    if (!writeBytes(root.path() + "/content/baseline/runtime/lua/sanguosha.lua", legacyLoader)
        || !writeBytes(oldRuntime + "/lua/sanguosha.lua", legacyLoader)) return false;
    QVariantMap baseline;
    const QString baselinePath = root.path() + "/content/baseline/metadata.json";
    if (!readMap(baselinePath, &baseline)) return false;
    if (sameApkRevision) baseline.remove("bootstrap_version");
    else baseline.insert("apk_revision", "previous-apk");
    if (!writeMap(baselinePath, baseline)) return false;

    AndroidContentStore upgraded(root.path());
    const bool prepared = upgraded.prepareStartup(&error);
    if (!check(prepared && !upgraded.needsRecovery(), "bootstrap APK upgrade: " + error)) return false;
    QVariantMap migrated;
    if (!check(readMap(baselinePath, &migrated) && migrated.value("bootstrap_version").toInt() == 1,
               "bootstrap repair publishes its metadata migration marker")) return false;
    const QByteArray apkLoader = readBytes(":/assets/lua/sanguosha.lua");
    if (!check(!apkLoader.isEmpty() && upgraded.runtimeRoot() != oldRuntime
               && readBytes(upgraded.runtimeRoot() + "/lua/sanguosha.lua") == apkLoader
               && readBytes(oldRuntime + "/lua/sanguosha.lua") == legacyLoader,
               "APK bootstrap activates without changing the previous snapshot")) return false;
    if (!check(upgraded.mediaReady()
               && readBytes(upgraded.runtimeRoot() + "/extensions/addon.lua") == overrideBytes,
               "bootstrap refresh preserves media and captured user override")) return false;
#ifdef Q_OS_UNIX
    if (!check(QFileInfo(upgraded.runtimeRoot() + "/audio/base.ogg").canonicalFilePath() == oldAudio,
               "bootstrap refresh reuses the same media blob")) return false;
#else
    Q_UNUSED(oldAudio);
#endif
    return true;
}

bool presentationUpgradeCases(bool userOverride, bool looseTranslationOverride)
{
    QTemporaryDir root;
    if (!check(root.isValid(), "presentation upgrade root")) return false;
    const QString translation = "lang/zh_CN/Audio/AddedPackageLines.lua";
    const auto writeBytes = [](const QString &path, const QByteArray &bytes) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
    };
    const auto readBytes = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    const QByteArray customScript("-- user rule version must stay pinned\n");
    if (userOverride && !writeBytes(root.path() + "/runtime/extensions/addon.lua", customScript)) return false;
    QString error;
    AndroidContentStore initial(root.path());
    if (!initial.prepareStartup(&error)) return false;
    QBuffer media; media.setData(mediaZip()); media.open(QIODevice::ReadOnly);
    if (!initial.stageMedia(media, nullptr, {}, &error)) return false;
    AndroidContentStore installed(root.path());
    if (!installed.prepareStartup(&error)) return false;
    const QString oldRuntime = installed.runtimeRoot();
    const QByteArray oldScript = readBytes(oldRuntime + "/extensions/addon.lua");
    const QString baselinePath = root.path() + "/content/baseline/metadata.json";
    const QString snapshotPath = QFileInfo(oldRuntime).absolutePath() + "/metadata.json";
    QVariantMap baseline, snapshot, content;
    if (!readMap(baselinePath, &baseline) || !readMap(snapshotPath, &snapshot)
        || !readMap(oldRuntime + "/runtime-content.json", &content)) return false;
    const auto stripEntries = [&](QVariantList entries) {
        for (QVariant &value : entries) {
            QVariantMap entry = value.toMap();
            QStringList lang = entry.value("lang").toStringList(); lang.removeAll(translation);
            entry.insert("lang", lang); value = entry;
        }
        return entries;
    };
    std::function<QVariantMap(QVariantMap)> stripPackage = [&](QVariantMap package) {
        QStringList files = package.value("files").toStringList(); files.removeAll(translation);
        package.insert("files", files); package.insert("entries", stripEntries(package.value("entries").toList()));
        if (package.contains("fallback")) package.insert("fallback", stripPackage(package.value("fallback").toMap()));
        return package;
    };
    for (QVariantMap *metadata : {&baseline, &snapshot}) {
        QVariantList packages = metadata->value("packages").toList();
        for (QVariant &package : packages) package = stripPackage(package.toMap());
        metadata->insert("packages", packages);
    }
    // Reproduce the failed same-APK upgrade: the file was copied, declarations
    // were retained, and bootstrap/APK revision markers already look current.
    baseline.remove("presentation_version");
    content.insert("extensions", stripEntries(content.value("extensions").toList()));
    snapshot.insert("descriptor", content);
    const QByteArray customTranslation("return { custom = 'kept' }\n");
    QString overridePath;
    if (looseTranslationOverride) {
        const QString blob = "11111111-1111-4111-8111-111111111111";
        overridePath = root.path() + "/content/blobs/" + blob + "/runtime/" + translation;
        if (!writeBytes(overridePath, customTranslation)) return false;
        QVariantMap overrides = snapshot.value("legacy_overrides").toMap();
        overrides.insert(translation, blob); snapshot.insert("legacy_overrides", overrides);
    }
    if (!writeMap(baselinePath, baseline) || !writeMap(snapshotPath, snapshot)
        || !writeMap(oldRuntime + "/runtime-content.json", content)) return false;
    const QByteArray previousDescriptorBytes = readBytes(oldRuntime + "/runtime-content.json");
    AndroidContentStore upgraded(root.path());
    const bool prepared = upgraded.prepareStartup(&error);
    if (!check(prepared && !upgraded.needsRecovery(), "repair APK translation declarations: " + error)) return false;
    QVariantMap repaired;
    if (!readMap(upgraded.runtimeRoot() + "/runtime-content.json", &repaired)) return false;
    int declarations = 0;
    for (const QVariant &value : repaired.value("extensions").toList())
        declarations += value.toMap().value("lang").toStringList().count(translation);
    const QByteArray expected = looseTranslationOverride ? customTranslation : readBytes(":/assets/" + translation);
    if (!check(declarations == 1 && upgraded.runtimeRoot() != oldRuntime && upgraded.mediaReady()
               && readBytes(upgraded.runtimeRoot() + '/' + translation) == expected
               && readBytes(upgraded.runtimeRoot() + "/extensions/addon.lua") == oldScript,
               "new translation is declared once while media and retained rules stay intact")) return false;
    if (!check(!userOverride || oldScript == customScript, "user rule override remains active")) return false;
    if (!check(overridePath.isEmpty() || readBytes(overridePath) == customTranslation,
               "loose translation override blob is retained")) return false;
    // Compare persisted bytes: JSON arrays deserialize as QVariantList rather
    // than the QStringList used while constructing the old fixture metadata.
    if (!check(!previousDescriptorBytes.isEmpty()
               && readBytes(oldRuntime + "/runtime-content.json") == previousDescriptorBytes,
               "translation upgrade does not rewrite the previous snapshot")) return false;
    AndroidContentStore warm(root.path());
    return check(warm.prepareStartup(&error) && warm.runtimeRoot() == upgraded.runtimeRoot(),
                 "presentation marker prevents repeated migration");
}

bool warmStartCases()
{
    QTemporaryDir root;
    if (!check(root.isValid(), "warm start root")) return false;
    QString error;
    AndroidContentStore first(root.path());
    if (!check(first.prepareStartup(&error), "initial startup")) return false;
    const QString active = first.status().value("active").toString();
    const int versions = versionCount(root.path());
    AndroidContentStore second(root.path());
    if (!check(second.prepareStartup(&error)
               && second.status().value("active").toString() == active
               && versionCount(root.path()) == versions
               && !second.status().contains("startup_validation"), "startup has no hash receipt")) return false;

    // Older manifests may include hashes. They no longer decide whether the
    // installed media can be used, even when every advertised hash is wrong.
    QBuffer media; media.setData(mediaZip(true)); media.open(QIODevice::ReadOnly);
    if (!check(second.stageMedia(media, nullptr, {}, &error), "media import ignores SHA fields: " + error)) return false;
    AndroidContentStore applied(root.path());
    if (!check(applied.prepareStartup(&error) && applied.mediaReady(), "activate media without hash checking")) return false;
    if (!check(QFile::remove(applied.runtimeRoot() + "/image/base.png"), "remove optional image")) return false;
    QVariantMap state;
    const QString statePath = root.path() + "/content/state.json";
    if (!readMap(statePath, &state)) return false;
    state.insert("startup_validation", QVariantMap{{"media_ready", false}, {"metadata_sha256", "obsolete"}});
    if (!writeMap(statePath, state)) return false;
    QVariantMap baseline;
    const QString basePath = root.path() + "/content/baseline/metadata.json";
    if (!readMap(basePath, &baseline)) return false;
    baseline.insert("apk_revision", "previous-apk");
    if (!writeMap(basePath, baseline)) return false;
    AndroidContentStore restarted(root.path());
    if (!check(restarted.prepareStartup(&error) && restarted.mediaReady()
               && !restarted.status().contains("startup_validation"),
               "APK refresh and old receipt do not rescan missing media")) return false;

    // Real configuration/descriptor errors still need recovery; this is
    // ordinary loading, not a resource hash or media inventory gate.
    QVariantMap descriptor;
    const QString descriptorPath = restarted.runtimeRoot() + "/runtime-content.json";
    if (!readMap(descriptorPath, &descriptor)) return false;
    descriptor.insert("extensions", QVariantList());
    if (!writeMap(descriptorPath, descriptor)) return false;
    AndroidContentStore invalid(root.path());
    if (!check(invalid.prepareStartup(&error) && invalid.needsRecovery(), "invalid descriptor recovery")) return false;

    QTemporaryDir coreRoot;
    if (!check(coreRoot.isValid(), "core fixture root")) return false;
    AndroidContentStore core(coreRoot.path());
    if (!core.prepareStartup(&error) || !QFile::remove(core.runtimeRoot() + "/lua/config.lua")) return false;
    AndroidContentStore missingCore(coreRoot.path());
    return check(missingCore.prepareStartup(&error) && missingCore.needsRecovery(), "missing core configuration recovery");
}

}

bool bundledMigrationCases(const QString &upgradeRoot)
{
    QString error;
    const auto modularActive = [&](AndroidContentStore &store) {
        QVariantMap manifest;
        if (!readMap(store.runtimeRoot() + "/runtime-content.json", &manifest)) return false;
        const QVariantList entries = manifest.value("extensions").toList();
        int count = 0;
        for (const auto &package : store.packages()) if (package.id == "base") ++count;
        const bool valid = count == 1 && entries.size() == 2
            && entries.at(0).toMap().value("script").toString() == "packages/base/lua/base.lua"
            && entries.at(1).toMap().value("name").toString() == "addon"
            && !QFileInfo::exists(store.runtimeRoot() + "/extensions/base.lua");
        if (!valid) QTextStream(stderr) << "Migration descriptor: " << QJsonDocument::fromVariant(manifest).toJson(QJsonDocument::Compact)
            << " base rows=" << count << " legacy exists=" << QFileInfo::exists(store.runtimeRoot() + "/extensions/base.lua") << '\n';
        return valid;
    };
    QTemporaryDir root;
    AndroidContentStore fresh(root.path());
    const bool prepared = fresh.prepareStartup(&error);
    if (!check(prepared, "prepare bundled migration: " + error)) return false;
    if (!check(modularActive(fresh), "bundled migration keeps one ID and original extension order")) return false;
    if (!check(fresh.setPackageEnabled("base", false, &error), "disable migrated bundle: " + error)) return false;
    AndroidContentStore disabled(root.path());
    if (!check(disabled.prepareStartup(&error)
        && !QFileInfo::exists(disabled.runtimeRoot() + "/extensions/base.lua")
        && !QFileInfo::exists(disabled.runtimeRoot() + "/packages/base/manifest.json"), "disable removes both legacy and modular payloads: " + error)) return false;
    if (!check(disabled.removePackage("base", &error), "restore legacy fallback: " + error)) return false;
    AndroidContentStore restored(root.path());
    if (!check(restored.prepareStartup(&error)
        && QFileInfo::exists(restored.runtimeRoot() + "/extensions/base.lua")
        && !QFileInfo::exists(restored.runtimeRoot() + "/packages/base/manifest.json"), "explicit removal restores legacy across restart: " + error)) return false;
    QVariantMap laterBase;
    const QString laterBasePath = root.path() + "/content/baseline/metadata.json";
    if (!readMap(laterBasePath, &laterBase)) return false;
    laterBase.insert("revision", "11111111-1111-4111-8111-111111111111");
    if (!writeMap(laterBasePath, laterBase)) return false;
    AndroidContentStore later(root.path());
    if (!check(later.prepareStartup(&error)
        && QFileInfo::exists(later.runtimeRoot() + "/extensions/base.lua")
        && !QFileInfo::exists(later.runtimeRoot() + "/packages/base/manifest.json"), "explicit legacy choice survives a later baseline revision: " + error)) return false;

    QTemporaryDir editedRoot;
    QDir().mkpath(editedRoot.path() + "/runtime/extensions");
    QFile edited(editedRoot.path() + "/runtime/extensions/base.lua");
    if (!edited.open(QIODevice::WriteOnly) || edited.write("-- preserved CP1 edit\n") < 0) return false;
    edited.close();
    AndroidContentStore preserved(editedRoot.path());
    if (!check(preserved.prepareStartup(&error), "preserve legacy edit during modular migration: " + error)) return false;
    QFile saved(preserved.runtimeRoot() + "/extensions/base.lua");
    if (!check(saved.open(QIODevice::ReadOnly) && saved.readAll() == "-- preserved CP1 edit\n", "legacy edit bytes retained")) return false;
    saved.close();
    QVariantMap interruptedBase, interruptedState;
    const QString editedBasePath = editedRoot.path() + "/content/baseline/metadata.json";
    const QString editedStatePath = editedRoot.path() + "/content/state.json";
    if (!readMap(editedBasePath, &interruptedBase) || !readMap(editedStatePath, &interruptedState)) return false;
    QVariantList oldBase = interruptedBase.value("packages").toList();
    QVariantMap newPackage = oldBase.first().toMap();
    const QVariant oldPackage = newPackage.take("fallback");
    oldBase[0] = oldPackage; oldBase.append(newPackage);
    QVariantMap initial = interruptedBase.value("initial_snapshot").toMap();
    QVariantList oldInitial = initial.value("packages").toList();
    QVariantMap oldOverride = oldInitial.first().toMap();
    oldOverride.insert("fallback", oldPackage);
    oldInitial[0] = oldOverride; oldInitial.append(newPackage);
    initial.insert("packages", oldInitial);
    interruptedBase.insert("packages", oldBase); interruptedBase.insert("initial_snapshot", initial);
    interruptedState.remove("active"); interruptedState.remove("startup_validation");
    if (!writeMap(editedBasePath, interruptedBase) || !writeMap(editedStatePath, interruptedState)) return false;
    AndroidContentStore editedRecovery(editedRoot.path());
    if (!check(editedRecovery.prepareStartup(&error), "recover duplicate metadata with CP1 edits: " + error)) return false;
    QFile recoveredEdit(editedRecovery.runtimeRoot() + "/extensions/base.lua");
    if (!check(recoveredEdit.open(QIODevice::ReadOnly) && recoveredEdit.readAll() == "-- preserved CP1 edit\n", "duplicate recovery keeps the user override active")) return false;
    recoveredEdit.close();
    if (!check(editedRecovery.removePackage("base", &error), "remove legacy edit override: " + error)) return false;
    AndroidContentStore original(editedRoot.path());
    if (!check(original.prepareStartup(&error) && modularActive(original), "removing edit restores modular APK bundle: " + error)) return false;

    AndroidContentStore upgraded(upgradeRoot);
    if (!check(upgraded.prepareStartup(&error) && modularActive(upgraded), "upgrade existing bundled legacy package in place: " + error)) return false;

    // Recreate metadata saved by the failed first-boot implementation, before
    // any active snapshot could be published. Recovery must need no pm clear.
    QTemporaryDir interruptedRoot;
    AndroidContentStore before(interruptedRoot.path());
    if (!before.prepareStartup(&error)) return false;
    QVariantMap baseline, state;
    const QString baselinePath = interruptedRoot.path() + "/content/baseline/metadata.json";
    const QString statePath = interruptedRoot.path() + "/content/state.json";
    if (!readMap(baselinePath, &baseline) || !readMap(statePath, &state)) return false;
    QVariantList packages = baseline.value("packages").toList();
    QVariantMap modular = packages.first().toMap();
    packages[0] = modular.take("fallback");
    packages.append(modular);
    baseline.insert("packages", packages);
    baseline.insert("initial_snapshot", QVariantMap{{"packages", packages}});
    state.remove("active"); state.remove("startup_validation");
    if (!writeMap(baselinePath, baseline) || !writeMap(statePath, state)) return false;
    AndroidContentStore recovered(interruptedRoot.path());
    return check(recovered.prepareStartup(&error) && modularActive(recovered), "recover duplicate metadata from interrupted first boot: " + error);
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Q_INIT_RESOURCE(android_content_fixture);
    Q_CLEANUP_RESOURCE(android_modular_migration_fixture);
    if (!application.arguments().contains("--bundled-migration-only")
        && !(readerCases() && indexedReaderCases() && spoolRecoveryCases() && storeCases()
             && sharedMediaCases() && mixedMediaCases() && modularPackageCases()
             && bootstrapUpgradeCases(false) && bootstrapUpgradeCases(true)
             && presentationUpgradeCases(false, false) && presentationUpgradeCases(true, false)
             && presentationUpgradeCases(true, true) && warmStartCases())) return 1;
    QTemporaryDir upgradeRoot;
    QString error;
    AndroidContentStore legacy(upgradeRoot.path());
    if (!legacy.prepareStartup(&error)) return 1;
    QVariantMap baseline;
    const QString path = upgradeRoot.path() + "/content/baseline/metadata.json";
    if (!readMap(path, &baseline)) return 1;
    baseline.remove("apk_revision");
    if (!writeMap(path, baseline)) return 1;
    Q_INIT_RESOURCE(android_modular_migration_fixture);
    return bundledMigrationCases(upgradeRoot.path()) ? 0 : 1;
}
