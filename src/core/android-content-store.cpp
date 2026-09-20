#include "android-content-store.h"
#include "android-zip-reader.h"
#include "android_assets.h"
#include "package-catalog.h"
#include "rules-content-manifest.h"

#include <QBuffer>
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
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef Q_OS_ANDROID
#include <fcntl.h>
#endif

namespace {
constexpr qint64 kJsonLimit = 32 * 1024 * 1024;
constexpr quint64 kReserve = 64 * 1024 * 1024;
constexpr int kBootstrapVersion = 2;
constexpr int kPresentationVersion = 1;
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
bool optionalMediaPath(const QString &path)
{
    if (mediaPath(path)) return true;
    // Modular media follows the same optional-presentation policy as legacy media.
    return safePath(path) && path.startsWith("packages/") && mediaPath(path.section('/', 2));
}
bool coreLuaPath(const QString &path)
{
    static const QSet<QString> core{ "lua/config.lua", "lua/sanguosha.lua", "lua/utilities.lua", "lua/sgs_ex.lua", "lua/lib/json.lua" };
    return core.contains(path);
}
bool extensionPath(const QString &path)
{
    if (mediaPath(path)) return true;
    if (!safePath(path) || !path.endsWith(".lua")) return false;
    return !coreLuaPath(path) && (path.startsWith("extensions/") || path.startsWith("lua/") || path.startsWith("lang/"));
}
bool directory(const QString &path, QString *error, QSet<QString> *checked = nullptr)
{
    QFileInfo info(path);
    if (checked && checked->contains(info.absoluteFilePath())) return true;
    if (info.isSymLink()) return fail(error, "content directory is a symbolic link: " + path);
    const QString parent = info.absolutePath();
    if (parent != info.absoluteFilePath() && !directory(parent, error, checked)) return false;
    if (info.exists() ? !info.isDir() : !QDir().mkpath(path)) return fail(error, "cannot create directory: " + path);
    if (checked) checked->insert(info.absoluteFilePath());
    return true;
}
bool realParents(const QString &path, QSet<QString> *checked = nullptr)
{
    QDir parent = QFileInfo(path).dir();
    QStringList visited;
    while (true) {
        if (checked && checked->contains(parent.absolutePath())) break;
        if (QFileInfo(parent.absolutePath()).isSymLink()) return false;
        visited.append(parent.absolutePath());
        if (!parent.cdUp()) break;
    }
    if (checked) for (const QString &dir : visited) checked->insert(dir);
    return true;
}
bool regular(const QString &path, QSet<QString> *checked = nullptr)
{
    const QFileInfo info(path);
    return info.isFile() && !info.isSymLink() && realParents(path, checked);
}
QString mediaSource(const QString &store, const QString &reference, const QString &path)
{
    if (!mediaPath(path)) return {};
    if (reference == "base") return store + "/baseline/runtime/" + path;
    if (versionId(reference)) return store + "/blobs/" + reference + "/runtime/" + path;
    return {};
}
bool runtimeFile(const QString &store, const QString &root, const QString &path,
                 const QVariantMap &mediaSources, const QVariantMap &mediaRoots,
                 const QString &expectedReference, QSet<QString> *checked)
{
    const QString target = root + "/runtime/" + path;
    const QString top = path.section('/', 0, 0);
    if (mediaPath(path) && mediaRoots.contains(top)) {
        const QString reference = mediaRoots.value(top).toString();
        const QString source = mediaSource(store, reference, path);
        const QFileInfo link(root + "/runtime/" + top);
        const QString sourceRoot = QFileInfo(mediaSource(store, reference, top + "/sentinel")).absolutePath();
        return !source.isEmpty() && reference == expectedReference && link.isSymLink()
            && realParents(link.absoluteFilePath(), checked) && link.symLinkTarget() == sourceRoot
            && regular(source, checked);
    }
    if (regular(target, checked)) return true; // Existing physical snapshots remain readable.
    const QString source = mediaSource(store, mediaSources.value(path).toString(), path);
    const QFileInfo info(target);
    // Only store-created, metadata-bound media links cross the snapshot boundary.
    // ZIP links, undeclared directory links, Lua links and arbitrary targets stay invalid.
    return !source.isEmpty() && mediaSources.value(path).toString() == expectedReference
        && info.isSymLink() && realParents(target, checked)
        && info.symLinkTarget() == source && regular(source, checked);
}
bool jsonRead(const QString &path, QVariantMap *map, QString *error)
{
    QFile file(path);
    if ((!path.startsWith(":/") && !regular(path)) || !file.open(QIODevice::ReadOnly)
        || file.size() > kJsonLimit) return fail(error, "cannot read bounded content metadata: " + path);
    QJsonParseError parse;
    const QByteArray bytes = file.read(kJsonLimit + 1);
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail(error, "invalid content JSON: " + path);
    *map = document.object().toVariantMap();
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
              AndroidContentStore::Cancelled *cancel = nullptr, QSet<QString> *checked = nullptr,
              bool optionalMedia = false)
{
    if (cancelled(cancel, error)) return false;
    // Snapshot composition copies existing payloads, but an absent optional
    // image must not prevent a Lua/package update from activating.
    if (optionalMedia && !QFileInfo::exists(source) && !QFileInfo(source).isSymLink()) return true;
    if (!source.startsWith(":/") && !regular(source, checked)) return fail(error, "non-regular content source: " + source);
    if (!directory(QFileInfo(target).absolutePath(), error, checked) || QFileInfo(target).isSymLink()) return false;
#ifdef Q_OS_UNIX
    // App-private media blobs outlive every referring snapshot. Unlike hard
    // links, these references need no Android link permission or payload copy.
    if (shareMedia) {
        if (::symlink(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) == 0) return true;
        return fail(error, "cannot reference shared media: " + target);
    }
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
bool sameContents(const QString &left, const QString &right, bool *same, QString *error)
{
    // Only the one-time legacy migration compares bytes, to preserve user edits.
    // Neither imports nor normal startup calculate content hashes.
    QFile a(left), b(right);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) return fail(error, "cannot read legacy content");
    *same = false;
    if (a.size() != b.size()) return true;
    while (!a.atEnd()) {
        const QByteArray lhs = a.read(256 * 1024), rhs = b.read(256 * 1024);
        if (a.error() != QFileDevice::NoError || b.error() != QFileDevice::NoError)
            return fail(error, "legacy content read failed");
        if (lhs != rhs) return true;
    }
    *same = b.atEnd();
    return true;
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
QVariantMap descriptor(const QSanRules::ContentManifest &manifest)
{
    return QSanRules::runtimeContentDescriptor(manifest).toVariantMap();
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
QVariantMap modularInfo(const QSanPackages::Package &package, const QString &blob,
                        const QStringList &files, bool bundled)
{
    QVariantMap result{{"id", package.id}, {"version", blob}, {"package_version", package.version},
                       {"role", "modular"}, {"enabled", true}, {"bundled", bundled},
                       {"files", files}, {"manifest", package.manifest.toVariantMap()}};
    return result;
}
QStringList modularFiles(const QSanPackages::Package &package)
{
    QStringList files{QStringLiteral("packages/%1/manifest.json").arg(package.id)};
    for (const QJsonValue &value : package.manifest.value(QStringLiteral("files")).toArray())
        files << QStringLiteral("packages/%1/%2").arg(package.id, value.toObject().value(QStringLiteral("path")).toString());
    files.removeDuplicates();
    return files;
}
QJsonObject modularRulesMetadata(QJsonObject manifest)
{
    // APK artwork inventory changes must not enter the rule-upgrade conflict
    // gate. Keep the active package metadata until an explicit package update.
    manifest.remove(QStringLiteral("assets"));
    QJsonArray files;
    for (const QJsonValue &value : manifest.value(QStringLiteral("files")).toArray())
        if (!mediaPath(value.toObject().value(QStringLiteral("path")).toString())) files.append(value);
    manifest.insert(QStringLiteral("files"), files);
    return manifest;
}
QStringList ownedFiles(const QVariantMap &package)
{
    // Retired APK files remain owned while a modular replacement is active.
    // Otherwise snapshot composition would resurrect them as loose core files.
    QStringList files = package.value("files").toStringList();
    const QVariantMap fallback = package.value("fallback").toMap();
    if (!fallback.isEmpty()) files << ownedFiles(fallback);
    files.removeDuplicates();
    return files;
}
QVariantList orderedEntries(const QVariantMap &package)
{
    QVariantList entries = package.value("entries").toList();
    if (package.value("role").toString() != "modular") return entries;
    QSet<QString> names;
    for (const QVariant &entry : package.value("manifest").toMap().value("extensions").toList())
        names.insert(entry.toMap().value("name").toString());
    // Keep only replaced legacy entries as order anchors. The final v3 merge
    // substitutes sealed package paths before any descriptor is published.
    for (const QVariant &entry : orderedEntries(package.value("fallback").toMap()))
        if (names.contains(entry.toMap().value("name").toString())) entries << entry;
    return entries;
}
bool addBundledModularPackage(QVariantList *packages, const QVariantMap &package, QString *error)
{
    for (QVariant &value : *packages) {
        const QVariantMap old = value.toMap();
        if (key(old.value("id").toString()) != key(package.value("id").toString())) continue;
        QVariantMap replacement = package;
        replacement.insert("fallback", old);
        if (old.value("id") != package.value("id") || old.value("role").toString() != "extension"
            || !old.value("bundled").toBool() || old.value("version").toString() != "base"
            || orderedEntries(replacement).size() != old.value("entries").toList().size())
            return fail(error, "bundled modular package conflicts with a legacy package: " + package.value("id").toString());
        value = replacement;
        return true;
    }
    packages->append(package);
    return true;
}
bool normalizeBundledPackages(QVariantList *packages, QString *error)
{
    // An interrupted first boot of the initial implementation may have saved
    // both rows before snapshot publication rejected their duplicate ID.
    for (int i = packages->size() - 1; i >= 0; --i) {
        const QVariantMap modular = packages->at(i).toMap();
        if (modular.value("role").toString() != "modular" || !modular.value("bundled").toBool()
            || modular.value("version").toString() != "base") continue;
        bool duplicate = false;
        for (int j = 0; j < packages->size(); ++j)
            if (j != i && packages->at(j).toMap().value("id") == modular.value("id")) duplicate = true;
        if (!duplicate) continue;
        packages->removeAt(i);
        bool preservedOverride = false;
        for (QVariant &value : *packages) {
            QVariantMap old = value.toMap();
            if (old.value("id") != modular.value("id") || old.value("bundled").toBool()) continue;
            QVariantList fallback{old.value("fallback")};
            if (old.value("role").toString() != "extension"
                || !addBundledModularPackage(&fallback, modular, error)) return false;
            old.insert("fallback", fallback.first());
            value = old; preservedOverride = true; break;
        }
        if (preservedOverride) continue;
        if (!addBundledModularPackage(packages, modular, error)) return false;
    }
    return true;
}
QVariantMap mergeApkPresentation(QVariantMap package, const QVariantList &apkEntries)
{
    // Only APK-owned translation declarations advance. Rule scripts, support
    // libraries, AI, package versions and existing override bytes stay pinned.
    QVariantMap fallback = package.value("fallback").toMap();
    if (!fallback.isEmpty()) package.insert("fallback", mergeApkPresentation(fallback, apkEntries));
    if (package.value("role").toString() != "extension") return package;
    QVariantList entries = package.value("entries").toList();
    QStringList files = package.value("files").toStringList();
    QStringList baselineLang = package.value("apk_presentation").toStringList();
    for (QVariant &value : entries) {
        QVariantMap entry = value.toMap();
        for (const QVariant &apkValue : apkEntries) {
            const QVariantMap apk = apkValue.toMap();
            if (entry.value("name") != apk.value("name")) continue;
            QStringList lang = entry.value("lang").toStringList();
            for (const QString &path : apk.value("lang").toStringList()) {
                if (!safePath(path) || !path.startsWith("lang/") || !path.endsWith(".lua")) continue;
                if (!lang.contains(path)) lang << path;
                if (!files.contains(path)) {
                    files << path;
                    if (package.value("version").toString() != "base") baselineLang << path;
                }
            }
            // Keep an unchanged JSON array's QVariant representation intact;
            // QStringList and JSON's QVariantList are not QVariant-equal.
            if (lang != entry.value("lang").toStringList()) entry.insert("lang", lang);
            break;
        }
        value = entry;
    }
    package.insert("entries", entries);
    if (files != package.value("files").toStringList()) package.insert("files", files);
    if (!baselineLang.isEmpty()) { baselineLang.removeDuplicates(); package.insert("apk_presentation", baselineLang); }
    return package;
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
        for (QVariant &existing : packages) {
            QVariantMap p = existing.toMap();
            if (key(p.value("id").toString()) != key(id)) continue;
            // A prior bundled override keeps its fallback. An unrelated user
            // package with the newly introduced name must not be overwritten.
            if (p.value("id").toString() != id
                || (!p.value("bundled").toBool() && !p.value("fallback").toMap().value("bundled").toBool()))
                return fail(error, "new APK package conflicts with a user package: " + id);
            // Reconcile presentation before comparing the legacy order anchor;
            // new translations must not prevent the same-ID modular migration.
            if (snapshot.value("baseline_revision") != base.value("revision")) {
                p = mergeApkPresentation(p, orderedEntries(bundled));
                existing = p;
            }
            if (snapshot.value("baseline_revision") != base.value("revision")
                && p.value("bundled").toBool() && p.value("version").toString() == "base"
                && !p.value("keep_legacy").toBool()
                && p.value("role").toString() == "extension" && bundled.value("role").toString() == "modular"
                && QJsonArray::fromVariantList(p.value("entries").toList())
                    == QJsonArray::fromVariantList(bundled.value("fallback").toMap().value("entries").toList())) {
                QVariantMap migrated = bundled;
                migrated.insert("enabled", p.value("enabled", true));
                existing = migrated;
            }
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
        p.packageVersion = map.value("package_version").toString();
        p.role = map.value("role").toString(); p.enabled = map.value("enabled", true).toBool();
        p.bundled = map.value("bundled").toBool(); p.state = p.enabled ? "enabled" : "disabled";
        p.files = map.value("files").toStringList(); p.archiveBytes = map.value("archiveBytes").toULongLong();
        p.expandedBytes = map.value("expandedBytes").toULongLong(); p.sha256 = map.value("sha256").toString();
        m_packages.append(p);
    }
}

bool AndroidContentStore::loadSnapshot(const QString &id, QVariantMap *snapshot, QString *error,
                                       bool inspectMedia) const
{
    if (!versionId(id)) return fail(error, "invalid content version id");
    const QString root = QDir(m_storeRoot).filePath("versions/" + id);
    if (!jsonRead(root + "/metadata.json", snapshot, error)) return false;
    QVariantMap runtimeDescriptor;
    if (!jsonRead(root + "/runtime/runtime-content.json", &runtimeDescriptor, error)) return false;
    const auto parsed = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(runtimeDescriptor));
    if (!parsed.isValid()) return fail(error, parsed.error);
    for (const QString &path : {QStringLiteral("lua/config.lua"), QStringLiteral("lua/sanguosha.lua"),
                                QStringLiteral("lua/ai/smart-ai.lua")})
        if (!regular(root + "/runtime/" + path)) return fail(error, "content version is incomplete: " + path);
    QVariantList entries;
    QSet<QString> checkedDirectories, enabledVersions;
    const QVariantMap mediaSources = snapshot->value("media_sources").toMap();
    const QVariantMap mediaRoots = snapshot->value("media_roots").toMap();
    bool completeMedia = false;
    bool mediaValid = true;
    for (const QVariant &value : snapshot->value("packages").toList()) {
        const QVariantMap p = effective(value.toMap());
        if (p.isEmpty()) continue;
        enabledVersions.insert(p.value("version").toString());
        const bool mediaPackage = p.value("role").toString() == "media";
        if (mediaPackage && p.value("complete_media").toBool()) completeMedia = true;
        // Successful import is persistent installation state. New APK image
        // names and missing optional media never trigger a startup rescan.
        if (mediaPackage && !inspectMedia) continue;
        entries << orderedEntries(p);
        for (const QString &path : p.value("files").toStringList()) {
            if (!safePath(path)) return fail(error, "invalid snapshot package path");
            if (optionalMediaPath(path) && !inspectMedia) continue;
            if (!runtimeFile(m_storeRoot, root, path, mediaSources, mediaRoots, p.value("version").toString(), &checkedDirectories)) {
                if (optionalMediaPath(path)) mediaValid = false;
                else return fail(error, "content package file is missing: " + path);
            }
        }
    }
    // Constant-size directory checks prevent a forged link from redirecting
    // the selected runtime outside its own private blob; no payload is read.
    for (auto it = mediaRoots.cbegin(); it != mediaRoots.cend(); ++it) {
        const QString source = mediaSource(m_storeRoot, it.value().toString(), it.key() + "/sentinel");
        const QFileInfo link(root + "/runtime/" + it.key());
        if ((it.key() != "image" && it.key() != "audio" && it.key() != "font")
            || source.isEmpty() || !enabledVersions.contains(it.value().toString())
            || !link.isSymLink() || !realParents(link.absoluteFilePath(), &checkedDirectories)
            || link.symLinkTarget() != QFileInfo(source).absolutePath()
            || !realParents(source, &checkedDirectories)) mediaValid = false;
    }
    const auto legacyManifest = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(descriptor(entries)));
    if (!legacyManifest.isValid()) return fail(error, legacyManifest.error);
    const QSanPackages::Catalog modularCatalog = QSanPackages::loadCatalog(root + "/runtime", false, false);
    if (!modularCatalog.isValid()) return fail(error, modularCatalog.error);
    const auto expectedManifest = QSanRules::mergePackageContent(legacyManifest,
        QSanPackages::extensionDescriptors(modularCatalog), QSanPackages::packageDescriptors(modularCatalog));
    if (!expectedManifest.isValid()
        || QJsonObject::fromVariantMap(descriptor(expectedManifest)) != QJsonObject::fromVariantMap(runtimeDescriptor))
        return fail(error, expectedManifest.error.isEmpty()
            ? QStringLiteral("snapshot descriptor does not match its packages") : expectedManifest.error);
    snapshot->insert("media_ready", completeMedia && mediaValid);
    for (const QString &path : QSanRules::manifestDeliveredFiles(parsed) + QSanRules::manifestServerOnlyFiles(parsed))
        if (!regular(root + "/runtime/" + path)) return fail(error, "content version is incomplete: " + path);
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
    static const QRegularExpression temporaryName(QStringLiteral("^(base|legacy|version|media|extension|modular)-[A-Za-z0-9]{6}$"));
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
        || base.value("bootstrap_version").toInt() != kBootstrapVersion
        || base.value("presentation_version").toInt() != kPresentationVersion
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
        const QSanPackages::Catalog bundledCatalog = QSanPackages::loadCatalog(temp.path() + "/runtime", false, false);
        if (!bundledCatalog.isValid()) return fail(error, bundledCatalog.error);
        for (const QSanPackages::Package &package : bundledCatalog.packages)
            if (!addBundledModularPackage(&packages, modularInfo(package, "base", modularFiles(package), true), error))
                return false;
        QSet<QString> changed;
        QSet<QString> sealedPackagePaths;
        for (const QVariant &value : packages) {
            const QVariantMap package = value.toMap();
            if (package.value("role").toString() == "modular")
                for (const QString &path : package.value("files").toStringList()) sealedPackagePaths.insert(key(path));
        }
        const QStringList deployedFiles = treeFiles(deployed, &scanError);
        if (!scanError.isEmpty()) return fail(error, scanError);
        for (const QString &path : deployedFiles) {
            if (!safePath(path)) return fail(error, "invalid legacy runtime path");
            // A bundled modular package is migrated as one unit. Do not turn
            // individual legacy edits into undeclared partial package overrides.
            if (sealedPackagePaths.contains(key(path))) continue;
            if (!QFileInfo::exists(":/assets/" + path)) changed.insert(path);
            else {
                bool same = false;
                if (!sameContents(deployed + '/' + path, ":/assets/" + path, &same, error)) return false;
                if (!same) changed.insert(path);
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
                const QStringList owned = ownedFiles(original);
                bool modified = false;
                for (const QString &path : owned) { packageOwned.insert(path); if (changed.contains(path)) modified = true; }
                if (!modified) continue;
                QVariantMap overridePackage = original;
                // Preserve edited CP1 legacy payloads as a whole extension;
                // removing that override explicitly restores the modular APK.
                if (original.value("role").toString() == "modular"
                    && original.value("fallback").toMap().value("role").toString() == "extension")
                    overridePackage = original.value("fallback").toMap();
                for (const QString &path : overridePackage.value("files").toStringList()) capture.insert(path);
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
    QVariantList normalizedPackages = base.value("packages").toList();
    QVariantMap normalizedInitial = base.value("initial_snapshot").toMap();
    QVariantList normalizedInitialPackages = normalizedInitial.value("packages").toList();
    if (!normalizeBundledPackages(&normalizedPackages, error)
        || !normalizeBundledPackages(&normalizedInitialPackages, error)) return false;
    if (normalizedPackages != base.value("packages").toList()
        || normalizedInitialPackages != normalizedInitial.value("packages").toList()) {
        normalizedInitial.insert("packages", normalizedInitialPackages);
        base.insert("packages", normalizedPackages); base.insert("initial_snapshot", normalizedInitial);
        base.insert("revision", uuid());
        if (!jsonWrite(baseMetadata, base, error)) return false;
    }
    // APK upgrades extend the original tree; the APK-owned core Lua files
    // follows the APK revision. Snapshot copies and captured user edits remain
    // independent. Publish the revision after all replacements/copies finish.
    if (refreshBaseline) {
        QVariantMap currentDescriptor;
        if (!jsonRead(":/assets/runtime-content-base.json", &currentDescriptor, error)) return false;
        const auto currentManifest = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(currentDescriptor));
        if (!currentManifest.isValid()) return fail(error, currentManifest.error);
        QString resourceError;
        const QStringList currentFiles = treeFiles(":/assets", &resourceError);
        if (!resourceError.isEmpty()) return fail(error, resourceError);
        quint64 incomingPackageBytes = 0;
        for (const QString &path : currentFiles)
            if (path.startsWith(QStringLiteral("packages/")))
                incomingPackageBytes += quint64(QFileInfo(":/assets/" + path).size());
        if (incomingPackageBytes && !space(m_storeRoot, incomingPackageBytes, error)) return false;
        QTemporaryDir incomingPackages(m_storeRoot + "/staging/base-XXXXXX");
        if (!incomingPackages.isValid()) return fail(error, "cannot stage bundled modular packages");
        for (const QString &path : currentFiles) {
            if (!path.startsWith(QStringLiteral("packages/"))) continue;
            if (!copyFile(":/assets/" + path, incomingPackages.path() + "/runtime/" + path, error)) return false;
        }
        const QSanPackages::Catalog incomingCatalog = QSanPackages::loadCatalog(incomingPackages.path() + "/runtime", false, false);
        if (!incomingCatalog.isValid()) return fail(error, incomingCatalog.error);
        bool baselineChanged = base.value("revision").toString().isEmpty()
            || base.value("presentation_version").toInt() != kPresentationVersion;
        QStringList knownResources = base.value("resource_paths").toStringList();
        QSet<QString> knownResourceSet(knownResources.cbegin(), knownResources.cend());
        for (const QString &path : currentFiles) {
            // Modular trees are admitted and copied as complete checksum-sealed packages below.
            if (path.startsWith(QStringLiteral("packages/"))) continue;
            if (!knownResourceSet.contains(path)) {
                knownResourceSet.insert(path); knownResources << path; baselineChanged = true;
            }
            const QString target = m_baseRoot + '/' + path;
            if (coreLuaPath(path)
                && (base.value("apk_revision").toString() != apkRevision
                    || base.value("bootstrap_version").toInt() != kBootstrapVersion)) {
                // Core helpers must match the APIs and packages shipped by this
                // APK. QSaveFile replaces only the baseline file atomically;
                // advancing its revision composes a new immutable active snapshot.
                if (QFileInfo::exists(target) && !regular(target))
                    return fail(error, "non-regular baseline bootstrap: " + path);
                if (!space(m_storeRoot, quint64(QFileInfo(":/assets/" + path).size()), error)
                    || !copyFile(":/assets/" + path, target, error)) return false;
                baselineChanged = true;
            } else if (!QFileInfo::exists(target)) {
                if (!space(m_storeRoot, quint64(QFileInfo(":/assets/" + path).size()), error)
                    || !AndroidAssets::copyAssetFile(path, target, error)) return false;
                baselineChanged = true;
            } else if (!regular(target)) return fail(error, "non-regular baseline content: " + path);
        }
        QVariantList baselinePackages = base.value("packages").toList();
        const QSanPackages::Catalog existingCatalog = QSanPackages::loadCatalog(m_baseRoot, false, false);
        if (!existingCatalog.isValid()) return fail(error, existingCatalog.error);
        QStringList packageConflicts;
        for (const QSanPackages::Package &package : incomingCatalog.packages) {
            const QSanPackages::Package *existing = nullptr;
            for (const QSanPackages::Package &candidate : existingCatalog.packages)
                if (candidate.id == package.id) { existing = &candidate; break; }
            bool metadataPresent = false;
            bool metadataModular = false;
            bool legacyMigration = false;
            for (const QVariant &item : baselinePackages) {
                const QVariantMap old = item.toMap();
                if (key(old.value("id").toString()) != key(package.id)) continue;
                if (old.value("id").toString() == package.id && old.value("role").toString() == "extension"
                    && old.value("bundled").toBool() && old.value("version").toString() == "base") {
                    QVariantList candidate{old};
                    if (addBundledModularPackage(&candidate, modularInfo(package, "base", modularFiles(package), true), error)) {
                        metadataPresent = true; legacyMigration = true; break;
                    }
                }
                if (old.value("id").toString() != package.id || old.value("role").toString() != "modular") {
                    packageConflicts << package.id;
                    metadataPresent = true;
                    break;
                }
                metadataPresent = true;
                metadataModular = true;
                break;
            }
            if (existing && modularRulesMetadata(existing->manifest) != modularRulesMetadata(package.manifest)) {
                packageConflicts << package.id;
                if (!metadataPresent) {
                    baselinePackages.append(modularInfo(*existing, "base", modularFiles(*existing), true));
                    baselineChanged = true;
                }
                continue;
            }
            if (metadataPresent && !metadataModular && !legacyMigration) continue;
            if (metadataPresent && !existing && !legacyMigration) {
                packageConflicts << package.id;
                continue;
            }
            if (existing) {
                if (!metadataPresent || legacyMigration) {
                    if (!addBundledModularPackage(&baselinePackages, modularInfo(*existing, "base", modularFiles(*existing), true), error))
                        return false;
                    baselineChanged = true;
                }
                continue;
            }

            const QStringList files = modularFiles(package);
            quint64 packageBytes = 0;
            for (const QString &path : files) packageBytes += quint64(QFileInfo(incomingPackages.path() + "/runtime/" + path).size());
            if (!space(m_storeRoot, packageBytes, error)) return false;
            QTemporaryDir packageStage(m_storeRoot + "/staging/modular-XXXXXX");
            if (!packageStage.isValid()) return fail(error, "cannot stage a complete APK modular package");
            for (const QString &path : files)
                if (!copyFile(incomingPackages.path() + "/runtime/" + path,
                              packageStage.path() + "/runtime/" + path, error, false, nullptr, nullptr,
                              optionalMediaPath(path))) return false;
            const QString sourceDirectory = packageStage.path() + "/runtime/packages/" + package.id;
            const QString targetDirectory = m_baseRoot + "/packages/" + package.id;
            if (!directory(QFileInfo(targetDirectory).absolutePath(), error)
                || !QDir().rename(sourceDirectory, targetDirectory))
                return fail(error, "cannot atomically add bundled modular package: " + package.id);
            if (!addBundledModularPackage(&baselinePackages, modularInfo(package, "base", files, true), error)) return false;
            for (const QString &path : files)
                if (!knownResourceSet.contains(path)) { knownResourceSet.insert(path); knownResources << path; }
            baselineChanged = true;
        }
        for (const QVariant &entry : currentDescriptor.value("extensions").toList()) {
            const QVariantMap e = entry.toMap();
            const QString id = e.value("name").toString();
            if (!packageId(id)) return fail(error, "invalid new bundled package name");
            bool present = false;
            for (QVariant &p : baselinePackages) {
                const QString oldId = p.toMap().value("id").toString();
                if (key(oldId) == key(id)) {
                    if (oldId != id) return fail(error, "case-colliding APK package name: " + id);
                    const QVariantMap merged = mergeApkPresentation(p.toMap(), {e});
                    if (merged != p.toMap()) { p = merged; baselineChanged = true; }
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
            // Metadata-only migration marker also repairs old baselines whose
            // failed launch already recorded this same APK resource revision.
            base.insert("bootstrap_version", kBootstrapVersion);
            base.insert("presentation_version", kPresentationVersion);
            if (!jsonWrite(baseMetadata, base, error)) return false;
        }
        if (!packageConflicts.isEmpty()) {
            packageConflicts.removeDuplicates();
            QVariantMap conflictState = m_state;
            conflictState.insert("boot_attempt", true);
            conflictState.insert("upgrade_conflict", true);
            conflictState.insert("recovery_error", QStringLiteral("APK modular package changed; preserving the previous complete package: %1")
                .arg(packageConflicts.join(QStringLiteral(", "))));
            if (!commitState(conflictState, error)) return false;
        }
    }
    // The imported private snapshot is already published. Startup selects it;
    // it does not hash resources or rescan media after an APK/receipt change.
    QMap<QString, QVariantMap> loadedSnapshots;
    const auto loadForStartup = [&](const QString &id, QVariantMap *snapshot, QString *loadError) {
        if (loadedSnapshots.contains(id)) { *snapshot = loadedSnapshots.value(id); return true; }
        if (!loadSnapshot(id, snapshot, loadError, false)) return false;
        loadedSnapshots.insert(id, *snapshot);
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
            bool migrateMedia = false;
#ifdef Q_OS_ANDROID
            // Reuse existing blobs when upgrading physical snapshots; no ZIP reimport.
            migrateMedia = checked.value("media_storage").toInt() != 1;
#endif
            if (checked.value("baseline_revision") != base.value("revision") || migrateMedia) {
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
    QElapsedTimer timer;
    timer.start();
    if (cancelled(cancel, error)) return false;
    QVariantMap base;
    if (!jsonRead(m_storeRoot + "/baseline/metadata.json", &base, error)) return false;
    QVariantMap composed;
    if (!mergeBaselinePackages(snapshot, base, &composed, error)) return false;
    QSet<QString> baselineOwned;
    QSet<QString> baselineLang;
    for (const QVariant &value : base.value("packages").toList()) {
        for (const QString &path : ownedFiles(value.toMap())) baselineOwned.insert(key(path));
        for (const QVariant &entry : orderedEntries(value.toMap()))
            for (const QString &path : entry.toMap().value("lang").toStringList()) baselineLang.insert(path);
    }
    QMap<QString, QString> sources;
    QVariantMap mediaSources;
    QSet<QString> checkedDirectories;
    QString scanError;
    const QStringList baseFiles = treeFiles(m_baseRoot, &scanError);
    if (!scanError.isEmpty()) return fail(error, scanError);
    for (const QString &path : baseFiles) {
        if (cancelled(cancel, error)) return false;
        if (!safePath(path)) return fail(error, "invalid baseline path");
        if (!baselineOwned.contains(key(path)) && path != "runtime-content.json" && path != "runtime-content-base.json")
            sources.insert(path, m_baseRoot + '/' + path);
        if (mediaPath(path)) mediaSources.insert(path, "base");
    }
    const QVariantMap legacy = composed.value("legacy_overrides").toMap();
    for (auto it = legacy.cbegin(); it != legacy.cend(); ++it) {
        if (!safePath(it.key()) || !versionId(it.value().toString())
            || (baselineOwned.contains(key(it.key())) && !baselineLang.contains(it.key())))
            return fail(error, "invalid preserved legacy override");
        // A newly declared APK translation can already have a user override.
        // Its source is selected with the owning package below, never discarded.
        if (baselineOwned.contains(key(it.key()))) continue;
        const QString source = m_storeRoot + "/blobs/" + it.value().toString() + "/runtime/" + it.key();
        if (!optionalMediaPath(it.key()) && !regular(source, &checkedDirectories))
            return fail(error, "preserved legacy override is missing");
        sources.insert(it.key(), source);
        if (mediaPath(it.key())) mediaSources.insert(it.key(), it.value());
    }
    QSet<QString> ids, owners;
    QVariantList entries;
    bool completeMedia = false;
    QSet<QString> wholeMediaBlobs;
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
        const QStringList apkPresentation = p.value("apk_presentation").toStringList();
        for (const QString &path : apkPresentation)
            if (!fileSet.contains(path) || !baselineLang.contains(path))
                return fail(error, "invalid APK presentation source: " + path);
        for (const QString &path : files) {
            if (cancelled(cancel, error)) return false;
            if (!safePath(path) || owners.contains(key(path))) return fail(error, "content path is owned by another enabled package: " + path);
            owners.insert(key(path));
            const bool modularPath = p.value("role").toString() == "modular"
                && path.startsWith(QStringLiteral("packages/%1/").arg(id));
            if (blob != "base" && !(p.value("role").toString() == "media" ? mediaPath(path)
                                   : p.value("role").toString() == "modular" ? modularPath : extensionPath(path)))
                return fail(error, "package attempts to replace protected runtime content: " + path);
            // Installed media is optional. Recomposition must not turn an APK
            // update or package removal into an exhaustive media-presence gate.
            QString source = root + '/' + path;
            if (apkPresentation.contains(path)) source = m_baseRoot + '/' + path;
            if (baselineLang.contains(path) && legacy.contains(path))
                source = m_storeRoot + "/blobs/" + legacy.value(path).toString() + "/runtime/" + path;
            if (!optionalMediaPath(path) && !regular(source, &checkedDirectories))
                return fail(error, "package payload is missing: " + path);
            // Imported content may replace media and a same-package bundled
            // payload, but never an unrelated core Lua/runtime file.
            if (sources.contains(path) && !mediaPath(path)) return fail(error, "package collides with baseline content: " + path);
            sources.insert(path, source);
            if (mediaPath(path)) mediaSources.insert(path, blob);
        }
        for (const QVariant &entry : p.value("entries").toList()) {
            for (const QString &path : entryFiles(entry.toMap()))
                if (!fileSet.contains(path)) return fail(error, "descriptor references a file outside its package: " + path);
        }
        entries << orderedEntries(p);
        if (p.value("role").toString() == "media" && p.value("complete_media").toBool()) {
            completeMedia = true;
            wholeMediaBlobs.insert(blob);
        }
    }
    const auto legacyManifest = QSanRules::parseRuntimeContent(QJsonObject::fromVariantMap(descriptor(entries)));
    if (!legacyManifest.isValid()) return fail(error, legacyManifest.error);
    quint64 bytes = 0;
    QSet<QString> portable;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        if (portable.contains(key(it.key()))) return fail(error, "case-colliding runtime paths");
        portable.insert(key(it.key()));
#ifdef Q_OS_UNIX
        if (!mediaPath(it.key()))
#endif
            bytes += quint64(QFileInfo(it.value()).size());
    }
    if (!space(m_storeRoot, bytes, error)) return false;
    QTemporaryDir temp(m_storeRoot + "/staging/version-XXXXXX");
    if (!temp.isValid()) return fail(error, "cannot stage content version");
    QVariantMap mediaRoots;
#ifdef Q_OS_UNIX
    QMap<QString, QString> candidates;
    for (auto it = mediaSources.cbegin(); it != mediaSources.cend(); ++it) {
        const QString top = it.key().section('/', 0, 0);
        if (!candidates.contains(top)) candidates.insert(top, it.value().toString());
        else if (candidates.value(top) != it.value().toString()) candidates[top].clear();
    }
    for (auto it = candidates.cbegin(); it != candidates.cend(); ++it) {
        if (!versionId(it.value()) || !wholeMediaBlobs.contains(it.value())) continue;
        if (cancelled(cancel, error) || !directory(temp.path() + "/runtime", error, &checkedDirectories)) return false;
        const QString source = m_storeRoot + "/blobs/" + it.value() + "/runtime/" + it.key();
        const QString target = temp.path() + "/runtime/" + it.key();
        // The enabled complete media package owns every selected file beneath
        // this root. Three directory references replace tens of thousands of copies.
        if (::symlink(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) != 0)
            return fail(error, "cannot reference shared media directory: " + target);
        mediaRoots.insert(it.key(), it.value());
    }
#endif
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        if (mediaRoots.contains(it.key().section('/', 0, 0))) continue;
        if (!copyFile(it.value(), temp.path() + "/runtime/" + it.key(), error, mediaPath(it.key()),
                      cancel, &checkedDirectories, optionalMediaPath(it.key()))) return false;
    }
    const QSanPackages::Catalog modularCatalog = QSanPackages::loadCatalog(temp.path() + "/runtime", false, false);
    if (!modularCatalog.isValid()) return fail(error, modularCatalog.error);
    const auto contentManifest = QSanRules::mergePackageContent(legacyManifest,
        QSanPackages::extensionDescriptors(modularCatalog), QSanPackages::packageDescriptors(modularCatalog));
    if (!contentManifest.isValid()) return fail(error, contentManifest.error);
    const QVariantMap contentDescriptor = descriptor(contentManifest);
    QVariantMap metadata = composed;
#ifdef Q_OS_UNIX
    for (auto it = mediaSources.begin(); it != mediaSources.end();) {
        if (mediaRoots.contains(it.key().section('/', 0, 0))) it = mediaSources.erase(it);
        else ++it;
    }
    metadata.insert("media_storage", 1);
    metadata.insert("media_sources", mediaSources);
    metadata.insert("media_roots", mediaRoots);
#else
    metadata.remove("media_storage");
    metadata.remove("media_sources");
    metadata.remove("media_roots");
#endif
    metadata.insert("media_ready", completeMedia);
    metadata.insert("descriptor", contentDescriptor);
    if (!jsonWrite(temp.path() + "/runtime/runtime-content.json", contentDescriptor, error)
        || !jsonWrite(temp.path() + "/metadata.json", metadata, error)) return false;
    const QString id = uuid();
    if (cancelled(cancel, error)) return false;
    if (!QDir().rename(temp.path(), m_storeRoot + "/versions/" + id)) return fail(error, "cannot publish complete content version");
    temp.setAutoRemove(false);
    *version = id;
    qInfo("Android content snapshot: shared_roots=%lld copy_bytes=%llu elapsed_ms=%lld",
          static_cast<long long>(mediaRoots.size()), static_cast<unsigned long long>(bytes),
          static_cast<long long>(timer.elapsed()));
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
        if (!p.value("fallback").toMap().isEmpty()) {
            QVariantMap restored = p.value("fallback").toMap();
            // An explicit return to the legacy APK package survives later
            // unrelated baseline revisions; only new migrations are automatic.
            if (p.value("role").toString() == "modular" && restored.value("role").toString() == "extension")
                restored.insert("keep_legacy", true);
            list[i] = restored;
        }
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
    const QVariantMap current = m_snapshot.value("descriptor").toMap();
    const QByteArray bytes = QJsonDocument::fromVariant(current).toJson(QJsonDocument::Indented);
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
                 const AndroidContentStore::Progress &progress, QString *error,
                 QSet<QString> *checked = nullptr)
{
    if (!directory(QFileInfo(path).absolutePath(), error, checked)) return false;
#ifdef Q_OS_ANDROID
    // This path is inside an unpublished QTemporaryDir. Stage each new file
    // without a per-file fsync; syncStagedPayload flushes the whole batch before
    // its directory is published. Active/previous files are never overwritten.
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return fail(error, "cannot stage imported file");
    if (!reader.extract(entry, output, cancel, progress, error)) return false;
    return output.flush() || fail(error, "cannot flush imported file");
#else
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) return fail(error, "cannot stage imported file");
    if (!reader.extract(entry, output, cancel, progress, error)) return false;
    return output.commit() || fail(error, "cannot publish imported file");
#endif
}
bool syncStagedPayload(const QString &root, QString *error)
{
#ifdef Q_OS_ANDROID
    // API 28 (our minimum) provides syncfs. One durable boundary replaces
    // tens of thousands of QSaveFile::commit filesystem syncs during import.
    const int fd = ::open(QFile::encodeName(root).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return fail(error, "cannot open staged payload for synchronization");
    const int result = ::syncfs(fd);
    ::close(fd);
    if (result != 0) return fail(error, "cannot synchronize staged payload");
#else
    Q_UNUSED(root);
    Q_UNUSED(error);
#endif
    return true;
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
    QElapsedTimer timer;
    timer.start();
    if (!m_prepared) return fail(error, "content store has not been prepared");
    QLockFile lock(m_lockPath); if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    ImportLimits limits;
    limits.maxArchiveBytes = quint64(3) * 1024 * 1024 * 1024;
    limits.maxExpandedBytes = quint64(4) * 1024 * 1024 * 1024;
    limits.maxEntryBytes = quint64(1024) * 1024 * 1024;
    limits.maxEntries = 200000; limits.maxCompressionRatio = 200;
    AndroidZipReader reader(m_storeRoot + QStringLiteral("/staging"));
    if (!reader.open(source, limits, cancel, error)) return false;
    const qint64 archiveReady = timer.elapsed();
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
    for (const QVariant &value : manifest.value("files").toList()) {
        const QVariantMap record = value.toMap();
        const QString path = record.value("path").toString();
        if (!mediaPath(path) || !payload.contains(path) || folded.contains(key(path))
            || record.value("size").toULongLong() != payload.value(path)->uncompressedSize)
            return fail(error, "media manifest path or size mismatch: " + path);
        folded.insert(key(path)); records.insert(path, record);
    }
    if (records.size() != payload.size() || payload.isEmpty()) return fail(error, "media manifest does not exactly describe ZIP payload");
    if (!space(m_storeRoot, total, error)) return false;
    QTemporaryDir temp(m_storeRoot + "/staging/media-XXXXXX");
    if (!temp.isValid()) return fail(error, "cannot stage media package");
    quint64 completed = 0;
    QSet<QString> checkedDirectories;
    for (auto it = payload.cbegin(); it != payload.cend(); ++it) {
        if (cancelled(cancel, error)) return false;
        const Progress report = [&](quint64 done, quint64) { if (progress) progress(completed + done, total); };
        const QString path = temp.path() + "/runtime/" + it.key();
        if (!extractFile(reader, *it.value(), path, cancel, report, error, &checkedDirectories)) return false;
        completed += it.value()->uncompressedSize;
    }
    const QString blob = uuid();
    QVariantMap package = infoMap("media", blob, payload.keys(), {}, false);
    package.insert("role", "media"); package.insert("complete_media", true);
    package.insert("media_records", manifest.value("files"));
    package.insert("archiveBytes", reader.archiveSize()); package.insert("expandedBytes", total);
    if (!syncStagedPayload(temp.path(), error) || cancelled(cancel, error)) return false;
    const qint64 payloadReady = timer.elapsed();
    if (!jsonWrite(temp.path() + "/package.json", package, error)) return false;
    if (cancelled(cancel, error)) return false;
    if (!QDir().rename(temp.path(), m_storeRoot + "/blobs/" + blob)) return fail(error, "cannot publish media payload");
    temp.setAutoRemove(false);
    const bool result = stageSnapshot(withPackage(m_snapshot, package), error, cancel);
    if (!result) collectUnusedVersions();
    qInfo("Android media import: success=%d files=%lld archive_ms=%lld payload_ms=%lld snapshot_ms=%lld total_ms=%lld",
          result ? 1 : 0, static_cast<long long>(payload.size()), static_cast<long long>(archiveReady),
          static_cast<long long>(payloadReady - archiveReady), static_cast<long long>(timer.elapsed() - payloadReady),
          static_cast<long long>(timer.elapsed()));
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
        QSet<QString> checkedDirectories;
        for (const auto &entry : reader.entries()) {
            if (entry.directory || entry.path == "runtime-content.json") continue;
            const Progress report = [&](quint64 done, quint64) { if (progress) progress(completed + done, total); };
            if (!extractFile(reader, entry, temp.path() + "/runtime/" + entry.path, cancel, report, error, &checkedDirectories)) return false;
            completed += entry.uncompressedSize;
        }
        archiveBytes = reader.archiveSize();
    }
    if (!coverage(entries, files, error) || cancelled(cancel, error)) return false;
    const QString blob = uuid();
    QVariantMap package = infoMap(bundleId, blob, files, entries, false);
    package.insert("archiveBytes", archiveBytes); package.insert("expandedBytes", total);
    if (!syncStagedPayload(temp.path(), error) || cancelled(cancel, error)) return false;
    if (!jsonWrite(temp.path() + "/package.json", package, error)) return false;
    if (!QDir().rename(temp.path(), m_storeRoot + "/blobs/" + blob)) return fail(error, "cannot publish extension payload");
    temp.setAutoRemove(false);
    const bool result = stageSnapshot(withPackage(m_snapshot, package), error, cancel);
    if (!result) collectUnusedVersions();
    return result;
}

bool AndroidContentStore::stageModularPackage(QIODevice &source, const QString &filename,
                                             Cancelled *cancel, const Progress &progress,
                                             QString *error)
{
    if (!m_prepared) return fail(error, "content store has not been prepared");
    if (!safePath(filename) || filename.contains('/') || !filename.endsWith(".zip", Qt::CaseInsensitive))
        return fail(error, "modular package import must be a ZIP file");
    QLockFile lock(m_lockPath);
    if (!lock.tryLock(1000)) return fail(error, "content store is busy");
    QTemporaryDir temp(m_storeRoot + "/staging/modular-XXXXXX");
    if (!temp.isValid()) return fail(error, "cannot stage modular package");

    ImportLimits limits;
    limits.maxArchiveBytes = quint64(512) * 1024 * 1024;
    limits.maxExpandedBytes = quint64(1024) * 1024 * 1024;
    limits.maxEntryBytes = quint64(128) * 1024 * 1024;
    limits.maxEntries = 100000;
    limits.maxCompressionRatio = 200;
    AndroidZipReader reader(m_storeRoot + QStringLiteral("/staging"));
    if (!reader.open(source, limits, cancel, error)) return false;

    const AndroidZipReader::Entry *manifestEntry = nullptr;
    for (const auto &entry : reader.entries()) {
        if (entry.directory) continue;
        if (entry.path == QStringLiteral("manifest.json")) {
            manifestEntry = &entry;
            continue;
        }
    }
    if (!manifestEntry) return fail(error, "modular package ZIP must contain root manifest.json");
    QVariantMap rawManifest;
    if (!readZipJson(reader, *manifestEntry, &rawManifest, cancel, error)) return false;
    const QString id = rawManifest.value("id").toString();
    static const QRegularExpression modularId(QStringLiteral("^[a-z0-9][a-z0-9_-]*$"));
    if (!modularId.match(id).hasMatch()) return fail(error, "invalid modular package id");

    const QString packageRoot = temp.path() + QStringLiteral("/runtime/packages/") + id;
    if (!directory(packageRoot, error)) return false;
    // Use archive metadata to reject undeclared payloads during extraction.
    // This does not enumerate installed media or inspect their checksums.
    QSet<QString> declaredPaths{QStringLiteral("manifest.json")};
    for (const QVariant &file : rawManifest.value("files").toList())
        declaredPaths.insert(file.toMap().value("path").toString());
    quint64 expanded = 0;
    for (const auto &entry : reader.entries()) {
        if (entry.directory) continue;
        if (!declaredPaths.contains(entry.path)) return fail(error, "undeclared modular package payload: " + entry.path);
        expanded += entry.uncompressedSize;
    }
    if (!space(m_storeRoot, expanded, error)) return false;
    quint64 completed = 0;
    for (const auto &entry : reader.entries()) {
        if (entry.directory) continue;
        const QString target = entry.path == QStringLiteral("manifest.json")
            ? packageRoot + QStringLiteral("/manifest.json")
            : packageRoot + QLatin1Char('/') + entry.path;
        const Progress report = [&](quint64 done, quint64) {
            if (progress) progress(completed + done, expanded);
        };
        if (!extractFile(reader, entry, target, cancel, report, error)) return false;
        completed += entry.uncompressedSize;
    }

    QString validationError;
    const QSanPackages::Package package = QSanPackages::parsePackage(packageRoot, &validationError, false, false);
    if (!validationError.isEmpty()) return fail(error, validationError);
    const QStringList files = modularFiles(package);
    for (const QString &path : files)
        if (!optionalMediaPath(path) && !regular(temp.path() + "/runtime/" + path))
            return fail(error, "modular package payload is missing: " + path);
    if (!syncStagedPayload(packageRoot, error)) return false;

    const QString blob = uuid();
    QVariantMap metadata = modularInfo(package, blob, files, false);
    metadata.insert("archiveBytes", reader.archiveSize());
    metadata.insert("expandedBytes", expanded);
    if (!jsonWrite(temp.path() + "/package.json", metadata, error)) return false;
    if (!QDir().rename(temp.path(), m_storeRoot + "/blobs/" + blob))
        return fail(error, "cannot publish modular package payload");
    temp.setAutoRemove(false);
    const bool result = stageSnapshot(withPackage(m_snapshot, metadata), error, cancel);
    if (!result) collectUnusedVersions();
    return result;
}
