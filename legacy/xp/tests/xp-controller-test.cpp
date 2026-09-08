#include "local-server-controller.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "game-rng.h"
#include "game-snapshot.h"
#include "game-snapshot-service.h"
#include "player.h"
#include "replay/replay-codec.h"
#include "runtime-paths.h"
#include "settings.h"
#include "xp-lua-paths-test.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <functional>
#include <windows.h>

namespace {

// The hard deadline also covers synchronous Engine/Lua bootstrap. A Qt timer
// alone cannot interrupt it; parent death activates the real helper watchdog.
class TestDeadline
{
public:
    bool start(DWORD timeout = 55000)
    {
        timeoutMs = timeout;
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stopEvent)
            worker = CreateThread(nullptr, 0, &TestDeadline::run, this, 0, nullptr);
        return worker != nullptr;
    }
    ~TestDeadline()
    {
        if (stopEvent) SetEvent(stopEvent);
        if (worker) { WaitForSingleObject(worker, 1000); CloseHandle(worker); }
        if (stopEvent) CloseHandle(stopEvent);
    }
private:
    static DWORD WINAPI run(void *context)
    {
        auto self = static_cast<TestDeadline *>(context);
        if (WaitForSingleObject(self->stopEvent, self->timeoutMs) != WAIT_OBJECT_0) {
            const char message[] = "FAIL: controller test exceeded configured deadline\n";
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1, &written, nullptr);
            ExitProcess(124);
        }
        return 0;
    }
    HANDLE stopEvent = nullptr;
    HANDLE worker = nullptr;
    DWORD timeoutMs = 55000;
};

bool expect(bool condition, const QString &message)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << message << '\n';
    return condition;
}

bool isolatedSettings()
{
    const QString configured = QString::fromLocal8Bit(qgetenv("QSAN_XP_SETTINGS"));
    const QString dataRoot = QString::fromLocal8Bit(qgetenv("QSAN_USER_DATA_ROOT"));
    if (!expect(!configured.isEmpty() && QFileInfo(configured).isAbsolute()
                    && !dataRoot.isEmpty() && QFileInfo(dataRoot).isAbsolute(),
                "set absolute isolated QSAN_XP_SETTINGS and QSAN_USER_DATA_ROOT before launch"))
        return false;
    const QString settingsPath = QDir::cleanPath(QFileInfo(configured).absoluteFilePath());
    const QString rootPath = QDir::cleanPath(QFileInfo(dataRoot).absoluteFilePath());
    if (!expect(settingsPath.startsWith(rootPath + '/', Qt::CaseInsensitive)
                    && settingsPath.compare(QDir::cleanPath(QFileInfo(Config.fileName()).absoluteFilePath()),
                                            Qt::CaseInsensitive) == 0,
                "global Config must use the isolated test data root"))
        return false;
    if (!expect(QDir().mkpath(QFileInfo(settingsPath).absolutePath()), "create isolated settings directory"))
        return false;
    Config.setValue("GameMode", "03_1v2");
    Config.setValue("ServerName", "XP controller integration test");
    Config.setValue("ServerPort", 0);
    Config.setValue("BindAddress", "127.0.0.1");
    Config.setValue("EnableUPnP", false);
    Config.setValue("EnableListServer", false);
    Config.setValue("AutoAddRobots", false);
    Config.setValue("BannedIP", QStringList());
    Config.setValue("DisableLua", false);
    Config.sync();
    return expect(Config.status() == QSettings::NoError, "write isolated test settings");
}

bool statusMatches(const QJsonObject &status, quint16 port, bool banned)
{
    int actualPort = 0;
    const QJsonArray bans = status.value("bans").toArray();
    return XpControl::integer(status.value("port"), 1, 65535, &actualPort)
        && actualPort == port && QHostAddress(status.value("bind").toString()).isLoopback()
        && status.value("mode").toString() == "03_1v2"
        && status.value("rooms").isArray() && !status.value("rooms").toArray().isEmpty()
        && status.value("players").isArray() && status.value("bans").isArray()
        && XpControl::decimal(status.value("uptimeMs").toString())
        && (banned ? bans == QJsonArray({QStringLiteral("192.0.2.123")}) : bans.isEmpty());
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject() ? document.object() : QJsonObject();
}

