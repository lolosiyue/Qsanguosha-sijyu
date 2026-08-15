#ifndef REQUEST_COORDINATOR_H
#define REQUEST_COORDINATOR_H

#include <QHash>
#include <QList>
#include <QMutex>
#include <QSemaphore>
#include <QString>
#include <QVariant>

#include "protocol.h"

#include <ctime>

class Room;
class ServerPlayer;

class RequestCoordinator
{
public:
    using ResponseVerifyFunction = bool (Room::*)(ServerPlayer *, const QVariant &, void *);

    explicit RequestCoordinator(Room &room);

    bool request(ServerPlayer *player, QSanProtocol::CommandType command,
                 const QVariant &arg, time_t timeOut, bool wait);
    bool broadcastRequest(QList<ServerPlayer *> players,
                          QSanProtocol::CommandType command, time_t timeOut);
    ServerPlayer *raceRequest(QList<ServerPlayer *> players,
                              QSanProtocol::CommandType command, time_t timeOut,
                              ResponseVerifyFunction validateFunc = nullptr,
                              void *funcArg = nullptr);
    bool getResult(ServerPlayer *player, time_t timeOut);

private:
    friend class Room;
    friend struct RoomTestAccess;

    using Callback = void (Room::*)(ServerPlayer *, const QVariant &);

    void initializeCallbacks();
    ServerPlayer *requestTarget(ServerPlayer *player) const;
    void notifyArrangeSeats(ServerPlayer *player);
    void clearDualControlRequest(ServerPlayer *player, bool restoreContext = true);
    ServerPlayer *getRaceResult(QList<ServerPlayer *> players,
                                QSanProtocol::CommandType command, time_t timeOut,
                                ResponseVerifyFunction validateFunc = nullptr,
                                void *funcArg = nullptr);
    bool verifyRaceReply(ServerPlayer *player, const QVariant &reply, void *funcArg);
    void processClientPacket(ServerPlayer *player, const QSanProtocol::Packet &packet,
                             const QString &rawRequest);
    void processResponse(ServerPlayer *player, const QSanProtocol::Packet *packet);
    void unblockWaits();

    Room &m_room;
    QSemaphore m_raceRequestSemaphore;
    QSemaphore m_roomSemaphore;
    QHash<QSanProtocol::CommandType, Callback> m_callbacks;
    QHash<QSanProtocol::CommandType, QSanProtocol::CommandType> m_requestResponsePairs;
    QHash<QString, QString> m_dualControlReplyOwners;
    QHash<QString, QString> m_dualControlRequestTargets;
    bool m_raceStarted;
    ServerPlayer *m_raceWinner;
    mutable QMutex m_mutex;
};

#endif
