#include "client-live-session.h"

#include "core/client-core.h"
#include "core/client-game-state-reducer.h"
#include "interaction-command-registry.h"
#include "interaction-reply-coordinator.h"
#include "protocol/session/session-payloads.h"
#include "nativesocket.h"
#include "socket.h"

#include <QDateTime>

using namespace QSanProtocol;

namespace {

bool reject(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

} // namespace

ClientLiveSession::ClientLiveSession(ClientCore *core, QObject *parent)
    : QObject(parent), m_core(core)
{
    m_phaseTimer.setSingleShot(true);
}

void ClientLiveSession::connectToServer(const ClientLiveSessionOptions &options,
                                        ClientSocket *injectedSocket)
{
    m_options = options;
    beginConnection(options.reconnectRequested, injectedSocket);
}

void ClientLiveSession::reconnect()
{
    beginConnection(true);
}

void ClientLiveSession::beginConnection(bool reconnectRequested,
                                        ClientSocket *injectedSocket)
{
    m_shuttingDown = false;
    m_failureEmitted = false;
    m_reconnectAttempt = reconnectRequested;
    m_phaseTimer.stop();
    m_pendingRequests.clear();
    m_requestStartedAt.clear();
    m_syncActive = false;
    m_syncId.clear();
    m_pendingPresentationEvents.clear();
    m_pendingFrontendMessages.clear();
    if (m_core != nullptr && m_core->hasActiveRequest())
        m_core->cancelActiveRequest(InteractionCancelReason::Disconnected);
    if (m_socket != nullptr) {
        m_socket->disconnect(this);
        if (NativeClientSocket *native = qobject_cast<NativeClientSocket *>(m_socket.data()))
            native->abort();
        else
            m_socket->disconnectFromHost();
        m_socket->deleteLater();
    }

    m_session.reset();
    const quint64 currentGeneration = m_session.generation();
    if (!reconnectRequested && m_core != nullptr)
        m_core->state()->reset();
    if (m_core != nullptr) {
        m_core->state()->setConnectionValue(QStringLiteral("host"), m_options.host);
        m_core->state()->setConnectionValue(QStringLiteral("port"), m_options.port);
        m_core->state()->setConnectionValue(QStringLiteral("state"),
            reconnectRequested ? QStringLiteral("reconnecting") : QStringLiteral("connecting"));
        m_core->state()->setConnectionValue(QStringLiteral("generation"),
            QString::number(currentGeneration));
    }

    ClientSocket *socket = injectedSocket != nullptr
        ? injectedSocket : static_cast<ClientSocket *>(new NativeClientSocket);
    socket->setParent(this);
    m_socket = socket;
    connect(socket, &ClientSocket::connected, this, [this, currentGeneration]() {
        if (currentGeneration != m_session.generation())
            return;
        if (m_core != nullptr)
            m_core->state()->setConnectionValue(QStringLiteral("state"), QStringLiteral("handshake"));
        emit connectionChanged(QStringLiteral("handshake"));
        emit transportConnected();
        startPhaseTimeout(QStringLiteral("handshake"), m_options.handshakeTimeoutMs);
    });
    connect(socket, &ClientSocket::message_got, this,
        [this, currentGeneration](const QByteArray &frame) {
            consumeFrame(frame, currentGeneration);
    });
    connect(socket, &ClientSocket::error_message, this,
        [this, currentGeneration](const QString &detail) {
            const NativeClientSocket *native
                = qobject_cast<const NativeClientSocket *>(m_socket.data());
            if (currentGeneration != m_session.generation() || m_shuttingDown
                || (native != nullptr
                    && native->lastError() == QAbstractSocket::RemoteHostClosedError))
                return;
            fail(3, QStringLiteral("network_error"),
                 detail.isEmpty() ? QStringLiteral("socket error") : detail);
        });
    connect(socket, &ClientSocket::disconnected, this, [this, currentGeneration]() {
        if (currentGeneration != m_session.generation())
            return;
        m_phaseTimer.stop();
        m_syncActive = false;
        m_syncId.clear();
        m_pendingPresentationEvents.clear();
        m_pendingFrontendMessages.clear();
        if (m_core != nullptr) {
            m_core->state()->setConnectionValue(QStringLiteral("state"), QStringLiteral("disconnected"));
            if (m_core->hasActiveRequest())
                m_core->cancelActiveRequest(InteractionCancelReason::Disconnected);
        }
        emit connectionChanged(QStringLiteral("disconnected"));
        emit disconnected();
    });

    emit connectionChanged(reconnectRequested ? QStringLiteral("reconnecting")
                                              : QStringLiteral("connecting"));
    startPhaseTimeout(QStringLiteral("connect"), m_options.connectTimeoutMs);
    if (NativeClientSocket *native = qobject_cast<NativeClientSocket *>(socket))
        native->connectToHost(m_options.host, m_options.port);
    else
        socket->connectToHost();
}

bool ClientLiveSession::consumeFrame(const QByteArray &frame, quint64 generation)
{
    if (generation != m_session.generation())
        return true;
    ProtocolMessage message;
    const ProtocolDecodeResult decoded = m_router.decode(frame, &message);
    if (!decoded.success) {
        fail(4, QStringLiteral("protocol_decode"), decoded.detail);
        return false;
    }
    QString error;
    if (!m_session.acceptIncoming(message, &error)) {
        fail(4, QStringLiteral("session_transition"), error);
        return false;
    }
    emit protocolMessageReceived(message);

    if (message.command == S_COMMAND_CHECK_VERSION
        && message.type == ProtocolMessageType::Notification) {
        ServerHelloPayload hello;
        if (!ServerHelloPayload::parse(message.payload, &hello, &error)) {
            fail(4, QStringLiteral("server_hello"), error);
            return false;
        }
        if (m_core != nullptr) {
            m_core->state()->setConnectionValue(QStringLiteral("game_version"), hello.gameVersion);
            m_core->state()->setConnectionValue(QStringLiteral("mod_name"), hello.modName);
            m_core->state()->setCardIdSpace(hello.cardCount);
        }
        emit stateChanged();
        emit frontendMessageReceived(message);
        if (m_options.automaticSignup) {
            if (!requestSignup(&error)) {
                fail(5, QStringLiteral("signup_send"), error);
                return false;
            }
        }
        return true;
    }

    if (message.command == S_COMMAND_SIGNUP
        && message.type == ProtocolMessageType::Reply) {
        SignupReplyPayload reply;
        if (!SignupReplyPayload::parse(message.payload, &reply, &error)) {
            fail(5, QStringLiteral("signup_reply"), error);
            return false;
        }
        if (!reply.accepted) {
            if (m_reconnectAttempt && m_options.fallbackToFreshSignup
                && reply.errorCode == QLatin1String("reconnect_target_missing")) {
                // The desktop checkbox historically means "reconnect if possible".
                // Start a fresh connection when the server has no seat to restore.
                beginConnection(false);
                return true;
            }
            fail(5, reply.errorCode.isEmpty() ? QStringLiteral("signup_rejected") : reply.errorCode,
                 reply.message);
            return false;
        }
        m_reconnectAttempt = reply.reconnected;
        if (m_core != nullptr) {
            m_core->state()->setSelfName(reply.playerId);
            m_core->state()->setConnectionValue(QStringLiteral("reconnected"), reply.reconnected);
            m_core->state()->setConnectionValue(QStringLiteral("room_id"), reply.roomId);
        }
        emit stateChanged();
        emit frontendMessageReceived(message);
        startPhaseTimeout(QStringLiteral("setup"), m_options.handshakeTimeoutMs);
        return true;
    }

    if (message.command == S_COMMAND_SETUP
        && message.type == ProtocolMessageType::Notification) {
        SetupPayload setup;
        if (!SetupPayload::parse(message.payload, &setup, &error)) {
            fail(4, QStringLiteral("setup_payload"), error);
            return false;
        }
        if (m_core != nullptr)
            m_core->state()->setSetup(message.payload.toMap());
        ProtocolMessage ready;
        if (!m_session.makeReadyNotification(&ready, &error) || !writeFrame(ready, &error)) {
            fail(4, QStringLiteral("ready_send"), error);
            return false;
        }
        m_phaseTimer.stop();
        if (m_core != nullptr) {
            m_core->state()->setConnectionValue(QStringLiteral("state"), QStringLiteral("active"));
            m_core->state()->setConnectionValue(QStringLiteral("ready"), true);
        }
        emit connectionChanged(QStringLiteral("active"));
        emit stateChanged();
        emit sessionActive(m_reconnectAttempt);
        emit frontendMessageReceived(message);
        return true;
    }

    return dispatchMessage(message);
}

bool ClientLiveSession::dispatchMessage(const ProtocolMessage &message)
{
    if (message.type == ProtocolMessageType::Request) {
        if (InteractionCommandRegistry::find(static_cast<CommandType>(message.command)) == nullptr) {
            fail(4, QStringLiteral("unsupported_interaction"),
                 QStringLiteral("unregistered production interaction %1").arg(message.command));
            return false;
        }
        emit interactionRequested(message);
        emit frontendMessageReceived(message);
        return true;
    }

    if (message.type == ProtocolMessageType::Reply) {
        const int expectedCommand = m_pendingRequests.take(message.replyTo);
        const qint64 startedAt = m_requestStartedAt.take(message.replyTo);
        if (message.replyTo == 0 || expectedCommand == 0 || expectedCommand != message.command) {
            fail(4, QStringLiteral("reply_correlation"),
                 QStringLiteral("unexpected application reply correlation"));
            return false;
        }
        CommandResultPayload result;
        QString error;
        if (!CommandResultPayload::parse(message.payload, &result, &error)) {
            fail(4, QStringLiteral("command_result"), error);
            return false;
        }
        if (message.command == S_COMMAND_NETWORK_DELAY_TEST && startedAt > 0) {
            m_core->state()->setConnectionValue(QStringLiteral("latency_ms"),
                qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt));
            emit stateChanged();
        }
        emit commandResult(message.command, result.success, result.message);
        emit frontendMessageReceived(message);
        return true;
    }