bool finalizedManifestCopy()
{
    QTemporaryDir directory(QFileInfo(Config.fileName()).absoluteDir().filePath("manifest-copy-XXXXXX"));
    if (!expect(directory.isValid(), "create isolated manifest fixture")) return false;
    // This is a schema fixture, not a completed game or runtime takeover proof.
    // Use the production serializers so byte-copy success also preserves a
    // snapshot that the real loader can parse.
    GlobalSnapshot state{};
    state.turnCount = 1; state.roundCount = 1; state.turnSerial = 1;
    state.currentPlayer = "p1";
    state.currentPhase = QString::number(int(Player::NotActive));
    state.gameMode = "02p";
    state.packages = QStringList({"standard"});
    state.seatOrder = QStringList({"p1", "p2"});
    state.drawPile = QList<int>({0}); state.discardPile = QList<int>({1});
    state.catalogFingerprint = {{"fixture", 1}, {"cardCount", 4}};
    state.configFingerprint = {{"fixture", 1}, {"gameMode", "02p"}, {"enableAI", true}, {"disableLua", false}};
    state.gameplayRng.algorithm = QString::number(GameRng::AlgorithmQsanRejectionV1);
    state.gameplayRng.seed = "123"; state.gameplayRng.drawCount = "0";
    state.aiRng = state.gameplayRng;
    for (int index = 0; index < 2; ++index) {
        PlayerSnapshot player{};
        player.objectName = QString("p%1").arg(index + 1);
        player.general = index ? "liubei" : "caocao";
        player.hp = player.maxhp = 4; player.alive = true;
        player.seat = player.playerSeat = index + 1;
        player.handcards = QList<int>({index + 2});
        state.players.append(player);
    }
    for (int id = 0; id < 4; ++id) {
        CardSnapshot card;
        card.id = id; card.objectName = "slash"; card.className = "Slash";
        state.cards.append(card);
        state.cardPlaces.insert(id, id == 0 ? int(Player::DrawPile) : id == 1 ? int(Player::DiscardPile) : int(Player::PlaceHand));
        state.cardOwners.insert(id, id < 2 ? QString() : QString("p%1").arg(id - 1));
    }
    const QString snapshotName = "turn_001_turn.json";
    const QString snapshotPath = QDir(directory.path()).filePath(snapshotName);
    GameSnapshot original;
    original.setState(state); original.setSnapshotType("turn");
    original.setDescription("Synthetic XP manifest copy fixture");
    const QByteArray replayBytes = QSanReplay::ReplayWriter("xp-test", "fixture").rawReplayData();
    const QString replayPath = QDir(directory.path()).filePath("export.txt");
    const QString sourceManifest = QDir(directory.path()).filePath("source-manifest.json");
    if (!expect(original.save(snapshotPath) && writeBytes(replayPath, replayBytes)
                    && QSanReplay::ReplayReader().read(replayBytes).success,
                "create parseable Replay V2 and snapshot fixtures")) return false;
    const QString snapshotHash = XpControl::fileHash(snapshotPath);
    QJsonObject entry{{"file", snapshotName}, {"sha256", snapshotHash}, {"turnSerial", "1"}};
    const QJsonObject manifest{{"schema", "qsanguosha-takeover-manifest-v1"}, {"sessionId", "fixture-session"},
                              {"replaySha256", "archived-replay-hash"}, {"snapshots", QJsonArray({entry})}};
    if (!expect(writeBytes(sourceManifest, QJsonDocument(manifest).toJson()), "write source manifest")) return false;
    const QString manifestHash = XpControl::fileHash(sourceManifest);
    QString error;
    if (!expect(GameSnapshotService::copyFinalizedManifest(sourceManifest, replayPath, &error),
                QString("copy finalized snapshot manifest: ") + error)) return false;
    const QDir target(GameSnapshot::getSnapshotDir(replayPath));
    const QJsonObject copied = readObject(target.filePath("manifest.json"));
    GameSnapshot loaded;
    if (!expect(copied.value("replaySha256").toString() == XpControl::fileHash(replayPath)
                    && copied.value("sessionId") == manifest.value("sessionId")
                    && copied.value("snapshots") == manifest.value("snapshots")
                    && XpControl::fileHash(target.filePath(snapshotName)) == snapshotHash
                    && loaded.load(target.filePath(snapshotName)) && loaded.getState().turnSerial == 1
                    && XpControl::fileHash(sourceManifest) == manifestHash
                    && XpControl::fileHash(snapshotPath) == snapshotHash,
                "copied manifest rebinds replay hash and preserves parseable immutable snapshot")) return false;
    int rejection = 0;
    auto reject = [&](const QJsonObject &badManifest, const QString &expected) {
        const QString output = QDir(directory.path()).filePath(QString("rejected-%1.txt").arg(++rejection));
        if (!writeBytes(sourceManifest, QJsonDocument(badManifest).toJson()) || !writeBytes(output, replayBytes)) return false;
        error.clear();
        return expect(!GameSnapshotService::copyFinalizedManifest(sourceManifest, output, &error)
                          && error == expected && !QFileInfo(QDir(GameSnapshot::getSnapshotDir(output)).filePath("manifest.json")).exists(),
                      QString("reject manifest without publishing output: ") + expected);
    };
    QJsonObject bad = manifest;
    entry.insert("sha256", QString(64, '0')); bad.insert("snapshots", QJsonArray({entry}));
    if (!reject(bad, "finalized snapshot hash mismatch")) return false;
    for (const QString &name : QStringList({"../escape.json", "C:escape.json", "sub\\escape.json"})) {
        entry.insert("file", name); bad.insert("snapshots", QJsonArray({entry}));
        if (!reject(bad, "invalid finalized snapshot filename")) return false;
    }
    bad = manifest; bad.insert("schema", "unsupported");
    if (!reject(bad, "incomplete finalized snapshot manifest")) return false;
    error.clear();
    if (!expect(!GameSnapshotService::copyFinalizedManifest(QDir(directory.path()).filePath("missing.json"), replayPath, &error)
                    && error == "cannot open finalized snapshots or replay", "missing archived manifest rejected")) return false;
    QTextStream(stdout) << "PASS: finalized manifest copy/rebind/parse/hash/schema/path rejection (synthetic fixture)\n";
    return true;
}

