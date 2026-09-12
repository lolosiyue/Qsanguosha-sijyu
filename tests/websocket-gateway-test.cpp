#include "protocol.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/session-payloads.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtWebSockets/QWebSocket>

#include <cstdio>
#include <functional>

using namespace QSanProtocol;

namespace {

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    qCritical().noquote() << message;
    return false;
}

class LiveServer
{
public:
    ~LiveServer()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(5000);
        }
    }

    bool start(const QString &serverPath, QString *error)
    {
        if (!m_directory.isValid() || !QFileInfo::exists(serverPath)) {
            *error = QStringLiteral("server executable or temporary directory is unavailable");
            return false;
        }

        const QString xdgRoot = m_directory.filePath(QStringLiteral("xdg"));
        QDir().mkpath(xdgRoot);
        const QString configPath = m_directory.filePath(QStringLiteral("server.ini"));
        QFile config(configPath);
        if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) {
            *error = config.errorString();
            return false;
        }
        config.write(
            "[General]\n"
            "GameMode=02p\n"
            "BindAddress=127.0.0.1\n"
            "OperationTimeout=1\n"
            "OperationNoLimit=false\n"
            "EnableAI=true\n"
            "OriginAIDelay=0\n"
            "DisableLua=false\n");
        config.close();

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), xdgRoot);
        m_process.setProcessEnvironment(environment);
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.setWorkingDirectory(QDir::currentPath());
        m_process.setProgram(QFileInfo(serverPath).absoluteFilePath());
        m_process.setArguments({QStringLiteral("--config"), configPath,
                                QStringLiteral("--port"), QStringLiteral("0"),
                                QStringLiteral("--websocket-port"), QStringLiteral("0"),
                                QStringLiteral("--ai-delay"), QStringLiteral("0"),
                                QStringLiteral("--seed"), QStringLiteral("2026090101")});
        m_process.start();
        if (!m_process.waitForStarted(10000)) {
            *error = m_process.errorString();
            return false;
        }

        const QRegularExpression tcpEndpoint(
            QStringLiteral("Listening on 127\\.0\\.0\\.1:(\\d+)"));
        const QRegularExpression wsEndpoint(
            QStringLiteral("WebSocket listening on 127\\.0\\.0\\.1:(\\d+)"));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 45000) {
            drain(100);
            const QString output = QString::fromUtf8(m_output);
            const QRegularExpressionMatch tcpMatch = tcpEndpoint.match(output);
            const QRegularExpressionMatch wsMatch = wsEndpoint.match(output);
            if (tcpMatch.hasMatch() && wsMatch.hasMatch()) {
                bool tcpOk = false;
                bool wsOk = false;
                const uint tcp = tcpMatch.captured(1).toUInt(&tcpOk);
                const uint ws = wsMatch.captured(1).toUInt(&wsOk);
                if (tcpOk && wsOk && tcp > 0 && tcp <= 65535 && ws > 0 && ws <= 65535) {
                    m_tcpPort = static_cast<quint16>(tcp);
                    m_wsPort = static_cast<quint16>(ws);
                    qInfo().noquote() << QStringLiteral("WebSocket test server ready after %1 ms")
                                             .arg(timer.elapsed());
                    return true;
                }
            }
            if (m_process.state() == QProcess::NotRunning)
                break;
        }
        *error = QStringLiteral(
                     "server did not publish TCP and WebSocket endpoints after %1 ms; "
                     "state=%2 error=%3 exitStatus=%4 exitCode=%5\n%6")
                     .arg(timer.elapsed())
                     .arg(static_cast<int>(m_process.state()))
                     .arg(m_process.errorString())
                     .arg(static_cast<int>(m_process.exitStatus()))
                     .arg(m_process.exitCode())
                     .arg(QString::fromUtf8(m_output.right(8000)));
        return false;
    }

    quint16 tcpPort() const { return m_tcpPort; }
    quint16 wsPort() const { return m_wsPort; }

private:
    void drain(int timeoutMs)
    {
        m_process.waitForReadyRead(timeoutMs);
        m_output += m_process.readAll();
    }

    QTemporaryDir m_directory;
    QProcess m_process;
    QByteArray m_output;
    quint16 m_tcpPort = 0;
    quint16 m_wsPort = 0;
};

