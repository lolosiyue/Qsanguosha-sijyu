#include "json.h"
#include "protocol.h"

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
constexpr int GameTimeoutMs = 240000;
constexpr int DisposalTimeoutMs = 20000;
constexpr int ShutdownTimeoutMs = 30000;

class WireClient
{
public:
    bool connectTo(quint16 port, QString &error)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);
        if (!m_socket.waitForConnected(HandshakeTimeoutMs)) {
            error = QStringLiteral("TCP connect failed: %1").arg(m_socket.errorString());
            return false;
        }
        return true;
    }

    bool handshake(QString &error)
    {
        Packet version;
        if (!waitForPacket([](const Packet &packet) {
                return packet.getCommandType() == S_COMMAND_CHECK_VERSION;
            }, version, HandshakeTimeoutMs, error)) {
            error.prepend(QStringLiteral("version handshake: "));
            return false;
        }
        if (!version.getMessageBody().canConvert<QString>()) {
            error = QStringLiteral("version handshake body is not a string");
            return false;
        }

        Packet setup;
        if (!waitForPacket([](const Packet &packet) {
                return packet.getCommandType() == S_COMMAND_SETUP;
            }, setup, HandshakeTimeoutMs, error)) {
            error.prepend(QStringLiteral("setup handshake: "));
            return false;
        }
        if (!setup.getMessageBody().canConvert<QString>()) {
            error = QStringLiteral("setup handshake body is not a string");
            return false;
        }
        return true;
    }

    bool signup(const QString &name, QString &playerId, bool &owner, QString &error)
    {
        JsonArray body;
        body << false << QString::fromLatin1(name.toUtf8().toBase64()) << QString();
        if (!sendNotification(S_COMMAND_SIGNUP, body, error))
            return false;

        Packet objectName;
        if (!waitForPacket([](const Packet &packet) {
                if (packet.getCommandType() != S_COMMAND_SET_PROPERTY)
                    return false;
                const JsonArray body = packet.getMessageBody().value<JsonArray>();
                return body.size() >= 3
                    && body.at(0).toString() == QLatin1String(S_PLAYER_SELF_REFERENCE_ID)
                    && body.at(1).toString() == QLatin1String("objectName");
            }, objectName, HandshakeTimeoutMs, error)) {
            error.prepend(QStringLiteral("signup identity: "));
            return false;
        }
        const JsonArray objectNameBody = objectName.getMessageBody().value<JsonArray>();
        playerId = objectNameBody.at(2).toString();
        if (playerId.isEmpty()) {
            error = QStringLiteral("signup returned an empty player ID");
            return false;
        }

        owner = false;
        Packet ownerPacket;
        QString ownerError;
        if (waitForPacket([](const Packet &packet) {
                if (packet.getCommandType() != S_COMMAND_SET_PROPERTY)
                    return false;
                const JsonArray body = packet.getMessageBody().value<JsonArray>();
                return body.size() >= 3
                    && body.at(0).toString() == QLatin1String(S_PLAYER_SELF_REFERENCE_ID)
                    && body.at(1).toString() == QLatin1String("owner");
            }, ownerPacket, 500, ownerError)) {
            owner = ownerPacket.getMessageBody().value<JsonArray>().at(2).toString()
                == QLatin1String("true");
        }
        return true;
    }

    bool enterTrust(const QString &playerId, QString &error)
    {
        if (!sendNotification(S_COMMAND_TRUST, playerId, error))
            return false;
        Packet statePacket;
        if (!waitForPacket([&playerId](const Packet &packet) {
                if (packet.getCommandType() != S_COMMAND_SET_PROPERTY)
                    return false;
                const JsonArray body = packet.getMessageBody().value<JsonArray>();
                return body.size() >= 3
                    && body.at(0).toString() == playerId
                    && body.at(1).toString() == QLatin1String("state")
                    && body.at(2).toString() == QLatin1String("trust");
            }, statePacket, HandshakeTimeoutMs, error)) {
            error.prepend(QStringLiteral("trust transition: "));
            return false;
        }
        return true;
    }

    bool ready(QString &error)
    {
        return sendNotification(S_COMMAND_TOGGLE_READY, QVariant(), error);
    }

    bool waitForCommand(CommandType command, int timeoutMs, QString &error)
    {
        Packet packet;
        return waitForPacket([command](const Packet &candidate) {
            return candidate.getCommandType() == command;
        }, packet, timeoutMs, error);
    }

    void pump()
    {
        readPackets(0);
    }

    bool disconnectCleanly()
    {
        if (m_socket.state() == QAbstractSocket::UnconnectedState)
            return true;
        m_socket.disconnectFromHost();
        return m_socket.waitForDisconnected(5000)
            || m_socket.state() == QAbstractSocket::UnconnectedState;
    }

