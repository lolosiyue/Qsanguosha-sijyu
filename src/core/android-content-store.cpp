#include "android-content-store.h"
#include "android-zip-reader.h"
#include "android_assets.h"
#include "rules-content-manifest.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QUuid>
#ifdef Q_OS_ANDROID
#include <unistd.h>
#endif

namespace {
constexpr qint64 kJsonLimit = 32 * 1024 * 1024;
constexpr quint64 kReserve = 64 * 1024 * 1024;
bool cancelled(AndroidContentStore::Cancelled *cancel, QString *error);
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool versionId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$"));
    return pattern.match(id).hasMatch();
}
bool packageId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$"));
    return pattern.match(id).hasMatch();
}
bool safePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\')
        || path.contains(':') || path.contains(QChar::Null)
        || path != path.normalized(QString::NormalizationForm_C)) return false;
    for (const QString &part : path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".."
            || part.endsWith('.') || part.endsWith(' ')) return false;
        for (QChar c : part) if (c.unicode() < 32) return false;
    }
    return true;
}
QString key(const QString &path) { return path.toCaseFolded(); }
bool mediaPath(const QString &path)
{
    return safePath(path) && (path.startsWith("image/") || path.startsWith("audio/") || path.startsWith("font/"));
}
bool extensionPath(const QString &path)
{
    if (mediaPath(path)) return true;
    if (!safePath(path) || !path.endsWith(".lua")) return false;
    static const QSet<QString> core{ "lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua", "lua/sgs_ex.lua", "lua/lib/json.lua" };
    return !core.contains(path) && (path.startsWith("extensions/") || path.startsWith("lua/") || path.startsWith("lang/"));
}
bool directory(const QString &path, QString *error)
{
    QFileInfo info(path);
    if (info.isSymLink()) return fail(error, "content directory is a symbolic link: " + path);
    const QString parent = info.absolutePath();
    if (parent != info.absoluteFilePath() && !directory(parent, error)) return false;
    if (info.exists()) return info.isDir() || fail(error, "not a directory: " + path);
    return QDir().mkpath(path) || fail(error, "cannot create directory: " + path);
}
bool regular(const QString &path)
{
    QFileInfo info(path);
    if (!info.isFile() || info.isSymLink()) return false;
    QDir parent = info.dir();
    while (true) {
        if (QFileInfo(parent.absolutePath()).isSymLink()) return false;
        if (!parent.cdUp()) break;
    }
    return true;
}
bool jsonRead(const QString &path, QVariantMap *map, QString *error, QString *sha256 = nullptr)
{
    QFile file(path);
    if ((!path.startsWith(":/") && !regular(path)) || !file.open(QIODevice::ReadOnly)
        || file.size() > kJsonLimit) return fail(error, "cannot read bounded content metadata: " + path);
    QJsonParseError parse;
    const QByteArray bytes = file.read(kJsonLimit + 1);
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail(error, "invalid content JSON: " + path);
    *map = document.object().toVariantMap();
    if (sha256) *sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return true;
}
bool jsonWrite(const QString &path, const QVariantMap &map, QString *error)
{
    if (!directory(QFileInfo(path).absolutePath(), error) || QFileInfo(path).isSymLink()) return false;
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);
    // Never fall back to in-place writes: an old runtime must stay intact.
    file.setDirectWriteFallback(false);
    if (bytes.size() > kJsonLimit || !file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit()) return fail(error, "cannot atomically publish metadata: " + path);
    return true;
}
bool space(const QString &path, quint64 bytes, QString *error)
{
    QStorageInfo storage(path);
    storage.refresh();
    if (!storage.isValid() || !storage.isReady() || storage.bytesAvailable() < 0
        || quint64(storage.bytesAvailable()) < bytes + kReserve)
        return fail(error, "insufficient private storage for a complete content version");
    return true;
}
bool copyFile(const QString &source, const QString &target, QString *error, bool shareMedia = false,
              AndroidContentStore::Cancelled *cancel = nullptr)
{
    if (cancelled(cancel, error)) return false;
    if (!source.startsWith(":/") && !regular(source)) return fail(error, "non-regular content source: " + source);
    if (!directory(QFileInfo(target).absolutePath(), error) || QFileInfo(target).isSymLink()) return false;
#ifdef Q_OS_ANDROID
    // Only immutable media payloads share storage across snapshots. Lua and
    // baseline capture remain physical copies; replacing links uses QSaveFile.
    if (shareMedia && !QFileInfo::exists(target)
        && ::link(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) == 0) return true;
#else
    Q_UNUSED(shareMedia);
#endif
    if (shareMedia && !space(QFileInfo(target).absolutePath(), quint64(QFileInfo(source).size()), error)) return false;
    QFile input(source);
    QSaveFile output(target);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return fail(error, "cannot copy content: " + source);
    // Fallback copies and writable Lua never share an inode with old versions.
    while (!input.atEnd()) {
        if (cancelled(cancel, error)) return false;
        const QByteArray block = input.read(256 * 1024);
        if (block.isEmpty() && input.error() != QFileDevice::NoError) return fail(error, "content read failed");
        if (output.write(block) != block.size()) return fail(error, "content write failed");
    }
    if (cancelled(cancel, error)) return false;
    return output.commit() || fail(error, "content publish failed");
}
QStringList treeFiles(const QString &root, QString *error)
{
    QStringList result;
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink() || (!info.isFile() && !info.isDir())) {
            fail(error, "non-regular object in content tree"); return {};
        }
        if (info.isFile()) result << QDir(root).relativeFilePath(info.filePath());
    }
    return result;
}
QString digest(const QString &path, QString *error, AndroidContentStore::Cancelled *cancel = nullptr)
{
    QFile file(path);
    if ((!path.startsWith(":/") && !regular(path)) || !file.open(QIODevice::ReadOnly)) { fail(error, "cannot hash content"); return {}; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (cancelled(cancel, error)) return {};
        const QByteArray block = file.read(256 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) { fail(error, "content hash read failed"); return {}; }
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}
QStringList entryFiles(const QVariantMap &entry)
{
    QStringList result{entry.value("script").toString()};
    for (const char *role : {"libs", "lang", "ai"}) result << entry.value(QLatin1String(role)).toStringList();
    return result;
}
QVariantMap descriptor(const QVariantList &entries)
{
    return {{"schema_version", 2}, {"profile", "declared-v2"}, {"extensions", entries}};
}
QVariantMap effective(const QVariantMap &package)
{
    // Disable means the whole package is absent. Restoring APK bytes is an
    // explicit remove/recovery action, never a hidden effect of disabling.
    return package.value("enabled", true).toBool() ? package : QVariantMap();
}
QVariantMap infoMap(const QString &id, const QString &blob, const QStringList &files, const QVariantList &entries, bool bundled)
{
    return {{"id", id}, {"version", blob}, {"role", "extension"}, {"enabled", true},
            {"bundled", bundled}, {"files", files}, {"entries", entries}};
}
bool mergeBaselinePackages(const QVariantMap &snapshot, const QVariantMap &base,
                           QVariantMap *merged, QString *error)
{
    QVariantList packages = snapshot.value("packages").toList();
    QStringList baselineIds;
    for (const QVariant &value : base.value("packages").toList()) {
        const QVariantMap bundled = value.toMap();
        const QString id = bundled.value("id").toString();
        baselineIds << id;
        bool found = false;
        for (const QVariant &existing : packages) {
            const QVariantMap p = existing.toMap();
            if (key(p.value("id").toString()) != key(id)) continue;
            // A prior bundled override keeps its fallback. An unrelated user
            // package with the newly introduced name must not be overwritten.
            if (p.value("id").toString() != id
                || (!p.value("bundled").toBool() && !p.value("fallback").toMap().value("bundled").toBool()))
                return fail(error, "new APK package conflicts with a user package: " + id);
            found = true;
            break;
        }
        if (!found) packages.append(bundled);
    }
    *merged = snapshot;
    merged->insert("packages", packages);
    merged->insert("baseline_revision", base.value("revision"));
    merged->insert("baseline_packages", baselineIds);
    return true;
}
bool cancelled(AndroidContentStore::Cancelled *cancel, QString *error)
{
    if (!cancel || !cancel->load()) return false;
    fail(error, "content import cancelled");
    return true;
}
bool readZipJson(AndroidZipReader &reader, const AndroidZipReader::Entry &entry,
                 QVariantMap *map, AndroidContentStore::Cancelled *cancel, QString *error)
{
    if (entry.uncompressedSize > quint64(kJsonLimit)) return fail(error, "content manifest is too large");
    QByteArray bytes;
    QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if (!reader.extract(entry, buffer, cancel, {}, error)) return false;
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail(error, "invalid import manifest JSON");
    *map = document.object().toVariantMap();
    return true;
}
}