// An empty rulesBundle intentionally omits rules_bundle for the negative test.
QByteArray encodeSignup(quint64 messageId, const QString &screenName,
                        bool hasRoomId, int roomId, const QJsonObject &rulesBundle,
                        QString *error)
{
    SignupRequestPayload request;
    request.screenName = screenName;
    request.avatar = QStringLiteral("caocao");
    request.hasRoomId = hasRoomId;
    request.roomId = roomId;
    request.hasRulesBundle = !rulesBundle.isEmpty();
    request.rulesBundle = rulesBundle;
    ProtocolMessage message;
    message.type = ProtocolMessageType::Request;
    message.source = ProtocolEndpoint::Client;
    message.destination = ProtocolEndpoint::Lobby;
    message.command = S_COMMAND_SIGNUP;
    message.messageId = messageId;
    message.hasPayload = true;
    message.payload = request.toVariant();
    ProtocolCodecRouter router;
    return router.encode(message, error);
}

bool decodeMessage(const QByteArray &frame, ProtocolMessage *message, QString *error)
{
    ProtocolCodecRouter router;
    const ProtocolDecodeResult decoded = router.decode(frame, message);
    if (decoded.success)
        return true;
    *error = decoded.detail;
    return false;
}

// WebSocket admission requires the client's rules bundle identity to match
// the server's (docs/rules-bundle-identity.md). This client is built from the
// same tree as the server, so it answers with the identity the hello advertises,
// the same bundle a matching Web client would send. A server whose content is
// not declared-v2 advertises {"error_code": ...} in place of an identity.
bool helloRulesBundle(const ProtocolMessage &hello, QJsonObject *bundle, QString *error)
{
    ServerHelloPayload payload;
    if (!ServerHelloPayload::parse(hello.payload, &payload, error))
        return false;
    if (payload.rulesBundle.isEmpty()) {
        *error = QStringLiteral("WebSocket hello did not advertise a rules bundle");
        return false;
    }
    *bundle = payload.rulesBundle;
    return true;
}

bool waitForDisconnect(QWebSocket *socket, int timeoutMs)
{
    if (socket->state() != QAbstractSocket::ConnectedState)
        return true;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(socket, &QWebSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    return socket->state() != QAbstractSocket::ConnectedState;
}

bool runWebSocketHelloSignup(quint16 wsPort)
{
    QString error;
    QWebSocket socket;
    QByteArray helloFrame;
    QByteArray signupFrame;
    bool connected = false;
    QObject::connect(&socket, &QWebSocket::connected, [&]() { connected = true; });
    QObject::connect(&socket, &QWebSocket::textMessageReceived,
        [&](const QString &text) {
            const QByteArray utf8 = text.toUtf8();
            if (helloFrame.isEmpty())
                helloFrame = utf8;
            else if (signupFrame.isEmpty())
                signupFrame = utf8;
        });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(&socket, &QWebSocket::errorOccurred,
        [&](QAbstractSocket::SocketError) { error = socket.errorString(); });
#else
    QObject::connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
        [&](QAbstractSocket::SocketError) { error = socket.errorString(); });
#endif

    socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(wsPort)));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 15000 && helloFrame.isEmpty() && error.isEmpty())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!expect(connected, "WebSocket did not connect")
        || !expect(!helloFrame.isEmpty(), "WebSocket hello was not received")) {
        if (!error.isEmpty())
            qCritical().noquote() << error;
        return false;
    }
    if (!expect(!helloFrame.contains('\n') && !helloFrame.contains('\r'),
                "WebSocket hello included a newline delimiter"))
        return false;

    ProtocolMessage hello;
    if (!decodeMessage(helloFrame, &hello, &error))
        return expect(false, qPrintable(error));
    if (!expect(hello.command == S_COMMAND_CHECK_VERSION, "first WS frame was not hello")
        || !expect(hello.type == ProtocolMessageType::Notification,
                   "hello type mismatch"))
        return false;
    QJsonObject rulesBundle;
    if (!helloRulesBundle(hello, &rulesBundle, &error))
        return expect(false, qPrintable(error));

    // An expanded data tree (undeclared Lua, etc/ scenarios; the repository
    // checkout is one) cannot admit clients until it has a declared-v2 identity.
    // Framing is still checked on the refusal.
    const QString identityError = rulesBundle.value(QStringLiteral("error_code")).toString();
    const bool admissible = identityError.isEmpty();
    if (!admissible) {
        if (!expect(identityError == QLatin1String("rules_content_unsupported"),
                    "hello advertised an unexpected rules identity error"))
            return false;
        qInfo().noquote() << "[INFO] server content is not declared-v2;"
                          << "WebSocket signup is expected to be rejected";
    }

    const QByteArray signupRequest = encodeSignup(
        1, QStringLiteral("ws-gateway"), false, 0,
        admissible ? rulesBundle : QJsonObject(), &error);
    if (signupRequest.isEmpty())
        return expect(false, qPrintable(error));
    if (socket.sendTextMessage(QString::fromUtf8(signupRequest)) == 0)
        return expect(false, "failed to send WebSocket signup");

    while (timer.elapsed() < 15000 && signupFrame.isEmpty() && error.isEmpty())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!expect(signupFrame.size() > 0, "WebSocket signup reply was not received")) {
        if (!error.isEmpty())
            qCritical().noquote() << error;
        return false;
    }
    if (!expect(!signupFrame.contains('\n') && !signupFrame.contains('\r'),
                "WebSocket signup reply included a newline delimiter"))
        return false;

    ProtocolMessage reply;
    if (!decodeMessage(signupFrame, &reply, &error))
        return expect(false, qPrintable(error));
    if (!expect(reply.command == S_COMMAND_SIGNUP, "second WS frame was not signup")
        || !expect(reply.type == ProtocolMessageType::Reply, "signup reply type mismatch")
        || !expect(reply.replyTo == 1, "signup reply_to mismatch"))
        return false;
    SignupReplyPayload payload;
    if (!SignupReplyPayload::parse(reply.payload, &payload, &error))
        return expect(false, qPrintable(error));
    if (admissible) {
        if (!payload.accepted)
            qCritical().noquote() << "signup rejected:" << payload.errorCode << payload.message;
        if (!expect(payload.accepted, "signup was not accepted"))
            return false;
    } else if (!expect(!payload.accepted
                           && payload.errorCode == QLatin1String("rules_identity_required"),
                       "WebSocket signup to an undeclared-content server was not rejected")) {
        return false;
    }

    socket.close();
    return true;
}

