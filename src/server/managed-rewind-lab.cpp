#include "managed-rewind-lab.h"

#include "ai.h"
#include "engine.h"
#include "game-session-controller.h"
#include "gamerule.h"
#include "room.h"
#include "room-managed-state.h"
#include "room-roster.h"
#include "roomthread.h"
#include "server.h"
#include "player-lifecycle-service.h"
#include "serverplayer.h"
#include "settings.h"
#include "util.h"

#include <QJsonDocument>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QThread>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
QVariantList cardIds(const QList<int> &ids)
{
    QVariantList result;
    for (int id : ids) result.append(id);
    return result;
}
}

ManagedRewindLab::ManagedRewindLab(QObject *parent) : QObject(parent) {}

ManagedRewindLab::~ManagedRewindLab()
{
    stop();
    if (m_room && m_room->getThread() && !m_room->getThread()->wait(10000))
        qFatal("Restricted rewind worker did not stop");
    if (m_ownedRoom) delete m_room;
}

bool ManagedRewindLab::start(quint64 seed, QString *error)
{
    if (QThread::currentThread() != thread() || m_room || m_started)
        return fail(error, QStringLiteral("lab must be started once on its local owner thread"));
    if (Config.EnableAI || Config.Enable2ndGeneral || Config.EnableHegemony)
        return fail(error, QStringLiteral("lab requires native TrustAI, one general slot, and normal rules"));

    m_room = new Room(nullptr, QStringLiteral("02p"), GameSessionConfig(seed),
                      Room::RuntimeInitializationPolicy::Deferred);
    EngineRuntimeContextScope context(*Sanguosha, m_room);
    auto &runtime = *m_room->roomRuntime();
    if (!runtime.lua().initialize(error) || !runtime.ai().lua().initialize(error)) return false;
    for (int index = 0; index < 2; ++index) {
        auto *player = m_room->addAIPlayer();
        player->setObjectName(QStringLiteral("lab%1").arg(index + 1));
        player->setScreenName(player->objectName());
    }
    return initializePlayers(error);
}

bool ManagedRewindLab::attach(Room *room, QString *error)
{
    if (!room || m_room || QThread::currentThread() != thread()
        || room->getMode() != QLatin1String("02p") || !Config.EnableCheat
        || Config.EnableAI || Config.Enable2ndGeneral || Config.EnableHegemony || Config.EnableMeleeMode
        || room->m_sessionConfig.takeover || room->m_sessionConfig.workLaunch)
        return fail(error, QStringLiteral("Restricted rewind requires cheat-enabled 02p, native TrustAI and no scenario/takeover"));
    m_ownedRoom = false;
    m_network = true;
    m_room = room;
    EngineRuntimeContextScope context(*Sanguosha, room);
    auto &runtime = *room->roomRuntime();
    if (!runtime.lua().initialize(error) || !runtime.ai().lua().initialize(error)) return false;
    m_rootGameId = room->gameTimeline().rootGameId();
    m_worldId = room->gameTimeline().worldId();
    m_busy = false;
    return true;
}

bool ManagedRewindLab::startAttached(QString *error)
{
    if (!m_network || m_started || !m_room || m_room->getPlayers().size() != 2 || !Config.EnableCheat)
        return fail(error, QStringLiteral("Restricted rewind requires two connected seats and room owner start"));
    for (auto *player : m_room->getPlayers()) {
        if (!m_peers.value(player).connected) return fail(error, QStringLiteral("Both seats must be connected"));
        player->setState(QStringLiteral("trust"));
        player->setGeneralName(QStringLiteral("sujiang"));
    }
    m_busy = true;
    return initializePlayers(error);
}

