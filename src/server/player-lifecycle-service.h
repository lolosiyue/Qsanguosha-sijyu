#ifndef PLAYER_LIFECYCLE_SERVICE_H
#define PLAYER_LIFECYCLE_SERVICE_H

#include "structs.h"

#include <QList>
#include <QString>
#include <QtGlobal>

class CardMovementService;
class ClientSocket;
class EventDispatcher;
class Room;
class RoomNotifier;
class RoomRoster;
class SkillRuntimeCoordinator;

class PlayerLifecycleService
{
public:
    PlayerLifecycleService(Room &room, RoomRoster &roster,
                           SkillRuntimeCoordinator &skillRuntime,
                           CardMovementService &cardMovement,
                           RoomNotifier &notifier,
                           EventDispatcher &eventDispatcher);

    ServerPlayer *addSocket(ClientSocket *socket);
    ServerPlayer *addAIPlayer();
    void signup(ServerPlayer *player, const QString &screenName,
                const QString &avatar, bool isRobot);
    void reconnect(ServerPlayer *player, ClientSocket *socket);
    void marshal(ServerPlayer *player, bool managedTimelineRestore = false);

    void killPlayer(ServerPlayer *victim, DamageStruct *reason, HpLostStruct *hpLost);
    bool hasActiveDeathCursor() const { return m_deathCursorDepth != 0; }
    void finishCancelledDeaths(quint64 cascadeId);
    void clearCancelledDeaths() { m_pendingDeaths.clear(); }
    void adoptCancelledDeaths(quint64 from, quint64 owner);
    void revivePlayer(ServerPlayer *player, bool sendLog, bool throwMark, bool visibleOnly);
    void restPlayer(ServerPlayer *player, const QString &reason, bool discardCards);
    void directRestPlayer(ServerPlayer *player, const QString &reason, bool discardCards);
    void unrestPlayer(ServerPlayer *player, bool restoreFullHp, bool restoreOriginalSkills);
    bool isRest(ServerPlayer *player) const;
    QList<ServerPlayer *> getRestPlayers() const;

    void changeHero(ServerPlayer *player, const QString &newGeneral, bool fullState,
                    bool invokeStart, bool isSecondaryHero, bool sendLog, int startHp);
    void changePlayerGeneral(ServerPlayer *player, const QString &newGeneral);
    void changePlayerGeneral2(ServerPlayer *player, const QString &newGeneral);

    void requestSummonBetween(ServerPlayer *before, ServerPlayer *after,
                              const QString &generalName);
    bool hasPendingSummons() const;
    void processPendingSummons();
    void finishDeferredCascade(quint64 cascadeId, bool cancelled, quint64 parentCascadeId = 0);
    ServerPlayer *insertPlayerMidGame(ServerPlayer *before, ServerPlayer *after,
                                      const QString &generalName);

private:
    friend struct PlayerLifecycleServiceTestAccess;

    bool replaceDragonPhoenixGeneral(ServerPlayer *player, const QString &newGeneral);

    struct SummonRequest
    {
        ServerPlayer *before;
        ServerPlayer *after;
        QString generalName;
        quint64 cascadeId = 0;
    };

    Room &m_room;
    RoomRoster &m_roster;
    SkillRuntimeCoordinator &m_skillRuntime;
    CardMovementService &m_cardMovement;
    RoomNotifier &m_notifier;
    EventDispatcher &m_eventDispatcher;
    QList<SummonRequest> m_pendingSummons;
    QList<ServerPlayer *> m_dynamicPlayers;
    struct DeathCursor;
    void continueDeath(const std::shared_ptr<DeathCursor> &cursor, bool canonicalOnly = false);
    QList<std::shared_ptr<DeathCursor>> m_pendingDeaths;
    unsigned m_deathCursorDepth = 0;
    quint64 m_nextStateSyncId = 1;
};

#endif
