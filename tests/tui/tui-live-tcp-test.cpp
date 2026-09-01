#include "client-live-session.h"
#include "client-core.h"
#include "protocol-interaction-request-builder.h"
#include "protocol/gameplay/protocol-gameplay-payload-registry.h"
#include "protocol/session/session-payloads.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>
#include <functional>

using namespace QSanProtocol;

namespace {

class FakeV2Server final : public QObject
{
public:
    using Completion = std::function<void(bool, const QString &)>;

    FakeV2Server(Completion completion, QObject *parent = nullptr)
        : QObject(parent), m_completion(std::move(completion))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            m_socket = m_server.nextPendingConnection();
            ++m_connectionCount;
            m_nextMessageId = 1;
            m_frames.clear();
            connect(m_socket, &QTcpSocket::readyRead, this, [this]() { consumeClient(); });
            sendHello();
        });
    }

    bool listen(QString *error)
    {
        if (m_server.listen(QHostAddress::LocalHost, 0))
            return true;
        if (error != nullptr)
            *error = m_server.errorString();
        return false;
    }

    quint16 port() const { return m_server.serverPort(); }
    int connectionCount() const { return m_connectionCount; }
    bool initialSignupSeen() const { return m_initialSignupSeen; }
    bool reconnectSignupSeen() const { return m_reconnectSignupSeen; }
    bool correlatedReplySeen() const { return m_correlatedReplySeen; }
    bool delayEchoSeen() const { return m_delayEchoSeen; }
    int interactionReplyCount() const { return m_interactionReplyCount; }

