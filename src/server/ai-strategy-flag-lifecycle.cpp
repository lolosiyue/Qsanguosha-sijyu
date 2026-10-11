#include "ai-strategy-flag-lifecycle.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>

namespace {
// Room ownership avoids process-global state, address reuse and viewer lifetime
// dependencies. No Lua is entered by the mutation hook.
class FlagClearLedger final : public QObject {
public:
    explicit FlagClearLedger(Room &room) : QObject(&room) {}
    qint64 sequence = 0;
    QHash<QString, qint64> latest;
    QHash<QString, qint64> playLatest;
    QHash<QString, qint64> turnLatest;
    QHash<QString, qint64> allMarkLatest;
    QHash<QString, qint64> lolWEmptyLatest;
};
FlagClearLedger *ledger(Room &room, bool create)
{
    for (QObject *child : room.children())
        if (auto *value = dynamic_cast<FlagClearLedger *>(child)) return value;
    return create ? new FlagClearLedger(room) : nullptr;
}
bool belongsToRoom(const Room &room, const ServerPlayer *player)
{
    if (!player || player->getRoom() != &room) return false;
    return room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(player));
}
}

void recordAIStrategyFlagsCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    // QJson's number and both Lua builds represent these integers exactly.
    // Exhaustion is impossible in a normal Room; never silently wrap an epoch.
    constexpr qint64 maxExactInteger = 9007199254740991LL;
    if (value->sequence == maxExactInteger)
        qFatal("isolated strategy flag-clear epoch exhausted");
    value->latest.insert(player->objectName(), ++value->sequence);
    // Player::clearFlags() emits no property change when the real set is empty.
    // That must still invalidate a decision made before the shadow-state clear.
    if (room.roomRuntime())
        room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyPlayMarksCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated play-mark epoch exhausted");
    value->playLatest.insert(player->objectName(), ++value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyTurnMarksCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated turn-mark epoch exhausted");
    value->turnLatest.insert(player->objectName(), ++value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyAllMarksCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated all-mark epoch exhausted");
    value->allMarkLatest.insert(player->objectName(), ++value->sequence);
    value->playLatest.insert(player->objectName(), value->sequence);
    value->turnLatest.insert(player->objectName(), value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyLolWEmptyCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated named-flag epoch exhausted");
    value->lolWEmptyLatest.insert(player->objectName(), ++value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void projectAIStrategyFlagLifecycle(AIWorldView &world, Room &room)
{
    QJsonObject epochs;
    const auto *value = ledger(room, false);
    // Only the answering viewer's clear history is disclosed, never another
    // player's private flag value or clear history.
    world.strategyFacts.insert("lol2015", QJsonObject{{"w_empty_clear_epoch",
        value ? value->lolWEmptyLatest.value(world.self.objectName, 0) : qint64(0)}});
    for (const auto *player : room.getAllPlayers(true))
        epochs.insert(player->objectName(), value ? value->latest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_flag_clear_epochs"), epochs);
    QJsonObject playEpochs;
    for (const auto *player : room.getAllPlayers(true))
        playEpochs.insert(player->objectName(), value ? value->playLatest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_play_clear_epochs"), playEpochs);
    QJsonObject turnEpochs;
    for (const auto *player : room.getAllPlayers(true))
        turnEpochs.insert(player->objectName(), value ? value->turnLatest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_turn_clear_epochs"), turnEpochs);
    QJsonObject allEpochs;
    for (const auto *player : room.getAllPlayers(true))
        allEpochs.insert(player->objectName(), value ? value->allMarkLatest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_all_mark_clear_epochs"), allEpochs);
}
