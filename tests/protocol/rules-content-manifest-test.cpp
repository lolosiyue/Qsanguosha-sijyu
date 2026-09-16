#include "rules-content-manifest.h"

#include <QTextStream>

namespace {
int caseCount = 0;

bool expect(bool condition, const QString &label)
{
    ++caseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

bool emptyManifestIsValid()
{
    const auto manifest = QSanRules::parseContentManifest({});
    return expect(manifest.isValid(), QStringLiteral("empty manifest is valid"))
        && expect(manifest.entries.isEmpty(), QStringLiteral("empty manifest has no entries"))
        && expect(QSanRules::manifestScripts(manifest).isEmpty(),
                  QStringLiteral("empty manifest yields no scripts"));
}

bool declarationOrderIsPreserved()
{
    const auto manifest = QSanRules::parseContentManifest({
        QStringLiteral("extensions/zabing.lua"), QStringLiteral("extensions/AIgeneral.lua"),
        QStringLiteral("extensions/sijyu.lua")});
    const QStringList expected{QStringLiteral("extensions/zabing.lua"),
                               QStringLiteral("extensions/AIgeneral.lua"),
                               QStringLiteral("extensions/sijyu.lua")};
    return expect(manifest.isValid(), QStringLiteral("ordered manifest is valid"))
        && expect(QSanRules::manifestScripts(manifest) == expected,
                  QStringLiteral("declaration order is preserved verbatim"));
}

bool satelliteRolesAreSeparated()
{
    const auto manifest = QSanRules::parseContentManifest({
        QStringLiteral("extensions/LuaOldEnemy.lua;libs=lua/luaoldenemy_lib.lua"),
        QStringLiteral("extensions/sijyu.lua;lang=lang/zh_CN/Package/Sijyu.lua;ai=lua/ai/sijyu-ai.lua")});
    const QStringList hashed{QStringLiteral("extensions/LuaOldEnemy.lua"),
                             QStringLiteral("lua/luaoldenemy_lib.lua"),
                             QStringLiteral("extensions/sijyu.lua")};
    const QStringList delivered{QStringLiteral("extensions/LuaOldEnemy.lua"),
                                 QStringLiteral("lua/luaoldenemy_lib.lua"),
                                 QStringLiteral("extensions/sijyu.lua"),
                                 QStringLiteral("lang/zh_CN/Package/Sijyu.lua")};
    const QStringList serverOnly{QStringLiteral("lua/ai/sijyu-ai.lua")};
    return expect(manifest.isValid(), QStringLiteral("satellite manifest is valid"))
        && expect(QSanRules::manifestHashedFiles(manifest) == hashed,
                  QStringLiteral("hashed closure excludes lang and ai"))
        && expect(QSanRules::manifestDeliveredFiles(manifest) == delivered,
                  QStringLiteral("delivered closure includes lang but excludes ai"))
        && expect(QSanRules::manifestServerOnlyFiles(manifest) == serverOnly,
                  QStringLiteral("server-only closure is the ai role"));
}

bool satelliteFieldsAreNormalised()
{
    const auto manifest = QSanRules::parseContentManifest({
        QStringLiteral("extensions/a.lua;ai=lua/ai/z.lua,lua/ai/a.lua;libs=lua/z.lua,lua/a.lua;lang=lang/z.lua,lang/a.lua")});
    return expect(manifest.isValid(), QStringLiteral("unsorted satellite fields are valid"))
        && expect(manifest.entries.first().libs == QStringList{QStringLiteral("lua/a.lua"), QStringLiteral("lua/z.lua")},
                  QStringLiteral("libs are sorted"))
        && expect(manifest.entries.first().lang == QStringList{QStringLiteral("lang/a.lua"), QStringLiteral("lang/z.lua")},
                  QStringLiteral("lang is sorted"))
        && expect(manifest.entries.first().ai == QStringList{QStringLiteral("lua/ai/a.lua"), QStringLiteral("lua/ai/z.lua")},
                  QStringLiteral("ai is sorted"));
}

bool middleclassIsAcceptedAsAi()
{
    const auto manifest = QSanRules::parseContentManifest({
        QStringLiteral("extensions/sijyu.lua;ai=lua/lib/middleclass.lua")});
    return expect(manifest.isValid(), QStringLiteral("middleclass is a valid ai path"));
}

bool invalidManifestsAreRejected()
{
    struct Case { const char *entry; const char *reason; };
    static const Case cases[] = {
        {"lua/sneaky.lua", "script must be extensions/<name>.lua"},
        {"extensions/a.lua;libs=lua/config.lua", "libs path is not extension library content"},
        {"extensions/a.lua;libs=lua/ai/smart-ai.lua", "libs path is not extension library content"},
        {"extensions/a.lua;libs=lua/lib/middleclass.lua", "libs path is not extension library content"},
        {"extensions/a.lua;lang=lua/utilities.lua", "lang path must be under lang/"},
        {"extensions/a.lua;ai=lua/lib/sqlite3.lua", "ai path is not server-only AI content"},
        {"extensions/../secret.lua", "path traversal is not allowed"},
        {"extensions/a.lua;libs=lua/../etc/x.lua", "path traversal is not allowed"},
        {"extensions/a.lua;libs=lua/x.lua;libs=lua/y.lua", "duplicate field key"},
        {"extensions/a.lua;packages=Standard", "unknown field key"},
        {"extensions/a.lua;libs", "libs path is not extension library content"},
        {"extensions/a.lua;libs=lua/x.lua;lang=lua/x.lua", "lang path must be under lang/"},
        {"extensions/a.lua;libs=/lua/x.lua", "path traversal is not allowed"},
        {"extensions/a.lua;libs=lua\\x.lua", "path traversal is not allowed"},
        {"extensions/a.lua;libs=lua/./x.lua", "path traversal is not allowed"}};
    for (const auto &one : cases) {
        const auto manifest = QSanRules::parseContentManifest({QString::fromLatin1(one.entry)});
        const QString expected = QStringLiteral("entry 0: ") + QString::fromLatin1(one.reason);
        if (!expect(manifest.error == expected,
                    QStringLiteral("rejects %1 with %2").arg(QString::fromLatin1(one.entry), expected)))
            return false;
    }
    const auto duplicated = QSanRules::parseContentManifest({QStringLiteral("extensions/a.lua"),
                                                               QStringLiteral("extensions/a.lua")});
    return expect(duplicated.error == QStringLiteral("entry 1: duplicate path"),
                  QStringLiteral("rejects a path declared twice"))
        && expect(QSanRules::parseContentManifest({
                      QStringLiteral("extensions/a.lua;ai=lua/ai/x.lua,lua/ai/x.lua")}).error
                      == QStringLiteral("entry 0: duplicate path"),
                  QStringLiteral("rejects paths duplicated within a role"));
}

QJsonArray samplePackages()
{
    return {QJsonObject{{QStringLiteral("id"), QStringLiteral("core_pack")},
        {QStringLiteral("version"), QStringLiteral("1.0")},
        {QStringLiteral("dependencies"), QJsonArray{}},
        {QStringLiteral("assets"), QJsonObject{{QStringLiteral("image/old.png"), QStringLiteral("image/new.png")}}}}};
}

bool packageDescriptorsAreValidated()
{
    const QJsonObject good{{QStringLiteral("schema_version"), 3},
        {QStringLiteral("profile"), QStringLiteral("packages-v1")},
        {QStringLiteral("packages"), samplePackages()},
        {QStringLiteral("extensions"), QJsonArray{QJsonObject{
            {QStringLiteral("name"), QStringLiteral("old")},
            {QStringLiteral("script"), QStringLiteral("packages/core_pack/lua/old.lua")},
            {QStringLiteral("dependencies"), QJsonArray{}},
            {QStringLiteral("libs"), QJsonArray{QStringLiteral("packages/core_pack/lua/lib.lua")}},
            {QStringLiteral("lang"), QJsonArray{QStringLiteral("packages/core_pack/translation/en.lua")}},
            {QStringLiteral("ai"), QJsonArray{QStringLiteral("packages/core_pack/lua/ai/old-ai.lua")}}}}}};
    const auto parsed = QSanRules::parseRuntimeContent(good);
    if (!expect(parsed.isValid(), QStringLiteral("valid packages-v1 descriptor parses"))) return false;
    const auto roundTrip = QSanRules::parseRuntimeContent(QSanRules::runtimeContentDescriptor(parsed));
    if (!expect(roundTrip.isValid(), QStringLiteral("packages-v1 descriptor round-trips"))) return false;
    if (!expect(!QSanRules::runtimeContentDescriptor(parsed).value(QStringLiteral("extensions"))
                    .toArray().first().toObject().contains(QStringLiteral("package")),
                QStringLiteral("runtime extension descriptor omits package field"))) return false;
    QJsonArray changedPackages = samplePackages();
    QJsonObject changedPackage = changedPackages.first().toObject();
    changedPackage.insert(QStringLiteral("version"), QStringLiteral("2.0"));
    changedPackages.replace(0, changedPackage);
    const auto changed = QSanRules::parseRuntimeContent(QJsonObject{
        {QStringLiteral("schema_version"), 3}, {QStringLiteral("profile"), QStringLiteral("packages-v1")},
        {QStringLiteral("packages"), changedPackages},
        {QStringLiteral("extensions"), good.value(QStringLiteral("extensions"))}});
    if (!expect(changed.isValid() && QSanRules::runtimeContentDigest(parsed)
                    == QSanRules::runtimeContentDigest(changed),
                QStringLiteral("package version metadata does not change runtime content identity"))) return false;
    QJsonObject bad = good;
    QJsonArray packages = samplePackages();
    QJsonObject package = packages.first().toObject();
    package.insert(QStringLiteral("dependencies"), QJsonArray{QStringLiteral("missing")});
    packages.replace(0, package);
    bad.insert(QStringLiteral("packages"), packages);
    if (!expect(!QSanRules::parseRuntimeContent(bad).isValid(),
                QStringLiteral("undeclared package dependency is rejected"))) return false;
    bad = good;
    QJsonArray extensions = bad.value(QStringLiteral("extensions")).toArray();
    QJsonObject entry = extensions.first().toObject();
    entry.insert(QStringLiteral("script"), QStringLiteral("packages/unknown/lua/old.lua"));
    extensions.replace(0, entry);
    bad.insert(QStringLiteral("extensions"), extensions);
    if (!expect(!QSanRules::parseRuntimeContent(bad).isValid(),
                QStringLiteral("extension cannot refer to an undeclared package"))) return false;
    bad = good;
    QJsonArray duplicateAliases = samplePackages();
    QJsonObject firstPackage = duplicateAliases.first().toObject();
    firstPackage.insert(QStringLiteral("assets"), QJsonObject{
        {QStringLiteral("image/old.png"), QStringLiteral("image/a.png")}});
    duplicateAliases.replace(0, firstPackage);
    QJsonObject secondPackage{{QStringLiteral("id"), QStringLiteral("other_pack")},
        {QStringLiteral("version"), QStringLiteral("1.0")}, {QStringLiteral("dependencies"), QJsonArray{}},
        {QStringLiteral("assets"), QJsonObject{{QStringLiteral("image/OLD.png"), QStringLiteral("image/b.png")}}}};
    duplicateAliases.append(secondPackage);
    bad.insert(QStringLiteral("packages"), duplicateAliases);
    if (!expect(!QSanRules::parseRuntimeContent(bad).isValid(),
                QStringLiteral("package asset aliases collide case-insensitively"))) return false;
    bad = good;
    extensions = bad.value(QStringLiteral("extensions")).toArray();
    entry = extensions.first().toObject();
    entry.insert(QStringLiteral("name"), QStringLiteral("OLD"));
    entry.insert(QStringLiteral("script"), QStringLiteral("packages/core_pack/lua/OLD.lua"));
    extensions.append(entry);
    bad.insert(QStringLiteral("extensions"), extensions);
    if (!expect(!QSanRules::parseRuntimeContent(bad).isValid(),
                QStringLiteral("extension names collide case-insensitively in v3"))) return false;
    bad = good;
    extensions = bad.value(QStringLiteral("extensions")).toArray();
    entry = extensions.first().toObject();
    entry.insert(QStringLiteral("name"), QStringLiteral("second"));
    entry.insert(QStringLiteral("script"), QStringLiteral("packages/core_pack/lua/OLD.lua"));
    extensions.append(entry);
    bad.insert(QStringLiteral("extensions"), extensions);
    return expect(!QSanRules::parseRuntimeContent(bad).isValid(),
                  QStringLiteral("extension paths collide case-insensitively in v3"));
}

bool packageMergeKeepsLegacySlots()
{
    const auto legacy = QSanRules::parseContentManifest({QStringLiteral("extensions/first.lua"),
                                                          QStringLiteral("extensions/replace.lua"),
                                                          QStringLiteral("extensions/last.lua")});
    const QJsonArray replacements{QJsonObject{
        {QStringLiteral("name"), QStringLiteral("replace")},
        {QStringLiteral("script"), QStringLiteral("packages/core_pack/lua/replace.lua")},
        {QStringLiteral("dependencies"), QJsonArray{}}, {QStringLiteral("libs"), QJsonArray{}},
        {QStringLiteral("lang"), QJsonArray{}}, {QStringLiteral("ai"), QJsonArray{}}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("new")},
        {QStringLiteral("script"), QStringLiteral("packages/core_pack/lua/new.lua")},
        {QStringLiteral("dependencies"), QJsonArray{}}, {QStringLiteral("libs"), QJsonArray{}},
        {QStringLiteral("lang"), QJsonArray{}}, {QStringLiteral("ai"), QJsonArray{}}}};
    const auto merged = QSanRules::mergePackageContent(legacy, replacements, samplePackages());
    return expect(merged.isValid(), QStringLiteral("package content merges"))
        && expect(QSanRules::manifestScripts(merged) == QStringList{
            QStringLiteral("extensions/first.lua"), QStringLiteral("packages/core_pack/lua/replace.lua"),
            QStringLiteral("extensions/last.lua"), QStringLiteral("packages/core_pack/lua/new.lua")},
            QStringLiteral("replacement keeps the legacy slot and new extension appends"))
        && expect(QSanRules::runtimeContentDescriptor(merged).value(QStringLiteral("schema_version")).toInt() == 3,
                  QStringLiteral("package descriptors select runtime schema v3"));
}
}

int main()
{
    if (!emptyManifestIsValid() || !declarationOrderIsPreserved() || !satelliteRolesAreSeparated()
        || !satelliteFieldsAreNormalised()
        || !middleclassIsAcceptedAsAi() || !invalidManifestsAreRejected()
        || !packageDescriptorsAreValidated() || !packageMergeKeepsLegacySlots())
        return 1;
    QTextStream(stdout) << "rules content manifest: " << caseCount << " checks passed\n";
    return 0;
}