bool runController(QCoreApplication &application, LocalServerController &controller,
                   const QString &cancelMode, bool holdOwner, QElapsedTimer &elapsed)
{
    const GameSessionConfig game(Q_UINT64_C(20260908));
    QPointer<QProcess> child;
    QLocalSocket outsider;
    XpControl::Channel outsiderChannel(&outsider);
    QTimer deadline;
    QTemporaryDir replayDirectory(QFileInfo(Config.fileName()).absoluteDir().filePath("replay-command-XXXXXX"));
    const QString replayPath = QDir(replayDirectory.path()).filePath("probe.txt");
    const QString wrongSuffix = QDir(replayDirectory.path()).filePath("probe.bin");
    if (!expect(replayDirectory.isValid()
                    && writeBytes(replayPath, QSanReplay::ReplayWriter("xp-test", "fixture").rawReplayData())
                    && writeBytes(wrongSuffix, "fixture"), "prepare isolated replay command inputs")) return false;
    deadline.setSingleShot(true);
    bool success = true, stopRequested = false, stopped = false, gracefulStop = false;
    bool outsiderConnected = false, outsiderRejected = false, commandsComplete = false;
    bool initializationSeen = false, forcedLog = false;
    int readyCount = 0, stoppedCount = 0;
    int replayResults = 0;
    quint16 port = 0;
    QString diagnosticStdout, diagnosticStderr;
    QString pendingId, pendingType;
    QStringList failures;
    QJsonObject latestStatus;
    auto stop = [&]() { stopRequested = true; controller.stop(); };
    auto check = [&](bool condition, const QString &message) {
        if (expect(condition, message)) return true;
        success = false;
        stop();
        return false;
    };
    std::function<void(const QString &, const QJsonObject &, const QString &)> request;
    request = [&](const QString &type, const QJsonObject &body, const QString &stage) {
        pendingType = stage;
        pendingId = controller.request(type, body);
        check(!pendingId.isEmpty(), QStringLiteral("request accepted: ") + stage);
    };
    QObject callbacks;
    QObject::connect(&controller, &LocalServerController::failed, &callbacks, [&](const QString &error) {
        failures << error;
        QTextStream(stderr) << "controller_failed: " << error << '\n';
        if (cancelMode.isEmpty() || !stopRequested) {
            success = false;
            stop();
        }
    });
    QObject::connect(&controller, &LocalServerController::logMessage, &callbacks, [&](const QString &message) {
        if (message.startsWith("forced_shutdown:")) forcedLog = true;
        QTextStream(stdout) << "controller_log: " << message << '\n';
    });
    QObject::connect(&controller, &LocalServerController::progress, &callbacks, [&](const QString &phase) {
        QTextStream(stdout) << "controller_progress: " << phase << '\n';
        // These progress messages come from the authenticated production helper.
        if (phase == "Validating shared rules..." || phase == "Initializing rules and extensions...") {
            initializationSeen = true;
            if (cancelMode == "initializing") stop();
        }
    });
    QObject::connect(&controller, &LocalServerController::statusChanged, &callbacks,
                     [&](const QJsonObject &status) { latestStatus = status; });
    QObject::connect(&controller, &LocalServerController::ready, &callbacks, [&]() {
        ++readyCount;
        if (!check(cancelMode.isEmpty() && !stopRequested, "ready is not emitted after cancellation")) return;
        const QString endpoint = controller.endpoint();
        const int separator = endpoint.lastIndexOf(':');
        bool validPort = false;
        const uint parsedPort = endpoint.mid(separator + 1).toUInt(&validPort);
        if (!check(separator > 0 && QHostAddress(endpoint.left(separator)).isLoopback()
                       && validPort && parsedPort > 0 && parsedPort <= 65535,
                   "owned private helper exposes a loopback nonzero endpoint")) return;
        port = quint16(parsedPort);
        if (!check(child && child->state() == QProcess::Running && child->processId() > 0
                       && controller.isReady() && controller.active() && !controller.hostOnly(),
                   "ready belongs to a running owned helper")) return;
        if (holdOwner) {
            QTextStream output(stdout);
            output << "OWNER_READY helper_pid=" << child->processId() << '\n';
            output.flush();
            return;
        }
        const QString generation = controller.generation();
        if (!check(!controller.start(LocalServerController::Ownership::OwnedPrivate, false, game)
                       && controller.generation() == generation && controller.endpoint() == endpoint,
                   "duplicate ready start cannot replace the owned helper")) return;
        if (!check(controller.finalizeReplay(replayPath, QString()).isEmpty()
                       && controller.finalizeReplay(QDir(replayDirectory.path()).filePath("missing.txt"), "p1").isEmpty()
                       && controller.finalizeReplay(wrongSuffix, "p1").isEmpty(),
                   "replay API rejects missing player/file and wrong file suffix")) return;
        request("status", {}, "initial_status");
    });
    QObject::connect(&controller, &LocalServerController::commandResult, &callbacks,
                     [&](const QString &id, bool accepted, const QJsonObject &body) {
        const bool expectedError = pendingType.startsWith("invalid_");
        const QString expectedCode = pendingType == "invalid_command" ? "invalid_command"
            : expectedError ? "replay_export_failed" : "ok";
        if (!check(id == pendingId && accepted == !expectedError && body.value("code").toString() == expectedCode,
                   QStringLiteral("matching command result: ") + pendingType)) return;
        if (pendingType == "initial_status") {
            if (!check(statusMatches(latestStatus, port, false), "initial status reports actual private server")) return;
            request("broadcast", {{"message", "XP controller integration probe"}}, "broadcast");
        } else if (pendingType == "broadcast") {
            request("ban_ips", {{"ips", QJsonArray({QStringLiteral("192.0.2.123")})}}, "ban_ips");
        } else if (pendingType == "ban_ips") {
            QSettings persisted(Config.fileName(), QSettings::IniFormat);
            if (!check(persisted.value("BannedIP").toStringList() == QStringList({QStringLiteral("192.0.2.123")}),
                       "acknowledged ban update persisted only in isolated owner settings")) return;
            request("status", {}, "updated_status");
        } else if (pendingType == "updated_status") {
            if (!check(statusMatches(latestStatus, port, true), "updated status reports acknowledged ban list")) return;
            request("unknown_test_command", {}, "invalid_command");
        } else if (pendingType == "invalid_command") {
            request("finalize_replay", {}, "invalid_replay_path");
        } else if (pendingType == "invalid_replay_path") {
            request("finalize_replay", {{"path", replayPath}, {"sha256", QString(64, '0')}, {"player", "missing-player"}},
                    "invalid_replay_digest");
        } else if (pendingType == "invalid_replay_digest") {
            pendingType = "replay_missing_room";
            pendingId = controller.finalizeReplay(replayPath, "missing-player");
            check(!pendingId.isEmpty(), "valid replay export request reaches owned helper");
        } else if (pendingType == "final_status") {
            if (!check(statusMatches(latestStatus, port, true) && replayResults == 1,
                       "helper remains ready after replay input/ownership rejection")) return;
            commandsComplete = true;
            stop();
        } else {
            check(false, "unexpected command result stage");
        }
    });
    QObject::connect(&controller, &LocalServerController::replayFinalized, &callbacks,
                     [&](const QString &path, bool exported, const QString &detail) {
        ++replayResults;
        if (!check(pendingType == "replay_missing_room" && path == replayPath && !exported
                       && detail == "owned replay room is unavailable" && controller.isReady(),
                   "replay export reports real missing-room rejection without stopping helper")) return;
        request("status", {}, "final_status");
    });
    QObject::connect(&outsider, &QLocalSocket::connected, &callbacks, [&]() {
        outsiderConnected = true;
        const QProcessEnvironment environment = child->processEnvironment();
        // Deliberately never read or copy the real token. The external candidate
        // knows the endpoint/session yet must not be adopted without its secret.
        outsiderChannel.send(XpControl::envelope(environment.value("QSAN_XP_SESSION"),
            environment.value("QSAN_XP_GENERATION"), "0", "hello",
            {{"token", QString()}, {"build", XpControl::buildIdentity()}}));
    });
    QObject::connect(&outsider, &QLocalSocket::disconnected, &callbacks, [&]() { outsiderRejected = true; });
    QObject::connect(&outsiderChannel, &XpControl::Channel::message, &callbacks, [&](const QJsonObject &) {
        check(false, "untrusted external control candidate received owner commands");
    });
    QObject::connect(&controller, &LocalServerController::stopped, &callbacks, [&](bool graceful) {
        ++stoppedCount;
        stopped = true;
        gracefulStop = graceful;
        if (!stopRequested) success = expect(false, "helper stopped before test requested shutdown");
        deadline.stop();
        application.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &callbacks, [&]() {
        success = expect(false, "controller scenario exceeded soft deadline; requesting owned cleanup");
        stop();
        if (!controller.active()) application.quit();
    });

    if (!expect(!controller.active() && !controller.isReady()
                    && controller.request("status").isEmpty() && controller.finalizeReplay(replayPath, "p1").isEmpty(),
                "idle controller does not adopt any server or export replay"))
        return false;
    controller.stop();
    if (!expect(!controller.active() && stoppedCount == 0 && !controller.findChild<QProcess *>(),
                "idle stop creates no helper and emits no spurious stopped event")) return false;
    if (!controller.start(LocalServerController::Ownership::OwnedPrivate, false, game))
        return expect(false, "production helper start accepted");
    child = controller.findChild<QProcess *>();
    if (child) {
        const QString session = child->processEnvironment().value("QSAN_XP_SESSION");
        const QString stem = QDir(QSanRuntimePaths::userDataPath("logs")).filePath(
            QStringLiteral("xp-server-") + session.left(16));
        diagnosticStdout = stem + QStringLiteral(".stdout.log");
        diagnosticStderr = stem + QStringLiteral(".stderr.log");
    }
    const QString generation = controller.generation();
    if (!check(child && controller.active() && !controller.isReady()
                   && !controller.start(LocalServerController::Ownership::OwnedPrivate, false, game)
                   && controller.generation() == generation && controller.request("status").isEmpty(),
               "launching rejects duplicate start and premature commands"))
        stop();
    if (cancelMode == "immediate") {
        stop();
    } else if (cancelMode.isEmpty() && !holdOwner && child) {
        outsider.connectToServer(child->processEnvironment().value("QSAN_XP_CONTROL"));
    }
    // Reserve ten seconds for production stop/reap and three for test teardown.
    deadline.start(qMax(1, 42000 - int(elapsed.elapsed())));
    if (!stopped) application.exec();
    success = expect(stopped && stoppedCount == 1 && !controller.active() && !controller.isReady(),
                     "exactly one stopped event returns the controller to Idle") && success;
    success = expect(!child || child->state() == QProcess::NotRunning,
                      "owned production helper has exited") && success;
    success = expect(QFileInfo(diagnosticStdout).isFile() && QFileInfo(diagnosticStderr).isFile(),
                     "owned helper preserves native stdout/stderr diagnostic files") && success;
    if (cancelMode.isEmpty()) {
        success = expect(readyCount == 1 && commandsComplete && failures.isEmpty() && gracefulStop,
                         "normal lifecycle completes commands and acknowledged graceful shutdown") && success;
        success = expect(outsiderConnected && outsiderRejected,
                         "external unauthenticated candidate was connected and rejected") && success;
        if (success) QTextStream(stdout) << "PASS: production XP controller ready/status/broadcast/ban_ips/replay rejection/graceful stop\n";
    } else {
        success = expect(readyCount == 0 && (cancelMode == "immediate" || initializationSeen),
                         "cancellation reached its requested phase without ready") && success;
        for (const QString &failure : failures)
            success = expect(forcedLog && failure.startsWith("helper_process_error:"),
                             QStringLiteral("cancellation has no unrelated failure: ") + failure) && success;
        if (success) {
            QTextStream(stdout) << "EXPECTED_CANCELLATION: phase=" << cancelMode
                                << " graceful=" << (gracefulStop ? "true" : "false")
                                << " forced=" << (forcedLog ? "true" : "false") << " ready=0\n";
        }
    }
    return success;
}

}