AndroidContentStore::AndroidContentStore(const QString &appDataRoot)
{
    const QString selected = appDataRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : appDataRoot;
    const QFileInfo info(selected);
    m_appDataRoot = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    m_storeRoot = QDir(m_appDataRoot).filePath("content");
    m_baseRoot = QDir(m_storeRoot).filePath("baseline/runtime");
    m_statePath = QDir(m_storeRoot).filePath("state.json");
    m_lockPath = QDir(m_storeRoot).filePath("store.lock");
}

bool AndroidContentStore::commitState(const QVariantMap &state, QString *error)
{
    if (!jsonWrite(m_statePath, state, error)) return false;
    m_state = state;
    return true;
}

void AndroidContentStore::refreshPackages()
{
    m_packages.clear();
    for (const QVariant &value : m_snapshot.value("packages").toList()) {
        const QVariantMap map = value.toMap();
        PackageInfo p;
        p.id = map.value("id").toString(); p.version = map.value("version").toString();
        p.role = map.value("role").toString(); p.enabled = map.value("enabled", true).toBool();
        p.bundled = map.value("bundled").toBool(); p.state = p.enabled ? "enabled" : "disabled";
        p.files = map.value("files").toStringList(); p.archiveBytes = map.value("archiveBytes").toULongLong();
        p.expandedBytes = map.value("expandedBytes").toULongLong(); p.sha256 = map.value("sha256").toString();
        m_packages.append(p);
    }
}

bool AndroidContentStore::loadSnapshot(const QString &id, QVariantMap *snapshot, QString *error,
                                       const QVariantMap &validated, QVariantMap *receipt) const
{
    if (!versionId(id)) return fail(error, "invalid content version id");
    const QString root = QDir(m_storeRoot).filePath("versions/" + id);
    QString metadataHash, descriptorHash;
    if (!jsonRead(root + "/metadata.json", snapshot, error, &metadataHash)) return false;
    QVariantMap runtimeDescriptor;
    if (!jsonRead(root + "/runtime/runtime-content.json", &runtimeDescriptor, error, &descriptorHash)) return false;
    const auto parsed = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(runtimeDescriptor));
    if (!parsed.isValid()) return fail(error, parsed.error);
    QVariantMap checked{{"active", id}, {"metadata_sha256", metadataHash}, {"descriptor_sha256", descriptorHash}};
    // Only prepareStartup may reuse a receipt, after checking the APK/baseline
    // revisions and excluding pending changes and failed boots. Payloads are
    // validated on change; ordinary boots inspect only these core sentinels.
    const bool reuse = validated.value("active").toString() == id
        && validated.value("metadata_sha256").toString() == metadataHash
        && validated.value("descriptor_sha256").toString() == descriptorHash;
    for (const QString &path : {QStringLiteral("lua/config.lua"), QStringLiteral("lua/sanguosha.lua"),
                                QStringLiteral("lua/ai/smart-ai.lua")})
        if (!regular(root + "/runtime/" + path)) return fail(error, "content version is incomplete: " + path);
    if (reuse) {
        snapshot->insert("media_ready", validated.value("media_ready", false));
        checked.insert("media_ready", snapshot->value("media_ready"));
        if (receipt) *receipt = checked;
        return true;
    }
    QVariantList entries;
    bool completeMedia = false;
    bool mediaValid = true;
    for (const QVariant &value : snapshot->value("packages").toList()) {
        const QVariantMap p = effective(value.toMap());
        if (p.isEmpty()) continue;
        entries << p.value("entries").toList();
        for (const QString &path : p.value("files").toStringList()) {
            if (!safePath(path)) return fail(error, "invalid snapshot package path");
            if (!regular(root + "/runtime/" + path)) {
                if (mediaPath(path)) mediaValid = false;
                else return fail(error, "content package file is missing: " + path);
            }
        }
        if (p.value("role").toString() == "media" && p.value("complete_media").toBool()) {
            completeMedia = true;
            const QVariantList records = p.value("media_records").toList();
            if (records.size() != p.value("files").toStringList().size()) mediaValid = false;
            for (const QVariant &record : records) {
                const QVariantMap r = record.toMap();
                const QString path = r.value("path").toString();
                if (!mediaPath(path) || !regular(root + "/runtime/" + path)
                    || quint64(QFileInfo(root + "/runtime/" + path).size()) != r.value("size").toULongLong())
                    mediaValid = false;
            }
            QVariantMap inventory;
            if (!jsonRead(":/assets/media-inventory.json", &inventory, error)) return false;
            const QStringList files = p.value("files").toStringList();
            const QSet<QString> names(files.cbegin(), files.cend());
            if (inventory.value("files").toStringList().isEmpty()) return fail(error, "APK media inventory is missing");
            for (const QString &path : inventory.value("files").toStringList())
                if (!names.contains(path)) mediaValid = false;
        }
    }
    if (QJsonObject::fromVariantMap(descriptor(entries)) != QJsonObject::fromVariantMap(runtimeDescriptor))
        return fail(error, "snapshot descriptor does not match its packages");
    snapshot->insert("media_ready", completeMedia && mediaValid);
    for (const QString &path : QSanRules::manifestDeliveredFiles(parsed) + QSanRules::manifestServerOnlyFiles(parsed))
        if (!regular(root + "/runtime/" + path)) return fail(error, "content version is incomplete: " + path);
    checked.insert("media_ready", snapshot->value("media_ready"));
    if (receipt) *receipt = checked;
    return true;
}

