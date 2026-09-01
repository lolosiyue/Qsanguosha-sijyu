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
        while (timer.elapsed() < 20000) {
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
                    return true;
                }
            }
            if (m_process.state() == QProcess::NotRunning)
                break;
        }
        *error = QStringLiteral("server did not publish TCP and WebSocket endpoints\n%1")
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

QByteArray encodeSignup(quint64 messageId, QString *error)
{
    SignupRequestPayload request;
    request.screenName = QStringLiteral("ws-gateway");
    request.avatar = QStringLiteral("caocao");
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

    const QByteArray signupRequest = encodeSignup(1, &error);
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
    if (!expect(payload.accepted, "signup was not accepted"))
        return false;

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
        {QStringLiteral("ws-hello-signup"),
         [&]() { return runWebSocketHelloSignup(server.wsPort()); }},
        {QStringLiteral("tcp-newline-hello"),
         [&]() { return runTcpNewlineHello(server.tcpPort()); }},
        {QStringLiteral("ws-binary-rejected"),
         [&]() { return runBinaryFrameRejected(server.wsPort()); }},
    };

    int passedCount = 0;
    for (const NamedCase &testCase : cases) {
        const bool passed = testCase.run();
        passedCount += passed ? 1 : 0;
        qInfo().noquote() << (passed ? QStringLiteral("[PASS]") : QStringLiteral("[FAIL]"))
                          << testCase.name;
    }
    qInfo().noquote() << QStringLiteral("\nTOTAL: %1\nPASS: %2\nFAIL: %3")
                             .arg(cases.size()).arg(passedCount).arg(cases.size() - passedCount);
    return passedCount == cases.size() ? 0 : 1;
}
