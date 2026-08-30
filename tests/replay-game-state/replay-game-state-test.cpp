#include "replay-game-state.h"
#include "protocol.h"
#include "protocol/protocol-v1-message-adapter.h"
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

ProtocolMessage provenanceMessage(const JsonArray &body)
{
    Packet packet(S_TYPE_NOTIFICATION | S_DEST_CLIENT, S_COMMAND_CARD_PROVENANCE);
    packet.setMessageBody(body);
    return protocolMessageFromV1Packet(packet);
}

ProtocolMessage gameSeedMessage(quint64 seed)
{
    JsonArray log;
    log << "#GameSeed" << "" << "" << "" << QString::number(seed)
        << "" << "" << "" << "";
    Packet packet(S_TYPE_NOTIFICATION | S_DEST_CLIENT, S_COMMAND_LOG_SKILL);
    packet.setMessageBody(log);
    return protocolMessageFromV1Packet(packet);
}

ProtocolMessage setupMessage(const QString &setup)
{
    Packet packet(S_TYPE_NOTIFICATION | S_DEST_CLIENT, S_COMMAND_SETUP);
    packet.setMessageBody(setup);
    return protocolMessageFromV1Packet(packet);
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    ReplayGameState state;

    JsonArray v2;
    v2 << 2 << "use" << "initiator" << "#testCard:.::"
       << "sourceOwner" << "sourceSkill" << 3
       << "activationOwner" << "activationSkill" << 7;
    ok = expect(state.applyMessage(provenanceMessage(v2)), "V2 provenance accepted") && ok;
    QList<QVariantMap> records = state.getCardProvenance();
    ok = expect(records.size() == 1, "V2 record count") && ok;
    ok = expect(records.first().value("sourceOwner").toString() == "sourceOwner", "V2 source owner") && ok;
    ok = expect(records.first().value("activationOwner").toString() == "activationOwner", "V2 activation owner") && ok;

    JsonArray v1;
    v1 << 1 << "response" << "legacyInitiator" << "#legacy:.::"
       << "legacySource" << 1 << "legacyActivation" << 2;
    ok = expect(state.applyMessage(provenanceMessage(v1)), "V1 provenance accepted") && ok;
    records = state.getCardProvenance();
    ok = expect(records.size() == 2, "V1 record count") && ok;
    ok = expect(records.last().value("sourceOwner").toString() == "legacyInitiator", "V1 source fallback") && ok;
    ok = expect(records.last().value("activationOwner").toString() == "legacyInitiator", "V1 activation fallback") && ok;

    ok = expect(state.applyMessage(gameSeedMessage(Q_UINT64_C(4815162342))),
                "Game seed replay log accepted") && ok;
    ok = expect(state.getCardProvenance().size() == 2,
                "Game seed replay log preserved existing state") && ok;

    ok = expect(state.applyMessage(setupMessage(
                    QStringLiteral("server:03_1v2:3:1:standard+wind:RC"))),
                "Setup command accepted") && ok;
    ok = expect(state.getGlobalState().gameMode == QStringLiteral("03_1v2"),
                "Setup command captured game mode") && ok;
    ok = expect(!state.applyMessage(setupMessage(
                    QStringLiteral("server:03_1v2:3:1:standard:RC!"))),
                "Setup command rejected trailing garbage") && ok;

    JsonArray malformed;
    malformed << 2 << "use" << "initiator";
    ok = expect(!state.applyMessage(provenanceMessage(malformed)), "Malformed provenance rejected") && ok;
    return ok ? 0 : 1;
}
