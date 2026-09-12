#include "solo-server-host.h"

#include "engine-bootstrap.h"
#include "engine.h"
#include "general.h"
#include "package/package.h"
#include "room.h"
#include "runtime-paths.h"
#include "server-core.h"
#include "settings.h"
#include "socket.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QThread>
#include <atomic>
#include <cmath>
#include <functional>
#include <utility>

namespace {
constexpr qint64 MaxFrameSize = 4 * 1024 * 1024;

class LocalClientSocket final : public ClientSocket
{
public:
    explicit LocalClientSocket(QObject *parent, std::function<void(const QByteArray &)> sender)
        : m_sender(std::move(sender))
    {
        setParent(parent);
        timerSignup.setSingleShot(true);
    }

    void connectToHost() override { emit connected(); }
    void send(const QByteArray &message) override
    {
        if (!m_connected.load())
            return;
        // RoomThread may send concurrently. Only the socket owner touches the
        // host's output queue; queued delivery also avoids protocol reentrancy.
        QMetaObject::invokeMethod(this, [this, message]() {
            // Frames accepted before disconnect (especially GAME_OVER and
            // diagnostics) must still reach the browser before its close event.
            m_sender(message);
        }, Qt::QueuedConnection);
    }
    bool isConnected() const override { return m_connected.load(); }
    QString peerName() const override { return QStringLiteral("in-process-solo"); }
    QString peerAddress() const override { return QStringLiteral("127.0.0.1"); }
    bool requiresRulesBundle() const override { return true; }

    void disconnectFromHost() override
    {
        if (QThread::currentThread() != thread()) {
            QMetaObject::invokeMethod(this, [this]() { disconnectFromHost(); }, Qt::QueuedConnection);
            return;
        }
        if (m_connected.exchange(false)) {
            timerSignup.stop();
            emit disconnected();
        }
    }

    void deliver(const QByteArray &message)
    {
        QMetaObject::invokeMethod(this, [this, message]() {
            if (m_connected.load())
                emit message_got(message);
        }, Qt::QueuedConnection);
    }

private:
    const std::function<void(const QByteArray &)> m_sender;
    std::atomic<bool> m_connected { true };
};

class LocalServerSocket final : public ServerSocket
{
public:
    bool listen() override { return true; }
    void daemonize() override {}
    QString listeningAddress() const override { return QStringLiteral("in-process"); }
    quint16 listeningPort() const override { return 0; }
    void accept(ClientSocket *socket) { emit new_connection(socket); }
};

QJsonObject readObject(const QString &path, QString *error)
{
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot_read_command");
        return {};
    }
    const QByteArray bytes = input.read(MaxFrameSize + 1);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (bytes.size() > MaxFrameSize || parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("invalid_command_json");
        return {};
    }
    return document.object();
}

bool boundedInteger(const QJsonValue &value, int maximum)
{
    const double number = value.toDouble(-1);
    return value.isDouble() && std::isfinite(number) && number >= 0
        && number <= maximum && number == std::floor(number);
}

bool selectablePackage(const Package *package)
{
    // Scenario/special-mode packages are selected by the native mode itself.
    return !package->isForbid() && !package->inherits("Scenario")
        && !Sanguosha->getPackageMap().value(QStringLiteral("g_special_play")).contains(package->adderName());
}
}

SoloServerHost::SoloServerHost(const QString &assets, const QString &work, const QString &userData)
    : m_assets(assets), m_work(work), m_userData(userData)
{
}

// This host belongs to one disposable Worker/heap. JS stops all pthreads before
// discarding it; native objects must never be destructed under running threads.
SoloServerHost::~SoloServerHost() = default;

QString SoloServerHost::file(const char *name) const
{
    return QDir(m_work).filePath(QString::fromLatin1(name));
}

int SoloServerHost::writeResult(const QJsonObject &result, int status)
{
    QSaveFile output(file("solo-result.json"));
    const QByteArray bytes = QJsonDocument(result).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return 5;
    return status;
}

int SoloServerHost::writeError(const QString &error, int status)
{
    m_error = error;
    return writeResult({{"schema_version", 1}, {"phase", "failed"}, {"error", error}}, status);
}

int SoloServerHost::reportFailure(const QString &message)
{
    return writeError(message, 3);
}