private:
    bool sendNotification(CommandType command, const QVariant &body, QString &error)
    {
        Packet packet(S_SRC_CLIENT | S_TYPE_NOTIFICATION | S_DEST_ROOM, command);
        if (!body.isNull())
            packet.setMessageBody(body);
        QByteArray wire = packet.toJson();
        wire.append('\n');
        if (m_socket.write(wire) != wire.size() || !m_socket.waitForBytesWritten(5000)) {
            error = QStringLiteral("send command %1 failed: %2")
                .arg(int(command)).arg(m_socket.errorString());
            return false;
        }
        return true;
    }

    bool waitForPacket(const std::function<bool(const Packet &)> &predicate, Packet &result,
                       int timeoutMs, QString &error)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            for (qsizetype i = 0; i < m_packets.size(); ++i) {
                if (predicate(m_packets.at(i))) {
                    result = m_packets.takeAt(i);
                    return true;
                }
            }
            if (!readPackets(qMin(50, timeoutMs - int(timer.elapsed())))) {
                error = m_error;
                return false;
            }
            QCoreApplication::processEvents();
        }
        error = QStringLiteral("timed out after %1 ms").arg(timeoutMs);
        return false;
    }

    bool readPackets(int waitMs)
    {
        if (m_socket.bytesAvailable() == 0 && waitMs > 0) {
            if (!m_socket.waitForReadyRead(waitMs)) {
                if (m_socket.state() == QAbstractSocket::UnconnectedState) {
                    m_error = QStringLiteral("connection closed while waiting for a packet");
                    return false;
                }
                return true;
            }
        }

        m_buffer.append(m_socket.readAll());
        while (true) {
            const qsizetype newline = m_buffer.indexOf('\n');
            if (newline < 0)
                break;
            QByteArray line = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            if (line.isEmpty())
                continue;
            Packet packet;
            if (!packet.parse(line)) {
                m_error = QStringLiteral("invalid packet from server: %1")
                    .arg(QString::fromUtf8(line.left(200)));
                return false;
            }
            m_packets.append(packet);
        }
        return true;
    }

    QTcpSocket m_socket;
    QByteArray m_buffer;
    QList<Packet> m_packets;
    QString m_error;
};

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

    bool start(const QString &serverPath, QString &error)
    {
        if (!m_temporaryDirectory.isValid()) {
            error = QStringLiteral("unable to create temporary network-test directory");
            return false;
        }

        const QString xdgRoot = m_temporaryDirectory.filePath(QStringLiteral("xdg"));
        if (!QDir().mkpath(xdgRoot)) {
            error = QStringLiteral("unable to create XDG_CONFIG_HOME");
            return false;
        }
        m_configPath = m_temporaryDirectory.filePath(QStringLiteral("server.ini"));
        m_markerPath = m_temporaryDirectory.filePath(QStringLiteral("autotest.log"));
        QFile config(m_configPath);
        if (!config.open(QIODevice::WriteOnly | QIODevice::Text)) {
            error = QStringLiteral("unable to create test INI: %1").arg(config.errorString());
            return false;
        }
        config.write(
            "[General]\n"
            "GameMode=02p\n"
            "BindAddress=127.0.0.1\n"
            "ForbidSIMC=false\n"
            "OperationTimeout=1\n"
            "OperationNoLimit=false\n"
            "CountDownSeconds=0\n"
            "EnableAI=true\n"
            "AIHumanized=false\n"
            "OriginAIDelay=0\n"
            "EnableLuckCard=false\n"
            "DisableLua=false\n");
        config.close();

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), xdgRoot);
        m_process.setProcessEnvironment(environment);
        m_process.setProcessChannelMode(QProcess::MergedChannels);
        m_process.setWorkingDirectory(QDir::currentPath());
        m_process.setProgram(QFileInfo(serverPath).absoluteFilePath());
        m_process.setArguments({ QStringLiteral("--config"), m_configPath,
            QStringLiteral("--port"), QStringLiteral("0"),
            QStringLiteral("--ai-delay"), QStringLiteral("0"),
            QStringLiteral("--seed"), QStringLiteral("2026082501"),
            QStringLiteral("--autotest-log"), m_markerPath });
        m_process.start();
        if (!m_process.waitForStarted(10000)) {
            error = QStringLiteral("server process failed to start: %1")
                .arg(m_process.errorString());
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        const QRegularExpression endpointPattern(
            QStringLiteral("Listening on 127\\.0\\.0\\.1:(\\d+)"));
        while (timer.elapsed() < StartupTimeoutMs) {
            drain(100);
            const QRegularExpressionMatch match = endpointPattern.match(
                QString::fromUtf8(m_output));
            if (match.hasMatch()) {
                bool ok = false;
                const uint port = match.captured(1).toUInt(&ok);
                if (ok && port > 0 && port <= 65535) {
                    m_port = quint16(port);
                    return true;
                }
            }
            if (m_process.state() == QProcess::NotRunning)
                break;
        }
        drain(0);
        error = QStringLiteral("server did not reach a listening endpoint");
        return false;
    }

    quint16 port() const
    {
        return m_port;
    }

    bool isRunning() const
    {
        return m_process.state() != QProcess::NotRunning;
    }

    qsizetype outputMark()
    {
        drain(0);
        return m_output.size();
    }

    bool sendConsoleCommand(const QByteArray &command, QString &error)
    {
        QByteArray line = command;
        if (!line.endsWith('\n'))
            line.append('\n');
        if (m_process.write(line) != line.size() || !m_process.waitForBytesWritten(5000)) {
            error = QStringLiteral("console command failed: %1").arg(m_process.errorString());
            return false;
        }
        return true;
    }

    bool waitForOutput(const QByteArray &needle, qsizetype from, int timeoutMs,
                       const std::function<void()> &tick = {})
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            drain(50);
            if (m_output.indexOf(needle, from) >= 0)
                return true;
            if (tick)
                tick();
            QCoreApplication::processEvents();
            if (m_process.state() == QProcess::NotRunning)
                break;
        }
        drain(0);
        return m_output.indexOf(needle, from) >= 0;
    }

    bool waitForRoomDisposal(const std::function<void()> &tick, QString &error)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < DisposalTimeoutMs) {
            const qsizetype from = outputMark();
            if (!sendConsoleCommand(QByteArrayLiteral("rooms"), error))
                return false;
            if (waitForOutput(QByteArrayLiteral("No rooms."), from, 500, tick))
                return true;
        }
        error = QStringLiteral("room was not disposed within %1 ms").arg(DisposalTimeoutMs);
        return false;
    }

    bool shutdown(QString &error)
    {
        if (m_process.state() == QProcess::NotRunning) {
            error = QStringLiteral("server stopped before SIGTERM");
            return false;
        }
        m_process.terminate();
        QElapsedTimer timer;
        timer.start();
        while (m_process.state() != QProcess::NotRunning
               && timer.elapsed() < ShutdownTimeoutMs) {
            drain(100);
            QCoreApplication::processEvents();
        }
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(5000);
            drain(0);
            error = QStringLiteral("server did not exit after SIGTERM");
            return false;
        }
        drain(0);
        if (m_process.exitStatus() != QProcess::NormalExit || m_process.exitCode() != 0) {
            error = QStringLiteral("server exited abnormally: status=%1 code=%2")
                .arg(int(m_process.exitStatus())).arg(m_process.exitCode());
            return false;
        }
        if (!m_output.contains("Shutdown requested by signal 15")) {
            error = QStringLiteral("server did not acknowledge SIGTERM");
            return false;
        }
        return true;
    }

    QByteArray output() const
    {
        return m_output;
    }

    QByteArray outputTail(int maximumBytes = 16000) const
    {
        return m_output.right(maximumBytes);
    }

    QByteArray markerLog() const
    {
        QFile file(m_markerPath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private:
    void drain(int waitMs)
    {
        if (m_process.bytesAvailable() == 0 && waitMs > 0)
            m_process.waitForReadyRead(waitMs);
        m_output.append(m_process.readAll());
    }

    QTemporaryDir m_temporaryDirectory;
    QProcess m_process;
    QByteArray m_output;
    QString m_configPath;
    QString m_markerPath;
    quint16 m_port = 0;
};

bool startServer(ServerRunner &server, const QString &serverPath, QString &error)
{
    if (serverPath.isEmpty()) {
        error = QStringLiteral("--server is required");
        return false;
    }
    if (!QFileInfo::exists(serverPath)) {
        error = QStringLiteral("server executable does not exist: %1").arg(serverPath);
        return false;
    }
    return server.start(serverPath, error);
}

bool runLevel1(const QString &serverPath, ServerRunner &server, QString &error)
{
    if (!startServer(server, serverPath, error))
        return false;

    WireClient client;
    if (!client.connectTo(server.port(), error))
        return false;
    if (!client.disconnectCleanly()) {
        error = QStringLiteral("TCP client did not disconnect cleanly");
        return false;
    }

    const qsizetype statusMark = server.outputMark();
    if (!server.sendConsoleCommand(QByteArrayLiteral("status"), error))
        return false;
    if (!server.waitForOutput(QByteArrayLiteral("Games running:"), statusMark, 5000)) {
        error = QStringLiteral("server stopped responding after TCP disconnect");
        return false;
    }
    return server.shutdown(error);
}

bool runLevel2(const QString &serverPath, ServerRunner &server, QString &error)
{
    if (!startServer(server, serverPath, error))
        return false;

    WireClient client;
    if (!client.connectTo(server.port(), error) || !client.handshake(error))
        return false;

    const QString playerName = QStringLiteral("network-level2-player");
    QString playerId;
    bool owner = false;
    if (!client.signup(playerName, playerId, owner, error))
        return false;
    if (!owner) {
        error = QStringLiteral("first signed-up player was not assigned room ownership");
        return false;
    }

    const qsizetype playersMark = server.outputMark();
    if (!server.sendConsoleCommand(QByteArrayLiteral("players"), error))
        return false;
    if (!server.waitForOutput(playerName.toUtf8(), playersMark, 5000)) {
        error = QStringLiteral("server players snapshot did not recognize signed-up player %1")
            .arg(playerId);
        return false;
    }
    const QByteArray playersOutput = server.output().mid(playersMark);
    if (!playersOutput.contains(playerId.toUtf8())) {
        error = QStringLiteral("players snapshot omitted player ID %1").arg(playerId);
        return false;
    }

    if (!client.disconnectCleanly()) {
        error = QStringLiteral("signed-up client did not disconnect cleanly");
        return false;
    }
    const qsizetype emptyMark = server.outputMark();
    if (!server.sendConsoleCommand(QByteArrayLiteral("players"), error))
        return false;
    if (!server.waitForOutput(QByteArrayLiteral("No players connected."), emptyMark, 5000)) {
        error = QStringLiteral("server did not remove disconnected waiting-room player");
        return false;
    }
    return server.shutdown(error);
}

bool runLevel3(const QString &serverPath, ServerRunner &server, QString &error)
{
    if (!startServer(server, serverPath, error))
        return false;

    WireClient first;
    WireClient second;
    QString firstId;
    QString secondId;
    bool firstOwner = false;
    bool secondOwner = false;
    if (!first.connectTo(server.port(), error) || !first.handshake(error)
        || !first.signup(QStringLiteral("network-e2e-a"), firstId, firstOwner, error)) {
        return false;
    }
    if (!second.connectTo(server.port(), error) || !second.handshake(error)
        || !second.signup(QStringLiteral("network-e2e-b"), secondId, secondOwner, error)) {
        return false;
    }
    if (!firstOwner || secondOwner) {
        error = QStringLiteral("unexpected room ownership: first=%1 second=%2")
            .arg(firstOwner).arg(secondOwner);
        return false;
    }
    const auto pumpClients = [&first, &second]() {
        first.pump();
        second.pump();
    };
    const qsizetype gameMark = server.outputMark();
    if (!first.ready(error))
        return false;
    if (!server.waitForOutput(QByteArrayLiteral(" game_started "), gameMark,
                              StartupTimeoutMs, pumpClients)) {
        error = QStringLiteral("filled network room did not start");
        return false;
    }
    if (!first.waitForCommand(S_COMMAND_GAME_START, HandshakeTimeoutMs, error)
        || !second.waitForCommand(S_COMMAND_GAME_START, HandshakeTimeoutMs, error)) {
        error.prepend(QStringLiteral("client did not observe game start: "));
        return false;
    }
    if (!first.enterTrust(firstId, error) || !second.enterTrust(secondId, error))
        return false;

    if (!server.waitForOutput(QByteArrayLiteral(" game_over "), gameMark,
                              GameTimeoutMs, pumpClients)) {
        error = QStringLiteral("automated network game did not finish within %1 ms")
            .arg(GameTimeoutMs);
        return false;
    }
    if (!first.waitForCommand(S_COMMAND_GAME_OVER, HandshakeTimeoutMs, error)
        || !second.waitForCommand(S_COMMAND_GAME_OVER, HandshakeTimeoutMs, error)) {
        error.prepend(QStringLiteral("client did not observe game over: "));
        return false;
    }
    if (!server.waitForRoomDisposal(pumpClients, error))
        return false;

    if (!first.disconnectCleanly() || !second.disconnectCleanly()) {
        error = QStringLiteral("network clients did not reach a clean disconnected state");
        return false;
    }
    if (!server.shutdown(error))
        return false;

    const QByteArray markers = server.markerLog();
    if (!markers.contains("[AUTOTEST] game start")
        || !markers.contains("[AUTOTEST] game over ")) {
        error = QStringLiteral("autotest marker log omitted the network game lifecycle");
        return false;
    }
    if (!server.output().contains("CARD_LIFETIME_ZERO")) {
        error = QStringLiteral("clean shutdown did not reach the zero card-lifetime gauge");
        return false;
    }
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    int level = 0;
    QString serverPath;
    for (int i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1String("--level") && i + 1 < arguments.size()) {
            level = arguments.at(++i).toInt();
        } else if (arguments.at(i) == QLatin1String("--server") && i + 1 < arguments.size()) {
            serverPath = arguments.at(++i);
        } else {
            QTextStream(stderr) << "Unknown or incomplete argument: " << arguments.at(i)
                                << Qt::endl;
            return 2;
        }
    }
    if (level < 1 || level > 3 || serverPath.isEmpty()) {
        QTextStream(stderr) << "Usage: " << arguments.constFirst()
                            << " --level <1|2|3> --server <qsanguosha_server>"
                            << Qt::endl;
        return 2;
    }

    const QString gameStatePath = QDir::current().filePath(QStringLiteral("g.json"));
    const bool gameStatePreexisted = QFileInfo::exists(gameStatePath);
    ServerRunner server;
    QString error;
    bool passed = false;
    if (level == 1)
        passed = runLevel1(serverPath, server, error);
    else if (level == 2)
        passed = runLevel2(serverPath, server, error);
    else
        passed = runLevel3(serverPath, server, error);

    if (!gameStatePreexisted)
        QFile::remove(gameStatePath);
    if (!passed) {
        QTextStream err(stderr);
        err << "[network-level" << level << "] " << error << Qt::endl;
        const QByteArray tail = server.outputTail();
        if (!tail.isEmpty())
            err << "--- server output tail ---\n" << QString::fromUtf8(tail) << Qt::endl;
        return 1;
    }

    QTextStream(stdout) << "[network-level" << level << "] passed" << Qt::endl;
    return 0;
}