    if (message.type != ProtocolMessageType::Notification)
        return true;
    if (m_core == nullptr) {
        fail(4, QStringLiteral("state_reducer"),
             QStringLiteral("client state core is unavailable"));
        return false;
    }
    if (message.command == S_COMMAND_NETWORK_DELAY_TEST) {
        NetworkDelayPayload delay;
        QString error;
        if (!NetworkDelayPayload::parse(message.payload, &delay, &error)
            || !sendRequest(S_COMMAND_NETWORK_DELAY_TEST, delay.toVariant(), &error)) {
            fail(4, QStringLiteral("network_delay"), error);
            return false;
        }
    }
    ClientGameState *target = m_core->state();
    StateSyncPayload sync;
    if (message.command == S_COMMAND_STATE_SYNC) {
        QString error;
        if (!StateSyncPayload::parse(message.payload, &sync, &error)) {
            fail(4, QStringLiteral("state_sync"), error);
            return false;
        }
        if (sync.phase == QLatin1String("begin")) {
            if (m_syncActive) {
                fail(4, QStringLiteral("state_sync_overlap"),
                     QStringLiteral("a state snapshot is already active"));
                return false;
            }
            m_pendingState = *target;
            m_pendingState.resetGameplayState();
            m_pendingPresentationEvents.clear();
            m_pendingFrontendMessages.clear();
            m_syncActive = true;
            m_syncId = sync.syncId;
            target = &m_pendingState;
        } else if (!m_syncActive || sync.syncId != m_syncId) {
            fail(4, QStringLiteral("state_sync_mismatch"),
                 QStringLiteral("STATE_SYNC end does not match begin"));
            return false;
        } else {
            target = &m_pendingState;
        }
    } else if (m_syncActive) {
        target = &m_pendingState;
    }

