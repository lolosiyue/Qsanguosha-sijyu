#include "xp-control-protocol.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "settings.h"
#include "runtime-paths.h"
#include "replay-takeover-validation.h"
#include "server-core.h"
#include "server-config.h"
#include "server-logger.h"
#include "room.h"
#include "serverplayer.h"
#include "game-snapshot.h"
#include "game-snapshot-service.h"
#include "banpair.h"
#include "crashhandler.h"
#include "websocket-gateway.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QTimer>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <memory>

int qsanStandaloneServerMain(int argc, char **argv);

namespace {
// XP has no nested job objects. Retain and validate a real parent handle before
// any engine work; this wait remains effective even if bootstrap blocks Qt.
class ParentWatch
{
public:
    ~ParentWatch() {
        if (stopEvent) SetEvent(stopEvent);
        if (worker) { WaitForSingleObject(worker, 1000); CloseHandle(worker); }
        if (parent) CloseHandle(parent);
        if (stopEvent) CloseHandle(stopEvent);
    }
    bool start() {
        quint64 pid = 0, expected = 0;
        if (!XpControl::decimal(QString::fromLatin1(qgetenv("QSAN_XP_PARENT_PID")), &pid)
            || !pid || pid > 0xffffffffULL
            || !XpControl::decimal(QString::fromLatin1(qgetenv("QSAN_XP_PARENT_CREATED")), &expected))
            return false;
        parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, DWORD(pid));
        FILETIME created, exited, kernel, user;
        if (!parent || !GetProcessTimes(parent, &created, &exited, &kernel, &user)
            || ((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime) != expected
            || WaitForSingleObject(parent, 0) != WAIT_TIMEOUT)
            return false;
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent) return false;
        worker = CreateThread(nullptr, 0, &ParentWatch::run, this, 0, nullptr);
        return worker != nullptr;
    }
private:
    static DWORD WINAPI run(void *context) {
        auto self = static_cast<ParentWatch *>(context);
        HANDLE handles[] = {self->stopEvent, self->parent};
        if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) {
            // No C++/Qt locks or destructors from the watchdog. Parent loss is
            // an emergency outcome (86), never a normal lifetime PASS.
            const char message[] = "forced_shutdown: parent_death\n";
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1, &written, nullptr);
            ExitProcess(86);
        }
        return 0;
    }
    HANDLE parent = nullptr, stopEvent = nullptr, worker = nullptr;
};

