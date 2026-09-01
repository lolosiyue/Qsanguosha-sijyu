#include "engine-bootstrap.h"
#include "request-coordinator.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/session-payloads.h"
#include "room-test-access.h"
#include "room.h"
#include "serverplayer.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QObject>

using namespace QSanProtocol;

namespace {

struct RequestRecord
{
    CommandType command;
    quint64 messageId;
};

class RequestRecorder
{
public:
    void watch(ServerPlayer *player)
    {
        QObject::connect(player, &ServerPlayer::message_ready, player,
                         [this](const QByteArray &message) {
            ProtocolMessage packet;
            if (!ProtocolCodecRouter().decode(message, &packet).success) {
                parseFailed = true;
                return;
            }
            if (packet.type == ProtocolMessageType::Request)
                records << RequestRecord{
                    static_cast<CommandType>(packet.command),
                    packet.messageId};
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

static bool makeReply(CommandType command, quint64 replyTo, const QVariant &domainValue,
                      ProtocolMessage *reply)
{
    ProtocolMessage logical;
    logical.type = ProtocolMessageType::Reply;
    logical.source = ProtocolEndpoint::Client;
    logical.destination = ProtocolEndpoint::Room;
    logical.command = static_cast<int>(command);
    logical.replyTo = replyTo;
    logical.hasPayload = domainValue.isValid();
    logical.payload = domainValue;
    return ProtocolGameplayPayloadRegistry::encodeForWire(
        logical, reply, nullptr);
}

static bool acceptedReplyCompletesRequest(Room &room, ServerPlayer *player,
                                          RequestRecorder &recorder)
{
    recorder.clear();
    if (!room.doRequest(player, S_COMMAND_PLAY_CARD, QStringLiteral("request"), 0, false))
        return false;

    const RequestRecord *request = recorder.last(S_COMMAND_PLAY_CARD);
    if (recorder.parseFailed || request == nullptr)
        return false;

    // The domain form of a response-card reply is
    // [card_text, targets, activation_skill_name, activation_skill_instance_id].
    const QVariant acceptedReply = QVariantList{QStringLiteral("accepted"),
                                                QVariantList(), QString(), 0};
    ProtocolMessage reply;
    if (!makeReply(S_COMMAND_RESPONSE_CARD, request->messageId, acceptedReply, &reply))
        return false;
    RoomTestAccess::dispatch(room, player, reply);

    return room.getResult(player, 0)
        && player->getClientReply() == acceptedReply
        && !player->m_isWaitingReply
        && player->m_expectedReplyCommand == S_COMMAND_UNKNOWN;
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

    ProtocolMessage reply;
    if (!makeReply(S_COMMAND_MULTIPLE_CHOICE, request->messageId,
                   QStringLiteral("mismatch"), &reply))
        return false;
    RoomTestAccess::dispatch(room, player, reply);

    return !room.getResult(player, 0)
        && !player->m_isWaitingReply
        && player->m_expectedReplyCommand == S_COMMAND_UNKNOWN;
}

static bool requestCommandsAreStrict(Room &room, ServerPlayer *player,
                                     RequestRecorder &recorder)
{
    struct AliasCase
    {
        CommandType requestCommand;
        CommandType replyCommand;
        QVariant payload;
    };
    // Requests carry typed payloads since the Protocol V2 cutover; an empty
    // QVariant only encodes for the commands whose request has no payload.
    auto requestPayload = [](CommandType command) -> QVariant {
        if (command == S_COMMAND_MULTIPLE_CHOICE) {
            return QVariantList{QStringLiteral("tuxi"), QStringLiteral("left+right"),
                                QString(), QString()};
        }
        if (command == S_COMMAND_INVOKE_SKILL)
            return QVariantList{QStringLiteral("tuxi"), QString()};
        return QVariant();
    };
    const QList<AliasCase> acceptedCases{
        {S_COMMAND_CHOOSE_DIRECTION, S_COMMAND_CHOOSE_DIRECTION, QStringLiteral("cw")},
        {S_COMMAND_LUCK_CARD, S_COMMAND_LUCK_CARD, true}
    };
    for (const auto &entry : acceptedCases) {
        recorder.clear();
        if (!room.doRequest(player, entry.requestCommand, requestPayload(entry.requestCommand), 0, false))
            return false;
        const RequestRecord *request = recorder.last(entry.requestCommand);
        if (recorder.parseFailed || request == nullptr)
            return false;

        ProtocolMessage reply;
        if (!makeReply(entry.replyCommand, request->messageId, entry.payload, &reply))
            return false;
        RoomTestAccess::dispatch(room, player, reply);
        if (!room.getResult(player, 0) || player->getClientReply() != entry.payload)
            return false;
    }

    const QList<AliasCase> rejectedAliases{
        {S_COMMAND_CHOOSE_DIRECTION, S_COMMAND_MULTIPLE_CHOICE, QStringLiteral("cw")},
        {S_COMMAND_LUCK_CARD, S_COMMAND_INVOKE_SKILL, true},
        {S_COMMAND_MULTIPLE_CHOICE, S_COMMAND_CHOOSE_DIRECTION, QStringLiteral("cw")},
        {S_COMMAND_INVOKE_SKILL, S_COMMAND_LUCK_CARD, true}
    };
    for (const auto &entry : rejectedAliases) {
        recorder.clear();
        if (!room.doRequest(player, entry.requestCommand, requestPayload(entry.requestCommand), 0, false))
            return false;
        const RequestRecord *request = recorder.last(entry.requestCommand);
        if (recorder.parseFailed || request == nullptr)
            return false;

        ProtocolMessage reply;
        if (!makeReply(entry.replyCommand, request->messageId, entry.payload, &reply))
            return false;
        RoomTestAccess::dispatch(room, player, reply);
        if (room.getResult(player, 0))
            return false;
    }
    return true;
}

static bool callbackDispatchesThroughCoordinator(Room &room, ServerPlayer *owner)
{
    owner->setOwner(true);

    ProtocolMessage pause;
    pause.type = ProtocolMessageType::Notification;
    pause.source = ProtocolEndpoint::Client;
    pause.destination = ProtocolEndpoint::Room;
    pause.command = S_COMMAND_PAUSE;
    pause.hasPayload = true;
    pause.payload = PausePayload{true}.toVariant();
    RoomTestAccess::dispatch(room, owner, pause);
    if (!RoomTestAccess::isPaused(room))
        return false;

    ProtocolMessage resume = pause;
    resume.payload = PausePayload{false}.toVariant();
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
        && player->m_expectedReplyCommand == S_COMMAND_UNKNOWN;
}

}

int runRequestCoordinatorTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    ScopedOperationLimit operationLimit;
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *owner = RoomTestAccess::addPlayer(room, QStringLiteral("request-owner"),
                                                    QStringLiteral("online"));
    RoomTestAccess::addPlayer(room, QStringLiteral("request-robot"), QStringLiteral("robot"));

    RequestRecorder recorder;
    recorder.watch(owner);

    if (!acceptedReplyCompletesRequest(room, owner, recorder))
        return 2;
    if (!mismatchedReplyTimesOut(room, owner, recorder))
        return 3;
    if (!requestCommandsAreStrict(room, owner, recorder))
        return 4;
    if (!callbackDispatchesThroughCoordinator(room, owner))
        return 5;
    if (!raceTimeoutClearsPendingState(room, owner))
        return 6;

    qInfo() << "request coordinator behavior passed";
    return 0;
}