bool ManagedRewindLab::initializePlayers(QString *error)
{
    EngineRuntimeContextScope context(*Sanguosha, m_room);
    auto &runtime = *m_room->roomRuntime();
    LuaRuntime::Binding gameBinding(runtime.lua(), false);
    GameRng::Binding rngBinding(runtime.rng());
    runtime.state().reset();
    const auto players = m_room->getPlayers();
    for (int index = 0; index < players.size(); ++index) {
        auto *player = players[index];
        player->setMaxHp(4);
        player->setHp(3);
        player->setPhase(Player::NotActive);
        player->setRole(index == 0 ? QStringLiteral("lord") : QStringLiteral("rebel"));
        player->setAI(new TrustAI(player));
    }
    for (int index = 0; index < players.size(); ++index) {
        players[index]->setNext(players[(index + 1) % players.size()]);
        players[index]->setSeat(index + 1);
    }
    m_room->m_roster->resetAliveToPlayers();
    m_room->getDrawPile().clear();
    for (const QString &name : {QStringLiteral("slash"), QStringLiteral("jink"), QStringLiteral("peach")}) {
        int count = 0;
        for (int id = 0; id < Sanguosha->getCardCount() && count < 8; ++id) {
            const auto *card = m_room->getCard(id);
            if (card && card->objectName() == name
                && QString::fromLatin1(card->getRealCard()->metaObject()->className()).compare(name, Qt::CaseInsensitive) == 0) {
                m_room->getDrawPile().append(id);
                m_room->setCardMapping(id, nullptr, Player::DrawPile);
                ++count;
            }
        }
    }
    if (m_room->getDrawPile().size() < 12)
        return fail(error, QStringLiteral("required physical Slash/Jink/Peach catalogue is unavailable"));
    qsanShuffle(m_room->getDrawPile());
    if (!m_room->m_gameSession->requestStart()
        || !m_room->m_gameSession->transitionTo(GameSessionController::State::Initializing))
        return fail(error, QStringLiteral("restricted session initialization failed"));
    m_room->markGameReadyCompleted();
    m_room->beginNumericStateHistory();
    m_room->setCurrent(players.first());

    GameState::ProviderRegistry registry;
    if (!registry.registerNativePackage(QStringLiteral("core.rewind-lab"), QStringLiteral("1"),
        QStringLiteral("Private fixed living 02p; exact normal GameRule and stateless TrustAI; no general/skill definitions; physical Slash/Jink/Peach only; no external input providers"), error))
        return false;
    auto *worker = new RoomThread(m_room);
    m_room->thread = worker;
    if (!worker->enableManagedTurns(registry, error)) return false;
    worker->setTimelineCommitObserver([this](const GameTimeline &, const GameTimeline::Anchor &) {
        ++m_restoreNotifications;
    });
    worker->m_managedLab = this;
    connect(worker, &QThread::finished, this, &ManagedRewindLab::stopped);
    m_started = true;
    if (m_network) {
        if (auto *server = qobject_cast<Server *>(m_room->parent()))
            for (auto *player : players) server->signupPlayer(player);
        connect(worker, &QThread::started, m_room, &Room::game_start);
    }
    worker->start();
    return true;
}

bool ManagedRewindLab::submit(const QString &input, QString *error)
{
    if (QThread::currentThread() != thread())
        return fail(error, QStringLiteral("only the local console owner may submit lab commands"));
    const QString command = input.simplified().toLower();
    if (command != QStringLiteral("step") && command != QStringLiteral("status") && command != QStringLiteral("retry stats")
        && command != QStringLiteral("rewind turn") && command != QStringLiteral("rewind round"))
        return fail(error, QStringLiteral("commands: step, rewind turn, rewind round, retry stats, status, quit"));
    QMutexLocker lock(&m_mutex);
    if (!m_started || m_stop || m_busy)
        return fail(error, QStringLiteral("wait for the previous command result before submitting another command"));
    m_command = command;
    m_expectedGeneration = m_visibleGeneration;
    m_busy = true;
    m_wake.wakeOne();
    return true;
}

void ManagedRewindLab::stop()
{
    QMutexLocker lock(&m_mutex);
    m_stop = true;
    m_wake.wakeAll();
}

