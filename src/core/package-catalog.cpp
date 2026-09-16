#include "package-catalog.h"

#include "runtime-paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QReadWriteLock>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace
{
using namespace QSanPackages;
Catalog g_activeCatalog;
QString g_activeRoot;
QHash<QString, Catalog> g_lazyCatalogs;
QReadWriteLock g_catalogLock;

bool validId(const QString &id)
{
    static const QRegularExpression expression(QStringLiteral("^[a-z0-9][a-z0-9_-]*$"));
    return expression.match(id).hasMatch();
}

bool safeRelativePath(const QString &path)
{
    static const QRegularExpression portablePath(QStringLiteral("^[A-Za-z0-9_./+-]+$"));
    if (path.isEmpty() || !portablePath.match(path).hasMatch()
        || path.startsWith(QLatin1Char('/')))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
            return false;
    }
    return true;
}

bool hasPrefix(const QString &path, const QString &prefix)
{
    return path.startsWith(prefix) && path.size() > prefix.size();
}

bool stringArray(const QJsonValue &value, QStringList *result, const QString &field,
                 QString *error)
{
    if (!value.isArray()) {
        *error = QStringLiteral("%1 must be an array").arg(field);
        return false;
    }
    for (const QJsonValue &item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty()) {
            *error = QStringLiteral("%1 entries must be non-empty strings").arg(field);
            return false;
        }
        result->append(item.toString());
    }
    return true;
}

bool packageFilePath(const QString &path, QString *error)
{
    if (!safeRelativePath(path)) {
        *error = QStringLiteral("unsafe package-relative path: %1").arg(path);
        return false;
    }
    return true;
}

bool validateExtensions(const QJsonObject &manifest, QHash<QString, QString> *declaredLua,
                        QHash<QString, QString> *declaredLuaPaths, QString *error)
{
    const QJsonValue extensionsValue = manifest.value(QStringLiteral("extensions"));
    if (!extensionsValue.isArray()) {
        *error = QStringLiteral("extensions must be an array");
        return false;
    }
    QSet<QString> names;
    QSet<QString> moduleNames;
    const auto claim = [declaredLua, declaredLuaPaths, &moduleNames, error](const QString &path, const QString &role) {
        const QString folded = path.toCaseFolded();
        if (declaredLua->contains(folded)) {
            *error = QStringLiteral("duplicate or case-colliding extension path: %1").arg(path);
            return false;
        }
        if (role == QStringLiteral("rules")) {
            QString module = path.mid(QStringLiteral("lua/").size());
            module.chop(QStringLiteral(".lua").size());
            module.prepend(QStringLiteral("lua."));
            module.replace(QLatin1Char('/'), QLatin1Char('.'));
            module = module.toCaseFolded();
            if (moduleNames.contains(module)) {
                *error = QStringLiteral("Lua module path collision: %1").arg(path);
                return false;
            }
            moduleNames.insert(module);
        }
        declaredLua->insert(folded, role);
        declaredLuaPaths->insert(folded, path);
        return true;
    };
    for (const QJsonValue &extensionValue : extensionsValue.toArray()) {
        if (!extensionValue.isObject()) {
            *error = QStringLiteral("extension entries must be objects");
            return false;
        }
        const QJsonObject extension = extensionValue.toObject();
        const QString name = extension.value(QStringLiteral("name")).toString();
        const QString script = extension.value(QStringLiteral("script")).toString();
        const QString foldedName = name.toCaseFolded();
        if (name.isEmpty() || names.contains(foldedName)
            || !hasPrefix(script, QStringLiteral("lua/"))
            || script.startsWith(QStringLiteral("lua/ai/"))
            || !script.endsWith(QStringLiteral(".lua")) || !packageFilePath(script, error)) {
            if (error->isEmpty())
                *error = QStringLiteral("extension requires a unique name and lua/*.lua rules script");
            return false;
        }
        names.insert(foldedName);
        if (!claim(script, QStringLiteral("rules"))) return false;
        for (const QString &field : {QStringLiteral("dependencies"), QStringLiteral("libs"),
                                     QStringLiteral("lang"), QStringLiteral("ai")}) {
            QStringList entries;
            if (!stringArray(extension.value(field), &entries, field, error))
                return false;
            for (const QString &path : entries) {
                const QString prefix = field == QStringLiteral("libs")
                    ? QStringLiteral("lua/") : field == QStringLiteral("lang")
                    ? QStringLiteral("translation/") : field == QStringLiteral("ai")
                    ? QStringLiteral("lua/ai/") : QString();
                if (!prefix.isEmpty()) {
                    if (!hasPrefix(path, prefix)
                        || (field == QStringLiteral("libs") && path.startsWith(QStringLiteral("lua/ai/")))
                        || !path.endsWith(QStringLiteral(".lua"))
                        || !packageFilePath(path, error)) {
                        if (error->isEmpty())
                            *error = QStringLiteral("invalid %1 Lua path: %2").arg(field, path);
                        return false;
                    }
                    const QString role = field == QStringLiteral("libs")
                        ? QStringLiteral("rules") : field == QStringLiteral("lang")
                        ? QStringLiteral("presentation") : QStringLiteral("ai");
                    if (!claim(path, role)) return false;
                }
            }
        }
    }
    return true;
}

