#ifndef ROOM_ROSTER_H
#define ROOM_ROSTER_H

#include <QList>
#include <QString>

class ServerPlayer;

class RoomRoster
{
public:
    void add(ServerPlayer *player);
    void remove(ServerPlayer *player);
    void insertAfter(ServerPlayer *before, ServerPlayer *player, bool addToAlive);
    void replacePlayers(const QList<ServerPlayer *> &players);

    QList<ServerPlayer *> players() const;
    QList<ServerPlayer *> alivePlayers() const;
    int aliveCount() const;

    QList<ServerPlayer *> orderedFrom(ServerPlayer *current, bool includeDead) const;
    QList<ServerPlayer *> otherPlayers(ServerPlayer *current, ServerPlayer *except, bool includeDead) const;

    ServerPlayer *findByGeneral(const QString &generalName, bool includeDead) const;
    ServerPlayer *findByObjectName(const QString &objectName, ServerPlayer *current, bool includeDead) const;
    QList<ServerPlayer *> findBySkill(const QString &skillName, ServerPlayer *current) const;
    ServerPlayer *findFirstBySkill(const QString &skillName, ServerPlayer *current, bool includeLose) const;

    QList<ServerPlayer *> pathBetween(ServerPlayer *from, ServerPlayer *to,
                                      bool includeFrom, bool includeTo) const;
    QList<ServerPlayer *> clockwisePath(ServerPlayer *from, ServerPlayer *to,
                                        bool includeFrom, bool includeTo) const;
    QList<ServerPlayer *> counterclockwisePath(ServerPlayer *from, ServerPlayer *to,
                                               bool includeFrom, bool includeTo) const;

    QList<ServerPlayer *> alivePlayersAfter(ServerPlayer *player) const;
    void removeAlive(ServerPlayer *player);
    void resetAliveToPlayers();
    void rebuildAlive();
    void reseatAlive();

    void swapSeats(ServerPlayer *first, ServerPlayer *second);
    void adjustSeats(bool keepOriginalStart);
    void reversePlayOrder();
    bool isPlayOrderReversed() const;

private:
    void relinkPlayers();

    QList<ServerPlayer *> m_players;
    QList<ServerPlayer *> m_alivePlayers;
    bool m_playOrderReversed = false;
};

#endif
