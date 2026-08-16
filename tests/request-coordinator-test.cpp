#include "engine-bootstrap.h"
#include "request-coordinator.h"
#include "room.h"
#include "serverplayer.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QObject>

using namespace QSanProtocol;

struct RoomTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &objectName,
                                   const QString &state = QStringLiteral("online"))
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        player->setState(state);
        player->drainAllLocks();
        player->releaseLock(ServerPlayer::SEMA_MUTEX);
        room.addPlayerToRoster(player);
        return player;
    }

    static void dispatch(Room &room, ServerPlayer *player, const Packet &packet)
    {
        room.m_requests->processClientPacket(player, packet, packet.toString());
    }

    static bool isPaused(const Room &room)
    {
        return room.game_paused;
    }
};

struct RequestRecord
{
    CommandType command;
    unsigned int serial;
};

class RequestRecorder
{
public:
    void watch(ServerPlayer *player)
    {
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this](const QString &message) {
            Packet packet;
            if (!packet.parse(message.toUtf8())) {
                parseFailed = true;
                return;
            }
            if (packet.getPacketType() == S_TYPE_REQUEST)
                records << RequestRecord{packet.getCommandType(), packet.globalSerial};
        });
    }

    void clear()
    {
        records.clear();
        parseFailed = false;
    }

    const RequestRecord *last(CommandType command) const
    {
        for (int i = records.length() - 1; i >= 0; --i) {
            if (records.at(i).command == command)
                return &records.at(i);
        }
        return nullptr;
    }

    QList<RequestRecord> records;
    bool parseFailed = false;
};

class ScopedOperationLimit
{
public:
    ScopedOperationLimit()
        : m_previous(Config.OperationNoLimit)
    {
        Config.OperationNoLimit = false;
    }

    ~ScopedOperationLimit()
    {
        Config.OperationNoLimit = m_previous;
    }

private:
    bool m_previous;
};

static bool acceptedReplyCompletesRequest(Room &room, ServerPlayer *player,
                                          RequestRecorder &recorder)
{
    recorder.clear();
    if (!room.doRequest(player, S_COMMAND_PLAY_CARD, QStringLiteral("request"), 0, false))
        return false;

    const RequestRecord *request = recorder.last(S_COMMAND_PLAY_CARD);
    if (recorder.parseFailed || request == nullptr)
        return false;

    Packet reply(S_SRC_CLIENT | S_TYPE_REPLY | S_DEST_ROOM, S_COMMAND_RESPONSE_CARD);
    reply.localSerial = request->serial;
    reply.setMessageBody(QStringLiteral("accepted"));
    RoomTestAccess::dispatch(room, player, reply);

    return room.getResult(player, 0)
        && player->getClientReply() == QStringLiteral("accepted")
        && !player->m_isWaitingReply
        && player->m_expectedReplyCommand == S_COMMAND_UNKNOWN
        && player->m_expectedReplySerial == -1;
}

static bool mismatchedReplyTimesOut(Room &room, ServerPlayer *player,
                                    RequestRecorder &recorder)
{
    recorder.clear();
    if (!room.doRequest(player, S_COMMAND_PLAY_CARD, QStringLiteral("request"), 0, false))
        return false;

    const RequestRecord *request = recorder.last(S_COMMAND_PLAY_CARD);
    if (recorder.parseFailed || request == nullptr)
        return false;

    Packet reply(S_SRC_CLIENT | S_TYPE_REPLY | S_DEST_ROOM, S_COMMAND_PLAY_CARD);
    reply.localSerial = request->serial;
    reply.setMessageBody(QStringLiteral("wrong-command"));
    RoomTestAccess::dispatch(room, player, reply);

    return !room.getResult(player, 0)
        && !player->m_isWaitingReply
        && player->m_expectedReplyCommand == S_COMMAND_UNKNOWN
        && player->m_expectedReplySerial == -1;
}

static bool callbackDispatchesThroughCoordinator(Room &room, ServerPlayer *owner)
{
    owner->setOwner(true);

    Packet pause(S_SRC_CLIENT | S_TYPE_NOTIFICATION | S_DEST_ROOM, S_COMMAND_PAUSE);
    pause.setMessageBody(true);
    RoomTestAccess::dispatch(room, owner, pause);
    if (!RoomTestAccess::isPaused(room))
        return false;

    Packet resume(S_SRC_CLIENT | S_TYPE_NOTIFICATION | S_DEST_ROOM, S_COMMAND_PAUSE);
    resume.setMessageBody(false);
    RoomTestAccess::dispatch(room, owner, resume);
    return !RoomTestAccess::isPaused(room);
}

static bool raceTimeoutClearsPendingState(Room &room, ServerPlayer *player)
{
    player->m_commandArgs = QStringLiteral("race");
    ServerPlayer *winner = room.doBroadcastRaceRequest(
        QList<ServerPlayer *>() << player, S_COMMAND_PLAY_CARD, 0);
    return winner == nullptr
        && !player->m_isWaitingReply
        && player->m_expectedReplyCommand == S_COMMAND_UNKNOWN
        && player->m_expectedReplySerial == -1;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    ScopedOperationLimit operationLimit;
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *owner = RoomTestAccess::addPlayer(room, QStringLiteral("request-owner"));
    RoomTestAccess::addPlayer(room, QStringLiteral("request-robot"), QStringLiteral("robot"));

    RequestRecorder recorder;
    recorder.watch(owner);

    if (!acceptedReplyCompletesRequest(room, owner, recorder))
        return 2;
    if (!mismatchedReplyTimesOut(room, owner, recorder))
        return 3;
    if (!callbackDispatchesThroughCoordinator(room, owner))
        return 4;
    if (!raceTimeoutClearsPendingState(room, owner))
        return 5;

    qInfo() << "request coordinator behavior passed";
    return 0;
}