QJsonObject statusBody(Server *server)
{
    QJsonArray rooms, players, bans;
    for (const RoomStatusSnapshot &room : server->roomSnapshots())
        rooms.append(QJsonObject{{"id", room.id}, {"mode", room.gameMode}, {"state", room.state},
                                 {"players", room.playerCount}, {"capacity", room.playerCapacity}});
    for (const PlayerStatusSnapshot &player : server->playerSnapshots())
        players.append(QJsonObject{{"id", player.id}, {"name", player.name}, {"ip", player.ip},
                                  {"room", player.roomId}, {"state", player.state}});
    for (const QString &ip : Config.value("BannedIP").toStringList()) bans.append(ip);
    const ServerStatusSnapshot status = server->statusSnapshot();
    return {{"rooms", rooms}, {"players", players}, {"bans", bans},
            {"port", status.port}, {"bind", status.bindAddress}, {"mode", status.gameMode},
            {"uptimeMs", QString::number(status.uptimeMs)}};
}
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--xp-build-id") {
            printf("%s\n", qPrintable(XpControl::buildIdentity()));
            return 0;
        }
    }
    bool managed = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--managed") managed = true;
    if (!managed) {
        if (qEnvironmentVariable("QSAN_USER_DATA_ROOT").isEmpty()) {
            // This managed entry is also linked by the Qt6 Excel helper. Use
            // the Unicode Windows environment without importing Qt5 shims.
            const QString root = qEnvironmentVariable("APPDATA")
#ifdef QSAN_XP_LEGACY
                + QStringLiteral("/QSanguoshaXP");
#else
                + QStringLiteral("/QSanguoshaExcel");
#endif
            _wputenv_s(L"QSAN_USER_DATA_ROOT", reinterpret_cast<const wchar_t *>(root.utf16()));
        }
        return qsanStandaloneServerMain(argc, argv);
    }

    ParentWatch parentWatch;
    if (!parentWatch.start()) return 85;
    QString token = QString::fromLatin1(qgetenv("QSAN_XP_TOKEN"));
    qputenv("QSAN_XP_TOKEN", QByteArray());
    const QString session = QString::fromLatin1(qgetenv("QSAN_XP_SESSION"));
    const QString generation = QString::fromLatin1(qgetenv("QSAN_XP_GENERATION"));
    const QString controlName = QString::fromLocal8Bit(qgetenv("QSAN_XP_CONTROL"));
    if (token.size() != 64 || session.size() != 64 || !XpControl::decimal(generation)
        || controlName.isEmpty()) return 85;
    QCoreApplication app(argc, argv);
    app.setApplicationName("QSanguoshaXPServer");
    app.setApplicationVersion(XpControl::buildIdentity());
    app.addLibraryPath(app.applicationDirPath());
    QString error;
    if (!QSanRuntimePaths::resolve(app.arguments(), &error)) return 78;
    ServerLogger logger;
    ServerLogConfiguration logConfig;
    logConfig.filePath = QSanRuntimePaths::userDataPath("logs/xp-server-" + session.left(16) + ".log");
    if (!logger.start(logConfig, error)) return 73;
    CrashHandler::install();

    QLocalSocket socket;
    XpControl::Channel channel(&socket);
    std::unique_ptr<Server> server;
    QHash<QString, QPointer<Room>> replayRooms;
    QHash<QString, QString> completedReplayManifests;
    QSet<Room *> watchedReplayRooms;
    QTemporaryDir replayArchive(QSanRuntimePaths::userDataPath("sessions/xp-replay-XXXXXX"));
    bool initialized = false, stopping = false, ready = false;
    int result = 0;
    quint64 lastRequest = 0;
    QJsonObject initialization;
    QTimer shutdownPoll;
    const bool shutdownTrace = qEnvironmentVariableIsSet("QSAN_XP_SHUTDOWN_TRACE");
    int shutdownBeginCount = 0;
    auto traceShutdown = [&](const QString &event, const QString &detail = QString()) {
        if (!shutdownTrace) return;
        logger.info("shutdown_trace", event + (detail.isEmpty() ? QString() : QStringLiteral(" ") + detail));
    };
    auto send = [&](const QString &id, const QString &type, const QJsonObject &body = QJsonObject()) {
        return channel.send(XpControl::envelope(session, generation, id, type, body));
    };
    auto shutdown = [&]() {
        if (stopping) return;
        stopping = true;
        ++shutdownBeginCount;
        traceShutdown(QStringLiteral("begin count=%1").arg(shutdownBeginCount));
        send("0", "shutdown_phase", {{"phase", "stopping_rooms"}});
        if (!server) {
            app.quit();
            return;
        }
        server->beginShutdown();
        shutdownPoll.start();
    };
    auto fail = [&](const QString &code, const QString &message) {
        result = 1;
        logger.error("control", code + ": " + message);
        send("0", "error", {{"code", code}, {"message", message}});
        shutdown();
    };
    QTimer authenticationDeadline;
    authenticationDeadline.setSingleShot(true);
    QObject::connect(&authenticationDeadline, &QTimer::timeout, &app, [&]() { fail("handshake_timeout", "No authenticated initialization"); });
    authenticationDeadline.start(8000);
    QObject::connect(&socket, &QLocalSocket::connected, &app, [&]() {
        send("0", "hello", {{"token", token}, {"build", XpControl::buildIdentity()}});
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, shutdown);
    QObject::connect(&channel, &XpControl::Channel::failed, &app,
        [&](const QString &code) { logger.error("control", code); result = 1; shutdown(); });

    QObject::connect(&channel, &XpControl::Channel::message, &app, [&](const QJsonObject &message) {
        if (!XpControl::validEnvelope(message, session, generation)) {
            fail("identity_mismatch", "Invalid control envelope"); return;
        }
        const QString type = message.value("type").toString();
        const QString id = message.value("id").toString();
        quint64 requestId = 0;
        XpControl::decimal(id, &requestId);
        if (!requestId || requestId <= lastRequest) { fail("request_order", "Stale or repeated request"); return; }
        lastRequest = requestId;
        const QJsonObject body = message.value("body").toObject();
        if (type == "shutdown_request") {
            traceShutdown(QStringLiteral("received shutdown_request"));
            shutdown(); return;
        }
        if (stopping) return;
        if (!initialized) {
            if (type != "initialize" || id != "1" || body.value("token").toString() != token
                || body.value("build").toString() != XpControl::buildIdentity()) {
                fail("handshake_mismatch", "Invalid initialization identity"); return;
            }
            token.clear();
            authenticationDeadline.stop();
            initialized = true;
            initialization = body;
            initialization.remove("token");
            QTimer::singleShot(0, &app, [&]() {
                if (stopping) return;
                const QJsonObject &body = initialization;
                quint64 seed = 0;
                if (!body.value("private").isBool() || !body.value("hostOnly").isBool()
                    || !body.value("takeover").isBool() || !XpControl::decimal(body.value("seed").toString(), &seed)
                    || QFileInfo(body.value("settings").toString()).absoluteFilePath() != QFileInfo(Config.fileName()).absoluteFilePath()
                    || body.value("settingsHash").toString().isEmpty()
                    || XpControl::fileHash(Config.fileName()) != body.value("settingsHash").toString()) {
                    fail("settings_mismatch", "Invalid settings snapshot or seed"); return;
                }
                if (QDir::cleanPath(body.value("assetRoot").toString()) != QSanRuntimePaths::assetRoot()
                    || QDir::cleanPath(body.value("dataRoot").toString()) != QSanRuntimePaths::userDataRoot()) {
                    fail("paths_mismatch", "Runtime paths do not match owner"); return;
                }
                QString hashError;
                send("0", "progress", {{"phase", "Validating shared rules..."}});
                if (XpControl::runtimeHash(QSanRuntimePaths::assetRoot(), &hashError) != body.value("runtime").toString()) {
                    fail("runtime_mismatch", hashError); return;
                }
                QVariantMap values, known;
                for (const QString &key : Config.allKeys()) {
                    values.insert(key, Config.value(key));
                    if (isKnownServerConfigKey(key)) known.insert(key, Config.value(key));
                }
                const QStringList validation = validateServerConfigValues(known);
                if (!validation.isEmpty()) { fail("settings_invalid", validation.join("; ")); return; }
                if (body.value("private").toBool()) {
                    values.insert("BindAddress", "127.0.0.1"); values.insert("ServerPort", 0);
                    values.insert("EnableUPnP", false); values.insert("EnableListServer", false);
                }
                Config.setValueOverrides(values);
                send("0", "progress", {{"phase", "Initializing rules and extensions..."}});
                if (!EngineBootstrap::initialize(false, &error)) { fail("engine_failed", error); return; }
                QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
                Config.init();
                BanPair::loadBanPairs();
                if (!Sanguosha->getGameMode(values.value("GameMode").toString()).isValid()) {
                    fail("mode_invalid", "Selected game mode is unavailable"); return;
                }
                if (!Server::configureGameSeed(QString::number(seed), &error)) { fail("seed_invalid", error); return; }
                GameSessionConfig game(seed);
                game.takeover = body.value("takeover").toBool();
                game.takeoverSnapshotPath = body.value("snapshot").toString();
                game.takeoverSeatName = body.value("seat").toString();
                if (game.takeover) {
                    const QString replay = body.value("replay").toString();
                    const QString manifest = QFileInfo(game.takeoverSnapshotPath).absolutePath() + "/manifest.json";
                    if (XpControl::fileHash(game.takeoverSnapshotPath) != body.value("snapshotHash").toString()
                        || XpControl::fileHash(replay) != body.value("replayHash").toString()
                        || XpControl::fileHash(manifest) != body.value("manifestHash").toString()
                        || !validateReplayTakeover(replay, game.takeoverSnapshotPath, game.takeoverSeatName, &error)) {
                        fail("takeover_validation", error); return;
                    }
                }
                Server::isHeadlessMode = true;
#if QSAN_ENABLE_WEBSOCKETS
                // The modern managed entry bypasses standalone main; register
                // its socket factory before Server constructs the transports.
                qsanLinkWebSocketGateway();
#endif
                server.reset(new Server(nullptr, game, Server::InitialRoomPolicy::Deferred));
                QObject::connect(server.get(), &Server::newPlayer, &app, [&](ServerPlayer *player) {
                    Room *room = player->getRoom();
                    replayRooms.insert(player->objectName(), room);
                    completedReplayManifests.remove(player->objectName());
                    if (watchedReplayRooms.contains(room)) return;
                    watchedReplayRooms.insert(room);
                    QObject::connect(room, &QObject::destroyed, &app,
                        [&, room]() { watchedReplayRooms.remove(room); });
                    QObject::connect(room, &Room::takeover_ready, room, [&, room]() {
                        // Snapshot restore can replace the signup seat names.
                        for (ServerPlayer *entry : room->getPlayers())
                            replayRooms.insert(entry->objectName(), room);
                    });
                    // Capture the completed snapshot set in the game-over emitter
                    // before Server disposes its Room. Later Save dialogs must not
                    // depend on a Room surviving their user interaction.
                    QObject::connect(room, &Room::game_over, room, [&, room](const QString &) {
                        QStringList playerIds;
                        const QList<ServerPlayer *> players = room->getPlayers();
                        for (ServerPlayer *entry : players) playerIds << entry->objectName();
                        QString archiveError;
                        QString manifest;
                        if (replayArchive.isValid() && !players.isEmpty()) {
                            const QString record = replayArchive.path() + QStringLiteral("/room-%1.anchor").arg(room->getId());
                            // Internal pairing anchor, never a playable replay.
                            // Avoid a second per-player record buffer in the helper;
                            // export will bind these snapshots to the GUI's real file.
                            QSaveFile anchor(record);
                            const QByteArray marker("qsanguosha-xp-snapshot-archive-v1\n");
                            if (!anchor.open(QIODevice::WriteOnly) || anchor.write(marker) != marker.size() || !anchor.commit())
                                archiveError = QStringLiteral("cannot create snapshot archive anchor");
                            else if (room->finalizeSnapshotManifest(record, &archiveError))
                                manifest = GameSnapshot::getSnapshotDir(record) + QStringLiteral("/manifest.json");
                        } else archiveError = QStringLiteral("replay archive directory or players unavailable");
                        QTimer::singleShot(0, &app, [&, playerIds, manifest, archiveError]() {
                            for (const QString &id : playerIds) {
                                replayRooms.remove(id);
                                if (!manifest.isEmpty()) completedReplayManifests.insert(id, manifest);
                            }
                            if (!archiveError.isEmpty()) logger.warning("replay", archiveError);
                        });
                    }, Qt::DirectConnection);
                });
                QObject::connect(server.get(), &Server::takeoverReady, &app, [&]() { send("0", "takeover_ready"); });
                QObject::connect(server.get(), &Server::takeoverFailed, &app, [&](const QString &message) {
                    send("0", "takeover_failed", {{"message", message}});
                });
                QObject::connect(server.get(), &Server::initialRoomFailed, &app, [&](const QString &message) { fail("room_failed", message); });
                QObject::connect(server.get(), &Server::logMessage, &app, [&](const QString &message) {
                    logger.info("server", message);
                    // Drop optional live logs under backpressure; never queue
                    // unlimited output behind a slow management window.
                    if (socket.bytesToWrite() < XpControl::MaximumFrame)
                        send("0", "log", {{"message", message.left(4096)}});
                });
                QObject::connect(server.get(), &Server::roomLogMessage, &app, [&](int roomId, const QString &message) {
                    logger.info("room", message, roomId);
                });
                QObject::connect(server.get(), &Server::roomGameStarted, &app, [&](int roomId, const QString &mode) {
                    logger.info("game", "GAME_STARTED", roomId, QString(), {{"mode", mode}});
                });
                QObject::connect(server.get(), &Server::roomGameOver, &app, [&](int roomId, const QString &mode, const QString &winner) {
                    logger.info("game", "GAME_OVER", roomId, QString(), {{"mode", mode}, {"winner", winner}});
                });
                QObject::connect(server.get(), &Server::initialRoomReady, &app, [&]() {
                    if (stopping) {
                        // A deferred initial room may publish after shutdown
                        // began; fold it into the same non-blocking cleanup.
                        server->beginShutdown();
                        return;
                    }
                    if (!server->listen()) { fail("listen_failed", "Configured endpoint is unavailable"); return; }
                    if (!initialization.value("private").toBool()) {
                        server->checkUpnpAndListServer();
                        if (initialization.value("hostOnly").toBool()) server->daemonize();
                    }
                    const ServerStatusSnapshot state = server->statusSnapshot();
                    QString host = state.bindAddress;
                    if (host == "0.0.0.0" || host == "::") host = "127.0.0.1";
                    ready = true;
                    QJsonObject reply{{"host", host}, {"port", state.port},
                        {"build", XpControl::buildIdentity()}, {"runtime", initialization.value("runtime")},
                        {"settingsHash", initialization.value("settingsHash")},
                        {"messages", QJsonArray::fromStringList(server->startupMessages())},
                        {"pid", QString::number(QCoreApplication::applicationPid())}};
                    send("1", "ready", reply);
                    logger.info("control", "ready", -1, QString(), {{"pid", QCoreApplication::applicationPid()}, {"port", state.port}});
                });
                send("0", "progress", {{"phase", "Preparing initial room..."}});
                if (!server->prepareInitialRoomAsync(&error)) fail("room_failed", error);
            });
            return;
        }
        if (!ready || !server) { send(id, "command_error", {{"code", "not_ready"}}); return; }
        bool ok = true;
        if (type == "broadcast") {
            const QString text = body.value("message").toString();
            ok = !text.isEmpty() && text.size() <= 4096;
            if (ok) server->broadcastAdminMessage(text);
        } else if (type == "kick") {
            const QString player = body.value("player").toString();
            ok = !player.isEmpty() && player.size() <= 256 && server->kickPlayer(player);
        } else if (type == "ban_ips") {
            QStringList ips;
            const QJsonArray list = body.value("ips").toArray();
            ok = body.value("ips").isArray() && list.size() <= 1024;
            for (const QJsonValue &entry : list) {
                QHostAddress address(entry.toString());
                if (!entry.isString() || address.isNull() || address.isLoopback()) { ok = false; break; }
                ips << address.toString();
            }
            if (ok) {
                QVariantMap values = Config.valueOverrides();
                values.insert("BannedIP", ips); Config.setValueOverrides(values);
            }
        } else if (type == "finalize_replay") {
            // Only this authenticated owner's existing replay is an output
            // anchor. No source directory or arbitrary method is accepted.
            const QString path = body.value("path").toString();
            const QString digest = body.value("sha256").toString();
            const QString playerId = body.value("player").toString();
            const QFileInfo file(path);
            QString detail;
            ok = !initialization.value("hostOnly").toBool() && file.isAbsolute()
                && file.isFile() && !file.isSymLink() && file.suffix().compare("txt", Qt::CaseInsensitive) == 0
                && digest.size() == 64 && XpControl::fileHash(path) == digest
                && !QFileInfo(GameSnapshot::getSnapshotDir(path)).isSymLink();
            if (ok) {
                const QPointer<Room> room = replayRooms.value(playerId);
                const QString archived = completedReplayManifests.value(playerId);
                if (!archived.isEmpty())
                    ok = GameSnapshotService::copyFinalizedManifest(archived, path, &detail);
                else if (room)
                    ok = room->finalizeSnapshotManifest(path, &detail);
                else { ok = false; detail = QStringLiteral("owned replay room is unavailable"); }
                if (ok && XpControl::fileHash(path) != digest) {
                    ok = false; detail = QStringLiteral("replay changed during export");
                }
            }
            send(id, ok ? "ack" : "command_error", {{"code", ok ? "ok" : "replay_export_failed"},
                {"message", detail}, {"path", path}, {"sha256", digest}});
            return;
        } else if (type == "status") {
            send("0", "status", statusBody(server.get()));
        } else ok = false;
        send(id, ok ? "ack" : "command_error", {{"code", ok ? "ok" : "invalid_command"}});
    });
    QTimer statusTimer;
    statusTimer.setInterval(1000);
    QObject::connect(&statusTimer, &QTimer::timeout, &app, [&]() {
        if (ready && !stopping && socket.bytesToWrite() < XpControl::MaximumFrame)
            send("0", "status", statusBody(server.get()));
    });
    statusTimer.start();
    shutdownPoll.setInterval(25);
    QObject::connect(&shutdownPoll, &QTimer::timeout, &app, [&]() {
        const bool complete = !server || server->shutdownComplete();
        if (complete) {
            traceShutdown(QStringLiteral("poll complete"));
            shutdownPoll.stop();
            app.quit();
        }
    });
    socket.connectToServer(controlName);
    app.exec();
    traceShutdown(QStringLiteral("shutdownFinal enter"));
    CrashHandler::beginShutdown();
    statusTimer.stop();
    send("0", "shutdown_phase", {{"phase", "room_runtime_cleanup"}});
    socket.flush();
    server.reset(); // existing RoomRuntime/CardLifetime ordering, including joins
    send("0", "shutdown_phase", {{"phase", "engine_cleanup"}});
    socket.flush();
    EngineBootstrap::shutdown();
    Config.sync();
    logger.info("control", "shutdown_complete");
    traceShutdown(QStringLiteral("shutdownFinal exit"));
    send("0", "shutdown_complete", {{"exitCode", result}});
    socket.flush();
    QElapsedTimer flushDeadline;
    flushDeadline.start();
    while (socket.bytesToWrite() > 0 && flushDeadline.elapsed() < 1000) {
        if (!socket.waitForBytesWritten(qMax(1, 1000 - int(flushDeadline.elapsed())))) break;
    }
    socket.disconnectFromServer();
    return result;
}