void ManagedRewindLab::report(const QString &command, bool ok, const QString &error)
{
    const auto *store = m_room->managedState().worldStore();
    const quint64 generation = store ? store->timeline().generation() : 0;
    if (m_network) {
        { QMutexLocker lock(&m_mutex); m_visibleGeneration = generation; }
        networkReport(ok ? QString() : error.left(256));
        return;
    }
    QVariantMap result{{"command", command}, {"ok", ok}, {"error", error},
        {"mode", QStringLiteral("private-restricted-02p")}, {"room_id", m_room->getId()},
        {"root_game_id", store ? store->timeline().rootGameId() : QString()},
        {"generation", QString::number(generation)},
        {"restore_notifications", QString::number(m_restoreNotifications)},
        {"statistics_root", m_room->statisticsRootMatchId()},
        {"statistics_generation", QString::number(m_room->statisticsGeneration())},
        {"statistics_pending", m_room->statisticsRestorePending()},
        {"statistics_path", BattleStatistics::defaultDatabasePath()},
        {"current_player", m_room->getCurrent()->objectName()},
        {"round", m_room->getTag("TurnLengthCount").toInt()},
        {"draw", cardIds(m_room->getDrawPile())}, {"discard", cardIds(m_room->getDiscardPile())},
        {"rng_draws", QString::number(m_room->roomRuntime()->rng().exportState().drawCount)}};
    const auto history = QJsonDocument::fromVariant(m_room->resolutionHistory().snapshot().serialize())
        .toJson(QJsonDocument::Compact);
    result.insert("history_sha256", QString::fromLatin1(QCryptographicHash::hash(history, QCryptographicHash::Sha256).toHex()));
    QVariantList seats;
    for (const auto *player : m_room->getPlayers())
        seats.append(QVariantMap{{"id", player->objectName()}, {"hp", player->getHp()},
            {"armor", player->getHujia()}, {"hand", cardIds(player->handCards())},
            {"phase", int(player->getPhase())}, {"turns", player->getMark("Global_TurnCount")}});
    result.insert("players", seats);
    {
        QMutexLocker lock(&m_mutex);
        m_visibleGeneration = generation;
        m_busy = false;
    }
    emit lineReady(QString::fromUtf8(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact)));
}

void ManagedRewindLab::run(RoomThread &worker)
{
    LuaRuntime::Binding aiBinding(m_room->roomRuntime()->ai().lua());
    m_room->beginBattleStatistics(QStringLiteral("restricted_rewind_lab"));
    GameRule rule(nullptr);
    worker.addTriggerSkill(&rule);
    // Enrollment is performed by the production executor at its first boundary.
    QString error;
    if (!worker.stepNormalTurn(&rule, &error)) {
        if (m_network) { QMutexLocker lock(&m_mutex); m_failure = error; m_stop = true; }
        report(QStringLiteral("ready"), false, error);
        return;
    }
    const auto *roomIdentity = m_room;
    const auto *runtimeIdentity = m_room->roomRuntime();
    const auto *gameVm = runtimeIdentity->lua().rawState();
    const auto *aiVm = runtimeIdentity->ai().lua().rawState();
    const auto players = m_room->getPlayers();
    report(QStringLiteral("ready"), true);
    unsigned advances = 1;
    unsigned restores = 0;
    for (;;) {
        QString command;
        quint64 expectedGeneration;
        {
            QMutexLocker lock(&m_mutex);
            while (!m_stop && m_command.isEmpty()) m_wake.wait(&m_mutex);
            if (m_stop) return;
            command.swap(m_command);
            expectedGeneration = m_expectedGeneration;
            if (m_network && m_requestPeer) {
                const auto peer = m_peers.value(m_requestPeer);
                if (!peer.connected || peer.token != m_requestToken || (command != QStringLiteral("status") && !peer.owner)
                    || m_deadline.elapsed() > 5000) {
                    command = QStringLiteral("cancelled");
                }
            }
            m_executing = true;
        }
        error.clear();
        bool ok = true;
        const auto *store = m_room->managedState().worldStore();
        if (command == QStringLiteral("cancelled")) {
            ok = false; error = QStringLiteral("Control cancelled or expired before execution");
        } else if (!store || expectedGeneration != store->timeline().generation()) {
            ok = false;
            error = QStringLiteral("stale console command generation");
        } else if (command == QStringLiteral("step")) {
            if (advances >= 128) {
                ok = false;
                error = QStringLiteral("restricted lab turn budget exhausted; quit and start a new lab to continue");
            } else {
                ++advances;
                ok = worker.stepNormalTurn(&rule, &error);
            }
        } else if (command == QStringLiteral("retry stats")) {
            ok = m_room->retryStatisticsTimelineRestore();
            if (!ok) error = QStringLiteral("statistics remain quarantined pending durable timeline retry");
        } else if (command.startsWith(QStringLiteral("rewind "))) {
            const auto kind = command == QStringLiteral("rewind turn")
                ? GameTimeline::AnchorKind::PlayerTurn : GameTimeline::AnchorKind::FullRound;
            if (restores >= 128) {
                ok = false;
                error = QStringLiteral("restricted lab restore budget exhausted");
            } else {
                ok = worker.requestManagedRestore(kind, &error) && worker.stepNormalTurn(&rule, &error, false);
                if (ok) ++restores;
            }
        }
        if (m_room != roomIdentity || m_room->roomRuntime() != runtimeIdentity
            || runtimeIdentity->lua().rawState() != gameVm || runtimeIdentity->ai().lua().rawState() != aiVm
            || m_room->getPlayers() != players)
            qFatal("Restricted rewind replaced an enrolled live identity");
        report(command, ok, error);
    }
}