bool AndroidContentStore::prepareStartup(QString *error)
{
    QElapsedTimer startupTimer;
    startupTimer.start();
    if (error) error->clear();
    if (!directory(m_storeRoot, error)) return false;
    QLockFile lock(m_lockPath);
    if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    if (!directory(m_storeRoot + "/staging", error) || !directory(m_storeRoot + "/versions", error)
        || !directory(m_storeRoot + "/blobs", error)) return false;
    // A killed process cannot run QTemporaryDir's destructor. Under the store
    // lock, only our known temporary directories are eligible for cleanup.
    static const QRegularExpression temporaryName(QStringLiteral("^(base|legacy|version|media|extension)-[A-Za-z0-9]{6}$"));
    for (const QFileInfo &info : QDir(m_storeRoot + "/staging").entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!info.isSymLink() && temporaryName.match(info.fileName()).hasMatch()) QDir(info.absoluteFilePath()).removeRecursively();
    // QTemporaryFile also needs recovery after process death during SAF reads.
    static const QRegularExpression spoolName(QStringLiteral("^archive-[A-Za-z0-9]{6}\\.zip$"));
    for (const QFileInfo &info : QDir(m_storeRoot + "/staging").entryInfoList(QDir::Files))
        if (!info.isSymLink() && spoolName.match(info.fileName()).hasMatch()) QFile::remove(info.absoluteFilePath());
    if (QFileInfo::exists(m_statePath) && !jsonRead(m_statePath, &m_state, error)) return false;
    const QString baseMetadata = m_storeRoot + "/baseline/metadata.json";
    QVariantMap base;
    if (QFileInfo::exists(baseMetadata) && !jsonRead(baseMetadata, &base, error)) return false;
    // Generated at build time from resource names and bytes, including the
    // independent media inventory. Older APKs without it keep the full path.
    QFile revisionFile(QStringLiteral(":/android-content-revision.txt"));
    QString apkRevision;
    if (revisionFile.open(QIODevice::ReadOnly) && revisionFile.size() <= 65)
        apkRevision = QString::fromLatin1(revisionFile.read(65)).trimmed();
    static const QRegularExpression revisionPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!revisionPattern.match(apkRevision).hasMatch()) apkRevision.clear();
    const bool refreshBaseline = apkRevision.isEmpty() || base.value("apk_revision").toString() != apkRevision
        || base.value("revision").toString().isEmpty() || needsRecovery()
        || !QFileInfo(m_baseRoot).isDir();
    // Missing-only deployment is needed on installation/upgrade/recovery,
    // not on every unchanged boot. Existing user's CP1 files stay untouched.
    const QString deployed = m_appDataRoot + "/runtime";
    if (refreshBaseline && !AndroidAssets::copyAssetDir({}, deployed, error)) return false;

    if (!QFileInfo::exists(baseMetadata)) {
        // The fallback is the actual APK bytes, not a user's edited CP1 tree.
        QVariantMap baseDescriptor;
        if (!jsonRead(":/assets/runtime-content-base.json", &baseDescriptor, error)) return false;
        const auto parsed = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(baseDescriptor));
        if (!parsed.isValid()) return fail(error, parsed.error);
        QVariantList packages;
        for (const QVariant &entry : baseDescriptor.value("extensions").toList()) {
            const QVariantMap e = entry.toMap();
            if (!packageId(e.value("name").toString())) return fail(error, "invalid bundled package name");
            packages.append(infoMap(e.value("name").toString(), "base", entryFiles(e), {e}, true));
        }
        QTemporaryDir temp(m_storeRoot + "/staging/base-XXXXXX");
        if (!temp.isValid()) return fail(error, "cannot stage baseline");
        QString scanError;
        const QStringList files = treeFiles(":/assets", &scanError);
        if (!scanError.isEmpty()) return fail(error, scanError);
        quint64 bytes = 0;
        for (const QString &path : files) bytes += quint64(QFileInfo(":/assets/" + path).size());
        if (!space(m_storeRoot, bytes, error)) return false;
        for (const QString &path : files)
            if (!copyFile(":/assets/" + path, temp.path() + "/runtime/" + path, error)) return false;
        QSet<QString> changed;
        const QStringList deployedFiles = treeFiles(deployed, &scanError);
        if (!scanError.isEmpty()) return fail(error, scanError);
        for (const QString &path : deployedFiles) {
            if (!safePath(path)) return fail(error, "invalid legacy runtime path");
            const QString existingHash = digest(deployed + '/' + path, error);
            if (existingHash.isEmpty()) return false;
            if (!QFileInfo::exists(":/assets/" + path)) changed.insert(path);
            else {
                const QString originalHash = digest(":/assets/" + path, error);
                if (originalHash.isEmpty()) return false;
                if (existingHash != originalHash) changed.insert(path);
            }
        }
        QVariantMap initial{{"packages", packages}};
        if (!changed.isEmpty()) {
            QTemporaryDir legacy(m_storeRoot + "/staging/legacy-XXXXXX");
            if (!legacy.isValid()) return fail(error, "cannot preserve legacy edits");
            const QString blob = uuid();
            QVariantList selected = packages;
            QSet<QString> packageOwned;
            QSet<QString> capture = changed;
            for (QVariant &value : selected) {
                const QVariantMap original = value.toMap();
                const QStringList owned = original.value("files").toStringList();
                bool modified = false;
                for (const QString &path : owned) { packageOwned.insert(path); if (changed.contains(path)) modified = true; }
                if (!modified) continue;
                for (const QString &path : owned) capture.insert(path);
                QVariantMap overridePackage = original;
                overridePackage.insert("version", blob); overridePackage.insert("bundled", false);
                overridePackage.insert("fallback", original); value = overridePackage;
            }
            QVariantMap legacyOverrides;
            quint64 legacyBytes = 0;
            for (const QString &path : capture) legacyBytes += quint64(QFileInfo(deployed + '/' + path).size());
            if (!space(m_storeRoot, legacyBytes, error)) return false;
            for (const QString &path : capture) {
                if (!copyFile(deployed + '/' + path, legacy.path() + "/runtime/" + path, error)) return false;
                if (!packageOwned.contains(path) && path != "runtime-content.json" && path != "runtime-content-base.json") legacyOverrides.insert(path, blob);
            }
            if (!QDir().rename(legacy.path(), m_storeRoot + "/blobs/" + blob)) return fail(error, "cannot preserve legacy payload");
            legacy.setAutoRemove(false);
            initial.insert("packages", selected); initial.insert("legacy_overrides", legacyOverrides);
        }
        if (!jsonWrite(temp.path() + "/metadata.json", {{"packages", packages}, {"initial_snapshot", initial}}, error)) return false;
        if (!QDir().rename(temp.path(), m_storeRoot + "/baseline")) return fail(error, "cannot publish baseline");
        temp.setAutoRemove(false);
    }
    if (!jsonRead(baseMetadata, &base, error)) return false;
    // APK upgrades extend the original tree without replacing any existing
    // inode. The revision is published only after every missing file exists;
    // interruption simply retries the remaining files on the next launch.
    if (refreshBaseline) {
        QVariantMap currentDescriptor;
        if (!jsonRead(":/assets/runtime-content-base.json", &currentDescriptor, error)) return false;
        const auto currentManifest = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(currentDescriptor));
        if (!currentManifest.isValid()) return fail(error, currentManifest.error);
        QString resourceError;
        const QStringList currentFiles = treeFiles(":/assets", &resourceError);
        if (!resourceError.isEmpty()) return fail(error, resourceError);
        bool baselineChanged = base.value("revision").toString().isEmpty();
        QStringList knownResources = base.value("resource_paths").toStringList();
        QSet<QString> knownResourceSet(knownResources.cbegin(), knownResources.cend());
        for (const QString &path : currentFiles) {
            if (!knownResourceSet.contains(path)) {
                knownResourceSet.insert(path); knownResources << path; baselineChanged = true;
            }
            const QString target = m_baseRoot + '/' + path;
            if (!QFileInfo::exists(target)) {
                if (!space(m_storeRoot, quint64(QFileInfo(":/assets/" + path).size()), error)
                    || !AndroidAssets::copyAssetFile(path, target, error)) return false;
                baselineChanged = true;
            } else if (!regular(target)) return fail(error, "non-regular baseline content: " + path);
        }
        QVariantList baselinePackages = base.value("packages").toList();
        for (const QVariant &entry : currentDescriptor.value("extensions").toList()) {
            const QVariantMap e = entry.toMap();
            const QString id = e.value("name").toString();
            if (!packageId(id)) return fail(error, "invalid new bundled package name");
            bool present = false;
            for (const QVariant &p : baselinePackages) {
                const QString oldId = p.toMap().value("id").toString();
                if (key(oldId) == key(id)) {
                    if (oldId != id) return fail(error, "case-colliding APK package name: " + id);
                    present = true; break;
                }
            }
            if (!present) {
                baselinePackages.append(infoMap(id, "base", entryFiles(e), {e}, true));
                baselineChanged = true;
            }
        }
        if (baselineChanged) {
            base.insert("packages", baselinePackages); base.insert("revision", uuid());
            base.insert("resource_paths", knownResources);
        }
        if (baselineChanged || base.value("apk_revision").toString() != apkRevision) {
            base.insert("apk_revision", apkRevision);
            if (!jsonWrite(baseMetadata, base, error)) return false;
        }
    }
    QVariantMap validation = m_state.value("startup_validation").toMap();
    if (refreshBaseline || hasPending() || needsRecovery() || m_state.value("pending_invalid").toBool()
        || m_state.value("upgrade_conflict").toBool() || !m_state.value("recovery_error").toString().isEmpty()
        || validation.value("schema").toInt() != 1 || !validation.contains("media_ready")
        || validation.value("active") != m_state.value("active")
        || validation.value("apk_revision").toString() != apkRevision
        || validation.value("baseline_revision") != base.value("revision")) validation.clear();
    // A pending selection and the final active load often refer to the same
    // immutable version. Validate it once under this store lock.
    QMap<QString, QVariantMap> loadedSnapshots, receipts;
    const auto loadForStartup = [&](const QString &id, QVariantMap *snapshot, QString *loadError) {
        if (loadedSnapshots.contains(id)) { *snapshot = loadedSnapshots.value(id); return true; }
        QVariantMap receipt;
        if (!loadSnapshot(id, snapshot, loadError, validation, &receipt)) return false;
        loadedSnapshots.insert(id, *snapshot); receipts.insert(id, receipt);
        return true;
    };
    if (m_state.value("active").toString().isEmpty()) {
        QString initial;
        const QVariantMap seed = base.value("initial_snapshot").toMap().isEmpty() ? base : base.value("initial_snapshot").toMap();
        if (!publishSnapshot(seed, &initial, error)) return false;
        QVariantMap state = m_state; state.insert("active", initial);
        if (!commitState(state, error)) return false;
    }
    // A previous boot failure is presented to the recovery UI before applying
    // another pending change, preserving the failed version for diagnosis.
    const QString pending = m_state.value("pending").toString();
    if (needsRecovery() && m_state.value("upgrade_conflict").toBool() && !pending.isEmpty()) {
        QVariantMap repaired; QString ignored;
        // A successful management operation has already recomposed the whole
        // pending version. Apply that explicit repair on this fresh startup.
        if (loadForStartup(pending, &repaired, &ignored) && repaired.value("baseline_revision") == base.value("revision")) {
            QVariantMap state = m_state; state.remove("boot_attempt"); state.remove("upgrade_conflict"); state.remove("recovery_error");
            if (!commitState(state, error)) return false;
        }
    }
    if (!needsRecovery()) {
        QVariantMap checked;
        QString pendingError;
        const QString selected = pending.isEmpty() ? m_state.value("active").toString() : pending;
        const bool valid = loadForStartup(selected, &checked, &pendingError);
        QVariantMap state = m_state;
        if (valid) {
            QString activated = selected;
            if (checked.value("baseline_revision") != base.value("revision")) {
                // Compose from the latest pending edits, preserving existing
                // names/order/enabled flags while appending new bundled names.
                QString migrationError;
                if (!publishSnapshot(checked, &activated, &migrationError)) {
                    state.insert("boot_attempt", true); state.insert("upgrade_conflict", true);
                    state.insert("recovery_error", migrationError); activated.clear();
                }
            }
            if (!activated.isEmpty() && activated != state.value("active").toString()) {
                state.insert("previous", state.value("active")); state.insert("active", activated);
            }
            if (!activated.isEmpty()) state.remove("pending");
        } else {
            state.insert("boot_attempt", true);
            if (!pending.isEmpty()) state.insert("pending_invalid", true);
            state.insert("recovery_error", pendingError);
        }
        if (!commitState(state, error)) return false;
    }
    const QString active = m_state.value("active").toString();
    QVariantMap activeSnapshot;
    QString activeError;
    if (!loadForStartup(active, &activeSnapshot, &activeError)) {
        QVariantMap state = m_state; state.insert("boot_attempt", true); state.insert("recovery_error", activeError);
        if (!commitState(state, error)) return false;
        QString ignored;
        if (!versionId(active) || !jsonRead(m_storeRoot + "/versions/" + active + "/metadata.json", &activeSnapshot, &ignored)) activeSnapshot = base;
        activeSnapshot.insert("media_ready", false);
    }
    m_runtimeRoot = m_storeRoot + "/versions/" + active + "/runtime";
    m_state.insert("active_media_ready", activeSnapshot.value("media_ready", false));
    m_snapshot = activeSnapshot;
    if (hasPending()) {
        QVariantMap staged; QString pendingError;
        if (loadForStartup(m_state.value("pending").toString(), &staged, &pendingError)) m_snapshot = staged;
        else {
            QVariantMap state = m_state; state.insert("boot_attempt", true); state.insert("pending_invalid", true); state.insert("recovery_error", pendingError);
            if (!commitState(state, error)) return false;
        }
    }
    QVariantMap state = m_state;
    state.remove("startup_validation");
    if (!apkRevision.isEmpty() && !needsRecovery() && !hasPending() && receipts.contains(active)) {
        QVariantMap receipt = receipts.value(active);
        receipt.insert("schema", 1); receipt.insert("apk_revision", apkRevision);
        receipt.insert("baseline_revision", base.value("revision"));
        state.insert("startup_validation", receipt);
    }
    if (!commitState(state, error)) return false;
    m_prepared = true;
    refreshPackages();
    collectUnusedVersions();
    qInfo("Android content startup: baseline=%s elapsed_ms=%lld", refreshBaseline ? "refresh" : "cached",
          static_cast<long long>(startupTimer.elapsed()));
    return true;
}

