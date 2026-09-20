#include "../src/core/package-catalog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

namespace
{
bool writeFile(const QString &root, const QString &path, const QByteArray &bytes)
{
    const QString full = QDir(root).filePath(path);
    if (!QDir().mkpath(QFileInfo(full).absolutePath())) return false;
    QFile file(full);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonObject inventoryEntry(const QString &root, const QString &path, const QString &role)
{
    QFile file(QDir(root).filePath(path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    return {{QStringLiteral("path"), path}, {QStringLiteral("role"), role},
            {QStringLiteral("size"), bytes.size()},
            {QStringLiteral("sha256"), QString::fromLatin1(
                 QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
}

bool makePackage(const QString &runtimeRoot, const QString &id, const QStringList &dependencies,
                 const QString &script = QStringLiteral("lua/main.lua"),
                 const QString &assetName = QStringLiteral("image/general/hero.png"))
{
    const QString root = QDir(runtimeRoot).filePath(QStringLiteral("packages/") + id);
    if (!writeFile(root, script, QByteArray("return true\n"))
        || !writeFile(root, assetName, QByteArray("png"))) return false;
    QJsonArray dependencyArray;
    for (const QString &dependency : dependencies) dependencyArray.append(dependency);
    QJsonObject extension {{QStringLiteral("name"), id}, {QStringLiteral("script"), script},
                           {QStringLiteral("dependencies"), QJsonArray()},
                           {QStringLiteral("libs"), QJsonArray()}, {QStringLiteral("lang"), QJsonArray()},
                           {QStringLiteral("ai"), QJsonArray()}};
    QJsonArray files {inventoryEntry(root, script, QStringLiteral("rules")),
                      inventoryEntry(root, assetName, QStringLiteral("data"))};
    QJsonObject assets {{assetName, assetName}};
    QJsonObject manifest {{QStringLiteral("schema_version"), 1}, {QStringLiteral("id"), id},
                          {QStringLiteral("version"), QStringLiteral("1.0.0")},
                          {QStringLiteral("engine_api"), 1},
                          {QStringLiteral("dependencies"), dependencyArray},
                          {QStringLiteral("extensions"), QJsonArray {extension}},
                          {QStringLiteral("assets"), assets}, {QStringLiteral("files"), files}};
    return writeFile(root, QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson());
}

bool catalogResolvesAndOrders()
{
    QTemporaryDir temp;
    if (!temp.isValid() || !makePackage(temp.path(), QStringLiteral("base"), {})
        || !makePackage(temp.path(), QStringLiteral("addon"), {QStringLiteral("base")},
                        QStringLiteral("lua/main.lua"), QStringLiteral("image/general/addon.png"))) return false;
    QSanPackages::Catalog catalog = QSanPackages::loadCatalog(temp.path());
    if (!catalog.isValid() || catalog.packages.size() != 2
        || catalog.packages.at(0).id != QStringLiteral("base")
        || catalog.packages.at(1).id != QStringLiteral("addon")) return false;
    QSanPackages::installCatalog(catalog);
    QString error;
    const QString explicitPath = QSanPackages::resolve(temp.path(), QStringLiteral("package://base/general/hero.png"), &error);
    if (!error.isEmpty() || explicitPath.isEmpty()) return false;
    const QString legacyPath = QSanPackages::resolve(temp.path(), QStringLiteral("image/general/hero.png"), &error);
    if (!error.isEmpty() || legacyPath.isEmpty()) return false;
    const QJsonArray extensions = QSanPackages::extensionDescriptors(catalog);
    const QJsonArray packages = QSanPackages::packageDescriptors(catalog);
    QSanPackages::clearCatalog();
    return extensions.size() == 2 && packages.size() == 2
        && extensions.first().toObject().value(QStringLiteral("script")).toString()
               == QStringLiteral("packages/base/lua/main.lua");
}

bool rejectsBrokenInventoryAndExplicitUri()
{
    QTemporaryDir temp;
    if (!temp.isValid() || !makePackage(temp.path(), QStringLiteral("bad"), {},
                                        QStringLiteral("lua/../escape.lua"))) return false;
    QString error;
    const QSanPackages::Package package = QSanPackages::parsePackage(
        QDir(temp.path()).filePath(QStringLiteral("packages/bad")), &error);
    if (error.isEmpty() || !package.id.isEmpty()) return false;

    QTemporaryDir good;
    if (!good.isValid() || !makePackage(good.path(), QStringLiteral("ok"), {})) return false;
    QSanPackages::Catalog catalog = QSanPackages::loadCatalog(good.path());
    if (!catalog.isValid()) return false;
    QSanPackages::installCatalog(catalog);
    const QString missing = QSanPackages::resolve(good.path(), QStringLiteral("package://ok/audio/missing.ogg"), &error);
    QSanPackages::clearCatalog();
    if (!missing.isEmpty() || error.isEmpty()) return false;
    const QString source = QDir(good.path()).filePath(QStringLiteral("packages/ok/lua/main.lua"));
    QFile changed(source);
    return changed.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && changed.write(QByteArray("modified after sealing\n")) > 0
        && !QSanPackages::loadCatalog(good.path()).isValid();
}

bool metadataOnlyRetainsLuaBoundary()
{
    QTemporaryDir temp;
    if (!temp.isValid() || !makePackage(temp.path(), QStringLiteral("optional"), {})) return false;
    const QString root = temp.path() + QStringLiteral("/packages/optional");
    QFile manifestFile(root + QStringLiteral("/manifest.json"));
    if (!manifestFile.open(QIODevice::ReadOnly)) return false;
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    QJsonArray files = manifest.value(QStringLiteral("files")).toArray();
    for (int i = 0; i < files.size(); ++i) {
        QJsonObject entry = files.at(i).toObject();
        entry.remove(QStringLiteral("sha256"));
        files[i] = entry;
    }
    manifest.insert(QStringLiteral("files"), files);
    if (!writeFile(root, QStringLiteral("manifest.json"), QJsonDocument(manifest).toJson())
        || !QFile::remove(root + QStringLiteral("/image/general/hero.png"))) return false;
    // Android accepts optional missing media and obsolete hash metadata, while
    // desktop verification stays strict and missing executable Lua still fails.
    if (!QSanPackages::loadCatalog(temp.path(), false, false).isValid()
        || QSanPackages::loadCatalog(temp.path()).isValid()) return false;
    if (!QFile::remove(root + QStringLiteral("/lua/main.lua"))) return false;
    return !QSanPackages::loadCatalog(temp.path(), false, false).isValid();
}

bool rejectsDependencyAndAssetCollisions()
{
    QTemporaryDir missing;
    if (!missing.isValid() || !makePackage(missing.path(), QStringLiteral("addon"), {QStringLiteral("absent")})) return false;
    if (QSanPackages::loadCatalog(missing.path()).error.isEmpty()) return false;
    QTemporaryDir collision;
    if (!collision.isValid() || !makePackage(collision.path(), QStringLiteral("one"), {})
        || !makePackage(collision.path(), QStringLiteral("two"), {})) return false;
    return QSanPackages::loadCatalog(collision.path()).error.contains(QStringLiteral("collision"));
}

bool rootCachePreservesAssetValidation()
{
    QTemporaryDir first, second;
    if (!first.isValid() || !second.isValid()
        || !makePackage(first.path(), QStringLiteral("base"), {})
        || !makePackage(second.path(), QStringLiteral("base"), {})) return false;
    QSanPackages::clearCatalog();
    const QString reference = QStringLiteral("image/general/hero.png");
    QString error;
    const QString firstAsset = QSanPackages::resolve(first.path(), reference, &error);
    if (!error.isEmpty() || firstAsset.isEmpty()) return false;
    const QString secondAsset = QSanPackages::resolve(second.path(), reference, &error);
    if (!error.isEmpty() || secondAsset.isEmpty() || firstAsset == secondAsset) return false;
    if (QSanPackages::resolve(first.path() + QStringLiteral("/."), reference, &error) != firstAsset
        || !error.isEmpty()) return false;

    // The same relative spelling must not reuse metadata from another CWD.
    const QString previousDirectory = QDir::currentPath();
    if (!QDir::setCurrent(first.path())) return false;
    const QString relativeFirst = QSanPackages::resolve(QStringLiteral("."), reference, &error);
    const bool firstOk = error.isEmpty();
#ifdef Q_OS_WIN
    const QString driveRelative = QDir::currentPath().left(2) + QStringLiteral(".");
    const QString driveAsset = QSanPackages::resolve(driveRelative, reference, &error);
    const bool driveOk = error.isEmpty() && driveAsset == firstAsset;
#endif
    const bool changedDirectory = QDir::setCurrent(second.path());
    const QString relativeSecond = QSanPackages::resolve(QStringLiteral("."), reference, &error);
    const bool secondOk = error.isEmpty();
    const bool restoredDirectory = QDir::setCurrent(previousDirectory);
    if (!changedDirectory || !restoredDirectory || !firstOk || !secondOk
        || relativeFirst != firstAsset || relativeSecond != secondAsset) return false;
#ifdef Q_OS_WIN
    if (!driveOk) return false;
#endif

    // Warm root metadata must never cache file existence or silently fall back
    // to a loose asset when a declared package mapping goes missing.
    if (!writeFile(first.path(), reference, QByteArray("loose fallback"))
        || !QFile::remove(firstAsset)) return false;
    if (!QSanPackages::resolve(first.path(), reference, &error).isEmpty()
        || error.isEmpty()) return false;
    if (!writeFile(first.path(), QStringLiteral("packages/base/") + reference, QByteArray("png"))) return false;
    if (QSanPackages::resolve(first.path(), reference, &error) != firstAsset || !error.isEmpty()) return false;
    if (!QSanPackages::resolve(first.path(), QStringLiteral("package://base/../escape"), &error).isEmpty()
        || error.isEmpty()) return false;

    const quint64 revision = QSanPackages::catalogRevision();
    const QString addonAsset = QStringLiteral("image/general/addon.png");
    if (!makePackage(first.path(), QStringLiteral("addon"), {},
                     QStringLiteral("lua/main.lua"), addonAsset)) return false;
    const auto replacement = QSanPackages::loadCatalog(first.path());
    if (!replacement.isValid()) return false;
    QSanPackages::installCatalog(replacement);
    if (QSanPackages::catalogRevision() == revision) return false;
    const QString installed = QSanPackages::resolve(first.path(), addonAsset, &error);
    if (!error.isEmpty() || !installed.contains(QStringLiteral("/packages/addon/"))) return false;
    QSanPackages::clearCatalog();
    return QSanPackages::resolve(first.path(), addonAsset, &error) == installed && error.isEmpty();
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (!catalogResolvesAndOrders()) { std::fprintf(stderr, "catalog order/resolve failed\n"); return 1; }
    if (!rejectsBrokenInventoryAndExplicitUri()) { std::fprintf(stderr, "catalog path/inventory validation failed\n"); return 2; }
    if (!rejectsDependencyAndAssetCollisions()) { std::fprintf(stderr, "catalog dependency/collision validation failed\n"); return 3; }
    if (!metadataOnlyRetainsLuaBoundary()) { std::fprintf(stderr, "metadata-only Lua boundary failed\n"); return 5; }
    if (!rootCachePreservesAssetValidation()) { std::fprintf(stderr, "root cache asset validation failed\n"); return 6; }
    // Optional integration gate: consume the manifest emitted by the migration CLI.
    if (argc == 2) {
        const auto migrated = QSanPackages::loadCatalog(QString::fromLocal8Bit(argv[1]));
        if (!migrated.isValid() || migrated.packages.isEmpty()) {
            std::fprintf(stderr, "migrated catalog rejected: %s\n", qPrintable(migrated.error));
            return 4;
        }
        std::fprintf(stdout, "validated migrated packages: %lld\n", static_cast<long long>(migrated.packages.size()));
    }
    return 0;
}
