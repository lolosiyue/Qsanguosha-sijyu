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
namespace {
const QStringList &coreFiles()
{
    static const QStringList files{QStringLiteral("lua/config.lua"), QStringLiteral("lua/sanguosha.lua"),
        QStringLiteral("lua/utilities.lua"), QStringLiteral("lua/sgs_ex.lua"), QStringLiteral("lua/lib/json.lua")};
    return files;
}

bool regularAsset(const QString &path)
{
    // Check every component: QFile::open alone would follow a symlinked parent.
    QString current = QSanRuntimePaths::assetRoot();
    if (QFileInfo(current).isSymLink()) return false;
    for (const auto &part : path.split(QLatin1Char('/'))) {
        current = QDir(current).filePath(part);
        if (QFileInfo(current).isSymLink()) return false;
    }
    return QFileInfo(current).isFile();
}

QJsonObject snapshot(const QStringList &paths)
{
    QJsonObject result;
    for (const auto &path : paths) {
        if (!regularAsset(path)) return {};
        QFile file(QSanRuntimePaths::assetPath(path));
        if (!file.open(QIODevice::ReadOnly)) return {};
        const QByteArray bytes = file.readAll();
        if (file.error() != QFile::NoError) return {};
        result.insert(path, QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    }
    return result;
}
}

QJsonObject coreLuaSnapshot()
{
    return snapshot(coreFiles());
}

QJsonObject declaredLuaSnapshot(const ContentManifest &manifest)
{
    if (!manifest.isValid()) return {};
    return snapshot(manifestHashedFiles(manifest));
}

bool contentScanIsDeclared(const ContentManifest &manifest)
{
    if (!manifest.isValid()) return false;
    for (const char *key : {"LUA_PATH", "LUA_CPATH", "LUA_INIT", "LUA_PATH_5_4", "LUA_CPATH_5_4", "LUA_INIT_5_4"})
        if (!qgetenv(key).isEmpty()) return false;
    // Custom scenarios, including the user-data overlay, remain outside this contract.
    for (const auto &root : {QSanRuntimePaths::assetRoot(), QSanRuntimePaths::userDataRoot()}) {
        if (root.isEmpty()) continue;
        const QString directory = QDir(root).filePath(QStringLiteral("etc"));
        if (QFileInfo(directory).isSymLink()) return false;
        QDirIterator scenarios(directory,
            QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (scenarios.hasNext()) {
            scenarios.next();
            if (scenarios.fileInfo().isSymLink() || scenarios.fileInfo().isFile())
                return false;
        }
    }
    const QStringList declared = manifestDeliveredFiles(manifest);
    // AI is optional on client deployments, even when declared as server metadata.
    const QStringList required = coreFiles() + declared;
    for (const auto &path : required)
        if (!regularAsset(path)) return false;
    // Server-only AI is excluded by path, but must obey the same symlink rule.
    for (const char *root : {"lua", "extensions", "lang"}) {
        const QString directory = QSanRuntimePaths::assetPath(QLatin1String(root));
        if (QFileInfo(directory).isSymLink()) return false;
        QDirIterator it(directory, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString path = QLatin1String(root) + QLatin1Char('/') + QDir(directory).relativeFilePath(it.filePath());
            if (it.fileInfo().isSymLink()) return false;
            const bool serverOnlyAi = path.startsWith(QLatin1String("lua/ai/"))
                || path == QLatin1String("lua/lib/middleclass.lua");
            if (it.fileInfo().isFile() && path.endsWith(QLatin1String(".lua"), Qt::CaseInsensitive)
                && !coreFiles().contains(path) && !declared.contains(path) && !serverOnlyAi)
                return false;
        }
    }
    return true;
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

namespace {
QJsonObject interactionSchemas()
{
    QJsonObject interactions;
    for (const auto &flow : QSanProtocol::ProtocolPayloadRegistry::descriptors()) {
        if (flow.key.messageType != QSanProtocol::ProtocolMessageType::Request
            || flow.key.source != QSanProtocol::ProtocolEndpoint::Room
            || flow.key.destination != QSanProtocol::ProtocolEndpoint::Client)
            continue;
        interactions.insert(QString::number(flow.key.command), digest(QStringLiteral("qsan-interaction-v1"),
            QJsonArray{flow.targetSchema, QString::fromLatin1(QSAN_RULES_PROTOCOL_HASH)}));
    }
    return interactions;
}
}

QJsonObject exportCodeIdentity()
{
    return sealCode({{QStringLiteral("schema_version"), IdentitySchema},
        {QStringLiteral("protocol_version"), 2}, {QStringLiteral("bridge_schema"), BridgeSchema},
        {QStringLiteral("cpp_hash"), QString::fromLatin1(QSAN_RULES_CPP_HASH)},
        {QStringLiteral("bindings_abi"), QString::fromLatin1(QSAN_RULES_BINDINGS_HASH)},
        {QStringLiteral("interaction_schemas"), interactionSchemas()}});
}

QJsonObject exportContentManifest(const Engine &engine)
{
    const auto identity = engine.rulesBundleIdentity();
    if (!QSanRules::validate(identity))
        return {};
    const auto &manifest = engine.rulesContentManifest();
    if (!manifest.isValid() || !contentScanIsDeclared(manifest))
        return {};
    QStringList paths = coreFiles();
    const QStringList rules = manifestHashedFiles(manifest);
    const QStringList presentation = manifestDeliveredFiles(manifest);
    for (const auto &path : rules)
        if (!paths.contains(path)) paths << path;
    QJsonArray files;
    QJsonObject deliveredRules;
    for (const auto &path : paths) {
        QFileInfo info(QSanRuntimePaths::assetPath(path));
        QFile file(info.filePath());
        if (!regularAsset(path) || !file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray bytes = file.readAll();
        if (file.error() != QFile::NoError)
            return {};
        deliveredRules.insert(path, QString::fromLatin1(QCryptographicHash::hash(bytes,
            QCryptographicHash::Sha256).toHex()));
        files.append(QJsonObject{{QStringLiteral("path"), path},
            {QStringLiteral("role"), QStringLiteral("rules")},
            {QStringLiteral("size"), static_cast<qint64>(bytes.size())},
            {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes,
                QCryptographicHash::Sha256).toHex())}});
    }
    for (const auto &path : presentation) {
        if (paths.contains(path)) continue;
        QFileInfo info(QSanRuntimePaths::assetPath(path));
        QFile file(info.filePath());
        if (!regularAsset(path) || !file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray bytes = file.readAll();
        if (file.error() != QFile::NoError)
            return {};
        files.append(QJsonObject{{QStringLiteral("path"), path},
            {QStringLiteral("role"), QStringLiteral("presentation")},
            {QStringLiteral("size"), static_cast<qint64>(bytes.size())},
            {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes,
                QCryptographicHash::Sha256).toHex())}});
    }
    // Seal the bytes read for delivery against the already loaded VM.
    deliveredRules.insert(QStringLiteral("@runtime-content"),
        runtimeContentDigest(manifest));
    if (digest(QStringLiteral("qsan-lua-closure-v1"), deliveredRules)
        != identity.value(QStringLiteral("lua_hash")).toString())
        return {};
    return {{QStringLiteral("schema_version"), 2},
            {QStringLiteral("profile"), QStringLiteral("declared-v2")},
            {QStringLiteral("runtime_content"), runtimeContentDescriptor(manifest)},
            {QStringLiteral("files"), files}};
}

QJsonObject exportIdentity(const Engine &engine, const QJsonObject &loadedLua)
{
    // Never relabel an already loaded VM using replacement files from disk.
    QJsonObject current = coreLuaSnapshot();
    const auto &manifest = engine.rulesContentManifest();
    const QJsonObject declared = declaredLuaSnapshot(manifest);
    const bool complete = current.size() == coreFiles().size()
        && declared.size() == manifestHashedFiles(manifest).size();
    for (auto it = declared.begin(); it != declared.end(); ++it)
        current.insert(it.key(), it.value());
    if (!complete || !contentScanIsDeclared(manifest) || loadedLua.isEmpty() || loadedLua != current)
        return {{QStringLiteral("schema_version"), IdentitySchema},
                {QStringLiteral("error_code"), QStringLiteral("rules_content_unsupported")}};
    const auto registry = exportRegistry(engine);
    if (registry.isEmpty())
        return {};
    QJsonObject identity = exportCodeIdentity();
    identity.insert(QStringLiteral("ruleset"), engine.getMODName());
    identity.insert(QStringLiteral("content_profile"), QStringLiteral("declared-v2"));
    identity.insert(QStringLiteral("packages"), QJsonArray::fromStringList(engine.rulesPackageOrder()));
    identity.insert(QStringLiteral("card_registry_hash"), digest(QStringLiteral("qsan-card-registry-v1"), registry));
    QJsonObject identityLua = loadedLua;
    identityLua.insert(QStringLiteral("@runtime-content"), runtimeContentDigest(manifest));
    identity.insert(QStringLiteral("lua_hash"),
        digest(QStringLiteral("qsan-lua-closure-v1"), identityLua));
    return seal(identity);
}
}