bool AndroidContentStore::publishSnapshot(const QVariantMap &snapshot, QString *version, QString *error, Cancelled *cancel)
{
    if (cancelled(cancel, error)) return false;
    QVariantMap base;
    if (!jsonRead(m_storeRoot + "/baseline/metadata.json", &base, error)) return false;
    QVariantMap composed;
    if (!mergeBaselinePackages(snapshot, base, &composed, error)) return false;
    QSet<QString> baselineOwned;
    for (const QVariant &value : base.value("packages").toList())
        for (const QString &path : value.toMap().value("files").toStringList()) baselineOwned.insert(key(path));
    QMap<QString, QString> sources;
    QString scanError;
    const QStringList baseFiles = treeFiles(m_baseRoot, &scanError);
    if (!scanError.isEmpty()) return fail(error, scanError);
    for (const QString &path : baseFiles) {
        if (cancelled(cancel, error)) return false;
        if (!safePath(path)) return fail(error, "invalid baseline path");
        if (!baselineOwned.contains(key(path)) && path != "runtime-content.json" && path != "runtime-content-base.json")
            sources.insert(path, m_baseRoot + '/' + path);
    }
    const QVariantMap legacy = composed.value("legacy_overrides").toMap();
    for (auto it = legacy.cbegin(); it != legacy.cend(); ++it) {
        if (!safePath(it.key()) || !versionId(it.value().toString()) || baselineOwned.contains(key(it.key())))
            return fail(error, "invalid preserved legacy override");
        const QString source = m_storeRoot + "/blobs/" + it.value().toString() + "/runtime/" + it.key();
        if (!regular(source)) return fail(error, "preserved legacy override is missing");
        sources.insert(it.key(), source);
    }
    QSet<QString> ids, owners;
    QVariantList entries;
    bool completeMedia = false;
    for (const QVariant &value : composed.value("packages").toList()) {
        if (cancelled(cancel, error)) return false;
        const QVariantMap original = value.toMap();
        const QString id = original.value("id").toString();
        if (!packageId(id) || ids.contains(key(id))) return fail(error, "duplicate or invalid package id");
        ids.insert(key(id));
        const QVariantMap p = effective(original);
        if (p.isEmpty()) continue;
        const QString blob = p.value("version").toString();
        if (blob != "base" && !versionId(blob)) return fail(error, "invalid package version");
        const QString root = blob == "base" ? m_baseRoot : m_storeRoot + "/blobs/" + blob + "/runtime";
        const QStringList files = p.value("files").toStringList();
        const QSet<QString> fileSet(files.cbegin(), files.cend());
        for (const QString &path : files) {
            if (cancelled(cancel, error)) return false;
            if (!safePath(path) || owners.contains(key(path))) return fail(error, "content path is owned by another enabled package: " + path);
            owners.insert(key(path));
            if (blob != "base" && !(p.value("role").toString() == "media" ? mediaPath(path) : extensionPath(path)))
                return fail(error, "package attempts to replace protected runtime content: " + path);
            if (!regular(root + '/' + path)) return fail(error, "package payload is missing: " + path);
            // Imported content may replace media and a same-package bundled
            // payload, but never an unrelated core Lua/runtime file.
            if (sources.contains(path) && !mediaPath(path)) return fail(error, "package collides with baseline content: " + path);
            sources.insert(path, root + '/' + path);
        }
        for (const QVariant &entry : p.value("entries").toList()) {
            for (const QString &path : entryFiles(entry.toMap()))
                if (!fileSet.contains(path)) return fail(error, "descriptor references a file outside its package: " + path);
            entries.append(entry);
        }
        if (p.value("role").toString() == "media" && p.value("complete_media").toBool()) completeMedia = true;
    }
    const QVariantMap contentDescriptor = descriptor(entries);
    const auto parsed = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(contentDescriptor));
    if (!parsed.isValid()) return fail(error, parsed.error);
    quint64 bytes = 0;
    QSet<QString> portable;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        if (portable.contains(key(it.key()))) return fail(error, "case-colliding runtime paths");
        portable.insert(key(it.key()));
