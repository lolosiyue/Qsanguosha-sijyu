#include "external-agent-transport.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "package.h"
#include "room.h"
#include "serverplayer.h"
#include "settings.h"
#include "runtime-paths.h"
#include "roomthread.h"
#include "snapshot-json-writer.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
#include <QElapsedTimer>
#include <QHashFunctions>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QMutexLocker>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <vector>

static qint64 hybridMonotonicNs()
{
    timespec stamp{};
    if (clock_gettime(CLOCK_MONOTONIC, &stamp) != 0) return -1;
    return qint64(stamp.tv_sec) * Q_INT64_C(1000000000) + stamp.tv_nsec;
}

static qint64 hybridProcessCpuNs()
{
    timespec stamp{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stamp) != 0) return -1;
    return qint64(stamp.tv_sec) * Q_INT64_C(1000000000) + stamp.tv_nsec;
}

// The journal and roster have no cross-thread read lock. Sample them only in
// their ordinary worker callbacks, then give the main loop primitive values.
// This observer adds no rule skill or wire consumer to either benchmark arm.
class HybridHostProgress
{
public:
    void sample(Room &room, bool workersStopped = false)
    {
        if (!workersStopped && QThread::currentThread() != room.getThread()
            && !(QThread::currentThread() == &room
                 && (!room.getThread() || !room.getThread()->isRunning()))) return;
        QMutexLocker lock(&m_mutex);
        const qint64 now = hybridMonotonicNs();
        if (!workersStopped && m_sampleNs && now - m_sampleNs < Q_INT64_C(5000000000)) return;
        bool more = false;
        bool queryOk = true;
        do {
            const auto page = room.queryHistoryEvents({{"after", QString::number(m_eventCursor)}, {"limit", 512}});
            if (page.contains("error")) { queryOk = false; break; }
            for (const auto &item : page.value("items").toList()) {
                const auto event = item.toMap();
                ++m_events;
                if (event.value("kind") == QLatin1String("turn")) {
                    ++m_turns;
                    if (!m_firstTurnObservedNs) m_firstTurnObservedNs = now;
                }
                if (event.value("kind") == QLatin1String("round")) ++m_rounds;
                m_lastEventId = event.value("id").toString();
                m_lastEventKind = event.value("kind").toString();
            }
            const auto next = page.value("next_after").toLongLong();
            more = page.value("has_more").toBool();
            if (next <= m_eventCursor) { if (more) queryOk = false; break; }
            m_eventCursor = next;
        } while (workersStopped && more);
        const qint64 current = room.currentHistoryEventId();
        const auto currentEvent = room.historyEvent(current);
        const auto turn = room.historyParent(current, "turn", true);
        const auto turnData = turn.value("data").toMap();
        m_values = {{"events_observed", QString::number(m_events)},
            {"player_turns_observed", QString::number(m_turns)},
            {"rounds_observed", QString::number(m_rounds)},
            {"counts_complete_at_sample", queryOk && !more},
            {"first_turn_observed_monotonic_ns", m_firstTurnObservedNs
                ? QJsonValue(QString::number(m_firstTurnObservedNs)) : QJsonValue::Null},
            {"first_turn_timing", "first worker callback observing a journal turn; upper bound on first turn start"},
            {"history_cursor", QString::number(m_eventCursor)},
            {"alive_count", room.getAlivePlayers().size()},
            {"current_turn_id", turn.value("id").toString()},
            {"current_turn_player", turnData.value("player").toString()},
            {"current_event_id", current ? QString::number(current) : QString()},
            {"current_event_kind", currentEvent.value("kind").toString()},
            {"last_event_id", m_lastEventId}, {"last_event_kind", m_lastEventKind},
            // Skill/card payloads may contain unrevealed information. Kind and
            // ancestry are enough to locate the private full-history evidence.
            {"current_skill", QJsonValue::Null},
            {"current_action_kind", currentEvent.value("kind").toString()},
            {"sample_monotonic_ns", QString::number(now)},
            {"sample_source", workersStopped ? "workers_stopped" : "worker_callback"}};
        m_sampleNs = now;
    }