bool validateAssets(const QJsonObject &manifest, QString *error)
{
    const QJsonValue assetsValue = manifest.value(QStringLiteral("assets"));
    if (!assetsValue.isObject()) {
        *error = QStringLiteral("assets must be an object");
        return false;
    }
    const QJsonObject assets = assetsValue.toObject();
    QSet<QString> legacyAliases;
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        const QString legacy = it.key();
        const QString target = it.value().toString();
        const bool mediaTarget = (hasPrefix(legacy, QStringLiteral("image/"))
                                  && hasPrefix(target, QStringLiteral("image/")))
            || (hasPrefix(legacy, QStringLiteral("audio/"))
                && hasPrefix(target, QStringLiteral("audio/")));
        if (!it.value().isString() || !mediaTarget || !safeRelativePath(legacy)
            || !safeRelativePath(target) || legacyAliases.contains(legacy.toCaseFolded())) {
            *error = QStringLiteral("invalid asset mapping: %1").arg(legacy);
            return false;
        }
        legacyAliases.insert(legacy.toCaseFolded());
    }
    return true;
}

bool verifyInventory(const QString &root, const QJsonObject &manifest,
                     const QHash<QString, QString> &declaredLua,
                     const QHash<QString, QString> &declaredLuaPaths,
                     bool verifyFiles, bool inspectFiles, QString *error)
{
    const QJsonValue filesValue = manifest.value(QStringLiteral("files"));
    if (!filesValue.isArray()) {
        *error = QStringLiteral("files must be an array");
        return false;
    }
    QHash<QString, QJsonObject> inventory;
    QHash<QString, QString> folded;
    QHash<QString, QString> actualCaseByFolded;
    const QSet<QString> roles = {QStringLiteral("rules"), QStringLiteral("ai"),
                                 QStringLiteral("presentation"), QStringLiteral("data")};
    for (const QJsonValue &value : filesValue.toArray()) {
        if (!value.isObject()) {
            *error = QStringLiteral("files entries must be objects");
            return false;
        }
        const QJsonObject entry = value.toObject();
        const QString path = entry.value(QStringLiteral("path")).toString();
        const QString role = entry.value(QStringLiteral("role")).toString();
        const QString key = path.toCaseFolded();
        const double size = entry.value(QStringLiteral("size")).toDouble(-1);
        if (!safeRelativePath(path) || inventory.contains(path) || folded.contains(key)
            || !roles.contains(role) || !entry.value(QStringLiteral("size")).isDouble()
            || size < 0 || size > 9007199254740991.0 || std::floor(size) != size
            || (inspectFiles && !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                    .match(entry.value(QStringLiteral("sha256")).toString()).hasMatch())) {
            *error = QStringLiteral("invalid or colliding file inventory entry: %1").arg(path);
            return false;
        }
        const bool luaPath = path.endsWith(QStringLiteral(".lua"));
        const bool roleMatchesPath =
            (role == QStringLiteral("rules") && hasPrefix(path, QStringLiteral("lua/"))
             && !path.startsWith(QStringLiteral("lua/ai/")) && luaPath)
            || (role == QStringLiteral("ai") && hasPrefix(path, QStringLiteral("lua/ai/")) && luaPath)
            || (role == QStringLiteral("presentation")
                && hasPrefix(path, QStringLiteral("translation/")) && luaPath)
            || (role == QStringLiteral("data")
                && (hasPrefix(path, QStringLiteral("image/"))
                    || hasPrefix(path, QStringLiteral("audio/"))
                    || hasPrefix(path, QStringLiteral("data/"))));
        if (!roleMatchesPath) {
            *error = QStringLiteral("file role does not match package path: %1 (%2)").arg(path, role);
            return false;
        }
        folded.insert(key, path);
        inventory.insert(path, entry);
        // Every executable payload must be reachable through an explicit declaration.
        if (role != QStringLiteral("data") && !declaredLua.contains(key)) {
            *error = QStringLiteral("package Lua file is not declared by an extension: %1").arg(path);
            return false;
        }
    }
    for (auto it = declaredLua.cbegin(); it != declaredLua.cend(); ++it) {
        if (!folded.contains(it.key())) {
            *error = QStringLiteral("declared Lua file is missing from files: %1").arg(it.key());
            return false;
        }
        const QString listedPath = folded.value(it.key());
        if (listedPath != declaredLuaPaths.value(it.key())) {
            *error = QStringLiteral("declared Lua path casing differs from inventory: %1")
                         .arg(declaredLuaPaths.value(it.key()));
            return false;
        }
        if (inventory.value(listedPath).value(QStringLiteral("role")).toString() != it.value()) {
            *error = QStringLiteral("declared Lua file has the wrong inventory role: %1").arg(listedPath);
            return false;
        }
    }
    const QJsonObject assets = manifest.value(QStringLiteral("assets")).toObject();
    for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
        const QString target = it.value().toString();
        const QString key = target.toCaseFolded();
        if (folded.contains(key)
            && (folded.value(key) != target
                || inventory.value(target).value(QStringLiteral("role")).toString()
                    != QStringLiteral("data"))) {
            *error = QStringLiteral("mapped asset spelling or role differs from files: %1").arg(target);
            return false;
        }
    }
    if (!inspectFiles) {
        // Android treats media as optional. Only executable declarations need
        // a real contained file; never enumerate or hash image/audio payloads.
        const QString boundary = QDir::fromNativeSeparators(root) + QLatin1Char('/');
        for (const QString &path : declaredLuaPaths) {
            QString cursor = root;
            for (const QString &part : path.split(QLatin1Char('/'))) {
                cursor = QDir(cursor).filePath(part);
                if (QFileInfo(cursor).isSymLink()) {
                    *error = QStringLiteral("declared Lua path contains a symlink: %1").arg(path);
                    return false;
                }
            }
            const QFileInfo info(cursor);
            if (!info.isFile() || !QDir::fromNativeSeparators(info.canonicalFilePath()).startsWith(boundary)) {
                *error = QStringLiteral("declared Lua file is missing or escapes its package: %1").arg(path);
                return false;
            }
        }
        return true;
    }
    int actualFileCount = 0;
    QDirIterator iterator(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString fullPath = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink()) {
            *error = QStringLiteral("package contains a symlink: %1").arg(fullPath);
            return false;
        }
        const QString relative = QDir(root).relativeFilePath(fullPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString foldedRelative = relative.toCaseFolded();
        if (info.isDir()) {
            const auto seen = actualCaseByFolded.constFind(foldedRelative);
            if (seen != actualCaseByFolded.cend() && seen.value() != relative) {
                *error = QStringLiteral("case-colliding package directories: %1 and %2")
                             .arg(seen.value(), relative);
                return false;
            }
            actualCaseByFolded.insert(foldedRelative, relative);
            continue;
        }
        if (!info.isFile()) {
            *error = QStringLiteral("package contains a non-regular path: %1").arg(relative);
            return false;
        }
        if (relative == QStringLiteral("manifest.json"))
            continue;
        if (!inventory.contains(relative)) {
            const QString collision = folded.value(foldedRelative);
            *error = collision.isEmpty()
                ? QStringLiteral("unlisted package file: %1").arg(relative)
                : QStringLiteral("case-colliding package files: %1 and %2").arg(collision, relative);
            return false;
        }
        const auto seen = actualCaseByFolded.constFind(foldedRelative);
        if (seen != actualCaseByFolded.cend() && seen.value() != relative) {
            *error = QStringLiteral("case-colliding package paths: %1 and %2").arg(seen.value(), relative);
            return false;
        }
        actualCaseByFolded.insert(foldedRelative, relative);
        ++actualFileCount;
        if (verifyFiles) {
            const QJsonObject entry = inventory.value(relative);
            if (static_cast<qint64>(entry.value(QStringLiteral("size")).toDouble()) != info.size()) {
                *error = QStringLiteral("package file size mismatch: %1").arg(relative);
                return false;
            }
            QFile file(fullPath);
            if (!file.open(QIODevice::ReadOnly)) {
                *error = QStringLiteral("cannot read package file: %1").arg(relative);
                return false;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) {
                *error = QStringLiteral("cannot hash package file: %1").arg(relative);
                return false;
            }
            const QByteArray digest = hash.result().toHex();
            if (QString::fromLatin1(digest) != entry.value(QStringLiteral("sha256")).toString()) {
                *error = QStringLiteral("package file checksum mismatch: %1").arg(relative);
                return false;
            }
        }
    }
    if (actualFileCount != inventory.size()) {
        *error = QStringLiteral("package file inventory does not match the installed files");
        return false;
    }
    return true;
}