#ifdef Q_OS_ANDROID
        if (!mediaPath(it.key()))
#endif
            bytes += quint64(QFileInfo(it.value()).size());
    }
    if (!space(m_storeRoot, bytes, error)) return false;
    QTemporaryDir temp(m_storeRoot + "/staging/version-XXXXXX");
    if (!temp.isValid()) return fail(error, "cannot stage content version");
    for (auto it = sources.cbegin(); it != sources.cend(); ++it)
        if (!copyFile(it.value(), temp.path() + "/runtime/" + it.key(), error, mediaPath(it.key()), cancel)) return false;
    QVariantMap metadata = composed;
    metadata.insert("media_ready", completeMedia);
    metadata.insert("descriptor", contentDescriptor);
    if (!jsonWrite(temp.path() + "/runtime/runtime-content.json", contentDescriptor, error)
        || !jsonWrite(temp.path() + "/metadata.json", metadata, error)) return false;
    const QString id = uuid();
    if (cancelled(cancel, error)) return false;
    if (!QDir().rename(temp.path(), m_storeRoot + "/versions/" + id)) return fail(error, "cannot publish complete content version");
    temp.setAutoRemove(false);
    *version = id;
    return true;
}

bool AndroidContentStore::stageSnapshot(const QVariantMap &snapshot, QString *error, Cancelled *cancel)
{
    QString version;
    if (!publishSnapshot(snapshot, &version, error, cancel)) return false;
    QVariantMap state = m_state;
    state.insert("pending", version);
    state.remove("startup_validation");
    if (cancelled(cancel, error)) return false;
    if (!commitState(state, error)) return false;
    if (!jsonRead(m_storeRoot + "/versions/" + version + "/metadata.json", &m_snapshot, error)) return false;
    refreshPackages();
    collectUnusedVersions();
    return true;
}