bool runTcpNewlineHello(quint16 tcpPort)
{
    QString error;
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, tcpPort);
    if (!expect(socket.waitForConnected(5000), "TCP connect failed"))
        return false;
    QElapsedTimer timer;
    timer.start();
    QByteArray buffer;
    while (timer.elapsed() < 10000) {
        socket.waitForReadyRead(100);
        buffer += socket.readAll();
        if (buffer.contains('\n'))
            break;
    }
    if (!expect(buffer.contains('\n'), "TCP hello was not newline-framed"))
        return false;
    const QByteArray frame = buffer.left(buffer.indexOf('\n'));
    if (!expect(!frame.isEmpty(), "TCP hello frame was empty"))
        return false;
    ProtocolMessage hello;
    if (!decodeMessage(frame, &hello, &error))
        return expect(false, qPrintable(error));
    socket.disconnectFromHost();
    return expect(hello.command == S_COMMAND_CHECK_VERSION, "TCP first frame was not hello");
}

bool runBinaryFrameRejected(quint16 wsPort)
{
    QWebSocket socket;
    bool sawHello = false;
    QObject::connect(&socket, &QWebSocket::textMessageReceived, [&](const QString &) {
        sawHello = true;
    });
    socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(wsPort)));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000 && !sawHello)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (!expect(sawHello, "binary-reject case did not receive hello"))
        return false;
    socket.sendBinaryMessage(QByteArrayLiteral("not-json"));
    return expect(waitForDisconnect(&socket, 5000),
                  "server did not close after a binary WebSocket frame");
}

