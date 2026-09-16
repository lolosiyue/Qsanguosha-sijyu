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

#include <zlib.h>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
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

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Q_INIT_RESOURCE(android_content_fixture);
    return readerCases() && indexedReaderCases() && spoolRecoveryCases() && storeCases()
        && sharedMediaCases() && mixedMediaCases() && warmStartCases() ? 0 : 1;
}
