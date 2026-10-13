#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round29-planning.h"
#include "ai-strategy-flag-lifecycle.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QHash>
#include <QObject>

namespace {
struct Observation { qint64 epoch = 0; bool value = false; };
class ValuationFlags final : public AIRoomLedger {
public:
    explicit ValuationFlags(Room &room) : AIRoomLedger(room) {}
    QHash<const ServerPlayer *, Observation> owners;
};
ValuationFlags *ledger(Room &room)
{
    for (QObject *child : room.children())
        if (auto *found = dynamic_cast<ValuationFlags *>(child)) return found;
    return new ValuationFlags(room);
}
bool member(Room &room, const ServerPlayer *owner)
{
    return owner && owner->getRoom() == &room
        && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(owner));
}
qint64 currentEpoch(Room &room, const ServerPlayer *owner)
{
    AIWorldView fresh;
    fresh.self.objectName = owner->objectName();
    projectAIStrategyFlagLifecycle(fresh, room);
    return qint64(fresh.strategyFacts.value("strategy_flag_clear_epochs").toObject()
        .value(owner->objectName()).toDouble());
}
bool validate(Room &room, const ServerPlayer *owner, const AIRequest &request, QString *error)
{
    if (!member(room, owner) || request.viewerObjectName != owner->objectName()
        || !room.roomRuntime() || !aiRound44RangeRequestCurrent(room,request,owner)) {
        if (error) *error = QStringLiteral("valuation flag owner/request revision mismatch");
        return false;
    }
    return true;
}
Observation &observation(Room &room, const ServerPlayer *owner)
{
    auto *state = ledger(room);
    const qint64 epoch = currentEpoch(room, owner);
    auto found = state->owners.find(owner);
    if (found == state->owners.end()) {
        // Native-only pointer key, removed on player destruction, never exposed.
        QObject::connect(owner, &QObject::destroyed, state, [state, owner]() {
            state->owners.remove(owner);
        }, Qt::DirectConnection);
        return state->owners.insert(owner, Observation{epoch,
            owner->hasFlag(QStringLiteral("stack_overflow_UseValue"))}).value();
    }
    if (found->epoch != epoch) {
        found->epoch = epoch;
        found->value = false;
    }
    return found.value();
}
}

bool readAIRound29UseValueFlag(Room &room, const ServerPlayer *owner,
    const AIRequest &request, bool &value, QString *error)
{
    if (!validate(room, owner, request, error)) return false;
    value = observation(room, owner).value;
    return true;
}
bool writeAIRound29UseValueFlag(Room &room, const ServerPlayer *owner,
    const AIRequest &request, bool value, QString *error)
{
    if (!validate(room, owner, request, error)) return false;
    observation(room, owner).value = value;
    // Deliberately no live Player flags, revision, effect queue, Lua state or
    // event transaction. Source errors and VM reset preserve the last write.
    return true;
}
void synchronizeAIRound29UseValueFlag(Room &room, const ServerPlayer *owner,
    const QString &command)
{
    if (!member(room, owner)) return;
    if (command == QLatin1String("stack_overflow_UseValue"))
        observation(room, owner).value = true;
    else if (command == QLatin1String("-stack_overflow_UseValue"))
        observation(room, owner).value = false;
}