QString packageRoot(const Catalog &catalog)
{
    if (catalog.packages.isEmpty())
        return QString();
    return QFileInfo(catalog.packages.first().root).absoluteDir().canonicalPath();
}

Catalog catalogForRoot(const QString &root)
{
    const QString packagesPath = QDir(root).filePath(QStringLiteral("packages"));
    const QString key = QFileInfo(packagesPath).canonicalFilePath().isEmpty()
        ? QDir::cleanPath(QFileInfo(packagesPath).absoluteFilePath())
        : QFileInfo(packagesPath).canonicalFilePath();
    {
        QReadLocker locker(&g_catalogLock);
        if (!g_activeCatalog.packages.isEmpty() && g_activeRoot == key)
            return g_activeCatalog;
        const auto cached = g_lazyCatalogs.constFind(key);
        if (cached != g_lazyCatalogs.cend())
            return cached.value();
    }

    // Parse outside the lock; the write-side recheck below makes duplicate first-use
    // lookups harmless while keeping steady-state asset resolution read-only.
#ifdef Q_OS_ANDROID
    const Catalog loaded = loadCatalog(root, false, false);
#else
    const Catalog loaded = loadCatalog(root, false);
#endif
    QWriteLocker locker(&g_catalogLock);
    if (!g_activeCatalog.packages.isEmpty() && g_activeRoot == key)
        return g_activeCatalog;
    const auto cached = g_lazyCatalogs.constFind(key);
    if (cached != g_lazyCatalogs.cend())
        return cached.value();
    g_lazyCatalogs.insert(key, loaded);
    return loaded;
}