bool runSignupRoomIdPayloadContract()
{
    SignupRequestPayload omitted;
    omitted.screenName = QStringLiteral("native");
    omitted.avatar = QStringLiteral("caocao");
    const QVariantMap encoded = omitted.toVariant();
    if (!expect(encoded.value(QStringLiteral("schema_version")).toInt() == 2,
                "signup encode did not use schema 2")
        || !expect(!encoded.contains(QStringLiteral("room_id")),
                   "signup encode included room_id without hasRoomId"))
        return false;

    SignupRequestPayload parsed;
    QString error;
    if (!expect(SignupRequestPayload::parse(encoded, &parsed, &error),
                qPrintable(error))
        || !expect(!parsed.hasRoomId, "omitted room_id was treated as present"))
        return false;

    const QVariantMap schema1{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("reconnect_requested"), false},
        {QStringLiteral("screen_name"), QStringLiteral("legacy")},
        {QStringLiteral("avatar"), QStringLiteral("caocao")}
    };
    if (!expect(SignupRequestPayload::parse(schema1, &parsed, &error),
                qPrintable(error))
        || !expect(!parsed.hasRoomId, "schema 1 signup should omit room_id"))
        return false;

    QVariantMap schema1WithRoom = schema1;
    schema1WithRoom.insert(QStringLiteral("room_id"), 0);
    if (!expect(!SignupRequestPayload::parse(schema1WithRoom, &parsed, &error),
                "schema 1 signup accepted room_id"))
        return false;

    SignupRequestPayload targeted;
    targeted.screenName = QStringLiteral("web");
    targeted.avatar = QStringLiteral("caocao");
    targeted.hasRoomId = true;
    targeted.roomId = 0;
    if (!expect(SignupRequestPayload::parse(targeted.toVariant(), &parsed, &error),
                qPrintable(error))
        || !expect(parsed.hasRoomId && parsed.roomId == 0,
                   "schema 2 room_id 0 was not preserved"))
        return false;

    QVariantMap negative = targeted.toVariant();
    negative.insert(QStringLiteral("room_id"), -1);
    return expect(!SignupRequestPayload::parse(negative, &parsed, &error),
                  "negative room_id was accepted");
}

class WebSocketSignupClient
{
public:
    bool open(quint16 wsPort, QString *error)
    {
        QObject::connect(&socket, &QWebSocket::connected, [&]() { connected = true; });
        QObject::connect(&socket, &QWebSocket::textMessageReceived,
            [&](const QString &text) { frames.append(text.toUtf8()); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QObject::connect(&socket, &QWebSocket::errorOccurred,
            [&](QAbstractSocket::SocketError) { lastError = socket.errorString(); });
#else
        QObject::connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            [&](QAbstractSocket::SocketError) { lastError = socket.errorString(); });
#endif
        socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(wsPort)));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 15000 && frames.isEmpty() && lastError.isEmpty())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!connected) {
            *error = lastError.isEmpty() ? QStringLiteral("WebSocket did not connect") : lastError;
            return false;
        }
        if (frames.isEmpty()) {
            *error = QStringLiteral("WebSocket hello was not received");
            return false;
        }
        ProtocolMessage hello;
        if (!decodeMessage(frames.first(), &hello, error))
            return false;
        if (hello.command != S_COMMAND_CHECK_VERSION) {
            *error = QStringLiteral("first WS frame was not hello");
            return false;
        }
        return helloRulesBundle(hello, &rulesBundle, error);
    }

    bool signup(const QString &name, bool hasRoomId, int roomId,
                SignupReplyPayload *reply, QString *error, bool sendRulesBundle = true)
    {
        const int before = frames.size();
        const QByteArray request = encodeSignup(
            static_cast<quint64>(before + 1), name, hasRoomId, roomId,
            sendRulesBundle ? rulesBundle : QJsonObject(), error);
        if (request.isEmpty())
            return false;
        if (socket.sendTextMessage(QString::fromUtf8(request)) == 0) {
            *error = QStringLiteral("failed to send WebSocket signup");
            return false;
        }
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            for (int i = before; i < frames.size(); ++i) {
                ProtocolMessage message;
                if (!decodeMessage(frames.at(i), &message, error))
                    return false;
                if (message.command != S_COMMAND_SIGNUP
                    || message.type != ProtocolMessageType::Reply)
                    continue;
                return SignupReplyPayload::parse(message.payload, reply, error);
            }
        }
        *error = QStringLiteral("signup reply was not received");
        return false;
    }

    void close()
    {
        socket.close();
        waitForDisconnect(&socket, 5000);
    }

    QWebSocket socket;
    QList<QByteArray> frames;
    QString lastError;
    QJsonObject rulesBundle;
    bool connected = false;
};

// Native Protocol V2 admission carries the hello-matched local rules bundle;
// the separate WebSocket missing-bundle case above remains a rejection test.
class TcpSignupClient
{
public:
    bool open(quint16 tcpPort, QString *error)
    {
        socket.connectToHost(QHostAddress::LocalHost, tcpPort);
        if (!socket.waitForConnected(5000)) {
            *error = socket.errorString();
            return false;
        }
        ProtocolMessage hello;
        if (!waitFor(S_COMMAND_CHECK_VERSION, ProtocolMessageType::Notification, &hello, error))
            return false;
        if (!helloRulesBundle(hello, &rulesBundle, error))
            return false;
        // A checkout without declared-v2 content advertises an error object
        // in hello. Native TCP keeps its legacy admission contract there;
        // only an actual identity is echoed back to the server.
        if (rulesBundle.contains(QStringLiteral("error_code"))) {
            const QString identityError = rulesBundle.value(QStringLiteral("error_code")).toString();
            if (identityError != QLatin1String("rules_content_unsupported")) {
                *error = QStringLiteral("unexpected rules identity error: %1").arg(identityError);
                return false;
            }
            m_contentUnsupported = true;
            rulesBundle = QJsonObject();
        }
        return true;
    }

