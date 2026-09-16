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
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (!catalogResolvesAndOrders()) { std::fprintf(stderr, "catalog order/resolve failed\n"); return 1; }
    if (!rejectsBrokenInventoryAndExplicitUri()) { std::fprintf(stderr, "catalog path/inventory validation failed\n"); return 2; }
    if (!rejectsDependencyAndAssetCollisions()) { std::fprintf(stderr, "catalog dependency/collision validation failed\n"); return 3; }
    if (!metadataOnlyRetainsLuaBoundary()) { std::fprintf(stderr, "metadata-only Lua boundary failed\n"); return 5; }
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
