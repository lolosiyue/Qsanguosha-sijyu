#include "request-coordinator.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include "protocol/session/session-payloads.h"

#include "json.h"
#include "protocol/switch-context-message.h"
#include "room.h"
#include "serverplayer.h"
#include "settings.h"

#include <QElapsedTimer>
#include <QMap>
#include <QMutexLocker>
#include <QStringList>

#include <functional>

using namespace QSanProtocol;

namespace {

class ScopedCallback
{
public:
    explicit ScopedCallback(const std::function<void()> &callback)
        : m_callback(callback)
    {
    }

    ~ScopedCallback()
    {
        if (m_callback)
            m_callback();
    }

private:
    std::function<void()> m_callback;
};

struct RaceVerifyContext
{
    RequestCoordinator::ResponseVerifyFunction validateFunc;
    void *funcArg;
};

QString onsoleVisibleHandMarkName(const QString &targetName)
{
    return targetName.isEmpty() ? QString() : "HandcardVisible_" + targetName + "+onsole+sys_";
}

void setViewerHandcardVisible(Room &room, ServerPlayer *viewer, const QString &markName,
                              bool visible)
{
    if (viewer == nullptr || markName.isEmpty())
        return;

    QList<ServerPlayer *> onlyViewers;
    onlyViewers << viewer;
    room.setPlayerMark(viewer, markName, visible ? 1 : 0, onlyViewers);
}

void syncKnownHandcards(Room &room, ServerPlayer *viewer, ServerPlayer *target)
{
    if (viewer == nullptr || target == nullptr)
        return;

    JsonArray knownCardsArg;
    knownCardsArg << target->objectName() << JsonUtils::toJsonArray(target->handCards());
    room.doNotify(viewer, S_COMMAND_SET_KNOWN_CARDS, knownCardsArg);
}

bool isCompatibleReplyCommand(CommandType expected, CommandType actual)
{
    return expected == actual;
}

}

RequestCoordinator::RequestCoordinator(Room &room)
    : m_room(room), m_raceRequestSemaphore(0), m_roomSemaphore(1),
      m_raceStarted(false), m_raceWinner(nullptr)
{
    initializeCallbacks();
}

void RequestCoordinator::initializeCallbacks()
{
    m_requestResponsePairs[S_COMMAND_PLAY_CARD] = S_COMMAND_RESPONSE_CARD;
    m_requestResponsePairs[S_COMMAND_NULLIFICATION] = S_COMMAND_RESPONSE_CARD;
    m_requestResponsePairs[S_COMMAND_SHOW_CARD] = S_COMMAND_RESPONSE_CARD;
    m_requestResponsePairs[S_COMMAND_ASK_PEACH] = S_COMMAND_RESPONSE_CARD;
    m_requestResponsePairs[S_COMMAND_PINDIAN] = S_COMMAND_RESPONSE_CARD;
    m_requestResponsePairs[S_COMMAND_EXCHANGE_CARD] = S_COMMAND_DISCARD_CARD;

    m_callbacks[S_COMMAND_SURRENDER] = &Room::processRequestSurrender;
    m_callbacks[S_COMMAND_CHEAT] = &Room::processRequestCheat;
    m_callbacks[S_COMMAND_READY] = &Room::setReadyCommand;
    m_callbacks[S_COMMAND_ADD_ROBOT] = &Room::addRobotCommand;
    m_callbacks[S_COMMAND_SPEAK] = &Room::speakCommand;
    m_callbacks[S_COMMAND_TRUST] = &Room::trustCommand;
    m_callbacks[S_COMMAND_PAUSE] = &Room::pauseCommand;
    m_callbacks[S_COMMAND_NETWORK_DELAY_TEST] = &Room::networkDelayTestCommand;
    m_callbacks[S_COMMAND_ANYTIME_SKILL] = &Room::handleAnytimeSkillRequest;
}

ServerPlayer *RequestCoordinator::requestTarget(ServerPlayer *player) const
{
    if (player == nullptr)
        return nullptr;

    ServerPlayer *actual = m_room.getActualController(player);
    if (actual != nullptr && actual != player && actual->isOnline()
        && actual->getState() != "trust")
        return actual;

    ServerPlayer *onsole = player->getOnsoleOwner();
    if (actual == player && onsole != nullptr && onsole != player && onsole->isOnline())
        return onsole;

    return player;
}