QString assetTarget(const Catalog &catalog, const QString &legacy, bool *collision)
{
    QString result;
    *collision = false;
    for (const Package &package : catalog.packages) {
        const QJsonObject assets = package.manifest.value(QStringLiteral("assets")).toObject();
        QString target;
        for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
            if (it.key().toCaseFolded() == legacy) {
                target = it.value().toString();
                break;
            }
        }
        if (target.isEmpty())
            continue;
        if (!result.isEmpty() && result != QDir::cleanPath(package.root + QLatin1Char('/') + target)) {
            *collision = true;
            return QString();
        }
        result = QDir::cleanPath(package.root + QLatin1Char('/') + target);
    }
    return result;
}

QString containedExistingFile(const QString &base, const QString &relative, QString *error)
{
    if (!safeRelativePath(relative)) {
        if (error) *error = QStringLiteral("unsafe path: %1").arg(relative);
        return QString();
    }
    const QString absoluteBase = QFileInfo(base).canonicalFilePath();
    const QString candidate = QDir(base).filePath(relative);
    QString cursor = base;
    for (const QString &part : relative.split(QLatin1Char('/'))) {
        cursor = QDir(cursor).filePath(part);
        const QFileInfo info(cursor);
        if (info.isSymLink()) {
            if (error) *error = QStringLiteral("path contains a symlink: %1").arg(relative);
            return QString();
        }
    }
    const QString canonical = QDir::fromNativeSeparators(QFileInfo(candidate).canonicalFilePath());
    const QString normalizedBase = QDir::fromNativeSeparators(absoluteBase);
    QString boundary = normalizedBase;
    if (!boundary.endsWith(QLatin1Char('/')))
        boundary.append(QLatin1Char('/'));
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    if (absoluteBase.isEmpty() || canonical.isEmpty() || !QFileInfo(canonical).isFile()
        || !canonical.startsWith(boundary, pathCase)) {
        if (error) *error = QStringLiteral("file is missing or escapes its package: %1").arg(relative);
        return QString();
    }
    return canonical;
}
}

