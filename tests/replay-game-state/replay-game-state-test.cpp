#include "replay-game-state.h"
#include "protocol.h"
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

QString provenanceCommand(const JsonArray &body)
{
    Packet packet(S_TYPE_NOTIFICATION | S_DEST_CLIENT, S_COMMAND_CARD_PROVENANCE);
    packet.setMessageBody(body);
    return packet.toString();
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
    ok = expect(state.applyCommand(provenanceCommand(v2)), "V2 provenance accepted") && ok;
    QList<QVariantMap> records = state.getCardProvenance();
    ok = expect(records.size() == 1, "V2 record count") && ok;
    ok = expect(records.first().value("sourceOwner").toString() == "sourceOwner", "V2 source owner") && ok;
    ok = expect(records.first().value("activationOwner").toString() == "activationOwner", "V2 activation owner") && ok;

    JsonArray v1;
    v1 << 1 << "response" << "legacyInitiator" << "#legacy:.::"
       << "legacySource" << 1 << "legacyActivation" << 2;
    ok = expect(state.applyCommand(provenanceCommand(v1)), "V1 provenance accepted") && ok;
    records = state.getCardProvenance();
    ok = expect(records.size() == 2, "V1 record count") && ok;
    ok = expect(records.last().value("sourceOwner").toString() == "legacyInitiator", "V1 source fallback") && ok;
    ok = expect(records.last().value("activationOwner").toString() == "legacyInitiator", "V1 activation fallback") && ok;

    JsonArray malformed;
    malformed << 2 << "use" << "initiator";
    ok = expect(!state.applyCommand(provenanceCommand(malformed)), "Malformed provenance rejected") && ok;
    return ok ? 0 : 1;
}