private:
    bool sendMessage(ProtocolMessage message)
    {
        if (m_socket == nullptr)
            return fail(QStringLiteral("fake server has no socket"));
        message.version = ProtocolVersion::V2;
        message.messageId = m_nextMessageId++;
        QString error;
        const QByteArray frame = m_router.encode(message, &error);
        if (frame.isEmpty())
            return fail(QStringLiteral("server encode failed: %1").arg(error));
        if (m_socket->write(frame + '\n') != frame.size() + 1)
            return fail(QStringLiteral("fake server socket write failed"));
        return true;
    }

    bool fail(const QString &message)
    {
        if (!m_finished) {
            m_finished = true;
            m_completion(false, message);
        }
        return false;
    }

    void sendHello()
    {
        ServerHelloPayload hello;
        hello.gameVersion = QStringLiteral("test-v2");
        hello.modName = QStringLiteral("QSanguosha");
        hello.cardCount = 2048;
        ProtocolMessage message;
        message.type = ProtocolMessageType::Notification;
        message.source = ProtocolEndpoint::Lobby;
        message.destination = ProtocolEndpoint::Client;
        message.command = S_COMMAND_CHECK_VERSION;
        message.hasPayload = true;
        message.payload = hello.toVariant();
        sendMessage(message);
    }

    void consumeClient()
    {
        const ProtocolFrameAppendResult appended = m_frames.append(m_socket->readAll());
        if (!appended.success) {
            fail(appended.detail);
            return;
        }
        for (const QByteArray &frame : appended.frames) {
            ProtocolMessage message;
            const ProtocolDecodeResult decoded = m_router.decode(frame, &message);
            if (!decoded.success) {
                fail(decoded.detail);
                return;
            }
            handleClient(message);
        }
    }

    void handleClient(const ProtocolMessage &message)
    {
        if (message.command == S_COMMAND_SIGNUP
            && message.type == ProtocolMessageType::Request) {
            SignupRequestPayload request;
            QString error;
            if (!SignupRequestPayload::parse(message.payload, &request, &error)) {
                fail(error);
                return;
            }
            if (m_connectionCount == 1)
                m_initialSignupSeen = !request.reconnectRequested;
            else
                m_reconnectSignupSeen = request.reconnectRequested;

            SignupReplyPayload reply;
            reply.accepted = true;
            reply.reconnected = m_connectionCount > 1;
            reply.playerId = QStringLiteral("p1");
            ProtocolMessage response;
            response.type = ProtocolMessageType::Reply;
            response.source = ProtocolEndpoint::Lobby;
            response.destination = ProtocolEndpoint::Client;
            response.command = S_COMMAND_SIGNUP;
            response.replyTo = message.messageId;
            response.hasPayload = true;
            response.payload = reply.toVariant();
            if (!sendMessage(response))
                return;

            SetupPayload setup;
            setup.serverName = QStringLiteral("fake-v2-server");
            setup.gameMode = QStringLiteral("03_1v2");
            setup.gameRuleMode = QStringLiteral("normal");
            setup.operationTimeout = 15;
            setup.nullificationCountdown = 10;
            setup.playerCount = 3;
            ProtocolMessage setupMessage;
            setupMessage.type = ProtocolMessageType::Notification;
            setupMessage.source = ProtocolEndpoint::Lobby;
            setupMessage.destination = ProtocolEndpoint::Client;
            setupMessage.command = S_COMMAND_SETUP;
            setupMessage.hasPayload = true;
            setupMessage.payload = setup.toVariant();
            sendMessage(setupMessage);
            return;
        }

        if (message.command == S_COMMAND_READY
            && message.type == ProtocolMessageType::Notification) {
            QTimer::singleShot(0, this, [this]() { sendGameplayState(); });
            return;
        }

        if (message.command == S_COMMAND_NETWORK_DELAY_TEST
            && message.type == ProtocolMessageType::Request) {
            NetworkDelayPayload payload;
            QString error;
            m_delayEchoSeen = NetworkDelayPayload::parse(message.payload, &payload, &error)
                && payload.nonce == QLatin1String("tui-delay-probe");
            CommandResultPayload result;
            result.success = m_delayEchoSeen;
            result.message = m_delayEchoSeen ? QString() : error;
            ProtocolMessage reply;
            reply.type = ProtocolMessageType::Reply;
            reply.source = ProtocolEndpoint::Room;
            reply.destination = ProtocolEndpoint::Client;
            reply.command = S_COMMAND_NETWORK_DELAY_TEST;
            reply.replyTo = message.messageId;
            reply.hasPayload = true;
            reply.payload = result.toVariant();
            sendMessage(reply);
            return;
        }

        if (message.command == S_COMMAND_MULTIPLE_CHOICE
            && message.type == ProtocolMessageType::Reply) {
            ++m_interactionReplyCount;
            QVariant domainReply;
            QString replyError;
            const bool decodedReply = ProtocolGameplayPayloadRegistry::decodeReplyDomainValue(
                message, &domainReply, &replyError);
            m_correlatedReplySeen = message.replyTo == m_interactionMessageId
                && decodedReply && domainReply.toString() == QLatin1String("yes");
            QTimer::singleShot(20, this, [this, domainReply, replyError, message]() {
                if (m_finished)
                    return;
                m_finished = true;
                const bool ok = m_correlatedReplySeen && m_delayEchoSeen
                    && m_interactionReplyCount == 1;
                m_completion(ok, ok ? QString()
                    : QStringLiteral("interaction/delay contract failed: reply_to=%1 expected=%2 payload=%3 error=%4 delay=%5 replies=%6")
                          .arg(message.replyTo).arg(m_interactionMessageId)
                          .arg(domainReply.toString(), replyError)
                          .arg(m_delayEchoSeen).arg(m_interactionReplyCount));
            });
        }
    }

    void sendRoomNotification(CommandType command, const QVariant &payload)
    {
        ProtocolMessage message;
        message.type = ProtocolMessageType::Notification;
        message.source = ProtocolEndpoint::Room;
        message.destination = ProtocolEndpoint::Client;
        message.command = command;
        message.hasPayload = payload.isValid();
        message.payload = payload;
        sendMessage(message);
    }

    void sendGameplayState()
    {
        if (m_connectionCount == 1) {
            sendRoomNotification(S_COMMAND_ADD_PLAYER,
                QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("player_name"), QStringLiteral("p1")},
                    {QStringLiteral("screen_name"), QStringLiteral("old-name")},
                    {QStringLiteral("avatar"), QStringLiteral("caocao")}});
            sendRoomNotification(S_COMMAND_CHANGE_HP,
                QVariantList{QStringLiteral("p1"), 3, 0, 0});
            sendInteraction();
            m_socket->flush();
            QTimer::singleShot(20, m_socket, [socket = m_socket]() {
                if (socket != nullptr)
                    socket->disconnectFromHost();
            });
            return;
        }

        StateSyncPayload begin;
        begin.syncId = QStringLiteral("9223372036854775809");
        begin.phase = QStringLiteral("begin");
        begin.reconnect = true;
        sendRoomNotification(S_COMMAND_STATE_SYNC, begin.toVariant());
        sendRoomNotification(S_COMMAND_ADD_PLAYER,
            QVariantMap{{QStringLiteral("schema_version"), 1},
                {QStringLiteral("player_name"), QStringLiteral("p1")},
                {QStringLiteral("screen_name"), QStringLiteral("new-name")},
                {QStringLiteral("avatar"), QStringLiteral("liubei")}});
        sendRoomNotification(S_COMMAND_CHANGE_HP,
            QVariantList{QStringLiteral("p1"), 4, 0, 0});
        sendRoomNotification(S_COMMAND_SPEAK,
            QVariantMap{{QStringLiteral("schema_version"), 1},
                {QStringLiteral("speaker"), QStringLiteral("p1")},
                {QStringLiteral("text"), QStringLiteral("snapshot-chat")}});
        StateSyncPayload end = begin;
        end.phase = QStringLiteral("end");
        sendRoomNotification(S_COMMAND_STATE_SYNC, end.toVariant());

        NetworkDelayPayload delay;
        delay.nonce = QStringLiteral("tui-delay-probe");
        sendRoomNotification(S_COMMAND_NETWORK_DELAY_TEST, delay.toVariant());
        sendInteraction();
    }

    void sendInteraction()
    {
        ProtocolMessage interaction;
        interaction.type = ProtocolMessageType::Request;
        interaction.source = ProtocolEndpoint::Room;
        interaction.destination = ProtocolEndpoint::Client;
        interaction.command = S_COMMAND_MULTIPLE_CHOICE;
        interaction.hasPayload = true;
        interaction.payload = QVariantList{QStringLiteral("test-skill"),
            QStringLiteral("yes+no"), QString(), QStringLiteral("choose")};
        m_interactionMessageId = m_nextMessageId;
        sendMessage(interaction);
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_socket;
    ProtocolCodecRouter m_router;
    ProtocolFrameBuffer m_frames;
    Completion m_completion;
    quint64 m_nextMessageId = 1;
    quint64 m_interactionMessageId = 0;
    int m_connectionCount = 0;
    bool m_initialSignupSeen = false;
    bool m_reconnectSignupSeen = false;
    bool m_correlatedReplySeen = false;
    bool m_delayEchoSeen = false;
    int m_interactionReplyCount = 0;
    bool m_finished = false;
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QEventLoop loop;
    bool success = false;
    QString failure;
    FakeV2Server server([&](bool ok, const QString &detail) {
        success = ok;
        failure = detail;
        loop.quit();
    });
    if (!server.listen(&failure)) {
        std::printf("FAIL listen: %s\n", failure.toUtf8().constData());
        return 1;
    }

    ClientCore core;
    ClientLiveSession session(&core);
    int activeCount = 0;
    bool reconnectRequested = false;
    bool oldStateObservedDuringSync = false;
    bool newStateCommitted = false;
    bool presentationSawCommittedState = false;
    int presentationCount = 0;
    int cancellationCount = 0;
    bool pendingCancelledBeforeReconnect = false;
    bool duplicateRejected = false;
    int manualSignupCount = 0;
    int bufferedFrontendCount = 0;
    bool frontendSawCommittedState = true;

    QObject::connect(&session, &ClientLiveSession::sessionActive,
        [&](bool) { ++activeCount; });
    QObject::connect(&session, &ClientLiveSession::disconnected, [&]() {
        if (activeCount == 1 && !reconnectRequested) {
            pendingCancelledBeforeReconnect = cancellationCount == 1
                && !core.hasActiveRequest();
            reconnectRequested = true;
            session.reconnect();
        }
    });
    QObject::connect(&core, &ClientCore::requestCancelled,
        [&](quint64, int) { ++cancellationCount; });
    QObject::connect(&session, &ClientLiveSession::protocolMessageReceived,
        [&](const ProtocolMessage &message) {
            if (activeCount != 2 || message.command != S_COMMAND_STATE_SYNC)
                return;
            StateSyncPayload sync;
            QString error;
            if (StateSyncPayload::parse(message.payload, &sync, &error)
                && sync.phase == QLatin1String("begin")) {
                const QVariantMap player = core.state()->player(QStringLiteral("p1"));
                oldStateObservedDuringSync = player.value(QStringLiteral("screen_name"))
                    == QLatin1String("old-name")
                    && player.value(QStringLiteral("hp")).toInt() == 3;
            }
        });
    QObject::connect(&session, &ClientLiveSession::frontendMessageReceived,
        [&](const ProtocolMessage &message) {
            if (message.command == S_COMMAND_CHECK_VERSION
                && message.type == ProtocolMessageType::Notification) {
                QString error;
                if (!session.requestSignup(&error)) {
                    failure = error;
                    loop.quit();
                    return;
                }
                ++manualSignupCount;
                return;
            }
            if (activeCount != 2 || message.source != ProtocolEndpoint::Room
                || message.destination != ProtocolEndpoint::Client)
                return;
            if (message.command == S_COMMAND_STATE_SYNC
                || message.command == S_COMMAND_ADD_PLAYER
                || message.command == S_COMMAND_CHANGE_HP
                || message.command == S_COMMAND_SPEAK) {
                ++bufferedFrontendCount;
                const QVariantMap player = core.state()->player(QStringLiteral("p1"));
                frontendSawCommittedState = frontendSawCommittedState
                    && player.value(QStringLiteral("screen_name")) == QLatin1String("new-name")
                    && player.value(QStringLiteral("hp")).toInt() == 4;
            }
        });
    QObject::connect(&session, &ClientLiveSession::stateChanged, [&]() {
        const QVariantMap player = core.state()->player(QStringLiteral("p1"));
        if (activeCount == 2
            && player.value(QStringLiteral("screen_name")) == QLatin1String("new-name")
            && player.value(QStringLiteral("hp")).toInt() == 4) {
            newStateCommitted = true;
        }
    });
    QObject::connect(&session, &ClientLiveSession::presentationEvent,
        [&](int command, const QString &text, const QVariant &) {
            if (command != S_COMMAND_SPEAK || !text.contains(QStringLiteral("snapshot-chat")))
                return;
            ++presentationCount;
            const QVariantMap player = core.state()->player(QStringLiteral("p1"));
            presentationSawCommittedState
                = player.value(QStringLiteral("screen_name")) == QLatin1String("new-name")
                && player.value(QStringLiteral("hp")).toInt() == 4;
        });
    QObject::connect(&session, &ClientLiveSession::interactionRequested,
        [&](const ProtocolMessage &message) {
            InteractionRequest request;
            QString error;
            if (!ProtocolInteractionRequestBuilder::build(message, *core.state(),
                    &request, &error)
                || core.beginRequest(std::move(request)) == 0) {
                failure = error.isEmpty() ? QStringLiteral("cannot begin interaction") : error;
                loop.quit();
                return;
            }
            if (activeCount == 1)
                return;
            InteractionResponse response = InteractionResponse::makeOption(
                core.activeRequestId(), QStringLiteral("yes"));
            response.command = core.activeRequest().command;
            InteractionResponse duplicate = response;
            if (!session.submitInteractionResponse(std::move(response), &error)) {
                failure = error;
                loop.quit();
                return;
            }
            duplicateRejected = !session.submitInteractionResponse(
                std::move(duplicate), &error);
        });
    QObject::connect(&session, &ClientLiveSession::fatalError,
        [&](int, const QString &code, const QString &detail) {
            failure = QStringLiteral("%1: %2").arg(code, detail);
            loop.quit();
        });

    ClientLiveSessionOptions options;
    options.host = QStringLiteral("127.0.0.1");
    options.port = server.port();
    options.screenName = QStringLiteral("live-tui-test");
    options.avatar = QStringLiteral("caocao");
    options.automaticSignup = false;
    options.connectTimeoutMs = 1000;
    options.handshakeTimeoutMs = 1000;
    session.connectToServer(options);

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        failure = QStringLiteral("live TCP test timed out");
        loop.quit();
    });
    watchdog.start(5000);
    loop.exec();
    watchdog.stop();

    success = success && failure.isEmpty() && activeCount == 2
        && server.connectionCount() == 2 && server.initialSignupSeen()
        && server.reconnectSignupSeen() && server.correlatedReplySeen()
        && server.delayEchoSeen() && server.interactionReplyCount() == 1
        && oldStateObservedDuringSync && newStateCommitted
        && presentationCount == 1 && presentationSawCommittedState
        && cancellationCount == 1 && pendingCancelledBeforeReconnect
        && duplicateRejected && manualSignupCount == 2
        && bufferedFrontendCount == 5 && frontendSawCommittedState;
    if (!success && failure.isEmpty())
        failure = QStringLiteral("lifecycle or atomic-state assertion failed");
    std::printf("[AUTOTEST] TUI_LIVE_TCP_RESULT status=%s active=%d connections=%d detail=%s\n",
        success ? "PASS" : "FAIL", activeCount, server.connectionCount(),
        failure.toUtf8().constData());
    session.disconnectGracefully();
    return success ? 0 : 1;
}