int SoloServerHost::initialize()
{
    if (m_initialized)
        return writeError(QStringLiteral("solo_runtime_already_initialized"), 2);
    if (QCoreApplication::instance() || Sanguosha)
        return writeError(QStringLiteral("runtime_already_owned"), 3);

    static int argc = 1;
    static char name[] = "qsanguosha_solo";
    static char *argv[] = {name, nullptr};
    new QCoreApplication(argc, argv);
    qputenv("QSAN_ASSET_ROOT", m_assets.toUtf8());
    qputenv("QSAN_USER_DATA_ROOT", m_userData.toUtf8());
    QString error;
    if (!QSanRuntimePaths::resolve(QCoreApplication::arguments(), &error)
        || !EngineBootstrap::initialize(false, &error) || !EngineBootstrap::hasLuaState())
        return writeError(error.isEmpty() ? QStringLiteral("engine_initialization_failed") : error, 3);

    // Match the native entry point: Lua registration precedes settings, whose
    // defaults include package policy, timeouts and mode-specific bans.
    Config.init();
    m_initialized = true;
    QJsonArray modes, packages, generals;
    const auto available = Sanguosha->getAvailableModes();
    for (auto it = available.cbegin(); it != available.cend(); ++it)
        modes.append(QJsonObject{{"id", it.key()}, {"name", it.value().display_name},
            {"player_count", it.value().player_count}});
    for (const Package *package : Sanguosha->getPackages())
        packages.append(QJsonObject{{"id", package->objectName()},
            {"name", Sanguosha->translate(package->objectName())},
            {"enabled", Config.EnabledPackages.contains(package->objectName())},
            {"forbidden", !selectablePackage(package)}});
    for (const General *general : Sanguosha->getAllGenerals()) {
        if (!general->isTotallyHidden())
            generals.append(QJsonObject{{"id", general->objectName()}, {"name", general->getBriefName()},
                {"package", general->getPackage()}});
    }
    return writeResult({{"schema_version", 1}, {"modes", modes}, {"packages", packages},
        {"generals", generals}, {"defaults", QJsonObject{
            {"mode", Config.GameMode.mode_id}, {"enabled_packages", QJsonArray::fromStringList(Config.EnabledPackages)},
            {"ban_generals", QJsonArray()}, {"operation_timeout", Config.OperationNoLimit ? 0 : Config.OperationTimeout},
            {"ai_delay", Config.AIDelay}}}});
}

