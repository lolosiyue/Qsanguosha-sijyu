#ifndef ROOM_TEST_ACCESS_H
#define ROOM_TEST_ACCESS_H

#include "ai.h"
#include "request-coordinator.h"
#include "room.h"
#include "room-roster.h"
#include "roomthread.h"
#include "roomthread1v1.h"
#include "roomthread3v3.h"
#include "roomthreadxmode.h"
#include "serverplayer.h"
#include "skill.h"
#include "skill-instance-types.h"

#include "protocol/protocol-message.h"

struct RoomTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        room.addPlayerToRoster(player);
        return player;
    }

    static ServerPlayer *addPlayer(Room &room, const QString &objectName,
                                   const QString &state)
    {
        ServerPlayer *player = addPlayer(room, objectName);
        player->setState(state);
        player->drainAllLocks();
        player->releaseLock(ServerPlayer::SEMA_MUTEX);
        return player;
    }

    static ServerPlayer *addOrdinaryPlayer(Room &room, const QString &objectName,
                                           bool setPhaseNotActive = false)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        player->setAlive(true);
        player->setRemoved(false);
        if (setPhaseNotActive)
            player->setPhase(Player::NotActive);
        TrustAI *ai = new TrustAI(player);
        ai->setParent(player);
        player->setAI(ai);
        room.addPlayerToRoster(player);
        return player;
    }

    static ServerPlayer *addListedPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        room.m_roster->add(player);
        return player;
    }

    static void attachThread(Room &room)
    {
        room.thread = new RoomThread(&room);
    }

    static void attachThread(Room &room, const TriggerSkill *skill)
    {
        attachThread(room);
        if (skill)
            room.thread->addTriggerSkill(skill);
    }

    static QThread *canonicalThread(Room &room)
    {
        return room.thread;
    }

    static void notifySkillInstanceState(Room &room, ServerPlayer *owner,
                                         const SkillInstance &instance,
                                         const QString &operation,
                                         const QString &key,
                                         const QVariant &value)
    {
        room.notifySkillInstanceState(owner, instance, operation, key, value);
    }

    static SkillInstanceRef resolveRoot(Room &room, const SkillInstanceRef &ref)
    {
        return room.resolveSkillInstanceRootRef(ref);
    }

    static bool reserveUsage(Room &room, const ViewAsSkillV2 *skill,
                             const SkillContext &context)
    {
        return room.reserveActiveSkillUsage(skill, context);
    }

    static void releaseUsage(Room &room, const ViewAsSkillV2 *skill,
                             const SkillContext &context)
    {
        room.releaseActiveSkillUsage(skill, context);
    }

    static void commitUsage(Room &room, const ViewAsSkillV2 *skill,
                            const SkillContext &context)
    {
        room.commitActiveSkillUsage(skill, context);
    }

    static void dispatch(Room &room, ServerPlayer *player,
                         const QSanProtocol::ProtocolMessage &message,
                         const QString &rawMessage = QString())
    {
        room.m_requests->processClientPacket(player, message, rawMessage);
    }

    // Delivers the same signal the socket layer emits when a client drops.
    static void simulateDisconnect(Room &room, ServerPlayer *player)
    {
        QObject::connect(player, SIGNAL(disconnected()), &room,
                         SLOT(reportDisconnection()), Qt::UniqueConnection);
        emit player->disconnected();
    }

    static bool isPaused(const Room &room)
    {
        return room.game_paused;
    }

    static void process(Room &room)
    {
        room.processScheduledExtraTurns();
    }

    static void execute(Room &room, ServerPlayer *player, QList<Player::Phase> phases,
                        const QString &reason, const SkillInstanceRef &sourceRef)
    {
        room.executeExtraTurn(player, phases, reason, sourceRef);
    }

    static RoomRoster &roster(Room &room)
    {
        return *room.m_roster;
    }

    static void resetAlive(Room &room)
    {
        room.m_roster->resetAliveToPlayers();
    }
};

#endif
