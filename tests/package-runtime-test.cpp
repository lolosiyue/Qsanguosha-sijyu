#include "package-runtime.h"
#include "package-catalog.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto baseline = QSanRules::parseContentManifest({QStringLiteral("extensions/probe.lua")});
    QSanPackages::Package package;
    package.id = QStringLiteral("probe");
    package.version = QStringLiteral("1");
    package.manifest = QJsonObject{{"id", "probe"}, {"version", "1"}, {"dependencies", QJsonArray()},
        {"assets", QJsonObject()}, {"extensions", QJsonArray{QJsonObject{{"name", "probe"},
            {"script", "lua/probe.lua"}, {"dependencies", QJsonArray()}, {"libs", QJsonArray()},
            {"lang", QJsonArray()}, {"ai", QJsonArray()}}}}};
    QSanPackages::Catalog catalog;
    catalog.packages.append(package);
    QSanPackages::installCatalog(catalog);
    const auto oldContent = QSanPackages::effectiveContent(baseline, baseline);
    if (!oldContent.isValid() || oldContent.entries.size() != 1
        || oldContent.entries.first().script != QLatin1String("packages/probe/lua/probe.lua")) return 1;
    catalog.packages[0].version = QStringLiteral("2");
    catalog.packages[0].manifest.insert(QStringLiteral("version"), QStringLiteral("2"));
    QSanPackages::installCatalog(catalog);
    // An exported v1 descriptor cannot block the legitimately installed v2 catalog.
    const auto updated = QSanPackages::effectiveContent(oldContent, baseline);
    if (!updated.isValid() || updated.packages.first().toObject().value("version") != QLatin1String("2")) return 2;
    QSanPackages::clearCatalog();
    const auto restored = QSanPackages::effectiveContent(updated, baseline);
    if (!restored.isValid() || restored.entries.first().script != QLatin1String("extensions/probe.lua")
        || !restored.packages.isEmpty()) return 3;
    QTextStream(stdout) << "package runtime replacement/update/removal passed\n";
    return 0;
}
