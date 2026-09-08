#include "local-server-controller.h"
#include "runtime-paths.h"
#include "settings.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QSettings>
#include <windows.h>

namespace {
QString parentCreationTime()
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
        return QString();
    return QString::number((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime);
}

bool captureSettings(const QString &path)
{
    // Keep unknown extension keys too; do not project the snapshot down to just
    // the dedicated CLI's schema. GUI-only metatypes are not game settings.
    QSettings snapshot(path, QSettings::IniFormat);
    for (const QString &key : Config.allKeys()) {
        const QVariant value = Config.value(key);
        if (value.userType() < QMetaType::User && value.userType() < QMetaType::QFont)
            snapshot.setValue(key, value);
    }
    const QVariantMap overrides = Config.valueOverrides();
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        snapshot.setValue(it.key(), it.value());
    // ServerDialog persists its options. These fields can additionally be
    // changed by one-shot GUI launch/replay flows without writing preferences.
    snapshot.setValue("GameMode", Config.GameMode.mode_id);
    snapshot.setValue("ServerName", Config.ServerName);
    snapshot.setValue("ServerPort", Config.ServerPort);
    snapshot.setValue("BindAddress", Config.BindAddress);
    snapshot.setValue("BanPackages", Config.BanPackages);
    snapshot.setValue("EnableAI", Config.EnableAI);
    snapshot.setValue("OriginAIDelay", Config.OriginAIDelay);
    snapshot.setValue("DisableLua", Config.DisableLua);
    snapshot.sync();
    return snapshot.status() == QSettings::NoError;
}
}

LocalServerController::LocalServerController(QObject *parent) : QObject(parent)
{
    m_clock.start();
    m_listener.setSocketOptions(QLocalServer::UserAccessOption);
    m_listener.setMaxPendingConnections(4);
    connect(&m_listener, &QLocalServer::newConnection, this, &LocalServerController::acceptConnections);
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (!active()) return;
        const qint64 now = m_clock.elapsed();
        if (m_deadline && now >= m_deadline) {
            if (m_state == State::Stopping) {
                m_forced = true;
                m_deadline = 0;
                emit logMessage(QStringLiteral("forced_shutdown: incomplete cleanup phase"));
                if (m_process) m_process->kill();
            } else {
                fail(QStringLiteral("startup_timeout"));
            }
        }
        if (m_takeoverPending && m_takeoverDeadline && now >= m_takeoverDeadline) {
            m_takeoverPending = false;
            emit takeoverFailed(QStringLiteral("takeover_timeout"));
            stop();
        }
        const QStringList ids = m_requests.keys();
        for (const QString &id : ids) {
            // A preceding timeout can stop the owner and cancel this request.
            if (!m_requests.contains(id)) continue;
            if (now < m_requests.value(id)) continue;
            m_requests.remove(id);
            m_banRequests.remove(id);
            if (m_replayRequests.contains(id)) {
                emit replayFinalized(m_replayRequests.take(id), false, QStringLiteral("replay export timed out"));
                continue; // Export failure must not tear down a running game.
            }
            emit commandResult(id, false, {{"code", "request_timeout"}});
            fail(QStringLiteral("request_timeout"));
        }
    });
    m_timer.start();
}