int SoloServerHost::start()
{
    if (!m_initialized || m_started || m_stopping || m_closed)
        return writeError(QStringLiteral("solo_start_invalid_phase"), 2);
    QString error;
    const QJsonObject command = readObject(file("solo-command.json"), &error);
    if (!error.isEmpty())
        return writeError(error, 2);
    const QString mode = command.value("mode").toString();
    const auto selected = Sanguosha->getGameMode(mode);
    if (!selected.isValid() || !Sanguosha->getAvailableModes().contains(mode)
        || !command.value("enabled_packages").isArray() || !command.value("ban_generals").isArray()
        || !boundedInteger(command.value("operation_timeout"), 3600)
        || !boundedInteger(command.value("ai_delay"), 60000))
        return writeError(QStringLiteral("invalid_solo_command"), 2);

    // Validate the entire command before mutating the native global settings.
    QSet<QString> selectable;
    for (const Package *package : Sanguosha->getPackages()) {
        if (selectablePackage(package))
            selectable.insert(package->objectName());
    }
    QStringList enabled, banned;
    for (const QJsonValue &value : command.value("enabled_packages").toArray()) {
        if (!value.isString() || !selectable.contains(value.toString()) || enabled.contains(value.toString()))
            return writeError(QStringLiteral("invalid_package_selection"), 2);
        enabled.append(value.toString());
    }
    for (const QJsonValue &value : command.value("ban_generals").toArray()) {
        if (!value.isString() || !Sanguosha->getGeneral(value.toString()) || banned.contains(value.toString()))
            return writeError(QStringLiteral("invalid_general_selection"), 2);
        banned.append(value.toString());
    }

    Config.GameMode = selected;
    Config.EnableAI = true;
    Config.OperationTimeout = command.value("operation_timeout").toInt();
    Config.OperationNoLimit = Config.OperationTimeout == 0;
    Config.AIDelay = Config.OriginAIDelay = command.value("ai_delay").toInt();
    Config.EnabledPackages = enabled;
    Config.BanPackages.clear();
    for (const Package *package : Sanguosha->getPackages()) {
        if (!enabled.contains(package->objectName()))
            Config.BanPackages.append(package->objectName());
    }
    // User bans augment each mode's native policy instead of erasing defaults.
    for (const char *key : {"Roles", "Doudizhu", "Happy2v2", "1v1", "BossMode",
                           "05_ol", "06_ol", "Basara", "Hegemony"}) {
        const QString setting = QStringLiteral("Banlist/") + QString::fromLatin1(key);
        QStringList values = Config.value(setting).toStringList();
        values.append(banned);
        values.removeDuplicates();
        Config.setValue(setting, values);
    }

    auto *transport = new LocalServerSocket;
    m_transport = transport;
    m_server = new Server(nullptr, GameSessionConfig(), Server::InitialRoomPolicy::Deferred, transport);
    QObject::connect(m_server, &Server::initialRoomReady, m_server, [this]() {
        m_preparing = false;
        if (m_stopping) {
            for (Room *room : m_server->findChildren<Room *>())
                room->abortWaitingRequests();
            return;
        }
        auto *socket = new LocalClientSocket(m_server, [this](const QByteArray &frame) {
            m_frames.append(frame);
        });
        m_client = socket;
        QObject::connect(&socket->timerSignup, &QTimer::timeout, socket, [socket]() {
            socket->disconnectFromHost();
        });
        QObject::connect(socket, &ClientSocket::disconnected, m_server, [this]() {
            if (!m_stopping)
                m_closed = true;
        });
        static_cast<LocalServerSocket *>(m_transport.data())->accept(socket);
    });
    QObject::connect(m_server, &Server::initialRoomFailed, m_server, [this](const QString &detail) {
        m_preparing = false;
        m_error = detail.isEmpty() ? QStringLiteral("room_initialization_failed") : detail;
    });
    m_preparing = true;
    if (!m_server->prepareInitialRoomAsync(&error)) {
        m_preparing = false;
        return writeError(error, 3);
    }
    m_started = true;
    return writeResult({{"schema_version", 1}, {"phase", "preparing"}});
}

int SoloServerHost::frame()
{
    if (!m_started || m_stopping || !m_client || !m_client->isConnected())
        return writeError(QStringLiteral("solo_not_started"), 2);
    QFile input(file("solo-frame.txt"));
    if (!input.open(QIODevice::ReadOnly))
        return writeError(QStringLiteral("cannot_read_frame"), 2);
    const QByteArray bytes = input.read(MaxFrameSize + 1);
    if (bytes.isEmpty() || bytes.size() > MaxFrameSize)
        return writeError(QStringLiteral("invalid_frame"), 2);
    static_cast<LocalClientSocket *>(m_client.data())->deliver(bytes);
    // Only pump consumes output. An input acknowledgment must not lose frames.
    return writeResult({{"schema_version", 1}, {"phase", "active"}});
}

int SoloServerHost::pump()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (!m_error.isEmpty())
        return writeError(m_error, 3);
    if (m_stopping && !m_preparing) {
        bool running = false;
        if (m_server) {
            for (QThread *thread : m_server->findChildren<QThread *>())
                running = running || thread->isRunning();
        }
        m_closed = !running;
    }
    QJsonArray frames;
    for (const QByteArray &frame : std::as_const(m_frames))
        frames.append(QString::fromUtf8(frame));
    m_frames.clear();
    const QString phase = m_closed ? QStringLiteral("closed") : m_stopping ? QStringLiteral("closing")
        : (m_started && m_client ? QStringLiteral("active") : QStringLiteral("preparing"));
    return writeResult({{"schema_version", 1}, {"phase", phase}, {"frames", frames}});
}

int SoloServerHost::stop()
{
    m_stopping = true;
    // Initialization owns Room until initialRoomReady. Abort it there if a close
    // arrived in the meantime; never inspect half-initialized Room state.
    if (m_server && !m_preparing) {
        for (Room *room : m_server->findChildren<Room *>())
            room->abortWaitingRequests();
    }
    if (m_client)
        m_client->disconnectFromHost();
    return writeResult({{"schema_version", 1}, {"phase", m_closed ? "closed" : "closing"}});
}
