#include "replay-game-state.h"
#include "protocol.h"
#include "protocol/card-provenance-message.h"
#include "protocol/session/session-payloads.h"
#include "json.h"

#include <QCoreApplication>
#include <QTextStream>

using namespace QSanProtocol;

namespace {
bool expect(bool condition, const QString &label)
{
    if (condition) return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

ProtocolMessage notification(CommandType command, const QVariant &body)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Room;
    message.destination = ProtocolEndpoint::Client;
    message.command = static_cast<int>(command);
    message.hasPayload = true;
    message.payload = body;
    return message;
}

ProtocolMessage provenanceMessage(const QVariant &body)
{
    return notification(S_COMMAND_CARD_PROVENANCE, body);
}

ProtocolMessage gameSeedMessage(quint64 seed)
{
    const QVariantMap log{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("log_type"), QStringLiteral("#GameSeed")},
        {QStringLiteral("from_player"), QString()},
        {QStringLiteral("to_players"), QStringList()},
        {QStringLiteral("card_string"), QString()},
        {QStringLiteral("arguments"),
         QStringList{QString::number(seed), QString(), QString(), QString(), QString()}}
    };
    return notification(S_COMMAND_LOG_SKILL, log);
}

ProtocolMessage setupMessage(const QVariant &setup)
{
    return notification(S_COMMAND_SETUP, setup);
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    ReplayGameState state;

    CardProvenanceMessage v2;
    v2.kind = QStringLiteral("use");
    v2.initiator = QStringLiteral("initiator");
    v2.card = QStringLiteral("#testCard:.::");
    v2.sourceOwner = QStringLiteral("sourceOwner");
    v2.sourceSkill = QStringLiteral("sourceSkill");
    v2.sourceInstanceId = 3;
    v2.activationOwner = QStringLiteral("activationOwner");
    v2.activationSkill = QStringLiteral("activationSkill");
    v2.activationInstanceId = 7;
    ok = expect(state.applyMessage(provenanceMessage(v2.toVariant())),
                "V2 provenance accepted") && ok;
    QList<QVariantMap> records = state.getCardProvenance();
    ok = expect(records.size() == 1, "V2 record count") && ok;
    ok = expect(records.first().value("sourceOwner").toString() == "sourceOwner", "V2 source owner") && ok;
    ok = expect(records.first().value("activationOwner").toString() == "activationOwner", "V2 activation owner") && ok;

    JsonArray obsolete;
    obsolete << 1 << "response" << "oldInitiator" << "#old:.::"
             << "oldSource" << 1 << "oldActivation" << 2;
    ok = expect(!state.applyMessage(provenanceMessage(obsolete)),
                "obsolete provenance rejected") && ok;

    ok = expect(state.applyMessage(gameSeedMessage(Q_UINT64_C(4815162342))),
                "Game seed replay log accepted") && ok;
    ok = expect(state.getCardProvenance().size() == 1,
                "Game seed replay log preserved existing state") && ok;

    SetupPayload setup;
    setup.serverName = QStringLiteral("server");
    setup.gameMode = QStringLiteral("03_1v2");
    setup.gameRuleMode = QStringLiteral("standard");
    setup.playerCount = 3;
    ok = expect(state.applyMessage(setupMessage(setup.toVariant())),
                "Setup command accepted") && ok;
    ok = expect(state.getGlobalState().gameMode == QStringLiteral("03_1v2"),
                "Setup command captured game mode") && ok;
    ok = expect(!state.applyMessage(setupMessage(QStringLiteral("obsolete setup"))),
                "positional Setup command rejected") && ok;

    JsonArray malformed;
    malformed << 2 << "use" << "initiator";
    ok = expect(!state.applyMessage(provenanceMessage(malformed)), "Malformed provenance rejected") && ok;
    return ok ? 0 : 1;
}