void RequestCoordinator::notifyArrangeSeats(ServerPlayer *player)
{
    if (player == nullptr)
        return;

    QStringList playerCircle;
    foreach (ServerPlayer *seatPlayer, m_room.getPlayers())
        playerCircle << seatPlayer->objectName();

    m_room.doNotify(player, S_COMMAND_ARRANGE_SEATS, JsonUtils::toJsonArray(playerCircle));
}

void RequestCoordinator::clearDualControlRequest(ServerPlayer *player, bool restoreContext)
{
    if (player == nullptr)
        return;

    QString requestTargetName;
    {
        QMutexLocker locker(&m_mutex);
        requestTargetName = m_dualControlRequestTargets.take(player->objectName());
        if (!requestTargetName.isEmpty())
            m_dualControlReplyOwners.remove(requestTargetName);
    }

    if (requestTargetName.isEmpty())
        return;

    ServerPlayer *target = m_room.findPlayerByObjectName(requestTargetName, true);
    if (target == nullptr)
        return;

    if (player->getClientReply().isNull() && !target->getClientReply().isNull())
        player->setClientReply(target->getClientReply());

    target->acquireLock(ServerPlayer::SEMA_MUTEX);
    target->m_expectedReplyCommand = S_COMMAND_UNKNOWN;
    target->m_isWaitingReply = false;
    target->m_expectedReplyMessageId = 0;
    target->m_isClientResponseReady = false;
    target->setClientReplyString("");
    target->releaseLock(ServerPlayer::SEMA_MUTEX);

    if (restoreContext && target->isOnline()) {
        SwitchContextMessage message;
        message.playerName = target->objectName();
        m_room.doNotify(target, S_COMMAND_SWITCH_CONTEXT, message.toVariant());
        notifyArrangeSeats(target);
    }
}

bool RequestCoordinator::request(ServerPlayer *player, CommandType command,
                                 const QVariant &arg, time_t timeOut, bool wait)
{
    ServerPlayer *actual = m_room.getActualController(player);
    ServerPlayer *target = requestTarget(player);
    bool redirectedToController = actual != nullptr && target == actual && actual != player
        && actual->isOnline();
    ScopedCallback restoreContextGuard([this, player, wait, redirectedToController]() {
        if (wait && redirectedToController)
            clearDualControlRequest(player);
    });

    if (!redirectedToController) {
        ServerPlayer *onsole = player->getOnsoleOwner();
        if (actual == player && onsole != player) {
            QString visibleMark = onsoleVisibleHandMarkName(player->objectName());
            setViewerHandcardVisible(m_room, onsole, visibleMark, true);
            syncKnownHandcards(m_room, onsole, player);
            m_room.setPlayerProperty(onsole, "onsole_target", player->objectName());
            bool hasResult = request(onsole, command, arg, timeOut, wait);
            m_room.setPlayerProperty(onsole, "onsole_target", "");
            setViewerHandcardVisible(m_room, onsole, visibleMark, false);
            player->setClientReply(onsole->getClientReply());
            return hasResult;
        }
    }

    ProtocolMessage requestMessage;
    requestMessage.type = ProtocolMessageType::Request;
    requestMessage.source = ProtocolEndpoint::Room;
    requestMessage.destination = ProtocolEndpoint::Client;
    requestMessage.command = command;
    requestMessage.hasPayload = !arg.isNull();
    requestMessage.payload = arg;
    CommandType expectedReply = m_requestResponsePairs.contains(command)
        ? m_requestResponsePairs[command]
        : command;

    target->acquireLock(ServerPlayer::SEMA_MUTEX);
    target->m_isClientResponseReady = false;
    target->drainLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
    target->setClientReply(QVariant());
    target->setClientReplyString("");
    target->m_isWaitingReply = true;
    target->m_expectedReplyCommand = expectedReply;
    target->releaseLock(ServerPlayer::SEMA_MUTEX);

    ServerPlayer *resultTarget = target;
    if (redirectedToController) {
        syncKnownHandcards(m_room, actual, player);
        SwitchContextMessage message;
        message.playerName = player->objectName();
        m_room.doNotify(actual, S_COMMAND_SWITCH_CONTEXT, message.toVariant());
        notifyArrangeSeats(actual);

        player->acquireLock(ServerPlayer::SEMA_MUTEX);
        player->m_isClientResponseReady = false;
        player->drainLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
        player->setClientReply(QVariant());
        player->setClientReplyString("");
        player->m_isWaitingReply = true;
        player->m_expectedReplyCommand = expectedReply;
        player->releaseLock(ServerPlayer::SEMA_MUTEX);

        {
            QMutexLocker locker(&m_mutex);
            m_dualControlReplyOwners[actual->objectName()] = player->objectName();
            m_dualControlRequestTargets[player->objectName()] = actual->objectName();
        }

        resultTarget = player;
    }

    const quint64 requestMessageId = target->sendProtocolMessage(requestMessage);
    target->acquireLock(ServerPlayer::SEMA_MUTEX);
    target->m_expectedReplyMessageId = requestMessageId;
    target->releaseLock(ServerPlayer::SEMA_MUTEX);
    if (redirectedToController) {
        player->acquireLock(ServerPlayer::SEMA_MUTEX);
        player->m_expectedReplyMessageId = requestMessageId;
        player->releaseLock(ServerPlayer::SEMA_MUTEX);
    }
    return !wait || getResult(resultTarget, timeOut);
}

