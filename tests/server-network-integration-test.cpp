#include "protocol.h"
#include "protocol/protocol-runtime.h"
#include "protocol/session/session-payloads.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>

#include <functional>

using namespace QSanProtocol;

namespace
{
constexpr int StartupTimeoutMs = 60000;
constexpr int HandshakeTimeoutMs = 10000;

class ServerRunner
{
public:
    ~ServerRunner()
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
                                QStringLiteral("--seed"), QStringLiteral("2026083001")});
        m_process.start();
        if (!m_process.waitForStarted(10000)) {
            *error = m_process.errorString();
            return false;
        }

        const QRegularExpression endpoint(
            QStringLiteral("Listening on 127\\.0\\.0\\.1:(\\d+)"));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < StartupTimeoutMs) {
            drain(100);
            const QRegularExpressionMatch match = endpoint.match(QString::fromUtf8(m_output));
            if (match.hasMatch()) {
                bool ok = false;
                const uint value = match.captured(1).toUInt(&ok);
                if (ok && value > 0 && value <= 65535) {
                    m_port = static_cast<quint16>(value);
                    return true;
                }
            }
            if (m_process.state() == QProcess::NotRunning)
                break;
        }
        *error = QStringLiteral("server did not publish a listening endpoint\n%1")
            .arg(QString::fromUtf8(m_output.right(8000)));
        return false;
    }

    quint16 port() const { return m_port; }

    bool shutdown(QString *error)
    {
        if (m_process.state() == QProcess::NotRunning)
            return true;
        m_process.terminate();
        if (!m_process.waitForFinished(30000)) {
            *error = QStringLiteral("server did not stop after terminate");
            return false;
        }
        drain(0);
        if (m_process.exitStatus() != QProcess::NormalExit || m_process.exitCode() != 0) {
            *error = QStringLiteral("server exited abnormally: %1\n%2")
                .arg(m_process.exitCode())
                .arg(QString::fromUtf8(m_output.right(8000)));
            return false;
        }
        return true;
    }

private:
    void drain(int waitMs)
    {
        if (m_process.bytesAvailable() == 0 && waitMs > 0)
            m_process.waitForReadyRead(waitMs);
        m_output.append(m_process.readAll());
    }

    QTemporaryDir m_directory;
    QProcess m_process;
    QByteArray m_output;
    quint16 m_port = 0;
};

class WireClient
{
public:
    bool connectAndAcceptHello(quint16 port, QString *error)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        if (!m_socket.waitForConnected(HandshakeTimeoutMs)) {
            *error = m_socket.errorString();
            return false;
        }

        ProtocolMessage hello;
        if (!waitFor([](const ProtocolMessage &candidate) {
                return candidate.command == S_COMMAND_CHECK_VERSION;
            }, &hello, error)) {
            return false;
        }
        if (hello.type != ProtocolMessageType::Notification
            || hello.source != ProtocolEndpoint::Lobby
            || hello.destination != ProtocolEndpoint::Client
            || hello.messageId == 0) {
            *error = QStringLiteral("first server frame is not typed SERVER_HELLO");
            return false;
        }
        ServerHelloPayload payload;
        if (!ServerHelloPayload::parse(hello.payload, &payload, error))
            return false;
        m_lastIncomingId = hello.messageId;
        return true;
    }

    bool signup(const QString &name, bool reconnectRequested,
                SignupReplyPayload *result, QString *error)
    {
        SignupRequestPayload payload;
        payload.reconnectRequested = reconnectRequested;
        payload.screenName = name;
        payload.avatar = QString();
        ProtocolMessage signup;
        signup.type = ProtocolMessageType::Request;
        signup.source = ProtocolEndpoint::Client;
        signup.destination = ProtocolEndpoint::Lobby;
        signup.messageId = m_outgoingIds.next();
        signup.command = S_COMMAND_SIGNUP;
        signup.hasPayload = true;
        signup.payload = payload.toVariant();
        if (!send(signup, error))
            return false;

        ProtocolMessage reply;
        if (!waitFor([](const ProtocolMessage &candidate) {
                return candidate.command == S_COMMAND_SIGNUP
                    && candidate.type == ProtocolMessageType::Reply;
            }, &reply, error)) {
            return false;
        }
        if (reply.replyTo != signup.messageId
            || !SignupReplyPayload::parse(reply.payload, result, error)) {
            if (error->isEmpty())
                *error = QStringLiteral("SIGNUP reply is not correlated");
            return false;
        }
        if (!result->accepted)
            return true;

        ProtocolMessage setup;
        if (!waitFor([](const ProtocolMessage &candidate) {
                return candidate.command == S_COMMAND_SETUP;
            }, &setup, error)) {
            return false;
        }
        SetupPayload setupPayload;
        if (setup.type != ProtocolMessageType::Notification
            || setup.source != ProtocolEndpoint::Lobby
            || !SetupPayload::parse(setup.payload, &setupPayload, error)) {
            return false;
        }

        ReadyPayload readyPayload;
        ProtocolMessage ready;
        ready.type = ProtocolMessageType::Notification;
        ready.source = ProtocolEndpoint::Client;
        ready.destination = ProtocolEndpoint::Room;
        ready.messageId = m_outgoingIds.next();
        ready.command = S_COMMAND_READY;
        ready.hasPayload = true;
        ready.payload = readyPayload.toVariant();
        return send(ready, error);
    }

    bool sendLegacyFrameAndExpectClose(QString *error)
    {
        const QByteArray legacy("[1,0,260,1]\n");
        if (m_socket.write(legacy) != legacy.size() || !m_socket.waitForBytesWritten(5000)) {
            *error = m_socket.errorString();
            return false;
        }
        if (m_socket.waitForDisconnected(HandshakeTimeoutMs)
            || m_socket.state() == QAbstractSocket::UnconnectedState) {
            return true;
        }
        *error = QStringLiteral("server did not reject a Protocol V1 frame");
        return false;
    }

    void disconnect()
    {
        m_socket.disconnectFromHost();
        m_socket.waitForDisconnected(5000);
    }

