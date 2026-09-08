#include "rules-bundle-identity.h"
#include "card.h"
#include "engine.h"
#include "package.h"
#include "runtime-paths.h"
#include "rules-source-identity.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

namespace {
struct IdentityState
{
    QMutex mutex;
    QVariantMap captured;
    QString beforeHash;
    QString root;
    QPointer<Engine> boundEngine;
};

IdentityState &identityState()
{
    // Process-lifetime storage avoids cross-TU static destruction order when
    // the production WASM host's destructor calls EngineBootstrap::shutdown.
    // clear() releases the Engine binding and captured data on every shutdown.
    static IdentityState *state = new IdentityState;
    return *state;
}

QString digest(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QVariantMap unavailable(const QString &reason)
{
    return {{QStringLiteral("schema_version"), 1},
            {QStringLiteral("available"), false},
            {QStringLiteral("reason"), reason}};
}

// W2 deliberately admits only the audited bootstrap closure used by the
// native/WASM gates. Unknown extensions must not be labelled builtin merely
// because their registered card count happens to match. W7 adds more profiles.
QString builtinContentHash(const QString &directory)
{
    const QStringList expected{QStringLiteral("lua/config.lua"),
        QStringLiteral("lua/lib/json.lua"), QStringLiteral("lua/sanguosha.lua"),
        QStringLiteral("lua/sgs_ex.lua"), QStringLiteral("lua/utilities.lua")};
    QStringList found;
    for (const QString &subdir : {QStringLiteral("lua"), QStringLiteral("extensions"),
             QStringLiteral("lang"), QStringLiteral("etc/customScenes"), QStringLiteral("etc/testScenes")}) {
        const QString path = QDir(directory).filePath(subdir);
        if (QFileInfo(path).isSymLink())
            return {};
        QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo file = it.fileInfo();
            if (file.isSymLink())
                return {};
            if (file.isDir())
                continue;
            if (!file.isFile())
                return {};
            found.append(QDir(directory).relativeFilePath(file.absoluteFilePath()));
        }
    }
    found.sort();
    if (found != expected)
        return {};
    // Custom scenarios in a separate writable root are outside builtin-v1.
    const QString userRoot = QSanRuntimePaths::userDataRoot();
    if (!userRoot.isEmpty() && QDir(userRoot).absolutePath() != QDir(directory).absolutePath()) {
        const QDir scenes(QDir(userRoot).filePath(QStringLiteral("etc/customScenes")));
        if (QFileInfo(scenes.absolutePath()).isSymLink()
            || !scenes.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty())
            return {};
    }
    QByteArray inventory("qsanguosha-builtin-v1\n");
    for (const QString &name : expected) {
        QFile file(QDir(directory).filePath(name));
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 16 * 1024 * 1024)
            return {};
        const QByteArray bytes = file.readAll();
        if (file.error() != QFile::NoError || bytes.size() != file.size())
            return {};
        inventory += name.toUtf8() + '\t' + QByteArray::number(bytes.size()) + '\t'
            + digest(bytes).toLatin1() + '\n';
    }
    return digest(inventory);
}

QString registryHash(Engine *engine)
{
    QJsonArray registry;
    for (int id = 0; id < engine->getCardCount(); ++id) {
        const Card *card = engine->getEngineCard(id);
        if (!card)
            return {};
        // Positional rows preserve exact numeric registration order.
        registry.append(QJsonArray{id, card->objectName(), card->getClassName(),
            static_cast<int>(card->getSuit()), card->getNumber(), card->getPackage()});
    }
    return digest(QJsonDocument(registry).toJson(QJsonDocument::Compact));
}

QVariantList packageNames(Engine *engine)
{
    QVariantList names;
    for (const Package *package : engine->getPackages())
        names.append(package->objectName());
    return names;
}
}

namespace QSanRulesIdentity {
void clear()
{
    IdentityState &state = identityState();
    QMutexLocker lock(&state.mutex);
    state.captured = unavailable(QStringLiteral("bootstrap_unsealed"));
    state.beforeHash.clear();
    state.root.clear();
    state.boundEngine.clear();
}

void beginBootstrap()
{
    IdentityState &state = identityState();
    QMutexLocker lock(&state.mutex);
    state.captured = unavailable(QStringLiteral("bootstrap_unsealed"));
    state.boundEngine.clear();
    state.root = QSanRuntimePaths::assetRoot();
    if (state.root.isEmpty())
        state.root = QDir::currentPath();
    state.beforeHash = builtinContentHash(state.root);
}

void finishBootstrap(bool manualMode)
{
    IdentityState &state = identityState();
    QMutexLocker lock(&state.mutex);
    state.boundEngine = Sanguosha;
    if (manualMode || !state.boundEngine || !state.boundEngine->getLuaState()) {
        state.captured = unavailable(QStringLiteral("bootstrap_unavailable"));
        return;
    }
#if defined(QSAN_XP_LEGACY)
    state.captured = unavailable(QStringLiteral("unsupported_build_profile"));
    return;
#endif
    LuaLocker luaLock;
    const QString afterHash = builtinContentHash(state.root);
    if (state.beforeHash.isEmpty() || afterHash != state.beforeHash) {
        state.captured = unavailable(QStringLiteral("unsupported_or_changed_content_profile"));
        return;
    }
    const QString cards = registryHash(state.boundEngine);
    if (cards.isEmpty() || state.boundEngine->getCardCount() <= 0) {
        state.captured = unavailable(QStringLiteral("incomplete_card_registry"));
        return;
    }
    state.captured = {{QStringLiteral("schema_version"), 1},
        {QStringLiteral("available"), true}, {QStringLiteral("profile"), QStringLiteral("builtin-v1")},
        {QStringLiteral("protocol_version"), 2}, {QStringLiteral("bridge_schema"), 1},
        {QStringLiteral("rules_abi"), QStringLiteral("qsan-client-rules-v1")},
        {QStringLiteral("source_sha256"), QStringLiteral(QSAN_RULES_SOURCE_SHA256)},
        {QStringLiteral("bindings_sha256"), QStringLiteral(QSAN_RULES_BINDINGS_SHA256)},
        {QStringLiteral("lua_sha256"), afterHash}, {QStringLiteral("card_registry_sha256"), cards},
        {QStringLiteral("card_count"), state.boundEngine->getCardCount()},
        {QStringLiteral("packages"), packageNames(state.boundEngine)},
        {QStringLiteral("interaction_schemas"), QVariantMap{{QStringLiteral("card-selection"), 1}}}};
}

QVariantMap current()
{
    IdentityState &state = identityState();
    QMutexLocker lock(&state.mutex);
    if (!state.boundEngine || state.boundEngine != Sanguosha)
        return unavailable(QStringLiteral("bootstrap_unsealed"));
    if (!state.captured.value(QStringLiteral("available")).toBool())
        return state.captured;
    LuaLocker luaLock;
    // Never refresh a running VM's identity from newly installed disk bytes.
    // A restart/rebuild, rather than retry(), is required after content drift.
    if (builtinContentHash(state.root) != state.beforeHash
        || registryHash(state.boundEngine) != state.captured.value(QStringLiteral("card_registry_sha256")).toString()
        || packageNames(state.boundEngine) != state.captured.value(QStringLiteral("packages")).toList()) {
        state.captured = unavailable(QStringLiteral("rules_changed_since_bootstrap"));
    }
    return state.captured;
}
}