void AndroidContentStore::collectUnusedVersions()
{
    // Only UUID directories created by this store can be garbage-collected.
    // Retain all snapshots referenced by the durable journal, then their blobs.
    QSet<QString> keepVersions, keepBlobs;
    QVariantMap baseline;
    QString baselineError;
    if (!jsonRead(m_storeRoot + "/baseline/metadata.json", &baseline, &baselineError)) return;
    const QVariantMap seed = baseline.value("initial_snapshot").toMap();
    for (const QVariant &blob : seed.value("legacy_overrides").toMap()) keepBlobs.insert(blob.toString());
    for (const QVariant &p : seed.value("packages").toList()) {
        keepBlobs.insert(p.toMap().value("version").toString());
        keepBlobs.insert(p.toMap().value("fallback").toMap().value("version").toString());
    }
    for (const char *field : {"active", "pending", "previous"}) {
        const QString id = m_state.value(QLatin1String(field)).toString();
        if (versionId(id)) keepVersions.insert(id);
    }
    for (const QString &id : keepVersions) {
        QVariantMap snapshot;
        QString ignored;
        if (!jsonRead(m_storeRoot + "/versions/" + id + "/metadata.json", &snapshot, &ignored)) return;
        for (const QVariant &blob : snapshot.value("legacy_overrides").toMap()) keepBlobs.insert(blob.toString());
        for (const QVariant &p : snapshot.value("packages").toList()) {
            const QVariantMap map = p.toMap();
            keepBlobs.insert(map.value("version").toString());
            keepBlobs.insert(map.value("fallback").toMap().value("version").toString());
        }
    }
    for (const QString &subdir : {QStringLiteral("versions"), QStringLiteral("blobs")}) {
        const QSet<QString> &keep = subdir == "versions" ? keepVersions : keepBlobs;
        QDir dir(m_storeRoot + '/' + subdir);
        for (const QFileInfo &info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (versionId(info.fileName()) && !keep.contains(info.fileName()) && !info.isSymLink()) QDir(info.absoluteFilePath()).removeRecursively();
    }
}

QString AndroidContentStore::runtimeRoot() const { return m_runtimeRoot; }
bool AndroidContentStore::mediaReady() const { return m_state.value("active_media_ready").toBool(); }
bool AndroidContentStore::needsRecovery() const { return m_state.value("boot_attempt").toBool(); }
bool AndroidContentStore::hasPending() const { return !m_state.value("pending").toString().isEmpty(); }
QVariantMap AndroidContentStore::status() const { return m_state; }
QList<AndroidContentStore::PackageInfo> AndroidContentStore::packages() const { return m_packages; }

bool AndroidContentStore::beginBootAttempt(QString *error)
{
    if (!m_prepared) return fail(error, "content store has not been prepared");
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QVariantMap state = m_state; state.insert("boot_attempt", true);
    return commitState(state, error);
}

bool AndroidContentStore::markBootSuccessful(QString *error)
{
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QVariantMap state = m_state; state.remove("boot_attempt");
    return commitState(state, error);
}

bool AndroidContentStore::recoverPrevious(QString *error)
{
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QString previous = m_state.value("previous").toString();
    QVariantMap restored;
    if (!loadSnapshot(previous, &restored, error)) return false;
    QVariantMap base;
    if (!jsonRead(m_storeRoot + "/baseline/metadata.json", &base, error)) return false;
    if (restored.value("baseline_revision") != base.value("revision")) {
        if (!publishSnapshot(restored, &previous, error) || !loadSnapshot(previous, &restored, error)) return false;
    }
    QVariantMap state = m_state;
    state.remove("startup_validation");
    state.insert("active", previous); state.remove("previous"); state.remove("pending"); state.remove("boot_attempt");
    state.remove("pending_invalid"); state.remove("recovery_error"); state.remove("upgrade_conflict");
    state.insert("active_media_ready", restored.value("media_ready", false));
    if (!commitState(state, error)) return false;
    m_runtimeRoot = m_storeRoot + "/versions/" + previous + "/runtime";
    m_snapshot = restored; refreshPackages();
    return true;
}

bool AndroidContentStore::discardPendingAndDisableLastImport(QString *error)
{
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    if (!needsRecovery() || m_state.value("pending_invalid").toBool()) {
        QVariantMap active;
        QString activeError;
        if (loadSnapshot(m_state.value("active").toString(), &active, &activeError)) {
            QVariantMap state = m_state; state.remove("pending"); state.remove("startup_validation");
            if (needsRecovery()) {
                QVariantMap base;
                if (!jsonRead(m_storeRoot + "/baseline/metadata.json", &base, error)) return false;
                if (active.value("baseline_revision") != base.value("revision")) {
                    QString migrated;
                    if (!publishSnapshot(active, &migrated, error) || !loadSnapshot(migrated, &active, error)) return false;
                    state.insert("previous", state.value("active")); state.insert("active", migrated);
                }
            }
            if (state.value("pending_invalid").toBool()) { state.remove("pending_invalid"); state.remove("boot_attempt"); state.remove("recovery_error"); }
            if (!commitState(state, error)) return false;
            m_runtimeRoot = m_storeRoot + "/versions/" + state.value("active").toString() + "/runtime";
            m_state.insert("active_media_ready", active.value("media_ready", false));
            m_snapshot = active; refreshPackages(); collectUnusedVersions();
            return true;
        }
        if (!needsRecovery()) return fail(error, activeError);
    }
    // Compose a safe boot runtime even when the first imported package failed
    // and there is no previous imported version. Keep valid complete media.
    QVariantMap active;
    const QString activeId = m_state.value("active").toString();
    if (!versionId(activeId) || !jsonRead(m_storeRoot + "/versions/" + activeId + "/metadata.json", &active, error)) {
        if (!jsonRead(m_storeRoot + "/baseline/metadata.json", &active, error)) return false;
    }
    if (!active.contains("packages") && !jsonRead(m_storeRoot + "/baseline/metadata.json", &active, error)) return false;
    if (m_state.value("upgrade_conflict").toBool()) {
        // Several colliding names cannot always be removed one at a time:
        // each intermediate composition could still fail on another name.
        // Explicit upgrade recovery restores the APK package set in one step,
        // retains media, and leaves all user payloads in the previous snapshot.
        QVariantMap base;
        if (!jsonRead(m_storeRoot + "/baseline/metadata.json", &base, error)) return false;
        QVariantList safePackages = base.value("packages").toList();
        for (const QVariant &value : active.value("packages").toList())
            if (value.toMap().value("role").toString() == "media") safePackages.append(value);
        active = {{"packages", safePackages}};
    }
    QVariantList list = active.value("packages").toList();
    const QString lastImport = active.value("last_extension_import").toString();
    bool disabled = false;
    for (QVariant &value : list) {
        QVariantMap p = value.toMap();
        if (!p.value("bundled").toBool() && p.value("role").toString() != "media"
            && (lastImport.isEmpty() || p.value("id").toString() == lastImport)) {
            if (!p.value("fallback").toMap().isEmpty()) p = p.value("fallback").toMap();
            else p.insert("enabled", false);
            disabled = true;
        }
        value = p;
    }
    Q_UNUSED(disabled);
    // Untracked legacy bootstrap edits can also be the failed boot cause.
    // Their bytes remain in the retained previous snapshot/blob for recovery.
    if (lastImport.isEmpty()) active.remove("legacy_overrides");
    active.insert("packages", list);
    QString safe;
    if (!publishSnapshot(active, &safe, error)) return false;
    QVariantMap state = m_state;
    state.remove("startup_validation");
    state.insert("previous", state.value("active")); state.insert("active", safe);
    state.remove("pending"); state.remove("boot_attempt");
    state.remove("pending_invalid"); state.remove("recovery_error"); state.remove("upgrade_conflict");
    if (!commitState(state, error)) return false;
    if (!loadSnapshot(safe, &m_snapshot, error)) return false;
    m_runtimeRoot = m_storeRoot + "/versions/" + safe + "/runtime";
    m_state.insert("active_media_ready", m_snapshot.value("media_ready", false));
    refreshPackages();
    return true;
}

bool AndroidContentStore::setPackageEnabled(const QString &id, bool enabled, QString *error)
{
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QVariantMap snapshot = m_snapshot;
    QVariantList list = snapshot.value("packages").toList();
    bool found = false;
    for (QVariant &value : list) {
        QVariantMap p = value.toMap();
        if (p.value("id").toString() == id) { p.insert("enabled", enabled); value = p; found = true; }
    }
    if (!found) return fail(error, "unknown package");
    snapshot.insert("packages", list);
    return stageSnapshot(snapshot, error);
}

bool AndroidContentStore::removePackage(const QString &id, QString *error)
{
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QVariantMap snapshot = m_snapshot;
    QVariantList list = snapshot.value("packages").toList();
    bool found = false;
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap p = list[i].toMap();
        if (p.value("id").toString() != id) continue;
        found = true;
        if (!p.value("fallback").toMap().isEmpty()) list[i] = p.value("fallback");
        else if (p.value("bundled").toBool()) { p.insert("enabled", false); list[i] = p; }
        else list.removeAt(i);
        break;
    }
    if (!found) return fail(error, "unknown package");
    snapshot.insert("packages", list);
    return stageSnapshot(snapshot, error);
}