namespace QSanPackages
{
Package parsePackage(const QString &directory, QString *error, bool verifyFiles, bool inspectFiles)
{
    Package package;
    if (error) error->clear();
    QString localError;
    QString *failure = error ? error : &localError;
    const QFileInfo dirInfo(directory);
    const QString root = dirInfo.canonicalFilePath();
    if (root.isEmpty() || !dirInfo.isDir() || dirInfo.isSymLink()) {
        *failure = QStringLiteral("package path is not a real directory: %1").arg(directory);
        return package;
    }
    QFile manifestFile(QDir(root).filePath(QStringLiteral("manifest.json")));
    if (QFileInfo(manifestFile.fileName()).isSymLink()) {
        *failure = QStringLiteral("manifest.json must not be a symlink in %1").arg(root);
        return package;
    }
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        *failure = QStringLiteral("cannot read manifest.json in %1").arg(root);
        return package;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *failure = QStringLiteral("invalid manifest.json: %1").arg(parseError.errorString());
        return package;
    }
    package.manifest = document.object();
    package.id = package.manifest.value(QStringLiteral("id")).toString();
    package.version = package.manifest.value(QStringLiteral("version")).toString();
    if (!validId(package.id)
        || package.version.trimmed().isEmpty()
        || package.manifest.value(QStringLiteral("schema_version")).toInt(-1) != 1
        || package.manifest.value(QStringLiteral("engine_api")).toInt(-1) != 1) {
        *failure = QStringLiteral("invalid package id, version, schema_version, or engine_api in %1").arg(root);
        return Package();
    }
    if (!stringArray(package.manifest.value(QStringLiteral("dependencies")), &package.dependencies,
                     QStringLiteral("dependencies"), failure))
        return Package();
    QSet<QString> dependencies;
    for (const QString &dependency : package.dependencies) {
        if (!validId(dependency) || dependencies.contains(dependency)) {
            *failure = QStringLiteral("invalid or duplicate package dependency: %1").arg(dependency);
            return Package();
        }
        dependencies.insert(dependency);
    }
    QHash<QString, QString> declaredLua;
    QHash<QString, QString> declaredLuaPaths;
    if (!validateExtensions(package.manifest, &declaredLua, &declaredLuaPaths, failure)
        || !validateAssets(package.manifest, failure)
        || !verifyInventory(root, package.manifest, declaredLua, declaredLuaPaths,
                            verifyFiles, inspectFiles, failure))
        return Package();
    package.root = root;
    return package;
}

