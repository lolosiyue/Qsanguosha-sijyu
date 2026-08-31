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
    void marshal(ServerPlayer *player);

    void killPlayer(ServerPlayer *victim, DamageStruct *reason, HpLostStruct *hpLost);
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
    ServerPlayer *insertPlayerMidGame(ServerPlayer *before, ServerPlayer *after,
                                      const QString &generalName);

private:
    friend struct PlayerLifecycleServiceTestAccess;

    struct SummonRequest
    {
        ServerPlayer *before;
        ServerPlayer *after;
        QString generalName;
    };

    Room &m_room;
    RoomRoster &m_roster;
    SkillRuntimeCoordinator &m_skillRuntime;
    CardMovementService &m_cardMovement;
    RoomNotifier &m_notifier;
    EventDispatcher &m_eventDispatcher;
    QList<SummonRequest> m_pendingSummons;
    QList<ServerPlayer *> m_dynamicPlayers;
    quint64 m_nextStateSyncId = 1;
};

#endif