bool AndroidContentStore::reorderPackages(const QStringList &ids, QString *error)
{
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    const QVariantList old = m_snapshot.value("packages").toList();
    if (ids.size() != old.size()) return fail(error, "package order must include every package once");
    QVariantList ordered; QSet<QString> seen;
    for (const QString &id : ids) {
        if (seen.contains(id)) return fail(error, "duplicate package in order");
        bool found = false;
        for (const QVariant &p : old) if (p.toMap().value("id").toString() == id) { ordered << p; found = true; break; }
        if (!found) return fail(error, "unknown package in order");
        seen.insert(id);
    }
    QVariantMap snapshot = m_snapshot; snapshot.insert("packages", ordered);
    // The final declared-v2 parser rejects dependency-breaking orders.
    return stageSnapshot(snapshot, error);
}

bool AndroidContentStore::rollback(QString *error)
{
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QVariantMap previous;
    if (!loadSnapshot(m_state.value("previous").toString(), &previous, error)) return false;
    return stageSnapshot(previous, error);
}

bool AndroidContentStore::exportDescriptor(QIODevice &destination, QString *error) const
{
    QVariantList entries;
    for (const QVariant &value : m_snapshot.value("packages").toList())
        entries << effective(value.toMap()).value("entries").toList();
    const QByteArray bytes = QJsonDocument::fromVariant(descriptor(entries)).toJson(QJsonDocument::Indented);
    qint64 written = 0;
    while (written < bytes.size()) {
        const qint64 n = destination.write(bytes.constData() + written, bytes.size() - written);
        if (n <= 0) return fail(error, "cannot export effective descriptor");
        written += n;
    }
    return true;
}

namespace {
QVariantMap withPackage(const QVariantMap &snapshot, QVariantMap package)
{
    QVariantMap result = snapshot;
    QVariantList list = snapshot.value("packages").toList();
    bool replaced = false;
    for (QVariant &value : list) {
        const QVariantMap old = value.toMap();
        if (old.value("id") != package.value("id")) continue;
        // Keep the immutable APK original even through several replacements.
        if (old.value("bundled").toBool()) package.insert("fallback", old);
        else if (!old.value("fallback").toMap().isEmpty()) package.insert("fallback", old.value("fallback"));
        value = package; replaced = true; break;
    }
    if (!replaced) list.append(package);
    result.insert("packages", list);
    result.insert("last_import", package.value("id"));
    if (package.value("role").toString() == "extension") result.insert("last_extension_import", package.value("id"));
    return result;
}
bool extractFile(AndroidZipReader &reader, const AndroidZipReader::Entry &entry,
                 const QString &path, AndroidContentStore::Cancelled *cancel,
                 const AndroidContentStore::Progress &progress, QString *error)
{
    if (!directory(QFileInfo(path).absolutePath(), error)) return false;
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) return fail(error, "cannot stage imported file");
    if (!reader.extract(entry, output, cancel, progress, error)) return false;
    return output.commit() || fail(error, "cannot publish imported file");
}
QVariantList inferEntries(const QStringList &files)
{
    QStringList scripts, libs, langs, ai;
    for (const QString &path : files) {
        if (path.startsWith("extensions/") && path.endsWith(".lua")) scripts << path;
        else if (path.startsWith("lua/ai/") || path == "lua/lib/middleclass.lua") ai << path;
        else if (path.startsWith("lua/") && path.endsWith(".lua")) libs << path;
        else if (path.startsWith("lang/") && path.endsWith(".lua")) langs << path;
    }
    scripts.sort(); libs.sort(); langs.sort(); ai.sort();
    QVariantList result;
    for (const QString &script : scripts) {
        QVariantMap entry{{"name", QFileInfo(script).completeBaseName()}, {"script", script}, {"dependencies", QStringList()},
                          {"libs", QStringList()}, {"lang", QStringList()}, {"ai", QStringList()}};
        // A descriptor-free ZIP is one package. Load its explicit support files
        // once before its alphabetically first script; never infer core libs.
        if (result.isEmpty()) { entry.insert("libs", libs); entry.insert("lang", langs); entry.insert("ai", ai); }
        result.append(entry);
    }
    return result;
}
bool coverage(const QVariantList &entries, const QStringList &files, QString *error)
{
    QSet<QString> declared;
    const QSet<QString> fileSet(files.cbegin(), files.cend());
    for (const QVariant &value : entries) {
        for (const QString &path : entryFiles(value.toMap())) {
            if (!fileSet.contains(path)) return fail(error, "descriptor references missing package file: " + path);
            if (declared.contains(key(path))) return fail(error, "duplicate descriptor file: " + path);
            declared.insert(key(path));
        }
    }
    for (const QString &path : files)
        if (path.endsWith(".lua") && !declared.contains(key(path))) return fail(error, "undeclared Lua in extension package: " + path);
    return !entries.isEmpty() || fail(error, "extension package contains no extension script");
}
}