    bool contentUnsupported() const { return m_contentUnsupported; }

    bool signup(const QString &name, bool hasRoomId, int roomId,
                SignupReplyPayload *reply, QString *error)
    {
        QByteArray request = encodeSignup(1, name, hasRoomId, roomId, rulesBundle, error);
        if (request.isEmpty())
            return false;
        request.append('\n');
        if (socket.write(request) != request.size() || !socket.waitForBytesWritten(5000)) {
            *error = socket.errorString();
            return false;
        }
        ProtocolMessage message;
        if (!waitFor(S_COMMAND_SIGNUP, ProtocolMessageType::Reply, &message, error))
            return false;
        return SignupReplyPayload::parse(message.payload, reply, error);
    }

    void close()
    {
        socket.disconnectFromHost();
        if (socket.state() != QAbstractSocket::UnconnectedState)
            socket.waitForDisconnected(5000);
    }

private:
    bool waitFor(int command, ProtocolMessageType type, ProtocolMessage *result, QString *error)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 15000) {
            for (qsizetype i = 0; i < messages.size(); ++i) {
                if (messages.at(i).command == command && messages.at(i).type == type) {
                    *result = messages.takeAt(i);
                    return true;
                }
            }
            if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(50)) {
                if (socket.state() == QAbstractSocket::UnconnectedState) {
                    *error = QStringLiteral("TCP connection closed while waiting for a frame");
                    return false;
                }
                continue;
            }
            const ProtocolFrameAppendResult framed = frames.append(socket.readAll());
            if (!framed.success) {
                *error = framed.detail;
                return false;
            }
            for (const QByteArray &frame : framed.frames) {
                ProtocolMessage message;
                if (!decodeMessage(frame, &message, error))
                    return false;
                messages.append(message);
            }
        }
        *error = QStringLiteral("timed out waiting for a TCP frame");
        return false;
    }

    QTcpSocket socket;
    QJsonObject rulesBundle;
    bool m_contentUnsupported = false;
    ProtocolFrameBuffer frames;
    QList<ProtocolMessage> messages;
};

// The documented WebSocket admission contract: a signup without a rules
// bundle is rejected before any Room or player binding.
bool runWebSocketSignupRequiresRulesBundle(quint16 wsPort)
{
    QString error;
    WebSocketSignupClient client;
    SignupReplyPayload reply;
    if (!client.open(wsPort, &error))
        return expect(false, qPrintable(error));
    if (!client.signup(QStringLiteral("no-rules-bundle"), false, 0, &reply, &error, false))
        return expect(false, qPrintable(error));
    const bool rejected = expect(!reply.accepted
                                     && reply.errorCode == QLatin1String("rules_identity_required"),
                                 "WebSocket signup without a rules bundle was not rejected "
                                 "as rules_identity_required");
    client.close();
    return rejected;
}

