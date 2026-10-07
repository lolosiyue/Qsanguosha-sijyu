#ifndef QSAN_MANAGED_REQUEST_CHECKS_H
#define QSAN_MANAGED_REQUEST_CHECKS_H

#include "game-timeline.h"
#include "room.h"
#include "serverplayer.h"
#include <QHash>
#include <QPair>

// The hosting fixture supplies a RoomTestAccess friend with protocol injection
// and read-only access to its live request coordinator. Check is a callback so
// this header can be reused by the room-managed running fixture without owning
// a separate Room/runtime setup.
template <typename Check>
void checkManagedRequestLifecycle(Room &room, ServerPlayer *player, Check check)
{
    const GameTimeline &timeline = RoomTestAccess::timeline(room);
    // Test-only observation, never exposed to gameplay code or snapshots.
    static QHash<QString, QPair<quint64, quint64>> lastTimedOutProbeByPlayer;
    const auto countClientDecisions = [&timeline]() {
        int count = 0;
        for (const auto &record : timeline.records())
            if (record.kind == QStringLiteral("accepted_decision")
                && record.data.value(QStringLiteral("source")).toString() == QStringLiteral("client"))
                ++count;
        return count;
    };

    const int initialClientDecisions = countClientDecisions();
    const quint64 firstMessage = RoomTestAccess::sendDirectionRequest(room, player);
    check(firstMessage != 0, "managed request has a wire message ID");
    const auto priorProbe = lastTimedOutProbeByPlayer.value(player->objectName());
    if (priorProbe.first != 0 && priorProbe.second != timeline.generation()) {
        check(RoomTestAccess::deliverDirectionReply(room, player, priorProbe.first,
                                                    QStringLiteral("clockwise")),
              "previous-generation reply envelope reaches rejection path");
        check(RoomTestAccess::waitingForReply(player)
                  && !RoomTestAccess::clientResponseReady(player),
              "reply from the prior real generation cannot satisfy this request");
    }
    const auto firstToken = RoomTestAccess::managedToken(room, player);
    check(firstToken.serial != 0 && firstToken.generation == timeline.generation(),
          "managed request has a current timeline token");
    check(RoomTestAccess::deliverDirectionReply(room, player, firstMessage,
                                                QStringLiteral("clockwise")),
          "normal reply encodes and reaches the coordinator");
    check(RoomTestAccess::consumeDirectionResult(room, player),
          "normal reply is consumed successfully");
    check(!timeline.isPending(firstToken), "normal reply consumes its request token");
    check(countClientDecisions() == initialClientDecisions + 1,
          "normal reply creates exactly one accepted decision");
    check(!RoomTestAccess::hasManagedPending(room), "normal reply leaves no pending coordinator entry");

    const quint64 secondMessage = RoomTestAccess::sendDirectionRequest(room, player);
    check(secondMessage != 0 && secondMessage != firstMessage,
          "successive managed request gets a distinct wire ID");
    check(RoomTestAccess::deliverDirectionReply(room, player, firstMessage,
                                                QStringLiteral("counterclockwise")),
          "stale reply envelope can be delivered for rejection testing");
    check(RoomTestAccess::waitingForReply(player)
              && !RoomTestAccess::clientResponseReady(player),
          "stale prior reply does not satisfy the new request");
    check(RoomTestAccess::deliverDirectionReply(room, player, secondMessage,
                                                QStringLiteral("counterclockwise")),
          "current reply encodes and reaches the coordinator");
    check(RoomTestAccess::consumeDirectionResult(room, player),
          "current reply remains consumable after stale input");
    check(countClientDecisions() == initialClientDecisions + 2,
          "stale reply adds no accepted decision");

    const quint64 timeoutMessage = RoomTestAccess::sendDirectionRequest(room, player);
    check(timeoutMessage != 0, "timeout probe is sent");
    const auto timeoutToken = RoomTestAccess::managedToken(room, player);
    const quint64 nextGeneration = timeoutToken.generation + 1;
    RoomTestAccess::commitRequestGeneration(room, nextGeneration);
    check(RoomTestAccess::deliverDirectionReply(room, player, timeoutMessage,
                                                QStringLiteral("clockwise")),
          "old-generation reply envelope can be delivered for rejection testing");
    check(RoomTestAccess::waitingForReply(player)
              && !RoomTestAccess::clientResponseReady(player),
          "generation fence rejects a reply for the old token");
    check(!RoomTestAccess::consumeDirectionResult(room, player),
          "rejected old-generation reply resolves only through timeout");
    check(!timeline.isPending(timeoutToken), "timeout consumes the old pending token");
    check(!RoomTestAccess::hasManagedPending(room), "timeout leaves no coordinator entry");
    bool recordedTimeout = false;
    for (const auto &record : timeline.records()) {
        if (record.kind == QStringLiteral("accepted_decision")
            && record.data.value(QStringLiteral("request")).toString()
                == QString::number(timeoutToken.serial)) {
            recordedTimeout = record.data.value(QStringLiteral("source")).toString()
                == QStringLiteral("timeout");
        }
    }
    check(recordedTimeout, "old-generation reply is recorded as timeout, not client choice");
    const int afterTimeoutClientDecisions = countClientDecisions();
    check(RoomTestAccess::deliverDirectionReply(room, player, timeoutMessage,
                                                QStringLiteral("clockwise")),
          "late timed-out reply envelope can be delivered for rejection testing");
    check(!RoomTestAccess::waitingForReply(player)
              && countClientDecisions() == afterTimeoutClientDecisions,
          "late timed-out reply cannot mutate the accepted-decision timeline");
    RoomTestAccess::commitRequestGeneration(room, timeline.generation());
    lastTimedOutProbeByPlayer.insert(player->objectName(),
                                     qMakePair(timeoutMessage, timeoutToken.generation));
}

#endif