    const ClientStateReduction reduction = ClientGameStateReducer::applyNotification(
        target, message.command, message.payload);
    if (!reduction.success) {
        fail(4, QStringLiteral("state_reducer"), reduction.detail);
        return false;
    }
    if (!reduction.eventText.isEmpty()) {
        if (m_syncActive)
            m_pendingPresentationEvents.append(
                PendingPresentationEvent{message.command, reduction.eventText, message.payload});
        else
            emit presentationEvent(message.command, reduction.eventText, message.payload);
    }
    if (m_syncActive)
        m_pendingFrontendMessages.append(message);
    if (message.command == S_COMMAND_STATE_SYNC && sync.phase == QLatin1String("end")) {
        *m_core->state() = m_pendingState;
        m_syncActive = false;
        m_syncId.clear();
        const QList<PendingPresentationEvent> committedEvents = m_pendingPresentationEvents;
        m_pendingPresentationEvents.clear();
        const QList<ProtocolMessage> committedMessages = m_pendingFrontendMessages;
        m_pendingFrontendMessages.clear();
        emit stateChanged();
        for (const ProtocolMessage &committed : committedMessages)
            emit frontendMessageReceived(committed);
        for (const PendingPresentationEvent &event : committedEvents)
            emit presentationEvent(event.command, event.text, event.payload);
        return true;
    }
    if (!m_syncActive) {
        emit stateChanged();
        emit frontendMessageReceived(message);
    }
    return true;
}