    QJsonObject values() const
    {
        QMutexLocker lock(&m_mutex);
        auto values = m_values;
        values.insert("sample_available", m_sampleNs != 0);
        values.insert("sample_age_ms", m_sampleNs ? QJsonValue((hybridMonotonicNs() - m_sampleNs) / 1000000) : QJsonValue::Null);
        return values;
    }
private:
    mutable QMutex m_mutex;
    qint64 m_sampleNs = 0, m_eventCursor = 0, m_firstTurnObservedNs = 0;
    quint64 m_events = 0, m_turns = 0, m_rounds = 0;
    QString m_lastEventId, m_lastEventKind;
    QJsonObject m_values;
};

// Explicit opt-in 50-seat benchmark. Every transport grants one seat only;
// capabilities are emitted solely into the parent process's private stdout pipe.
static int hybrid50Host(QCoreApplication &app)
{
    const qint64 hostEntryNs = hybridMonotonicNs(), hostEntryCpuNs = hybridProcessCpuNs();
    const auto args = app.arguments();
    const auto option = [&](const QString &name) {
        const int at = args.indexOf(name);
        if (at >= 0 && at + 1 < args.size() && !args.at(at + 1).startsWith("--")) return args.at(at + 1);
        for (const auto &arg : args)
            if (arg.startsWith(name + '=')) return arg.mid(name.size() + 1);
        return QString();
    };
    const auto hasOption = [&](const QString &name) {
        for (const auto &arg : args) if (arg == name || arg.startsWith(name + '=')) return true;
        return false;
    };
    int capSeconds = 0, progressSeconds = 10;
    const auto capText = option("--max-gameplay-seconds");
    const auto progressText = option("--progress-interval-seconds");
    bool optionOk = true;
    if (!capText.isEmpty()) {
        capSeconds = capText.toInt(&optionOk);
        if (!optionOk || capSeconds < 1 || capSeconds > 900) {
            qCritical() << "HYBRID invalid-gameplay-cap"; return 2;
        }
    } else if (hasOption("--max-gameplay-seconds")) {
        qCritical() << "HYBRID missing-gameplay-cap"; return 2;
    }
    if (!progressText.isEmpty()) {
        progressSeconds = progressText.toInt(&optionOk);
        if (!optionOk || progressSeconds < 1 || progressSeconds > 60) {
            qCritical() << "HYBRID invalid-progress-interval"; return 2;
        }
    } else if (hasOption("--progress-interval-seconds")) {
        qCritical() << "HYBRID missing-progress-interval"; return 2;
    }
    const auto historyPath = option("--history-file");
    const auto progressPath = option("--progress-file");
    if ((hasOption("--history-file") && historyPath.isEmpty())
        || (hasOption("--progress-file") && progressPath.isEmpty())) {
        qCritical() << "HYBRID missing-measurement-path"; return 2;
    }
    const bool measure = capSeconds || !historyPath.isEmpty() || !progressPath.isEmpty();
    const int seedAt = args.indexOf("--seed");
    bool seedOk = false;
    const quint64 seed = seedAt >= 0 && seedAt + 1 < args.size()
        ? args.at(seedAt + 1).toULongLong(&seedOk) : 0;
    if (!seedOk || seed > Q_UINT64_C(4294967295)) {
        qCritical() << "HYBRID seed-required"; return 2;
    }
    qSetMessagePattern("[%{time yyyy-MM-dd hh:mm:ss.zzz}] %{message}");
    QString error;
    if (!QSanRuntimePaths::resolve(args, &error)) { qCritical() << error; return 1; }
    Config.setValueOverrides({
        {"GameMode", "50p"}, {"BanPackages", QStringList()},
        {"EnableAI", true}, {"Enable2ndGeneral", false}, {"EnableHegemony", false},
        {"MaxChoice", 5}, {"LordMaxChoice", 5}, {"NonLordMaxChoice", 5},
        {"AIDelay", 0}, {"CountDownSeconds", 0},
        {"recorder/autosave", true}, {"recorder/networkonly", false},
        {"AiLegacyDirectCallbacks", QStringLiteral(
            "activate askForUseCard askForSkillInvoke askForChoice askForSuit askForKingdom "
            "askForGeneral askForDiscard askForAG askForCardChosen askForYiji askForPlayerChosen "
            "askForPlayersChosen askForCard askForNullification askForCardShow askForPindian "
            "askForSinglePeach askForGuanxing askForTriggerOrder").split(' ')},
        {"AiIsolatedCallbacks", QStringList()}});
    if (!EngineBootstrap::initialize(false, &error)) { qCritical() << error; return 1; }
    QObject::disconnect(&app, SIGNAL(aboutToQuit()), Sanguosha, SLOT(deleteLater()));
    Config.init(); Config.EnableAI = true; Config.Enable2ndGeneral = false;
    Config.EnableHegemony = false; Config.GameMode = Sanguosha->getGameMode("50p");
    Config.AIDelay = Config.OriginAIDelay = 0; Config.CountDownSeconds = 0;
    // Preserve the engine's Scenario/special-play exclusions and forbidden packs.
    // All remaining compiled and declared Lua packages stay enabled.
    for (const auto *package : Sanguosha->getPackages())
        if (package->isForbid() && !Config.BanPackages.contains(package->objectName()))
            Config.BanPackages << package->objectName();
    const int generals = Sanguosha->getGeneralCount();
    if (!Config.GameMode.isValid() || Sanguosha->getPlayerCount("50p") != 50 || generals < 250) {
        qCritical() << "HYBRID insufficient-mode-or-generals" << generals;
        EngineBootstrap::shutdown(); return 1;
    }
    QJsonArray enabled, banned;
    for (const auto *package : Sanguosha->getPackages())
        (Config.BanPackages.contains(package->objectName()) ? banned : enabled).append(package->objectName());
    auto *room = new Room(nullptr, "50p", GameSessionConfig(seed));
    if (!room->hasLuaRuntime()) { delete room; EngineBootstrap::shutdown(); return 1; }
    room->setProperty("to_test", "headless"); room->setNoClock(true);
    std::vector<std::shared_ptr<ExternalAgentEndpoint>> endpoints;
    std::vector<std::unique_ptr<ExternalAgentLocalTransport>> transports;
    QJsonArray seats;
    for (int i = 0; i < 50; ++i) {
        auto *seat = room->addAIPlayer();
        if (i == 0) seat->setOwner(true);
        // signup(is_robot=true) auto-starts when the last seat fills the room.
        // Register identities directly, then start only after all 50 endpoints
        // and the private bootstrap are ready (preflight must never start).
        seat->setScreenName(QString("Hybrid_%1").arg(i + 1));
        auto endpoint = room->attachExternalAgent(seat, ExternalAgentEndpoint::Pause,
            ExternalAgentProjectionPolicy::LocalDecisions);
        if (!endpoint) { qCritical() << "HYBRID attach-failed" << i; delete room; EngineBootstrap::shutdown(); return 1; }
        auto transport = std::make_unique<ExternalAgentLocalTransport>(endpoint);
        if (!transport->listen()) { delete room; EngineBootstrap::shutdown(); return 1; }
        auto bootstrap = transport->bootstrap();
        bootstrap.insert("objectName", seat->objectName()); seats.append(bootstrap);
        endpoints.push_back(endpoint); transports.push_back(std::move(transport));
    }
    const QJsonObject settings{{"mode", "50p"}, {"seed", QString::number(seed)},
        {"controlled_seats", 50}, {"no_clock", true}, {"nonlord_choices", 5},
        {"second_general", false}, {"full_resolution_history", true},
        {"local_decision_projection", true},
        {"measurement_enabled", measure}, {"gameplay_cap_seconds", capSeconds},
        {"progress_interval_seconds", progressSeconds},
        {"history_export_requested", !historyPath.isEmpty()},
        {"gameplay_timer_start", "CLOCK_MONOTONIC immediately before Room::start; includes room preparation and general selection; excludes engine/bootstrap setup"},
        {"progress_metrics", "worker-callback snapshots; bounded observed counters until final worker-stopped drain; stale age explicit"},
        {"cap_cancellation", "main-event-loop precise QTimer; cooperative worker stop; cleanup overshoot separate"},
        {"replay_autosave", true}, {"available_generals", generals},
        {"enabled_packages", enabled}, {"banned_packages", banned}};
    const int settingsAt = args.indexOf("--settings-file");
    if (settingsAt >= 0 && settingsAt + 1 < args.size()) {
        QFile file(args.at(settingsAt + 1));
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(settings).toJson()) < 0) {
            qCritical() << "HYBRID settings-write-failed";
            delete room; EngineBootstrap::shutdown(); return 1;
        }
    }
    qInfo().noquote() << "HYBRID_READY" << QJsonDocument(settings).toJson(QJsonDocument::Compact);
    if (args.contains("--preflight")) {
        for (auto &transport : transports) transport->close();
        delete room; EngineBootstrap::shutdown(); return 0;
    }
    bool finished = false;
    QString winner;
    int callbackErrors = 0;
    bool capReached = false;
    qint64 startNs = 0, startCpuNs = 0, capRequestedNs = 0, capRequestedCpuNs = 0;
    std::atomic<qint64> gameOverNs{0}, gameOverCpuNs{0};
    QElapsedTimer elapsed;
    HybridHostProgress metrics;
    QFile progressFile(progressPath);
    if (!progressPath.isEmpty()
        && (!progressFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || !progressFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))) {
        qCritical() << "HYBRID progress-write-failed";
        delete room; EngineBootstrap::shutdown(); return 1;
    }
    QTimer gameplayCap, progress;
    gameplayCap.setSingleShot(true); gameplayCap.setTimerType(Qt::PreciseTimer);
    const auto emitProgress = [&](const QString &phase) {
        auto values = metrics.values();
        values.insert("phase", phase);
        values.insert("elapsed_ms", elapsed.isValid() ? elapsed.elapsed() : 0);
        values.insert("monotonic_ns", QString::number(hybridMonotonicNs()));
        values.insert("process_cpu_ns", QString::number(hybridProcessCpuNs()));
        const qint64 firstTurn = values.value("first_turn_observed_monotonic_ns").toString().toLongLong();
        values.insert("time_to_first_turn_observed_ms", firstTurn
            ? QJsonValue((firstTurn - startNs) / 1000000) : QJsonValue::Null);
        values.insert("gameplay_cap_seconds", capSeconds);
        values.insert("cap_reached", capReached);
        QJsonArray pending;
        for (const auto &endpoint : endpoints) {
            AIRequest request;
            if (endpoint->pending(request)) pending.append(QJsonObject{
                {"seat", request.viewerObjectName}, {"decision_id", QString::number(request.decisionId)},
                {"decision_kind", int(request.kind)}});
        }
        values.insert("pending_decisions", pending);
        const auto encoded = QJsonDocument(values).toJson(QJsonDocument::Compact);
        qInfo().noquote() << "HYBRID_PROGRESS" << encoded;
        if (progressFile.isOpen()
            && (progressFile.write(encoded + '\n') != encoded.size() + 1 || !progressFile.flush()))
            qWarning() << "HYBRID progress-write-failed";
    };
    if (measure) {
        QObject::connect(room, &Room::signalSetProperty, &app,
            [&](ServerPlayer *, const char *, const QVariant &) { metrics.sample(*room); }, Qt::DirectConnection);
        QObject::connect(room, &Room::room_message, &app,
            [&](const QString &) { metrics.sample(*room); }, Qt::DirectConnection);
        QObject::connect(&progress, &QTimer::timeout, &app, [&] { emitProgress("running"); });
        QObject::connect(&gameplayCap, &QTimer::timeout, &app, [&] {
            const qint64 naturalEnd = gameOverNs.load();
            if (naturalEnd && naturalEnd <= startNs + qint64(capSeconds) * 1000000000) return;
            capReached = true; capRequestedNs = hybridMonotonicNs();
            capRequestedCpuNs = hybridProcessCpuNs();
            for (const auto &endpoint : endpoints) endpoint->cancel();
            room->requestStopGameThreads();
            qInfo().noquote() << "HYBRID_GAMEPLAY_CAP" << QJsonDocument(QJsonObject{
                {"status", "incomplete"}, {"reason", "gameplay-cap"},
                {"gameplay_cap_seconds", capSeconds},
                {"cancel_monotonic_ns", QString::number(capRequestedNs)},
                {"cancel_process_cpu_ns", QString::number(capRequestedCpuNs)},
                {"gameplay_process_cpu_ns", QString::number(capRequestedCpuNs - startCpuNs)},
                {"cancel_elapsed_ms", elapsed.elapsed()},
                {"timer_overshoot_ms", qMax(qint64(0), elapsed.elapsed() - qint64(capSeconds) * 1000)}})
                .toJson(QJsonDocument::Compact);
            emitProgress("cap_cancel_requested");
        });
    }
    QObject::connect(room, &Room::room_message, &app, [&](const QString &message) {
        if (message.contains("[AI_CALLBACK_ERROR]")) ++callbackErrors;
        qInfo().noquote() << "[HYBRID_ROOM]" << message;
    });
    QObject::connect(room, &Room::game_over, &app, [&](const QString &value) {
        const qint64 emittedNs = hybridMonotonicNs();
        const qint64 emittedCpuNs = hybridProcessCpuNs();
        gameOverCpuNs.store(emittedCpuNs);
        gameOverNs.store(emittedNs);
        QMetaObject::invokeMethod(&app, [&, value, emittedNs, emittedCpuNs] {
            finished = !capReached && (!capSeconds || emittedNs <= startNs + qint64(capSeconds) * 1000000000);
            winner = value;
            qInfo().noquote() << "HYBRID_GAME_OVER" << winner << "wall_ms=" << (emittedNs - startNs) / 1000000;
            qInfo().noquote() << "HYBRID_NATURAL_FINISH" << QJsonDocument(QJsonObject{
                {"within_gameplay_cap", finished}, {"finish_monotonic_ns", QString::number(emittedNs)},
                {"finish_process_cpu_ns", QString::number(emittedCpuNs)},
                {"gameplay_process_cpu_ns", QString::number(emittedCpuNs - startCpuNs)}})
                .toJson(QJsonDocument::Compact);
        }, Qt::QueuedConnection);
    }, Qt::DirectConnection);
    QTimer lifecycle;
    QObject::connect(&lifecycle, &QTimer::timeout, &app, [&] {
        bool cancelled = false;
        for (const auto &endpoint : endpoints) cancelled |= endpoint->status() == "cancelled";
        if ((!finished && !cancelled) || !room->allGameThreadsStopped()) return;
        lifecycle.stop(); gameplayCap.stop(); progress.stop();
        const qint64 workersStoppedNs = hybridMonotonicNs();
        const qint64 workersStoppedCpuNs = hybridProcessCpuNs();
        qint64 exportMs = 0, exportBytes = 0;
        qint64 exportCpuNs = 0, exportFinishedNs = workersStoppedNs, exportFinishedCpuNs = workersStoppedCpuNs;
        bool exportOk = historyPath.isEmpty();
        bool historyComplete = false;
        QJsonObject finalMetrics;
        if (measure) {
            metrics.sample(*room, true);
            emitProgress("workers_stopped");
            finalMetrics = metrics.values();
            const qint64 firstTurn = finalMetrics.value("first_turn_observed_monotonic_ns").toString().toLongLong();
            finalMetrics.insert("time_to_first_turn_observed_ms", firstTurn
                ? QJsonValue((firstTurn - startNs) / 1000000) : QJsonValue::Null);
        }
        if (!historyPath.isEmpty()) {
            QElapsedTimer exportTimer; exportTimer.start();
            const qint64 exportStartCpuNs = hybridProcessCpuNs();
            const auto snapshot = room->resolutionHistory().snapshot();
            historyComplete = snapshot.isComplete();
            QSaveFile file(historyPath);
            if (file.open(QIODevice::WriteOnly)
                && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
                SnapshotJsonWriter writer(file);
                exportOk = snapshot.writeJson(writer) && writer.finish();
                exportBytes = file.pos();
                if (exportOk) exportOk = file.commit();
                else file.cancelWriting();
            }
            exportMs = exportTimer.elapsed();
            exportFinishedNs = hybridMonotonicNs(); exportFinishedCpuNs = hybridProcessCpuNs();
            exportCpuNs = exportFinishedCpuNs - exportStartCpuNs;
            qInfo().noquote() << "HYBRID_HISTORY_EXPORT" << QJsonDocument(QJsonObject{
                {"ok", exportOk}, {"history_structurally_complete", historyComplete},
                {"elapsed_ms", exportMs}, {"bytes", QString::number(exportBytes)},
                {"process_cpu_ns", QString::number(exportCpuNs)},
                {"finish_monotonic_ns", QString::number(exportFinishedNs)},
                {"finish_process_cpu_ns", QString::number(exportFinishedCpuNs)}})
                .toJson(QJsonDocument::Compact);
        }
        QElapsedTimer destroyTimer; destroyTimer.start();
        delete room; room = nullptr;
        const qint64 destroyMs = destroyTimer.elapsed();
        for (auto &transport : transports) transport->complete(winner);
        qInfo().noquote() << "HYBRID_TERMINAL" << QJsonDocument(QJsonObject{
            {"natural_finished", finished}, {"winner", winner},
            {"callback_errors", callbackErrors}, {"wall_ms", elapsed.elapsed()},
            {"run_status", finished && !winner.isEmpty() ? "complete" : "incomplete"},
            {"stop_reason", capReached ? "gameplay-cap" : finished ? "natural-finish" : "external-cancellation"},
            {"gameplay_cap_seconds", capSeconds}, {"cap_reached", capReached},
            {"start_monotonic_ns", QString::number(startNs)},
            {"start_process_cpu_ns", QString::number(startCpuNs)},
            {"natural_finish_monotonic_ns", gameOverNs.load()
                ? QJsonValue(QString::number(gameOverNs.load())) : QJsonValue::Null},
            {"natural_finish_process_cpu_ns", gameOverNs.load()
                ? QJsonValue(QString::number(gameOverCpuNs.load())) : QJsonValue::Null},
            {"cap_cancel_monotonic_ns", capReached ? QJsonValue(QString::number(capRequestedNs)) : QJsonValue::Null},
            {"cap_cancel_process_cpu_ns", capReached ? QJsonValue(QString::number(capRequestedCpuNs)) : QJsonValue::Null},
            {"workers_stopped_monotonic_ns", QString::number(workersStoppedNs)},
            {"workers_stopped_process_cpu_ns", QString::number(workersStoppedCpuNs)},
            {"process_cpu_ns_until_workers_stopped", QString::number(workersStoppedCpuNs - startCpuNs)},
            {"gameplay_ms", capReached ? qint64(capSeconds) * 1000
                : gameOverNs.load() ? (gameOverNs.load() - startNs) / 1000000 : (workersStoppedNs - startNs) / 1000000},
            {"workers_stopped_elapsed_ms", (workersStoppedNs - startNs) / 1000000},
            {"cleanup_overshoot_ms", capReached ? qMax(qint64(0), (workersStoppedNs - startNs) / 1000000 - qint64(capSeconds) * 1000) : 0},
            {"history_export_requested", !historyPath.isEmpty()}, {"history_export_ok", exportOk},
            {"history_export_ms", exportMs}, {"history_export_bytes", QString::number(exportBytes)},
            {"history_export_process_cpu_ns", QString::number(exportCpuNs)},
            {"export_finished_monotonic_ns", QString::number(exportFinishedNs)},
            {"export_finished_process_cpu_ns", QString::number(exportFinishedCpuNs)},
            {"history_structurally_complete", historyComplete}, {"room_destroy_ms", destroyMs},
            {"final_progress", finalMetrics},
            {"workers_stopped", true}, {"room_destroyed", true}}).toJson(QJsonDocument::Compact);
        const bool valid = exportOk && (!finished || !winner.isEmpty());
        QTimer::singleShot(500, &app, [&, valid] { app.exit(valid ? 0 : 1); });
    });
    lifecycle.start(20);
    const QJsonObject bootstrap{{"version", 2},
        {"hostPid", QString::number(QCoreApplication::applicationPid())}, {"seats", seats}};
    const auto line = QByteArray("QSAN_HYBRID_BOOTSTRAP ")
        + QJsonDocument(bootstrap).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, size_t(line.size()), stdout); std::fflush(stdout);
    startNs = hybridMonotonicNs();
    startCpuNs = hybridProcessCpuNs();
    if (startNs < 0) {
        qCritical() << "HYBRID monotonic-clock-failed";
        delete room; EngineBootstrap::shutdown(); return 1;
    }
    elapsed.start();
    if (measure) progress.start(progressSeconds * 1000);
    if (capSeconds) gameplayCap.start(capSeconds * 1000);
    qInfo().noquote() << "HYBRID_GAMEPLAY_START" << QJsonDocument(QJsonObject{
        {"start_monotonic_ns", QString::number(startNs)}, {"gameplay_cap_seconds", capSeconds},
        {"start_process_cpu_ns", QString::number(startCpuNs)},
        {"host_entry_monotonic_ns", QString::number(hostEntryNs)},
        {"host_entry_process_cpu_ns", QString::number(hostEntryCpuNs)},
        {"initialization_ms_since_host_entry", (startNs - hostEntryNs) / 1000000},
        {"initialization_process_cpu_ns_since_host_entry", QString::number(startCpuNs - hostEntryCpuNs)},
        {"timer_start", "immediately_before_room_start"}}).toJson(QJsonDocument::Compact);
    room->start();
    const int result = app.exec();
    for (auto &transport : transports) transport->close();
    if (room) { room->requestStopGameThreads(); room->wait(); delete room; }
    EngineBootstrap::shutdown(); return result;
}

