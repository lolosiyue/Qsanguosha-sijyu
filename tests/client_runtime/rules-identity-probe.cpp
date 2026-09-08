#include "card.h"
#include "engine.h"
#include "engine-bootstrap.h"
#include "rules-bundle-identity.h"
#include "runtime-paths.h"
#include "server-connection-context.h"
#include "socket.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>

namespace {
class CaptureSocket final : public ClientSocket
{
public:
    QByteArray frame;
    void connectToHost() override {}
    void disconnectFromHost() override {}
    void send(const QByteArray &value) override { frame = value; }
    bool isConnected() const override { return true; }
    QString peerName() const override { return QStringLiteral("identity-test"); }
    QString peerAddress() const override { return QStringLiteral("127.0.0.1"); }
};
struct EngineGuard { ~EngineGuard() { EngineBootstrap::shutdown(); } };
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int outputIndex = args.indexOf(QStringLiteral("--output"));
    if (outputIndex < 0 || outputIndex + 1 >= args.size())
        return 2;
    const QString output = QFileInfo(args.at(outputIndex + 1)).absoluteFilePath();
    const bool expectUnavailable = args.contains(QStringLiteral("--expect-unavailable"));
    if (!expectUnavailable && !args.contains(QStringLiteral("--allow-staged-mutation"))) {
        QTextStream(stderr) << "Use check-rules-identity.py; mutation requires a private staged asset copy" << Qt::endl;
        return 2;
    }
    QString error;
    if (!QSanRuntimePaths::resolve(args, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 2;
    }
    EngineGuard guard;
    if (!EngineBootstrap::initialize(false, &error)) {
        QTextStream(stderr) << error << Qt::endl;
        return 2;
    }
    const QVariantMap identity = QSanRulesIdentity::current();
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char *label) {
        ++checks;
        if (!ok) { ++failures; QTextStream(stderr) << "FAIL: " << label << Qt::endl; }
    };
    check(identity.value(QStringLiteral("available")).toBool() == !expectUnavailable,
          "expected bootstrap content profile");
    check(QSanRulesIdentity::current() == identity, "repeated identity query is stable");

    CaptureSocket socket;
    ServerConnectionContext connection(&socket, 1);
    QSanProtocol::ServerHelloPayload hello;
    hello.gameVersion = QStringLiteral("identity-test");
    hello.modName = QStringLiteral("identity-test");
    hello.cardCount = Sanguosha->getCardCount();
    check(connection.sendHello(hello, &error), "real server HELLO encoding");
    QSanProtocol::ProtocolCodecRouter router;
    QSanProtocol::ProtocolMessage decoded;
    check(router.decode(socket.frame, &decoded).success, "real wire decode");
    check(decoded.payload.toMap().value(QStringLiteral("rules_bundle")).toMap() == identity,
          "identity survives the codec without dropping fields");
    QSanProtocol::ServerHelloPayload legacy;
    check(QSanProtocol::ServerHelloPayload::parse(decoded.payload, &legacy, &error)
          && legacy.cardCount == hello.cardCount && legacy.gameVersion == hello.gameVersion,
          "legacy native HELLO reader accepts the additive identity");

    if (!expectUnavailable) {
        const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
        for (const QString &key : {QStringLiteral("source_sha256"), QStringLiteral("bindings_sha256"),
                 QStringLiteral("lua_sha256"), QStringLiteral("card_registry_sha256")})
            check(hash.match(identity.value(key).toString()).hasMatch(), "nonempty SHA-256");
        check(identity.value(QStringLiteral("card_count")).toInt() == Sanguosha->getCardCount(),
              "identity count is actual engine count");
        // Only this probe's privately staged assets may be changed by this test.
        QFile config(QSanRuntimePaths::assetPath(QStringLiteral("lua/config.lua")));
        if (!config.open(QIODevice::ReadOnly)) return 2;
        const QByteArray original = config.readAll(); config.close();
        if (!config.open(QIODevice::WriteOnly | QIODevice::Append)) return 2;
        const QByteArray change("\n-- identity probe mutation\n");
        check(config.write(change) == change.size(), "change private staged content"); config.close();
        const QVariantMap changed = QSanRulesIdentity::current();
        check(!changed.value(QStringLiteral("available")).toBool()
            && changed.value(QStringLiteral("reason")) == QStringLiteral("rules_changed_since_bootstrap"),
            "a live engine is not re-labelled using newer disk bytes");
        if (!config.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
        check(config.write(original) == original.size(), "restore private content"); config.close();
        check(QSanRulesIdentity::current() == changed, "restoring files cannot silently reseal a poisoned identity");
    }
    EngineBootstrap::shutdown();
    check(!QSanRulesIdentity::current().value(QStringLiteral("available")).toBool(),
          "shutdown clears the published identity");
    QSaveFile file(output);
    const QByteArray json = QJsonDocument(QJsonObject{
        {QStringLiteral("schema_version"), 1}, {QStringLiteral("status"), failures ? "FAIL" : "PASS"},
        {QStringLiteral("checks"), checks}, {QStringLiteral("failures"), failures},
        {QStringLiteral("identity"), QJsonObject::fromVariantMap(identity)}}).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) return 2;
    return failures ? 1 : 0;
}