private:
    bool send(const ProtocolMessage &message, QString *error)
    {
        QString encodeError;
        QByteArray wire = m_router.encode(message, &encodeError);
        if (wire.isEmpty()) {
            *error = encodeError;
            return false;
        }
        wire.append('\n');
        if (m_socket.write(wire) != wire.size() || !m_socket.waitForBytesWritten(5000)) {
            *error = m_socket.errorString();
            return false;
        }
        return true;
    }

    bool waitFor(const std::function<bool(const ProtocolMessage &)> &predicate,
                 ProtocolMessage *result, QString *error)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < HandshakeTimeoutMs) {
            for (qsizetype index = 0; index < m_messages.size(); ++index) {
                if (predicate(m_messages.at(index))) {
                    *result = m_messages.takeAt(index);
                    return true;
                }
            }
            if (!read(qMin(50, HandshakeTimeoutMs - static_cast<int>(timer.elapsed())), error))
                return false;
            QCoreApplication::processEvents();
        }
        *error = QStringLiteral("timed out waiting for Protocol V2 frame");
        return false;
    }

    bool read(int waitMs, QString *error)
    {
        if (m_socket.bytesAvailable() == 0 && waitMs > 0
            && !m_socket.waitForReadyRead(waitMs)) {
            if (m_socket.state() == QAbstractSocket::UnconnectedState) {
                *error = QStringLiteral("connection closed while waiting for Protocol V2");
                return false;
            }
            return true;
        }
        const ProtocolFrameAppendResult framed = m_frames.append(m_socket.readAll());
        if (!framed.success) {
            *error = framed.detail;
            return false;
        }
        for (const QByteArray &frame : framed.frames) {
            ProtocolMessage message;
            const ProtocolDecodeResult decoded = m_router.decode(frame, &message);
            if (!decoded.success) {
                *error = decoded.detail;
                return false;
            }
            if (message.messageId <= m_lastIncomingId) {
                *error = QStringLiteral("server message_id is not monotonic");
                return false;
            }
            m_lastIncomingId = message.messageId;
            m_messages.append(message);
        }
        return true;
    }

    QTcpSocket m_socket;
    ProtocolCodecRouter m_router;
    ProtocolFrameBuffer m_frames;
    ProtocolMessageIdGenerator m_outgoingIds;
    QList<ProtocolMessage> m_messages;
    quint64 m_lastIncomingId = 0;
};

bool runContract(const QString &serverPath, QString *error)
{
    ServerRunner server;
    if (!server.start(serverPath, error))
        return false;

    WireClient primary;
    SignupReplyPayload primaryReply;
    if (!primary.connectAndAcceptHello(server.port(), error)
        || !primary.signup(QStringLiteral("protocol-v2-live"), false,
                           &primaryReply, error)
        || !primaryReply.accepted || primaryReply.reconnected
        || primaryReply.playerId.isEmpty()) {
        if (error->isEmpty())
            *error = QStringLiteral("initial V2 signup contract failed");
        return false;
    }

    WireClient duplicate;
    SignupReplyPayload duplicateReply;
    if (!duplicate.connectAndAcceptHello(server.port(), error)
        || !duplicate.signup(QStringLiteral("protocol-v2-live"), false,
                             &duplicateReply, error)
        || duplicateReply.accepted || duplicateReply.errorCode.isEmpty()) {
        if (error->isEmpty())
            *error = QStringLiteral("duplicate signup was not rejected transactionally");
        return false;
    }
    duplicate.disconnect();
    primary.disconnect();

    WireClient reconnect;
    SignupReplyPayload reconnectReply;
    if (!reconnect.connectAndAcceptHello(server.port(), error)
        || !reconnect.signup(QStringLiteral("protocol-v2-live"), true,
                             &reconnectReply, error)
        || !reconnectReply.accepted || !reconnectReply.reconnected
        || reconnectReply.playerId != primaryReply.playerId) {
        if (error->isEmpty())
            *error = QStringLiteral("reconnect did not preserve player identity");
        return false;
    }
    reconnect.disconnect();

    WireClient legacy;
    if (!legacy.connectAndAcceptHello(server.port(), error)
        || !legacy.sendLegacyFrameAndExpectClose(error)) {
        return false;
    }
    return server.shutdown(error);
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    QString serverPath;
    for (int index = 1; index < arguments.size(); ++index) {
        if (arguments.at(index) == QLatin1String("--server")
            && index + 1 < arguments.size()) {
            serverPath = arguments.at(++index);
        } else if (arguments.at(index) == QLatin1String("--level")
                   && index + 1 < arguments.size()) {
            ++index;
        } else {
            QTextStream(stderr) << "Unknown argument: " << arguments.at(index) << '\n';
            return 2;
        }
    }
    if (serverPath.isEmpty()) {
        QTextStream(stderr) << "--server is required\n";
        return 2;
    }

    QString error;
    const bool success = runContract(serverPath, &error);
    QTextStream(success ? stdout : stderr)
        << "[AUTOTEST] PROTOCOL_V2_LIVE_TCP_RESULT status="
        << (success ? "PASS" : "FAIL")
        << (error.isEmpty() ? QString() : QStringLiteral(" detail=") + error)
        << '\n';
    return success ? 0 : 1;
}