void ManagedRewindLab::connected(ServerPlayer *player)
{
    if (!m_network || !player || player->getRoom() != m_room) return;
    QList<ServerPlayer *> recipients;
    {
        QMutexLocker lock(&m_mutex);
        auto &peer = m_peers[player];
        peer.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        peer.sequence = 0;
        peer.connected = true;
        peer.owner = player->isOwner();
        if (m_started && !m_room->getOwner()) { player->setOwner(true); peer.owner = true; }
        if (m_started && m_busy) m_resyncRequested = true;
        // Reconnect resync is scheduled on the game worker, never read from I/O.
        if (m_started && !m_busy) {
            m_command = QStringLiteral("status");
            m_expectedGeneration = m_visibleGeneration;
            m_requestPeer = nullptr;
            m_busy = true;
            m_wake.wakeOne();
        }
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it)
            if (it.value().connected) recipients.append(it.key());
    }
    for (auto *recipient : recipients) sendStatus(recipient);
}

void ManagedRewindLab::disconnected(ServerPlayer *player)
{
    if (!m_network || !player) return;
    QList<ServerPlayer *> recipients;
    {
        QMutexLocker lock(&m_mutex);
        auto &peer = m_peers[player];
        peer.connected = false;
        peer.token.clear();
        if (peer.owner) {
            peer.owner = false;
            player->setOwner(false);
            for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
                if (it.value().connected) {
                    it.value().owner = true;
                    it.key()->setOwner(true);
                    break;
                }
            }
        }
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it)
            if (it.value().connected) recipients.append(it.key());
    }
    player->setSocket(nullptr);
    for (auto *seat : m_room->getPlayers()) m_room->broadcastProperty(seat, "owner");
    // Transport availability is outside the gameplay snapshot. Keep native
    // TrustAI state fixed, so disconnect cannot create a human request path.
    for (auto *recipient : recipients) sendStatus(recipient, QStringLiteral("A peer disconnected"));
}

void ManagedRewindLab::sendStatus(ServerPlayer *player, const QString &message, const QString &ack)
{
    QSanProtocol::RewindStatusPayload status;
    {
        QMutexLocker lock(&m_mutex);
        const auto peer = m_peers.value(player);
        if (!peer.connected) return;
        status.rootGameId = m_rootGameId;
        status.worldId = m_worldId;
        status.generation = QString::number(m_visibleGeneration);
        status.token = peer.token;
        status.supported = m_started && m_failure.isEmpty();
        status.authorized = peer.owner && Config.EnableCheat;
        status.busy = m_busy;
        status.startAllowed = !m_started && m_failure.isEmpty() && !m_busy
            && status.authorized && m_room && m_room->getPlayers().size() == 2;
        if (status.startAllowed)
            for (auto *seat : m_room->getPlayers())
                if (!m_peers.value(seat).connected) status.startAllowed = false;
        status.ackSequence = ack;
        status.profile = QStringLiteral("restricted_trust_02p");
        status.message = !m_failure.isEmpty() ? m_failure.left(256)
            : message.isEmpty() && !m_started
            ? QStringLiteral("Waiting for owner to start the two-seat restricted room") : message.left(256);
    }
    m_room->doNotify(player, QSanProtocol::S_COMMAND_MANAGED_REWIND_STATE, status.toVariant());
}