Catalog loadCatalog(const QString &runtimeRoot, bool verifyFiles, bool inspectFiles)
{
    Catalog catalog;
    const QFileInfo runtimeInfo(runtimeRoot);
    const QString packagesPath = QDir(runtimeRoot).filePath(QStringLiteral("packages"));
    const QFileInfo packagesInfo(packagesPath);
    if (runtimeInfo.isSymLink() || packagesInfo.isSymLink()) {
        catalog.error = QStringLiteral("runtime root and packages directory must not be symlinks");
        return catalog;
    }
    if (packagesInfo.exists() && !packagesInfo.isDir()) {
        catalog.error = QStringLiteral("packages path is not a directory: %1").arg(packagesPath);
        return catalog;
    }
    const QString base = packagesInfo.canonicalFilePath();
    if (base.isEmpty())
        return catalog;
    QDir packagesDir(base);
    const QStringList entries = packagesDir.entryList(
        QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    QHash<QString, Package> byId;
    for (const QString &name : entries) {
        if (!validId(name)) {
            catalog.error = QStringLiteral("invalid package directory name: %1").arg(name);
            return catalog;
        }
        QString error;
        Package package = parsePackage(packagesDir.filePath(name), &error, verifyFiles, inspectFiles);
        if (!error.isEmpty()) {
            catalog.error = error;
            return catalog;
        }
        if (package.id != name) {
            catalog.error = QStringLiteral("package directory name does not match manifest id: %1").arg(name);
            return catalog;
        }
        if (byId.contains(package.id)) {
            catalog.error = QStringLiteral("duplicate package id: %1").arg(package.id);
            return catalog;
        }
        byId.insert(package.id, package);
    }
    QStringList ready = byId.keys();
    std::sort(ready.begin(), ready.end());
    QSet<QString> emitted;
    while (!ready.isEmpty()) {
        bool progressed = false;
        for (int i = 0; i < ready.size();) {
            const QString id = ready.at(i);
            const Package package = byId.value(id);
            bool canEmit = true;
            for (const QString &dependency : package.dependencies) {
                if (!validId(dependency)) {
                    catalog.error = QStringLiteral("package %1 has invalid dependency id %2").arg(id, dependency);
                    return catalog;
                }
                if (!byId.contains(dependency)) {
                    catalog.error = QStringLiteral("package %1 has missing dependency %2").arg(id, dependency);
                    return catalog;
                }
                if (!emitted.contains(dependency)) canEmit = false;
            }
            if (!canEmit) { ++i; continue; }
            catalog.packages.append(package);
            emitted.insert(id);
            ready.removeAt(i);
            progressed = true;
            // Restart from the lexical head after each emission. The native
            // and Web packager must choose the same newly-ready package next.
            break;
        }
        if (!progressed) {
            catalog.error = QStringLiteral("package dependency cycle: %1").arg(ready.join(QStringLiteral(", ")));
            catalog.packages.clear();
            return catalog;
        }
    }
    // Legacy references must resolve to exactly one installed package asset.
    QHash<QString, QString> owners;
    for (const Package &package : catalog.packages) {
        const QJsonObject assets = package.manifest.value(QStringLiteral("assets")).toObject();
        for (auto it = assets.begin(); it != assets.end(); ++it) {
            const QString key = it.key().toCaseFolded();
            if (owners.contains(key)) {
                catalog.error = QStringLiteral("legacy asset mapping collision: %1").arg(it.key());
                catalog.packages.clear();
                return catalog;
            }
            owners.insert(key, package.id);
        }
    }
    return catalog;
}

QJsonArray extensionDescriptors(const Catalog &catalog)
{
    QJsonArray result;
    for (const Package &package : catalog.packages) {
        for (const QJsonValue &value : package.manifest.value(QStringLiteral("extensions")).toArray()) {
            const QJsonObject source = value.toObject();
            QJsonObject descriptor;
            descriptor.insert(QStringLiteral("package"), package.id);
            descriptor.insert(QStringLiteral("name"), source.value(QStringLiteral("name")));
            descriptor.insert(QStringLiteral("script"), QStringLiteral("packages/%1/%2").arg(package.id, source.value(QStringLiteral("script")).toString()));
            for (const QString &field : {QStringLiteral("dependencies"), QStringLiteral("libs"), QStringLiteral("lang"), QStringLiteral("ai")}) {
                QJsonArray paths;
                for (const QJsonValue &path : source.value(field).toArray())
                    paths.append(field == QStringLiteral("dependencies") ? path : QJsonValue(QStringLiteral("packages/%1/%2").arg(package.id, path.toString())));
                descriptor.insert(field, paths);
            }
            result.append(descriptor);
        }
    }
    return result;
}

QJsonArray packageDescriptors(const Catalog &catalog)
{
    QJsonArray result;
    for (const Package &package : catalog.packages) {
        QJsonObject descriptor;
        descriptor.insert(QStringLiteral("id"), package.id);
        descriptor.insert(QStringLiteral("version"), package.version);
        QJsonArray dependencies;
        for (const QString &dependency : package.dependencies) dependencies.append(dependency);
        descriptor.insert(QStringLiteral("dependencies"), dependencies);
        const QJsonValue assets = package.manifest.value(QStringLiteral("assets"));
        descriptor.insert(QStringLiteral("assets"), assets.isObject() ? assets : QJsonObject());
        result.append(descriptor);
    }
    return result;
}

void installCatalog(const Catalog &catalog)
{
    QWriteLocker locker(&g_catalogLock);
    g_activeCatalog = catalog;
    g_activeRoot = packageRoot(catalog);
    g_lazyCatalogs.clear();
}

void clearCatalog()
{
    QWriteLocker locker(&g_catalogLock);
    g_activeCatalog = Catalog();
    g_activeRoot.clear();
    g_lazyCatalogs.clear();
}

Catalog activeCatalog()
{
    QReadLocker locker(&g_catalogLock);
    return g_activeCatalog;
}

QString resolve(const QString &runtimeRoot, const QString &reference, QString *error)
{
    if (error) error->clear();
    QString root = QFileInfo(runtimeRoot).canonicalFilePath();
    if (root.isEmpty()) root = QDir::cleanPath(QFileInfo(runtimeRoot).absoluteFilePath());
    if (reference.startsWith(QStringLiteral("package://"))) {
        const QString tail = reference.mid(10);
        const int slash = tail.indexOf(QLatin1Char('/'));
        const QString packageId = slash < 0 ? tail : tail.left(slash);
        QString relative = slash < 0 ? QString() : tail.mid(slash + 1);
        if (!validId(packageId) || !safeRelativePath(relative)) {
            if (error) *error = QStringLiteral("invalid package URI: %1").arg(reference);
            return QString();
        }
        const Catalog catalog = catalogForRoot(root);
        const auto it = std::find_if(catalog.packages.cbegin(), catalog.packages.cend(), [&](const Package &p) { return p.id == packageId; });
        if (it == catalog.packages.cend()) {
            if (error) *error = catalog.error.isEmpty() ? QStringLiteral("unknown package: %1").arg(packageId) : catalog.error;
            return QString();
        }
        if (hasPrefix(relative, QStringLiteral("general/"))) {
            relative.prepend(QStringLiteral("image/"));
        } else if (!hasPrefix(relative, QStringLiteral("audio/"))
                   && !hasPrefix(relative, QStringLiteral("image/"))
                   && !hasPrefix(relative, QStringLiteral("lua/"))
                   && !hasPrefix(relative, QStringLiteral("translation/"))
                   && !hasPrefix(relative, QStringLiteral("data/"))) {
            if (error) *error = QStringLiteral("package URI must name general/, image/, audio/, lua/, translation/, or data/: %1").arg(reference);
            return QString();
        }
        return containedExistingFile(it->root, relative, error);
    }
    if (!safeRelativePath(reference)) {
        if (error) *error = QStringLiteral("unsafe legacy asset reference: %1").arg(reference);
        return QString();
    }
    if (!reference.startsWith(QStringLiteral("image/"))
        && !reference.startsWith(QStringLiteral("audio/")))
        return QDir(root).filePath(reference);

    const Catalog catalog = catalogForRoot(root);
    if (!catalog.error.isEmpty()) {
        if (error) *error = catalog.error;
        return QString();
    }
    {
        bool collision = false;
        const QString mapped = assetTarget(catalog, reference.toCaseFolded(), &collision);
        if (collision) {
            if (error) *error = QStringLiteral("ambiguous legacy asset reference: %1").arg(reference);
            return QString();
        }
        if (!mapped.isEmpty()) {
            for (const Package &package : catalog.packages) {
                const QString prefix = QDir::cleanPath(package.root + QLatin1Char('/'));
                if (mapped.startsWith(prefix)) {
                    const QString resolved = containedExistingFile(package.root,
                        QDir(package.root).relativeFilePath(mapped), error);
                    if (resolved.isEmpty() && error && error->isEmpty())
                        *error = QStringLiteral("mapped package asset is missing: %1").arg(reference);
                    return resolved;
                }
            }
            if (error) *error = QStringLiteral("mapped package asset has no owning package: %1").arg(reference);
            return QString();
        }
    }
    return QDir(root).filePath(reference);
}

QString packageDataPath(const QString &id)
{
    if (!validId(id)) return QString();
    const QString path = QSanRuntimePaths::userDataPath(QStringLiteral("packages/%1").arg(id));
    return QDir().mkpath(path) ? path : QString();
}
}