bool RequestCoordinator::broadcastRequest(QList<ServerPlayer *> players, CommandType command,
                                          time_t timeOut)
{
    QMap<ServerPlayer *, QList<ServerPlayer *> > controllerMap;
    foreach (ServerPlayer *player, players)
        controllerMap[requestTarget(player)] << player;

    QList<ServerPlayer *> pendingPlayers;
    QElapsedTimer timer;
    timer.start();
    for (QMap<ServerPlayer *, QList<ServerPlayer *> >::const_iterator it = controllerMap.constBegin();
         it != controllerMap.constEnd(); ++it) {
        const QList<ServerPlayer *> &group = it.value();
        if (group.length() == 1) {
            time_t remainTime = timeOut - timer.elapsed();
            if (remainTime < 0)
                remainTime = 0;
            request(group.first(), command, group.first()->m_commandArgs, remainTime, false);
            pendingPlayers << group.first();
            continue;
        }

        int index = 0;
        while (index < group.length()) {
            time_t remainTime = timeOut - timer.elapsed();
            if (remainTime < 0)
                remainTime = 0;
            request(group.at(index), command, group.at(index)->m_commandArgs, remainTime, true);
            ++index;
        }
    }
    foreach (ServerPlayer *player, pendingPlayers) {
        time_t remainTime = timeOut - timer.elapsed();
        if (remainTime < 0)
            remainTime = 0;
        getResult(player, remainTime);
    }
    return true;
}

ServerPlayer *RequestCoordinator::raceRequest(QList<ServerPlayer *> players,
                                              CommandType command, time_t timeOut,
                                              ResponseVerifyFunction validateFunc,
                                              void *funcArg)
{
    QMap<ServerPlayer *, QList<ServerPlayer *> > controllerMap;
    foreach (ServerPlayer *player, players)
        controllerMap[requestTarget(player)] << player;

    QMap<ServerPlayer *, int> controllerIndex;
    RaceVerifyContext context = { validateFunc, funcArg };
    QElapsedTimer timer;
    timer.start();

    while (true) {
        QList<ServerPlayer *> activePlayers;
        for (QMap<ServerPlayer *, QList<ServerPlayer *> >::const_iterator it = controllerMap.constBegin();
             it != controllerMap.constEnd(); ++it) {
            int index = controllerIndex.value(it.key(), 0);
            if (index < it.value().length())
                activePlayers << it.value().at(index);
        }

        if (activePlayers.isEmpty())
            return nullptr;

        time_t remainTime = timeOut - timer.elapsed();
        if (remainTime < 0)
            remainTime = 0;

        m_roomSemaphore.acquire();
        m_raceStarted = true;
        m_raceWinner = nullptr;
        while (m_raceRequestSemaphore.tryAcquire(1)) {
        }
        m_roomSemaphore.release();

        Countdown countdown;
        countdown.max = remainTime;
        countdown.type = Countdown::S_COUNTDOWN_USE_SPECIFIED;
        if (command == S_COMMAND_NULLIFICATION)
            m_room.notifyMoveFocus(m_room.getAlivePlayers(), command, countdown);
        else
            m_room.notifyMoveFocus(activePlayers, command, countdown);

        foreach (ServerPlayer *player, activePlayers)
            request(player, command, player->m_commandArgs, remainTime, false);

        ServerPlayer *winner = getRaceResult(activePlayers, command, remainTime,
                                             &Room::verifyRaceReply, &context);
        foreach (ServerPlayer *player, activePlayers)
            clearDualControlRequest(player);
        if (winner != nullptr)
            return winner;

        for (QMap<ServerPlayer *, QList<ServerPlayer *> >::const_iterator it = controllerMap.constBegin();
             it != controllerMap.constEnd(); ++it)
            controllerIndex[it.key()] = controllerIndex.value(it.key(), 0) + 1;

        if (timer.elapsed() >= timeOut)
            return nullptr;
    }
}