bool ClientLiveSession::requestSignup(QString *error)
{
    SignupRequestPayload signup;
    signup.reconnectRequested = m_reconnectAttempt;
    signup.screenName = m_options.screenName;
    signup.avatar = m_options.avatar;
    ProtocolMessage request;
    if (!m_session.makeSignupRequest(signup, &request, error)
        || !writeFrame(request, error)) {
        return false;
    }
    startPhaseTimeout(QStringLiteral("signup"), m_options.handshakeTimeoutMs);
    return true;
}

bool ClientLiveSession::sendControl(CommandType command, const QVariant &payload, QString *error)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Notification;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.command = command;
    message.hasPayload = payload.isValid() && !payload.isNull();
    message.payload = payload;
    return sendPrepared(&message, true, error);
}

bool ClientLiveSession::sendRequest(CommandType command, const QVariant &payload, QString *error)
{
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.command = command;
    message.hasPayload = payload.isValid() && !payload.isNull();
    message.payload = payload;
    if (!sendPrepared(&message, true, error))
        return false;
    m_pendingRequests.insert(message.messageId, command);
    m_requestStartedAt.insert(message.messageId, QDateTime::currentMSecsSinceEpoch());
    return true;
}

bool ClientLiveSession::sendReply(CommandType command, const QVariant &payload,
                                  quint64 replyTo, QString *error)
{
    if (replyTo == 0)
        return reject(error, QStringLiteral("reply correlation id is required"));
    ProtocolMessage message;
    message.type = ProtocolMessageType::Reply;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Room;
    message.command = command;
    message.replyTo = replyTo;
    message.hasPayload = payload.isValid() && !payload.isNull();
    message.payload = payload;
    return sendPrepared(&message, true, error);
}