int main(int argc, char **argv)
{
    QElapsedTimer elapsed;
    elapsed.start();
    QCoreApplication application(argc, argv);
    application.setApplicationName("QSanguoshaXPControllerTest");
    const QStringList arguments = application.arguments();
    if (arguments.contains("--lua-paths-only")) return runXpLuaPathsTest();
    const bool holdOwner = arguments.contains("--hold-owner");
    int cycles = 1;
    const int cyclesIndex = arguments.indexOf("--cycles");
    if (cyclesIndex >= 0) {
        bool valid = false;
        cycles = arguments.value(cyclesIndex + 1).toInt(&valid);
        if (!expect(valid && cycles >= 1 && cycles <= 30 && !holdOwner,
                    "--cycles must be 1..30 and cannot combine with --hold-owner")) return 2;
    }
    TestDeadline watchdog;
    if (!watchdog.start(DWORD(cycles * 55000))) return 125;
    QString cancelMode;
    if (arguments.contains("--cancel")) cancelMode = "immediate";
    if (arguments.contains("--cancel=initializing")) cancelMode = "initializing";
    if (!expect(arguments.contains("--asset-root"), "explicit --asset-root is required") || !isolatedSettings()) return 2;
    QString error;
    if (!expect(QSanRuntimePaths::resolve(arguments, &error), QStringLiteral("resolve runtime assets: ") + error)) return 2;
    if (!expect(EngineBootstrap::initialize(false, &error), QStringLiteral("bootstrap parent Engine: ") + error)) return 2;
    // Keep ownership explicit: controller/helper cleanup precedes parent Engine deletion.
    QObject::disconnect(&application, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init();
    bool result = !cancelMode.isEmpty() || holdOwner || finalizedManifestCopy();
    {
        LocalServerController controller;
        quint64 previousGeneration = 0;
        for (int cycle = 1; cycle <= cycles && result; ++cycle) {
            Config.setValue("BannedIP", QStringList());
            Config.sync();
            QElapsedTimer iteration;
            iteration.start();
            result = runController(application, controller, cancelMode, holdOwner, iteration);
            quint64 generation = 0;
            result = expect(XpControl::decimal(controller.generation(), &generation)
                                && generation == previousGeneration + 1,
                            "reuse increments generation exactly once") && result;
            previousGeneration = generation;
            // Drop old process/socket callbacks before reusing the same owner.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QTextStream(stdout) << "controller_cycle=" << cycle << '/' << cycles
                                << " result=" << (result ? "PASS" : "FAIL") << '\n';
        }
    }
    EngineBootstrap::shutdown();
    Config.sync();
    QTextStream(stdout) << "controller_test_elapsed_ms=" << elapsed.elapsed() << '\n';
    return result ? 0 : 1;
}