ServerPlayer *RequestCoordinator::getRaceResult(QList<ServerPlayer *> players, CommandType,
                                                time_t timeOut,
                                                ResponseVerifyFunction validateFunc,
                                                void *funcArg)
{
    QElapsedTimer timer;
    timer.start();
    bool validResult = false;
    for (int i = 0; i < players.size(); ++i) {
        bool acquired = true;
        if (Config.OperationNoLimit)
            m_raceRequestSemaphore.acquire();
        else {
            time_t remainTime = timeOut - timer.elapsed();
            if (remainTime < 0)
                remainTime = 0;
            acquired = m_raceRequestSemaphore.tryAcquire(1, remainTime);
        }
        bool roomSemaphoreHeld = false;
        if (!acquired)
            roomSemaphoreHeld = m_roomSemaphore.tryAcquire(1);
        else
            roomSemaphoreHeld = true;

        if (m_raceWinner == nullptr) {
            if (roomSemaphoreHeld)
                m_roomSemaphore.release();
            continue;
        }

        if (validateFunc == nullptr
            || (m_raceWinner->m_isClientResponseReady
                && (m_room.*validateFunc)(m_raceWinner, m_raceWinner->getClientReply(), funcArg))) {
            validResult = true;
            break;
        }

        m_raceWinner->m_isWaitingReply = false;
        m_raceWinner = nullptr;
        if (roomSemaphoreHeld)
            m_roomSemaphore.release();
    }

    if (!validResult)
        m_roomSemaphore.acquire();
    m_raceStarted = false;
    foreach (ServerPlayer *player, players) {
        player->acquireLock(ServerPlayer::SEMA_MUTEX);
        player->m_expectedReplyCommand = S_COMMAND_UNKNOWN;
        player->m_isWaitingReply = false;
        player->m_expectedReplyMessageId = 0;
        player->releaseLock(ServerPlayer::SEMA_MUTEX);
    }
    m_roomSemaphore.release();
    return m_raceWinner;
}

bool RequestCoordinator::getResult(ServerPlayer *player, time_t timeOut)
{
    bool validResult = false;
    QString redirectedTargetName;
    {
        QMutexLocker locker(&m_mutex);
        redirectedTargetName = m_dualControlRequestTargets.value(player->objectName());
    }
    player->acquireLock(ServerPlayer::SEMA_MUTEX);

    if (player->isOnline() || !redirectedTargetName.isEmpty()) {
        player->releaseLock(ServerPlayer::SEMA_MUTEX);

        if (Config.OperationNoLimit) {
            const time_t kMaxWaitMs = 600000;
            player->tryAcquireLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE, kMaxWaitMs);
        } else {
            player->tryAcquireLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE, timeOut);
        }

        player->acquireLock(ServerPlayer::SEMA_MUTEX);
        validResult = player->m_isClientResponseReady;
    }
    player->m_expectedReplyCommand = S_COMMAND_UNKNOWN;
    player->m_isWaitingReply = false;
    player->m_expectedReplyMessageId = 0;
    player->releaseLock(ServerPlayer::SEMA_MUTEX);
    if (!redirectedTargetName.isEmpty())
        clearDualControlRequest(player);
    return validResult && !player->getClientReply().isNull();
}

bool RequestCoordinator::verifyRaceReply(ServerPlayer *player, const QVariant &reply,
                                         void *funcArg)
{
    if (!reply.isValid() || reply.toString() == "cancel")
        return false;

    RaceVerifyContext *context = static_cast<RaceVerifyContext *>(funcArg);
    if (context == nullptr || context->validateFunc == nullptr)
        return true;

    return (m_room.*context->validateFunc)(player, reply, context->funcArg);
}