bool AndroidContentStore::stageMedia(QIODevice &source, Cancelled *cancel, const Progress &progress, QString *error)
{
    if (!m_prepared) return fail(error, "content store has not been prepared");
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    ImportLimits limits;
    limits.maxArchiveBytes = quint64(3) * 1024 * 1024 * 1024;
    limits.maxExpandedBytes = quint64(4) * 1024 * 1024 * 1024;
    limits.maxEntryBytes = quint64(1024) * 1024 * 1024;
    limits.maxEntries = 200000; limits.maxCompressionRatio = 200;
    AndroidZipReader reader(m_storeRoot + QStringLiteral("/staging"));
    if (!reader.open(source, limits, cancel, error)) return false;
    const AndroidZipReader::Entry *manifestEntry = nullptr;
    QMap<QString, const AndroidZipReader::Entry *> payload;
    quint64 total = 0;
    for (const auto &entry : reader.entries()) {
        if (entry.directory) continue;
        if (entry.path == "qsan-media.json") { manifestEntry = &entry; continue; }
        if (!mediaPath(entry.path)) return fail(error, "unauthorized media payload path: " + entry.path);
        payload.insert(entry.path, &entry); total += entry.uncompressedSize;
    }
    if (!manifestEntry) return fail(error, "qsan-media.json is missing");
    QVariantMap manifest;
    if (!readZipJson(reader, *manifestEntry, &manifest, cancel, error)) return false;
    if (manifest.value("format").toInt() != 1 || manifest.value("role").toString() != "media"
        || !manifest.value("files").canConvert<QVariantList>() || manifest.value("expandedBytes").toULongLong() != total)
        return fail(error, "invalid media manifest");
    QMap<QString, QVariantMap> records;
    QSet<QString> folded;
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{64}$"));
    for (const QVariant &value : manifest.value("files").toList()) {
        const QVariantMap record = value.toMap();
        const QString path = record.value("path").toString();
        if (!mediaPath(path) || !payload.contains(path) || folded.contains(key(path))
            || !hashPattern.match(record.value("sha256").toString()).hasMatch()
            || record.value("size").toULongLong() != payload.value(path)->uncompressedSize)
            return fail(error, "media manifest path, size or digest mismatch: " + path);
        folded.insert(key(path)); records.insert(path, record);
    }
    if (records.size() != payload.size() || payload.isEmpty()) return fail(error, "media manifest does not exactly describe ZIP payload");
    QVariantMap inventory;
    if (!jsonRead(":/assets/media-inventory.json", &inventory, error)) return false;
    const QStringList required = inventory.value("files").toStringList();
    if (inventory.value("format").toInt() != 1 || inventory.value("role").toString() != "media-inventory" || required.isEmpty())
        return fail(error, "APK media inventory is invalid");
    for (const QString &path : required)
        if (!mediaPath(path) || !payload.contains(path)) return fail(error, "complete media package is missing: " + path);
    if (!space(m_storeRoot, total, error)) return false;
    QTemporaryDir temp(m_storeRoot + "/staging/media-XXXXXX");
    if (!temp.isValid()) return fail(error, "cannot stage media package");
    quint64 completed = 0;
    for (auto it = payload.cbegin(); it != payload.cend(); ++it) {
        if (cancelled(cancel, error)) return false;
        const Progress report = [&](quint64 done, quint64) { if (progress) progress(completed + done, total); };
        const QString path = temp.path() + "/runtime/" + it.key();
        if (!extractFile(reader, *it.value(), path, cancel, report, error)) return false;
        if (digest(path, error, cancel) != records.value(it.key()).value("sha256").toString().toLower()) return fail(error, "media SHA-256 mismatch: " + it.key());
        completed += it.value()->uncompressedSize;
    }
    const QString blob = uuid();
    QVariantMap package = infoMap("media", blob, payload.keys(), {}, false);
    package.insert("role", "media"); package.insert("complete_media", true);
    package.insert("media_records", manifest.value("files"));
    package.insert("archiveBytes", reader.archiveSize()); package.insert("expandedBytes", total);
    package.insert("sha256", QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(manifest).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
    if (!jsonWrite(temp.path() + "/package.json", package, error)) return false;
    if (cancelled(cancel, error)) return false;
    if (!QDir().rename(temp.path(), m_storeRoot + "/blobs/" + blob)) return fail(error, "cannot publish media payload");
    temp.setAutoRemove(false);
    const bool result = stageSnapshot(withPackage(m_snapshot, package), error, cancel);
    if (!result) collectUnusedVersions();
    return result;
}

bool AndroidContentStore::stageExtension(QIODevice &source, const QString &filename,
                                        const QString &bundleId, Cancelled *cancel,
                                        const Progress &progress, QString *error)
{
    if (!m_prepared) return fail(error, "content store has not been prepared");
    if (!packageId(bundleId) || bundleId == "media" || !safePath(filename) || filename.contains('/'))
        return fail(error, "invalid extension filename or package id");
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QTemporaryDir temp(m_storeRoot + "/staging/extension-XXXXXX");
    if (!temp.isValid()) return fail(error, "cannot stage extension package");
    QStringList files;
    QVariantList entries;
    quint64 archiveBytes = 0, total = 0;
    if (filename.endsWith(".lua", Qt::CaseInsensitive)) {
        const QString path = "extensions/" + QFileInfo(filename).completeBaseName() + ".lua";
        if (!extensionPath(path)) return fail(error, "invalid Lua filename");
        if (!directory(temp.path() + "/runtime/extensions", error)) return false;
        QSaveFile output(temp.path() + "/runtime/" + path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) return fail(error, "cannot stage Lua file");
        while (!source.atEnd()) {
            if (cancelled(cancel, error)) return false;
            const QByteArray block = source.read(256 * 1024);
            if (block.isEmpty() && !source.atEnd()) return fail(error, "Lua input read failed");
            total += quint64(block.size());
            if (total > quint64(128) * 1024 * 1024 || !space(m_storeRoot, quint64(block.size()), error)) return fail(error, "Lua file exceeds import limits");
            if (output.write(block) != block.size()) return fail(error, "cannot write Lua import");
            if (progress) progress(total, source.isSequential() ? 0 : quint64(source.size()));
        }
        if (!total || !output.commit()) return fail(error, "empty or incomplete Lua import");
        archiveBytes = total; files << path; entries = inferEntries(files);
    } else {
        if (!filename.endsWith(".zip", Qt::CaseInsensitive)) return fail(error, "extension import must be .lua or .zip");
        ImportLimits limits;
        limits.maxArchiveBytes = quint64(512) * 1024 * 1024;
        limits.maxExpandedBytes = quint64(1024) * 1024 * 1024;
        limits.maxEntryBytes = quint64(128) * 1024 * 1024;
        limits.maxEntries = 100000; limits.maxCompressionRatio = 200;
        AndroidZipReader reader(m_storeRoot + QStringLiteral("/staging"));
        if (!reader.open(source, limits, cancel, error)) return false;
        const AndroidZipReader::Entry *descriptorEntry = nullptr;
        for (const auto &entry : reader.entries()) {
            if (entry.directory) continue;
            if (entry.path == "runtime-content.json") { descriptorEntry = &entry; continue; }
            if (!extensionPath(entry.path)) return fail(error, "extension ZIP contains protected or unsupported content: " + entry.path);
            files << entry.path; total += entry.uncompressedSize;
        }
        if (descriptorEntry) {
            QVariantMap map;
            if (!readZipJson(reader, *descriptorEntry, &map, cancel, error)) return false;
            if (map.value("schema_version").toInt() != 2 || map.value("profile").toString() != "declared-v2") return fail(error, "invalid extension descriptor schema");
            entries = map.value("extensions").toList();
        } else entries = inferEntries(files);
        if (!coverage(entries, files, error) || !space(m_storeRoot, total, error)) return false;
        quint64 completed = 0;
        for (const auto &entry : reader.entries()) {
            if (entry.directory || entry.path == "runtime-content.json") continue;
            const Progress report = [&](quint64 done, quint64) { if (progress) progress(completed + done, total); };
            if (!extractFile(reader, entry, temp.path() + "/runtime/" + entry.path, cancel, report, error)) return false;
            completed += entry.uncompressedSize;
        }
        archiveBytes = reader.archiveSize();
    }
    if (!coverage(entries, files, error) || cancelled(cancel, error)) return false;
    const QString blob = uuid();
    QVariantMap package = infoMap(bundleId, blob, files, entries, false);
    package.insert("archiveBytes", archiveBytes); package.insert("expandedBytes", total);
    if (!jsonWrite(temp.path() + "/package.json", package, error)) return false;
    if (!QDir().rename(temp.path(), m_storeRoot + "/blobs/" + blob)) return fail(error, "cannot publish extension payload");
    temp.setAutoRemove(false);
    const bool result = stageSnapshot(withPackage(m_snapshot, package), error, cancel);
    if (!result) collectUnusedVersions();
    return result;
}