bool runTcpSignupRoomId(quint16 tcpPort)
{
    QString error;
    TcpSignupClient first;
    SignupReplyPayload firstReply;
    if (!first.open(tcpPort, &error))
        return expect(false, qPrintable(error));
    if (!first.signup(QStringLiteral("room-host"), false, 0, &firstReply, &error))
        return expect(false, qPrintable(error));
    if (first.contentUnsupported()) {
        // Only an explicitly unsupported hello makes rejection the expected
        // result; an advertised identity must pass the room-id matrix below.
        return expect(!firstReply.accepted
                          && firstReply.errorCode == QLatin1String("rules_identity_required"),
                      "undeclared-v2 TCP signup was not rejected with rules_identity_required");
    }
    if (!expect(firstReply.accepted, "first signup without room_id was rejected")
        || !expect(firstReply.roomId == 0, "first signup reply room_id was not 0"))
        return false;

    TcpSignupClient second;
    SignupReplyPayload secondReply;
    if (!second.open(tcpPort, &error))
        return expect(false, qPrintable(error));
    if (!second.signup(QStringLiteral("room-guest"), true, 0, &secondReply, &error))
        return expect(false, qPrintable(error));
    if (!expect(secondReply.accepted, "signup with room_id 0 was rejected"))
        return false;

    TcpSignupClient missing;
    SignupReplyPayload missingReply;
    if (!missing.open(tcpPort, &error))
        return expect(false, qPrintable(error));
    if (!missing.signup(QStringLiteral("missing-room"), true, 99, &missingReply, &error))
        return expect(false, qPrintable(error));
    if (!expect(!missingReply.accepted
                && missingReply.errorCode == QLatin1String("room_not_found"),
                "unknown room_id was not rejected as room_not_found"))
        return false;
    missing.close();

    TcpSignupClient full;
    SignupReplyPayload fullReply;
    if (!full.open(tcpPort, &error))
        return expect(false, qPrintable(error));
    if (!full.signup(QStringLiteral("full-room"), true, 0, &fullReply, &error))
        return expect(false, qPrintable(error));
    if (!expect(!fullReply.accepted
                && fullReply.errorCode == QLatin1String("room_full"),
                "full room_id was not rejected as room_full"))
        return false;
    full.close();

    TcpSignupClient nextCurrent;
    SignupReplyPayload nextReply;
    if (!nextCurrent.open(tcpPort, &error))
        return expect(false, qPrintable(error));
    if (!nextCurrent.signup(QStringLiteral("next-host"), false, 0, &nextReply, &error))
        return expect(false, qPrintable(error));
    if (!expect(nextReply.accepted, "signup without room_id after a full current failed"))
        return false;

    TcpSignupClient joinNext;
    SignupReplyPayload joinReply;
    if (!joinNext.open(tcpPort, &error))
        return expect(false, qPrintable(error));
    if (!joinNext.signup(QStringLiteral("next-guest"), true, 1, &joinReply, &error))
        return expect(false, qPrintable(error));
    if (!expect(joinReply.accepted, "signup with room_id 1 was rejected")
        || !expect(joinReply.roomId == 1, "signup reply for room_id 1 did not echo 1"))
        return false;

    first.close();
    second.close();
    nextCurrent.close();
    joinNext.close();
    return true;
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QString serverPath;
    const QStringList arguments = application.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("--server") && i + 1 < arguments.size())
            serverPath = arguments.at(++i);
        else {
            qCritical().noquote() << "Unknown or incomplete argument:" << arguments.at(i);
            return 64;
        }
    }
    if (serverPath.isEmpty()) {
        qCritical().noquote() << "--server is required";
        return 64;
    }

    struct NamedCase
    {
        QString name;
        std::function<bool()> run;
    };
    QString error;
    LiveServer server;
    if (!server.start(serverPath, &error)) {
        qCritical().noquote() << error;
        return 1;
    }

    const QList<NamedCase> cases = {
        {QStringLiteral("signup-room-id-payload"),
         [&]() { return runSignupRoomIdPayloadContract(); }},
        // Keep the room_id matrix before any admissible WebSocket signup can
        // bind a player to the current room.
        {QStringLiteral("tcp-signup-room-id"),
         [&]() { return runTcpSignupRoomId(server.tcpPort()); }},
        {QStringLiteral("ws-hello-signup"),
         [&]() { return runWebSocketHelloSignup(server.wsPort()); }},
        {QStringLiteral("ws-signup-requires-rules-bundle"),
         [&]() { return runWebSocketSignupRequiresRulesBundle(server.wsPort()); }},
        {QStringLiteral("tcp-newline-hello"),
         [&]() { return runTcpNewlineHello(server.tcpPort()); }},
        {QStringLiteral("ws-binary-rejected"),
         [&]() { return runBinaryFrameRejected(server.wsPort()); }},
    };

    int passedCount = 0;
    for (const NamedCase &testCase : cases) {
        QElapsedTimer caseTimer;
        caseTimer.start();
        const bool passed = testCase.run();
        passedCount += passed ? 1 : 0;
        qInfo().noquote() << (passed ? QStringLiteral("[PASS]") : QStringLiteral("[FAIL]"))
                          << testCase.name << QStringLiteral("(%1 ms)").arg(caseTimer.elapsed());
    }
    qInfo().noquote() << QStringLiteral("\nTOTAL: %1\nPASS: %2\nFAIL: %3")
                             .arg(cases.size()).arg(passedCount).arg(cases.size() - passedCount);
    return passedCount == cases.size() ? 0 : 1;
}