bool ClientLiveSession::submitInteractionResponse(InteractionResponse response, QString *error)
{
    if (m_core == nullptr || !m_core->hasActiveRequest())
        return reject(error, QStringLiteral("there is no active interaction"));
    const InteractionCommandDescriptor *descriptor = InteractionCommandRegistry::find(
        m_core->activeRequest().type);
    if (descriptor == nullptr)
        return reject(error, QStringLiteral("active interaction has no reply encoder"));

    bool sent = false;
    const bool accepted = InteractionReplyCoordinator::submit(m_core,
        descriptor->replyEncoder, std::move(response),
        [this, &sent, error](const InteractionWireReply &reply) {
            ProtocolMessage message;
            message.type = ProtocolMessageType::Reply;
            message.source = ProtocolEndpoint::Client;
            message.destination = ProtocolEndpoint::Room;
            message.command = reply.command;
            message.replyTo = reply.replyTo;
            message.hasPayload = reply.payload.isValid() && !reply.payload.isNull();
            message.payload = reply.payload;
            sent = sendPrepared(&message, true, error);
        });
    return accepted && sent;
}

bool ClientLiveSession::sendPrepared(ProtocolMessage *message, bool applicationMessage,
                                     QString *error)
{
    if (message == nullptr)
        return reject(error, QStringLiteral("protocol message output is null"));
    if (m_socket == nullptr || !m_socket->isConnected())
        return reject(error, QStringLiteral("socket is not connected"));
    if (applicationMessage && !m_session.prepareApplicationMessage(message, error))
        return false;
    return writeFrame(*message, error);
}

bool ClientLiveSession::writeFrame(const ProtocolMessage &message, QString *error)
{
    const QByteArray frame = m_router.encode(message, error);
    if (frame.isEmpty())
        return false;
    if (frame.contains('\n') || frame.contains('\r'))
        return reject(error, QStringLiteral("encoded protocol frame contains a delimiter"));
    if (m_socket == nullptr || !m_socket->isConnected())
        return reject(error, QStringLiteral("socket write failed"));
    m_socket->send(frame);
    return true;
}

void ClientLiveSession::startPhaseTimeout(const QString &phase, int timeoutMs)
{
    m_phaseTimer.stop();
    QObject::disconnect(&m_phaseTimer, nullptr, this, nullptr);
    const quint64 currentGeneration = m_session.generation();
    connect(&m_phaseTimer, &QTimer::timeout, this, [this, currentGeneration, phase]() {
        if (currentGeneration == m_session.generation())
            fail(phase == QLatin1String("connect") ? 3 : 4,
                 QStringLiteral("timeout"), phase + QStringLiteral(" timed out"));
    });
    m_phaseTimer.start(qMax(1, timeoutMs));
}

void ClientLiveSession::disconnectGracefully()
{
    m_shuttingDown = true;
    m_phaseTimer.stop();
    m_pendingRequests.clear();
    m_requestStartedAt.clear();
    m_pendingPresentationEvents.clear();
    m_pendingFrontendMessages.clear();
    if (m_core != nullptr && m_core->hasActiveRequest())
        m_core->cancelActiveRequest(InteractionCancelReason::Disconnected);
    if (m_socket != nullptr)
        m_socket->disconnectFromHost();
}

bool ClientLiveSession::isActive() const
{
    return m_session.phase() == ClientSessionPhase::Active
        && m_socket != nullptr && m_socket->isConnected();
}

quint64 ClientLiveSession::generation() const
{
    return m_session.generation();
}

void ClientLiveSession::fail(int exitCode, const QString &code, const QString &detail)
{
    if (m_failureEmitted)
        return;
    m_failureEmitted = true;
    m_phaseTimer.stop();
    m_pendingRequests.clear();
    m_requestStartedAt.clear();
    m_pendingPresentationEvents.clear();
    m_pendingFrontendMessages.clear();
    m_session.fail();
    if (m_core != nullptr)
        m_core->state()->setConnectionValue(QStringLiteral("state"), QStringLiteral("failed"));
    emit fatalError(exitCode, code, detail);
    if (NativeClientSocket *native = qobject_cast<NativeClientSocket *>(m_socket.data()))
        native->abort();
    else if (m_socket != nullptr)
        m_socket->disconnectFromHost();
}
