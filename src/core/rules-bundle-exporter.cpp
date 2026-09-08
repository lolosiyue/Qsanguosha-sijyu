#include "rules-bundle-exporter.h"
#include "rules-bundle-build.h"
#include "engine.h"
#include "card.h"
#include "package.h"
#include "runtime-paths.h"
#include "protocol/rules-bundle-identity.h"
#include "protocol/protocol-payload-registry.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>

namespace QSanRules {
QJsonObject builtinLuaSnapshot()
{
    for (const char *key : {"LUA_PATH", "LUA_CPATH", "LUA_INIT", "LUA_PATH_5_4", "LUA_CPATH_5_4", "LUA_INIT_5_4"})
        if (!qgetenv(key).isEmpty()) return {};
    const QStringList files{QStringLiteral("lua/config.lua"), QStringLiteral("lua/sanguosha.lua"),
        QStringLiteral("lua/utilities.lua"), QStringLiteral("lua/sgs_ex.lua"), QStringLiteral("lua/lib/json.lua")};
    QJsonObject result;
    // Custom scenario definitions are executable rules content too. They are
    // outside the five-file builtin closure, including the user-data overlay.
    for (const auto &root : {QSanRuntimePaths::assetRoot(), QSanRuntimePaths::userDataRoot()}) {
        if (root.isEmpty()) continue;
        const QString directory = QDir(root).filePath(QStringLiteral("etc"));
        if (QFileInfo(directory).isSymLink()) return {};
        QDirIterator scenarios(directory,
            QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (scenarios.hasNext()) {
            scenarios.next();
            if (scenarios.fileInfo().isSymLink() || scenarios.fileInfo().isFile())
                return {};
        }
    }
    // Extra executable content is not silently assigned the builtin profile,
    // even when its package is banned or DisableLua is set.
    for (const char *root : {"lua", "extensions", "lang"}) {
        if (QFileInfo(QLatin1String(root)).isSymLink()) return {};
        QDirIterator it(QLatin1String(root), QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QDir::cleanPath(it.next());
            if (it.fileInfo().isSymLink())
                return {};
            if (it.fileInfo().isFile() && path.endsWith(QLatin1String(".lua"), Qt::CaseInsensitive)
                && !files.contains(path))
                return {};
        }
    }
    for (const auto &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray bytes = file.readAll();
        if (bytes.isEmpty() || file.error() != QFile::NoError)
            return {};
        result.insert(path, QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }
    return result;
}

QJsonObject exportRegistry(const Engine &engine)
{
    QJsonArray cards;
    for (int id = 0; id < engine.getCardCount(); ++id) {
        const Card *card = engine.getEngineCard(id);
        if (!card)
            return {};
        cards.append(QJsonObject{{QStringLiteral("id"), id},
            {QStringLiteral("object_name"), card->objectName()},
            {QStringLiteral("suit"), static_cast<int>(card->getSuit())},
            {QStringLiteral("number"), card->getNumber()},
            {QStringLiteral("class_name"), card->getClassName()},
            {QStringLiteral("package"), card->getPackage()}});
    }
    return {{QStringLiteral("schema_version"), 1}, {QStringLiteral("card_count"), cards.size()},
            {QStringLiteral("registry"), cards}};
}

QJsonObject exportIdentity(const Engine &engine, const QJsonObject &loadedLua)
{
    // Never relabel an already loaded VM using replacement files from disk.
    if (loadedLua.isEmpty() || loadedLua != builtinLuaSnapshot())
        return {{QStringLiteral("schema_version"), IdentitySchema},
                {QStringLiteral("error_code"), QStringLiteral("rules_content_unsupported")}};
    const auto registry = exportRegistry(engine);
    if (registry.isEmpty())
        return {};
    QJsonObject interactions;
    for (const auto &flow : QSanProtocol::ProtocolPayloadRegistry::descriptors()) {
        if (flow.key.messageType != QSanProtocol::ProtocolMessageType::Request
            || flow.key.source != QSanProtocol::ProtocolEndpoint::Room
            || flow.key.destination != QSanProtocol::ProtocolEndpoint::Client)
            continue;
        // Bind the production schema and its source, not a second Web manifest.
        interactions.insert(QString::number(flow.key.command), digest(QStringLiteral("qsan-interaction-v1"),
            QJsonArray{flow.targetSchema, QString::fromLatin1(QSAN_RULES_PROTOCOL_HASH)}));
    }
    return seal({{QStringLiteral("schema_version"), IdentitySchema},
        {QStringLiteral("protocol_version"), 2}, {QStringLiteral("bridge_schema"), BridgeSchema},
        {QStringLiteral("ruleset"), engine.getMODName()},
        {QStringLiteral("content_profile"), QStringLiteral("builtin-v1")},
        {QStringLiteral("cpp_hash"), QString::fromLatin1(QSAN_RULES_CPP_HASH)},
        {QStringLiteral("bindings_abi"), QString::fromLatin1(QSAN_RULES_BINDINGS_HASH)},
        {QStringLiteral("packages"), QJsonArray::fromStringList(engine.rulesPackageOrder())},
        {QStringLiteral("card_registry_hash"), digest(QStringLiteral("qsan-card-registry-v1"), registry)},
        {QStringLiteral("lua_hash"), digest(QStringLiteral("qsan-lua-closure-v1"), loadedLua)},
        {QStringLiteral("interaction_schemas"), interactions}});
}
}
