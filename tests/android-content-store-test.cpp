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

QByteArray mediaZip()
{
    const QList<QPair<QByteArray, QByteArray>> payload{{"image/base.png", "image"}, {"audio/base.ogg", "audio"}, {"font/base.ttf", "font"}};
    QJsonArray records;
    QList<ZipItem> items;
    quint64 expanded = 0;
    for (const auto &entry : payload) {
        const QByteArray digest = QCryptographicHash::hash(entry.second, QCryptographicHash::Sha256).toHex();
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

bool warmStartCases()
{
    // A successful receipt makes an unchanged restart cheap and must not
    // publish another immutable version.
    QTemporaryDir warmRoot;
    if (!check(warmRoot.isValid(), "warm start root")) return false;
    QString error;
    AndroidContentStore first(warmRoot.path());
    if (!check(first.prepareStartup(&error), "warm start initial prepare: " + error)) return false;
    const QString active = first.status().value("active").toString();
    const QVariantMap receipt = first.status().value("startup_validation").toMap();
    if (!check(receipt.value("schema").toInt() == 1 && !receipt.value("apk_revision").toString().isEmpty()
               && receipt.value("active").toString() == active
               && !receipt.value("metadata_sha256").toString().isEmpty()
               && !receipt.value("descriptor_sha256").toString().isEmpty(),
               "startup validation receipt")) return false;
    const int versions = versionCount(warmRoot.path());
    AndroidContentStore second(warmRoot.path());
    if (!check(second.prepareStartup(&error), "warm start cached prepare: " + error)) return false;
    if (!check(second.status().value("active").toString() == active
               && versionCount(warmRoot.path()) == versions
               && second.status().value("startup_validation").toMap() == receipt,
               "unchanged restart reuses receipt")) return false;

    // Missing, malformed, and stale receipts must all fall back to full
    // validation and receive a fresh receipt.
    const auto refreshReceipt = [&](const QString &caseName, const std::function<void(QVariantMap &)> &mutate) {
        QVariantMap state;
        if (!readMap(warmRoot.path() + "/content/state.json", &state)) return false;
        QVariantMap changed = state.value("startup_validation").toMap();
        mutate(changed); state.insert("startup_validation", changed);
        if (!writeMap(warmRoot.path() + "/content/state.json", state)) return false;
        AndroidContentStore restarted(warmRoot.path());
        if (!restarted.prepareStartup(&error)) return false;
        const QVariantMap refreshed = restarted.status().value("startup_validation").toMap();
        return check(refreshed.value("schema").toInt() == 1
                     && refreshed.value("active").toString() == active
                     && refreshed != changed,
                     caseName);
    };
    if (!refreshReceipt("missing receipt refresh", [](QVariantMap &map) { map.clear(); })) return false;
    if (!refreshReceipt("schema mismatch refresh", [](QVariantMap &map) { map.insert("schema", 99); })) return false;
    if (!refreshReceipt("APK revision mismatch refresh", [](QVariantMap &map) { map.insert("apk_revision", QString(64, QLatin1Char('0'))); })) return false;

    // Metadata changes invalidate the receipt but remain recoverable. A
    // descriptor mismatch is a real content failure and enters recovery.
    QVariantMap metadata;
    if (!check(readMap(warmRoot.path() + "/content/versions/" + active + "/metadata.json", &metadata),
               "read active metadata")) return false;
    const QString oldMetadataHash = first.status().value("startup_validation").toMap().value("metadata_sha256").toString();
    metadata.insert("warm_start_probe", true);
    if (!check(writeMap(warmRoot.path() + "/content/versions/" + active + "/metadata.json", metadata),
               "mutate active metadata")) return false;
    AndroidContentStore metadataChanged(warmRoot.path());
    if (!check(metadataChanged.prepareStartup(&error), "metadata change full validation: " + error)) return false;
    if (!check(metadataChanged.status().value("startup_validation").toMap().value("metadata_sha256").toString() != oldMetadataHash,
               "metadata change refreshes receipt")) return false;

    QVariantMap descriptor;
    if (!check(readMap(warmRoot.path() + "/content/versions/" + active + "/runtime/runtime-content.json", &descriptor),
               "read active descriptor")) return false;
    descriptor.insert("extensions", QVariantList());
    if (!check(writeMap(warmRoot.path() + "/content/versions/" + active + "/runtime/runtime-content.json", descriptor),
               "mutate active descriptor")) return false;
    AndroidContentStore descriptorChanged(warmRoot.path());
    if (!check(descriptorChanged.prepareStartup(&error) && descriptorChanged.needsRecovery()
               && !descriptorChanged.status().value("recovery_error").toString().isEmpty(),
               "descriptor mismatch enters recovery")) return false;

    // Core sentinels remain checked on the cached path; a missing sentinel
    // must never be hidden by a matching receipt.
    QTemporaryDir sentinelRoot;
    if (!check(sentinelRoot.isValid(), "sentinel root")) return false;
    AndroidContentStore sentinel(sentinelRoot.path());
    if (!check(sentinel.prepareStartup(&error), "sentinel initial prepare: " + error)) return false;
    const QString sentinelActive = sentinel.status().value("active").toString();
    if (!check(QFile::remove(sentinelRoot.path() + "/content/versions/" + sentinelActive + "/runtime/lua/config.lua"),
               "remove core sentinel")) return false;
    AndroidContentStore missingSentinel(sentinelRoot.path());
    if (!check(missingSentinel.prepareStartup(&error) && missingSentinel.needsRecovery(),
               "missing core sentinel enters recovery")) return false;

    // Media is deliberately outside the normal cached scan. Removing it is
    // visible only after a receipt miss/full validation, preserving startup
    // performance while retaining an explicit recovery path.
    QTemporaryDir mediaRoot;
    if (!check(mediaRoot.isValid(), "media warm start root")) return false;
    AndroidContentStore mediaStore(mediaRoot.path());
    if (!check(mediaStore.prepareStartup(&error), "media initial prepare: " + error)) return false;
    QBuffer media; media.setData(mediaZip()); media.open(QIODevice::ReadOnly);
    if (!check(mediaStore.stageMedia(media, nullptr, {}, &error), "stage media for warm start: " + error)) return false;
    AndroidContentStore mediaApplied(mediaRoot.path());
    if (!check(mediaApplied.prepareStartup(&error) && mediaApplied.mediaReady(), "apply media for warm start: " + error)) return false;
    const QString mediaActive = mediaApplied.status().value("active").toString();
    if (!check(QFile::remove(mediaRoot.path() + "/content/versions/" + mediaActive + "/runtime/image/base.png"),
               "remove private media payload")) return false;
    AndroidContentStore mediaCached(mediaRoot.path());
    if (!check(mediaCached.prepareStartup(&error) && mediaCached.mediaReady(),
               "cached startup does not rescan media")) return false;
    QVariantMap mediaState;
    if (!check(readMap(mediaRoot.path() + "/content/state.json", &mediaState), "read media state")) return false;
    mediaState.remove("startup_validation");
    if (!check(writeMap(mediaRoot.path() + "/content/state.json", mediaState), "remove media receipt")) return false;
    AndroidContentStore mediaFull(mediaRoot.path());
    return check(mediaFull.prepareStartup(&error) && !mediaFull.mediaReady(),
                 "full validation detects missing media");
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    Q_INIT_RESOURCE(android_content_fixture);
    return readerCases() && spoolRecoveryCases() && storeCases() && warmStartCases() ? 0 : 1;
}