// A runnable two-seat native-role host. The bootstrap is written only to stdout,
// intended for a parent process's private pipe, never to the game logs.
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--hybrid-50p") == 0) QHashSeed::setDeterministicGlobalSeed();
    QCoreApplication app(argc,argv);
    if (app.arguments().contains("--hybrid-50p")) return hybrid50Host(app);
    QString error;
    if (!QSanRuntimePaths::resolve(app.arguments(),&error)) { qCritical() << error; return 1; }
    Config.setValueOverrides({{"AiLegacyDirectCallbacks",QStringLiteral(
        "activate askForUseCard askForSkillInvoke askForChoice askForSuit askForKingdom "
        "askForGeneral askForDiscard askForAG askForCardChosen askForYiji askForPlayerChosen "
        "askForPlayersChosen askForCard askForNullification askForCardShow askForPindian "
        "askForSinglePeach askForGuanxing askForTriggerOrder").split(' ')},
        {"AiIsolatedCallbacks",QStringList()}});
    if (!EngineBootstrap::initialize(false,&error)) { qCritical() << error; return 1; }
    QObject::disconnect(&app,SIGNAL(aboutToQuit()),Sanguosha,SLOT(deleteLater()));
    Config.init(); Config.EnableAI = true; Config.Enable2ndGeneral = false;
    Config.EnableHegemony = false; Config.GameMode = Sanguosha->getGameMode("02p");
    Config.AIDelay = Config.OriginAIDelay = 0; Config.CountDownSeconds = 0;
    Config.BanPackages.clear();
    const QStringList allowed = {"standard","standard_cards","standard_ex_cards","maneuvering"};
    for (const auto *package : Sanguosha->getPackages())
        if (!allowed.contains(package->objectName())) Config.BanPackages << package->objectName();
    auto *room = new Room(nullptr,"02p",GameSessionConfig(20261003));
    room->setProperty("to_test","headless"); room->setNoClock(true);
    auto *seat = room->addAIPlayer(); seat->setOwner(true); room->signup(seat,"External","",true);
    auto endpoint = room->attachExternalAgent(seat,ExternalAgentEndpoint::Pause);
    if (!endpoint) { delete room; EngineBootstrap::shutdown(); return 1; }
    ExternalAgentLocalTransport transport(endpoint);
    if (!transport.listen()) { delete room; EngineBootstrap::shutdown(); return 1; }
    bool finished = false;
    QString winner;
    int callbackErrors = 0;
    QObject::connect(room,&Room::room_message,&app,[&](const QString &message) {
        if (message.contains("[AI_CALLBACK_ERROR]")) ++callbackErrors;
    });
    QObject::connect(room,&Room::game_over,&app,[&](const QString &value) {
        finished = true; winner = value;
    });
    auto *opponent = room->addAIPlayer(); room->signup(opponent,"SmartAI","",true);
    QTimer lifecycle;
    QObject::connect(&lifecycle,&QTimer::timeout,&app,[&] {
        if ((!finished && endpoint->status() != "cancelled") || !room->allGameThreadsStopped()) return;
        const bool valid = callbackErrors == 0 && (!finished || !winner.isEmpty());
        lifecycle.stop(); delete room; room = nullptr;
        transport.complete(winner);
        qInfo() << "EXTERNAL_HOST_TERMINAL winner=" << winner << "callback_errors=" << callbackErrors
                << "workers_stopped=true room_destroyed=true endpoint=" << endpoint->status();
        QTimer::singleShot(500,&app,[&,valid] { app.exit(valid ? 0 : 1); });
    });
    lifecycle.start(10);
    auto bootstrap = transport.bootstrap();
    bootstrap.insert("hostPid",QString::number(QCoreApplication::applicationPid()));
    const auto line = QByteArray("QSAN_AGENT_BOOTSTRAP ") + QJsonDocument(bootstrap).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(),1,size_t(line.size()),stdout); std::fflush(stdout);
    room->start();
    const int result = app.exec();
    transport.close();
    if (room) { room->requestStopGameThreads(); room->wait(); delete room; }
    EngineBootstrap::shutdown();
    return result;
}