LocalServerController::~LocalServerController()
{
    // Normal close waits asynchronously in MainWindow. Unexpected destruction
    // still owns exactly this child; never terminate by executable name/PID.
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void LocalServerController::transition(State state, int timeoutMs)
{
    m_state = state;
    m_deadline = timeoutMs ? m_clock.elapsed() + timeoutMs : 0;
}

bool LocalServerController::start(Ownership ownership, bool hostOnly,
                                 const GameSessionConfig &config, const QString &replayPath)
{
    if (active()) return false;
    ++m_generation;
    m_failed = m_forced = m_shutdownComplete = false;
    m_takeoverPending = config.takeover;
    m_takeoverSession = config.takeover;
    m_takeoverDeadline = 0;
    m_hostOnly = hostOnly;
    m_endpoint.clear(); m_messages.clear(); m_status = {};
    m_requests.clear(); m_banRequests.clear();
    m_replayRequests.clear();
    QString error;
    m_session = XpControl::randomIdentity(&error);
    m_token = XpControl::randomIdentity(&error);
    const QString helper = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("QSanguoshaXPServer.exe");
    if (m_session.isEmpty() || m_token.isEmpty() || !QFileInfo(helper).isFile()) {
        emit failed(error.isEmpty() ? QStringLiteral("helper_missing: ") + helper : error);
        return false;
    }
    const QString parentTime = parentCreationTime();
    if (parentTime.isEmpty()) { emit failed(QStringLiteral("parent_identity_unavailable")); return false; }
    const QString sessions = QSanRuntimePaths::userDataPath("sessions");
    const QString diagnostics = QSanRuntimePaths::userDataPath("logs");
    if (!QDir().mkpath(sessions) || !QDir().mkpath(diagnostics)) {
        emit failed(QStringLiteral("session_directory_unwritable")); return false;
    }
    m_directory.reset(new QTemporaryDir(QDir(sessions).filePath("xp-XXXXXX")));
    const QString settings = m_directory->path() + "/config.ini";
    if (!m_directory->isValid() || !captureSettings(settings)) {
        emit failed(QStringLiteral("session_settings_unwritable")); return false;
    }
    const QString runtime = XpControl::runtimeHash(QSanRuntimePaths::assetRoot(), &error);
    if (runtime.isEmpty()) { emit failed(error); return false; }
    m_initialize = {{"build", XpControl::buildIdentity()}, {"token", m_token},
        {"settings", settings}, {"settingsHash", XpControl::fileHash(settings)},
        {"runtime", runtime}, {"assetRoot", QSanRuntimePaths::assetRoot()},
        {"dataRoot", QSanRuntimePaths::userDataRoot()},
        {"private", ownership == Ownership::OwnedPrivate}, {"hostOnly", hostOnly},
        {"seed", QString::number(config.seed)}, {"takeover", config.takeover}};
    if (config.takeover) {
        m_initialize.insert("snapshot", QFileInfo(config.takeoverSnapshotPath).absoluteFilePath());
        m_initialize.insert("snapshotHash", XpControl::fileHash(config.takeoverSnapshotPath));
        m_initialize.insert("seat", config.takeoverSeatName);
        m_initialize.insert("replay", QFileInfo(replayPath).absoluteFilePath());
        m_initialize.insert("replayHash", XpControl::fileHash(replayPath));
        const QString manifest = QFileInfo(config.takeoverSnapshotPath).absolutePath() + "/manifest.json";
        m_initialize.insert("manifestHash", XpControl::fileHash(manifest));
    }
    if (!m_listener.listen("qsan-xp-" + m_session)) {
        emit failed(QStringLiteral("control_listen_failed: ") + m_listener.errorString()); return false;
    }
    QProcess *process = new QProcess(this);
    m_process = process;
    const quint64 generation = m_generation;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QSAN_XP_CONTROL", m_listener.serverName());
    environment.insert("QSAN_XP_TOKEN", m_token);
    environment.insert("QSAN_XP_SESSION", m_session);
    environment.insert("QSAN_XP_GENERATION", this->generation());
    environment.insert("QSAN_XP_PARENT_PID", QString::number(QCoreApplication::applicationPid()));
    environment.insert("QSAN_XP_PARENT_CREATED", parentTime);
    environment.insert("QSAN_XP_SETTINGS", settings);
    environment.insert("QSAN_USER_DATA_ROOT", QSanRuntimePaths::userDataRoot());
    process->setProcessEnvironment(environment);
    process->setWorkingDirectory(QCoreApplication::applicationDirPath());
    const QString diagnosticStem = QDir(diagnostics).filePath(
        QStringLiteral("xp-server-") + m_session.left(16));
    const QString stdoutPath = diagnosticStem + QStringLiteral(".stdout.log");
    const QString stderrPath = diagnosticStem + QStringLiteral(".stderr.log");
    process->setStandardOutputFile(stdoutPath);
    process->setStandardErrorFile(stderrPath);
    emit logMessage(QStringLiteral("helper_diagnostics: stdout=%1 stderr=%2")
        .arg(stdoutPath, stderrPath));
    connect(process, &QProcess::started, this, [this, process, generation]() {
        if (m_process != process || m_generation != generation) return;
        if (m_state != State::Launching) return;
        transition(State::Handshaking, 8000);
        emit progress(QStringLiteral("Authenticating local server..."));
    });
    connect(process, static_cast<void(QProcess::*)(QProcess::ProcessError)>(&QProcess::error),
        this, [this, process, generation](QProcess::ProcessError) {
            if (m_process != process || m_generation != generation) return;
            if (m_state == State::Stopping) {
                // Cancellation may already have restored a replay. A late
                // crash/forced kill is a stop outcome, not a new startup error.
                emit logMessage(QStringLiteral("shutdown_process_error: ") + process->errorString());
                if (process->state() == QProcess::NotRunning) reap(false);
                return;
            }
            fail(QStringLiteral("helper_process_error: ") + process->errorString());
            if (process->state() == QProcess::NotRunning) reap(false);
        });
    connect(process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
        this, [this, process, generation](int code, QProcess::ExitStatus status) {
            if (m_process != process || m_generation != generation) return;
            // Process and local-pipe notifications are independent. Allow the
            // terminal frame a bounded event-loop turn before classifying exit.
            QTimer::singleShot(100, this, [this, process, generation, code, status]() {
                if (m_process != process || m_generation != generation) return;
                const bool graceful = !m_forced && m_shutdownComplete && code == 0 && status == QProcess::NormalExit;
                if (m_state != State::Stopping && !m_failed)
                    fail(QStringLiteral("helper_exited: %1").arg(code));
                reap(graceful);
            });
        });
    transition(State::Launching, 8000);
    process->start(helper, QStringList() << "--managed" << "--asset-root" << QSanRuntimePaths::assetRoot());
    return true;
}

void LocalServerController::acceptConnections()
{
    while (m_listener.hasPendingConnections()) {
        QLocalSocket *socket = m_listener.nextPendingConnection();
        if (m_candidates.size() >= 4 || m_channel || m_state == State::Stopping) {
            socket->abort(); socket->deleteLater(); continue;
        }
        auto channel = new XpControl::Channel(socket, socket);
        m_candidates << channel;
        connect(channel, &XpControl::Channel::message, this,
            [this, channel](const QJsonObject &message) { receive(channel, message); });
        connect(channel, &XpControl::Channel::failed, this, [this, channel](const QString &code) {
            if (m_channel == channel) fail(code);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket, channel]() {
            m_candidates.removeAll(channel);
            if (m_channel == channel && m_state != State::Stopping) fail(QStringLiteral("control_disconnected"));
            socket->deleteLater();
        });
        QTimer::singleShot(2000, socket, [this, socket, channel]() {
            if (m_channel != channel) socket->abort();
        });
    }
}

bool LocalServerController::send(const QString &id, const QString &type, const QJsonObject &body)
{
    return m_channel && m_channel->send(XpControl::envelope(m_session, generation(), id, type, body));
}

void LocalServerController::receive(XpControl::Channel *channel, const QJsonObject &message)
{
    const QJsonObject body = message.value("body").toObject();
    const QString type = message.value("type").toString();
    if (!m_channel) {
        if (!XpControl::validEnvelope(message, m_session, generation()) || type != "hello"
            || body.value("token").toString() != m_token || m_state == State::Stopping) {
            channel->socket()->abort(); return;
        }
        m_channel = channel;
        if (body.value("build").toString() != XpControl::buildIdentity()) {
            fail(QStringLiteral("helper_build_mismatch")); return;
        }
        // XP optical media can spend over 90 seconds loading the shared rules
        // and the room Lua state. Keep startup bounded without rejecting that
        // supported read-only installation path before initialization finishes.
        transition(State::Initializing, 180000);
        send("1", "initialize", m_initialize);
        m_token.clear(); m_initialize.remove("token");
        return;
    }
    if (m_channel != channel || !XpControl::validEnvelope(message, m_session, generation())) {
        if (m_channel == channel) fail(QStringLiteral("control_identity_mismatch"));
        return;
    }
    if (type == "shutdown_complete") { m_shutdownComplete = true; return; }
    if (type == "shutdown_phase") { emit logMessage(body.value("phase").toString()); return; }
    if (m_state == State::Stopping) return;
    if (type == "error") { fail(body.value("code").toString() + ": " + body.value("message").toString()); return; }
    if (type == "progress") { emit progress(body.value("phase").toString()); return; }
    if (type == "ready") {
        if (m_state != State::Initializing || message.value("id").toString() != "1") {
            fail(QStringLiteral("unexpected_ready")); return;
        }
        int port = 0;
        if (body.value("build") != m_initialize.value("build")
            || body.value("settingsHash") != m_initialize.value("settingsHash")
            || body.value("runtime") != m_initialize.value("runtime")
            || !XpControl::integer(body.value("port"), 1, 65535, &port)
            || body.value("host").toString().isEmpty()) {
            fail(QStringLiteral("ready_mismatch")); return;
        }
        m_endpoint = body.value("host").toString() + ":" + QString::number(port);
        for (const QJsonValue &line : body.value("messages").toArray()) m_messages << line.toString();
        transition(State::Ready, 0);
        if (m_takeoverPending) m_takeoverDeadline = m_clock.elapsed() + 20000;
        emit ready(); return;
    }
    if (type == "takeover_ready" && m_takeoverPending && isReady()) {
        m_takeoverPending = false; emit takeoverReady(); return;
    }
    if (type == "takeover_failed" && m_takeoverPending) {
        m_takeoverPending = false; emit takeoverFailed(body.value("message").toString()); stop(); return;
    }
    if (type == "status") { m_status = body; emit statusChanged(body); return; }
    if (type == "log") { emit logMessage(body.value("message").toString().left(4096)); return; }
    if (type == "ack" || type == "command_error") {
        const QString id = message.value("id").toString();
        if (!m_requests.remove(id)) return;
        if (m_replayRequests.contains(id)) {
            const QString path = m_replayRequests.take(id);
            const bool ok = type == "ack" && body.value("path").toString() == path;
            emit replayFinalized(path, ok, body.value("message").toString().isEmpty()
                ? body.value("code").toString() : body.value("message").toString());
            return;
        }
        if (type == "ack" && m_banRequests.contains(id)) {
            QStringList ips;
            for (const QJsonValue &ip : m_banRequests.value(id).value("ips").toArray()) ips << ip.toString();
            Config.setValue("BannedIP", ips); Config.sync();
        }
        m_banRequests.remove(id);
        emit commandResult(id, type == "ack", body);
    }
}

QString LocalServerController::request(const QString &type, const QJsonObject &body)
{
    if (!isReady() || m_requests.size() >= 32) return QString();
    const QString id = QString::number(m_nextId++);
    m_requests.insert(id, m_clock.elapsed() + 5000);
    if (type == "ban_ips") m_banRequests.insert(id, body);
    if (!send(id, type, body)) fail(QStringLiteral("command_send_failed"));
    return id;
}

QString LocalServerController::finalizeReplay(const QString &path, const QString &playerId)
{
    const QFileInfo file(path);
    if (!isReady() || m_hostOnly || m_requests.size() >= 32 || playerId.isEmpty()
        || !file.isFile() || file.isSymLink() || file.suffix().compare("txt", Qt::CaseInsensitive) != 0)
        return QString();
    const QString absolute = file.absoluteFilePath();
    const QString digest = XpControl::fileHash(absolute);
    if (digest.isEmpty()) return QString();
    const QString id = QString::number(m_nextId++);
    m_requests.insert(id, m_clock.elapsed() + 15000);
    m_replayRequests.insert(id, absolute);
    if (!send(id, "finalize_replay", {{"path", absolute}, {"sha256", digest}, {"player", playerId}}))
        fail(QStringLiteral("replay_send_failed"));
    return id;
}

void LocalServerController::fail(const QString &code)
{
    if (m_failed) return;
    m_failed = true;
    stop();
    emit failed(code);
}

void LocalServerController::stop()
{
    if (!active() || m_state == State::Stopping) return;
    transition(State::Stopping, 10000);
    m_takeoverPending = false;
    const QStringList cancelled = m_requests.keys();
    m_requests.clear(); m_banRequests.clear();
    for (const QString &id : cancelled) {
        if (m_replayRequests.contains(id))
            emit replayFinalized(m_replayRequests.take(id), false, QStringLiteral("cancelled"));
        else emit commandResult(id, false, {{"code", "cancelled"}});
    }
    send(QString::number(m_nextId++), "shutdown_request");
    m_listener.close();
}

void LocalServerController::reap(bool graceful)
{
    m_listener.close();
    const auto candidates = m_candidates;
    for (const auto &channel : candidates) if (channel) channel->socket()->abort();
    m_candidates.clear(); m_channel.clear();
    if (m_process) { m_process->deleteLater(); m_process = nullptr; }
    m_directory.reset(); m_token.clear(); m_initialize = {};
    transition(State::Idle, 0);
    emit stopped(graceful);
}