void RequestCoordinator::processClientPacket(
    ServerPlayer *player, const ProtocolMessage &message,
    const QString &rawRequest)
{
    if (message.type == ProtocolMessageType::Reply) {
        if (player == nullptr)
            return;
        player->setClientReplyString(rawRequest);
        processResponse(player, message);
        return;
    }

    if (message.type != ProtocolMessageType::Request
        && message.type != ProtocolMessageType::Notification)
        return;

    Callback callback = m_callbacks.value(
        static_cast<CommandType>(message.command), nullptr);
    if (callback != nullptr) {
        (m_room.*callback)(player,
            message.hasPayload ? message.payload : QVariant());
        if (message.type == ProtocolMessageType::Request && player != nullptr) {
            CommandResultPayload result;
            result.success = true;
            ProtocolMessage reply;
            reply.type = ProtocolMessageType::Reply;
            reply.source = ProtocolEndpoint::Room;
            reply.destination = ProtocolEndpoint::Client;
            reply.command = message.command;
            reply.replyTo = message.messageId;
            reply.hasPayload = true;
            reply.payload = result.toVariant();
            player->sendProtocolMessage(reply);
        }
    }
}

void RequestCoordinator::processResponse(
    ServerPlayer *player, const ProtocolMessage &message)
{
    player->acquireLock(ServerPlayer::SEMA_MUTEX);
    bool success = false;
    QString replyOwnerName;
    {
        QMutexLocker locker(&m_mutex);
        replyOwnerName = m_dualControlReplyOwners.value(player->objectName());
    }
    ServerPlayer *replyOwner = replyOwnerName.isEmpty()
        ? player
        : m_room.findPlayerByObjectName(replyOwnerName, true);
    if (replyOwner == nullptr)
        replyOwner = player;
    if (player == nullptr)
        emit m_room.room_message(m_room.tr("Unable to parse player"));
    else if (!player->m_isWaitingReply || player->m_isClientResponseReady)
        emit m_room.room_message(m_room.tr("Server is not waiting for reply from %1")
                                 .arg(player->objectName()));
    else if (!isCompatibleReplyCommand(player->m_expectedReplyCommand,
                                       static_cast<CommandType>(message.command)))
        emit m_room.room_message(m_room.tr("Reply command should be %1 instead of %2")
                                 .arg(player->m_expectedReplyCommand)
                                 .arg(message.command));
    else if (message.replyTo != player->m_expectedReplyMessageId)
        emit m_room.room_message(m_room.tr("Reply message id should be %1 instead of %2")
                                 .arg(player->m_expectedReplyMessageId)
                                 .arg(message.replyTo));
    else
        success = true;

    QVariant reply;
    if (success) {
        QString decodeError;
        success = ProtocolGameplayPayloadRegistry::decodeReplyDomainValue(
            message, &reply, &decodeError);
        if (!success)
            emit m_room.room_message(m_room.tr("Invalid interaction reply: %1")
                                     .arg(decodeError));
    }

    if (success) {
        player->setClientReply(reply);
        player->m_isClientResponseReady = true;
        player->m_isWaitingReply = false;
        player->m_expectedReplyCommand = S_COMMAND_UNKNOWN;
        player->m_expectedReplyMessageId = 0;

        if (replyOwner != player) {
            replyOwner->acquireLock(ServerPlayer::SEMA_MUTEX);
            replyOwner->setClientReply(reply);
            replyOwner->m_isClientResponseReady = true;
            replyOwner->releaseLock(ServerPlayer::SEMA_MUTEX);
        }

        m_roomSemaphore.acquire();
        if (m_raceStarted) {
            // Keep the winner assignment as the last state write before waking the race waiter.
            m_raceWinner = replyOwner;
            m_raceRequestSemaphore.release();
        } else {
            m_roomSemaphore.release();
            if (replyOwner == player || replyOwner->m_isWaitingReply)
                replyOwner->releaseLock(ServerPlayer::SEMA_COMMAND_INTERACTIVE);
        }
    }
    player->releaseLock(ServerPlayer::SEMA_MUTEX);
}

void RequestCoordinator::unblockWaits()
{
    m_raceRequestSemaphore.release();
    m_roomSemaphore.release();
}
