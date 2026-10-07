#include "client/core/client-game-state-reducer.h"
#include "client/core/client-game-state.h"
#include "core/protocol.h"
#include "core/protocol/session/session-payloads.h"

#include <QVariantList>
#include <iostream>

using namespace QSanProtocol;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char *message)
{
    ++checks;
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

bool reduce(ClientGameState *state, int command, const QVariant &payload)
{
    const ClientStateReduction result = ClientGameStateReducer::applyNotification(
        state, command, payload);
    if (!result.success)
        std::cerr << "reducer rejected notification: " << result.detail.toStdString() << '\n';
    return result.success;
}

QVariantMap gameStart(const QVariantList &cardIds)
{
    return {{QStringLiteral("schema_version"), 1},
            {QStringLiteral("card_ids"), cardIds}};
}

} // namespace

int main()
{
    StateSyncPayload oldPayload;
    oldPayload.syncId = QStringLiteral("1");
    oldPayload.phase = QStringLiteral("begin");
    QVariantMap oldWire = oldPayload.toVariant();
    StateSyncPayload parsedOld;
    QString error;
    check(StateSyncPayload::parse(oldWire, &parsedOld, &error),
          "legacy state sync payload remains valid");
    check(!parsedOld.managedTimelineRestore && !parsedOld.hasRound,
          "legacy state sync defaults to ordinary reconnect");

    StateSyncPayload badRound = oldPayload;
    badRound.phase = QStringLiteral("end");
    badRound.hasRound = true;
    badRound.round = -1;
    StateSyncPayload ignored;
    check(!StateSyncPayload::parse(badRound.toVariant(), &ignored, &error),
          "negative absolute round is rejected");
    badRound.round = 4;
    badRound.phase = QStringLiteral("begin");
    check(!StateSyncPayload::parse(badRound.toVariant(), &ignored, &error),
          "absolute round is only valid at sync end");

    const QVariantList activePool{2, 7, 18};
    ClientGameState managed;
    StateSyncPayload begin;
    begin.syncId = QStringLiteral("2");
    begin.phase = QStringLiteral("begin");
    begin.managedTimelineRestore = true;
    StateSyncPayload parsedBegin;
    check(StateSyncPayload::parse(begin.toVariant(), &parsedBegin, &error),
          "managed restore begin payload parses");
    check(reduce(&managed, S_COMMAND_STATE_SYNC, parsedBegin.toVariant()),
          "managed restore begin reaches reducer");
    check(managed.connectionValue(QStringLiteral("managed_timeline_restore")).toBool(),
          "begin brackets managed restore state");
    check(reduce(&managed, S_COMMAND_GAME_START, gameStart(activePool)),
          "managed restore game-start reaches reducer");
    check(managed.gameValue(QStringLiteral("available_cards")).toList() == activePool,
          "managed game-start stores active card universe as known pool");
    check(managed.gameValue(QStringLiteral("draw_pile")).toList().isEmpty(),
          "managed game-start does not expose or invent draw order");
    check(managed.gameValue(QStringLiteral("draw_pile_count")).toInt() == activePool.size(),
          "managed game-start keeps the pool count");
    check(reduce(&managed, S_COMMAND_UPDATE_PILE,
                 QVariantMap{{QStringLiteral("schema_version"), 1},
                             {QStringLiteral("count"), 5}}),
          "managed restore pile-count reaches reducer");
    check(managed.gameValue(QStringLiteral("draw_pile_count")).toInt() == 5,
          "managed restore uses the actual restored pile count");
    check(managed.gameValue(QStringLiteral("draw_pile")).toList().isEmpty(),
          "managed restore pile-count never reveals draw order");

    StateSyncPayload end;
    end.syncId = begin.syncId;
    end.phase = QStringLiteral("end");
    end.managedTimelineRestore = true;
    end.hasRound = true;
    end.round = 9;
    StateSyncPayload parsedEnd;
    check(StateSyncPayload::parse(end.toVariant(), &parsedEnd, &error),
          "managed restore end carries absolute round");
    check(reduce(&managed, S_COMMAND_STATE_SYNC, parsedEnd.toVariant()),
          "managed restore end reaches reducer");
    check(managed.gameValue(QStringLiteral("round")).toInt() == 9,
          "reducer applies absolute restored round");
    check(!managed.connectionValue(QStringLiteral("managed_timeline_restore")).toBool(),
          "end clears managed restore marker");

    ClientGameState ordinary;
    check(reduce(&ordinary, S_COMMAND_GAME_START, gameStart(activePool)),
          "ordinary game-start reaches reducer");
    check(ordinary.gameValue(QStringLiteral("draw_pile")).toList() == activePool,
          "ordinary game-start behavior remains unchanged");
    check(!ordinary.gameValue(QStringLiteral("available_cards")).isValid(),
          "ordinary game-start does not gain managed-only pool semantics");

    if (failures != 0)
        return 1;
    std::cout << "client-state-sync: " << checks << " checks passed\n";
    return 0;
}