void ManagedRewindLab::control(ServerPlayer *player, const QVariant &value)
{
    using namespace QSanProtocol;
    RewindControlPayload payload;
    QString error;
    if (!player || player->getRoom() != m_room) return;
    if (!RewindControlPayload::parse(value, &payload, &error)) { sendStatus(player, error); return; }
    QList<ServerPlayer *> recipients;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_peers.find(player);
        if (it == m_peers.end() || !it->connected) return;
        auto &peer = it.value();
        const bool bootstrap = payload.operation == QLatin1String("status") && payload.token.isEmpty();
        const quint64 sequence = payload.sequence.toULongLong();
        if (m_stop) error = m_failure.isEmpty() ? QStringLiteral("Restricted worker has stopped") : m_failure;
        else if (!bootstrap && (payload.rootGameId != m_rootGameId || payload.worldId != m_worldId
            || payload.generation != QString::number(m_visibleGeneration) || payload.token != peer.token))
            error = QStringLiteral("Stale or foreign room, connection or timeline");
        else if (sequence <= peer.sequence)
            error = QStringLiteral("Repeated or out-of-order control sequence");
        else {
            peer.sequence = sequence;
            const QString operation = payload.operation;
            if (operation == QLatin1String("status")) {
                // Read-only capabilities are available to either connected seat.
            } else if (operation == QLatin1String("cancel")) {
                if (m_requestPeer != player || m_requestToken != peer.token)
                    error = QStringLiteral("No pending control belongs to this connection");
                else if (m_executing)
                    error = QStringLiteral("Execution already started; resynchronize to observe its result");
                else {
                    m_command = QStringLiteral("cancelled");
                    m_wake.wakeOne();
                }
            } else if (!m_started) error = QStringLiteral("Restricted game has not started");
            else if (m_busy) error = QStringLiteral("Another control is in progress; wait for state synchronization");
            else if (operation == QLatin1String("resync") && peer.resyncCooldown.isValid()
                && peer.resyncCooldown.elapsed() < 250)
                error = QStringLiteral("State synchronization is rate limited; retry shortly");
            else if (operation != QLatin1String("resync") && (!peer.owner || !Config.EnableCheat))
                error = QStringLiteral("Only the cheat-enabled room owner may advance or rewind");
            else {
                m_command = operation == QLatin1String("turn") ? QStringLiteral("rewind turn")
                    : operation == QLatin1String("round") ? QStringLiteral("rewind round")
                    : operation == QLatin1String("resync") ? QStringLiteral("status") : operation;
                m_expectedGeneration = m_visibleGeneration;
                m_requestPeer = player;
                m_requestReadOnly = operation == QLatin1String("resync");
                if (m_requestReadOnly) peer.resyncCooldown.start();
                m_requestToken = peer.token;
                m_requestSequence = payload.sequence;
                m_deadline.restart();
                m_busy = true;
                m_wake.wakeOne();
                for (auto iter = m_peers.cbegin(); iter != m_peers.cend(); ++iter)
                    if (iter.value().connected) recipients.append(iter.key());
            }
        }
    }
    if (recipients.isEmpty()) sendStatus(player, error, payload.sequence);
    else for (auto *recipient : recipients) sendStatus(recipient);
}

void ManagedRewindLab::networkReport(const QString &message)
{
    QList<ServerPlayer *> recipients, snapshotRecipients;
    {
        QMutexLocker lock(&m_mutex);
        const auto &timeline = m_room->gameTimeline();
        m_rootGameId = timeline.rootGameId();
        m_worldId = timeline.worldId();
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it)
            if (it.value().connected) recipients.append(it.key());
    }
    {
        QMutexLocker lock(&m_mutex);
        if (m_requestReadOnly && m_requestPeer) {
            if (recipients.contains(m_requestPeer)) snapshotRecipients.append(m_requestPeer);
        } else snapshotRecipients = recipients;
    }
    // marshal constructs each recipient's private view on the worker boundary;
    // the console's omniscient report is deliberately never sent over a socket.
    if (m_room->managedState().worldStore())
        for (auto *recipient : snapshotRecipients) m_room->m_playerLifecycle->marshal(recipient, true);
    ServerPlayer *requestPeer;
    QString sequence;
    {
        QMutexLocker lock(&m_mutex);
        requestPeer = m_requestPeer;
        sequence = m_requestSequence;
        m_requestPeer = nullptr;
        m_requestSequence.clear();
        m_requestReadOnly = false;
        m_executing = false;
        m_busy = m_resyncRequested;
        if (m_resyncRequested) {
            m_resyncRequested = false;
            m_command = QStringLiteral("status");
            m_expectedGeneration = m_visibleGeneration;
            m_wake.wakeOne();
        }
    }
    for (auto *recipient : recipients)
        sendStatus(recipient, message, recipient == requestPeer && !sequence.isEmpty() ? sequence : QStringLiteral("0"));
}

bool ManagedRewindLab::isConnected(ServerPlayer *player)
{
    QMutexLocker lock(&m_mutex);
    return m_peers.value(player).connected;
}

void ManagedRewindLab::beforeTurn()
{
    if (!m_network || m_initialSynced) return;
    m_initialSynced = true;
    QList<ServerPlayer *> recipients;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it)
            if (it.value().connected) recipients.append(it.key());
    }
    for (auto *recipient : recipients) m_room->m_playerLifecycle->marshal(recipient, true);
}
