#include "room-runtime.h"

#include "engine.h"
#include "room.h"
#include "card-lifetime-manager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <cstdio>

namespace {
thread_local QList<RoomRuntime *> activeRoomRuntimes;
}

RoomRuntime::RoomRuntime(Room *room)
    : m_room(room), m_definitions(*Sanguosha), m_lua(LuaRuntime::Game), m_ai(room),
      m_roomState(false), m_loadingDefinitions(false), m_definitionsLoaded(false),
      m_nextDecisionId(0), m_stateRevision(0)
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const CardLifetimeGauge baseline = manager.gauge();
    m_baselineManagedLive = baseline.managed_live;
    m_baselinePendingDelete = baseline.pending_delete;
    m_baselineAdoptionReserved = baseline.adoption_reserved;
    m_baselineWrapperLeases = baseline.wrapper_leases;
    m_baselineNativeLeases = baseline.native_leases;
    m_baselineLuaPinsGauge = baseline.lua_pins;
    m_baselineSidecarEdges = baseline.sidecar_edges;
    m_baselineActuallyDestroyed = baseline.actually_destroyed;
    m_baselineCloneCreated = baseline.clone_created;
    m_baselineFactoryUnclaimed = baseline.factory_unclaimed;
    m_baselineUnknownUnclaimed = baseline.unknown_unclaimed;
    m_baselineEntries = manager.entryCount();
    m_baselineActiveScopes = manager.activeScopeDepth();
    m_baselineLuaPins = manager.luaPinDepth();
    m_baselineAddresses = manager.entryAddresses();
    for (const void *address : m_baselineAddresses)
        if (const auto token = manager.liveToken(address))
            m_baselineTokenEntries.insert(address, token);
    m_definitions.setBaselineAddresses(m_baselineAddresses);
    manager.setDomainBaseline(this, m_baselineAddresses);
    m_lua.setLifetimeDomain(this);
    m_ai.lua().setLifetimeDomain(this);
    m_previousDomain = CardLifetimeManager::setCurrentDomain(this);
    activeRoomRuntimes.append(this);
}

RoomRuntime::~RoomRuntime()
{
    shutdownFinal();
}

void RoomRuntime::finalizeWorker()
{
    // Lua can outlive the worker: a Lua-owned QVariant boxing a CardUseStruct
    // keeps that struct's owning QSharedPointer alive until lua_close(), which
    // used to run in shutdownFinal(). Retiring the domain first left those
    // deleters pointing at freed Cards. Close the room's Lua states here so the
    // finalizers hand ownership back while the Cards are still alive.
    m_ai.shutdown();
    m_lua.shutdown();
    releaseShutdownRoots();
    quint64 retired = 0;
    if (!globalCardLifetimeManager().finalizeWorkerDomain(this, &retired)) {
        globalCardLifetimeManager().dumpDomain(this);
        qFatal("Room worker exited with live worker-affinity Card transients");
    }
    std::fprintf(stdout, "CARD_LIFETIME_WORKER_FINAL retired=%llu\n",
                 static_cast<unsigned long long>(retired));
    std::fflush(stdout);
}

void RoomRuntime::shutdownFinal()
{
    ShutdownState expected = ShutdownState::Running;
    if (!m_shutdownState.compare_exchange_strong(expected, ShutdownState::Closing))
        return;

    if (!onCanonicalOwner()) {
        restorePreviousDomain();
        failShutdown("owner", globalCardLifetimeManager().gauge());
        return;
    }
    if (m_room && !m_room->stopGameThreads(10000)) {
        restorePreviousDomain();
        failShutdown("worker-stop", globalCardLifetimeManager().gauge());
        return;
    }

    const CardLifetimeGauge workerGauge = globalCardLifetimeManager().gauge();
    if (globalCardLifetimeManager().activeScopeDepthForDomain(this) != 0
        || globalCardLifetimeManager().gaugeForDomain(this).lua_pins != 0)
        failShutdown("worker-final", workerGauge);

    releaseShutdownRoots();
    drainShutdownStage("preclose");
    const CardLifetimeGauge precloseGauge = globalCardLifetimeManager().gauge();
    if (globalCardLifetimeManager().activeScopeDepthForDomain(this) != 0
        || globalCardLifetimeManager().gaugeForDomain(this).lua_pins != 0)
        failShutdown("preclose", precloseGauge);

    m_ai.shutdown();
    m_lua.shutdown();
    globalCardLifetimeManager().releaseWrapperBindings(this);
    drainShutdownStage("lua-close");

    const CardLifetimeRuntimeContext previousContext =
        CardLifetimeManager::setCurrentRuntimeContext(this, nullptr, 0, nullptr);
    m_roomState.clear();
    m_definitions.clear();
    CardLifetimeManager::setCurrentRuntimeContext(previousContext.domain,
                                                  previousContext.identity,
                                                  previousContext.generation,
                                                  previousContext.state);
    releaseShutdownRoots();
    invalidateOrphanedRuntimeEntries();
    drainShutdownStage("postclose");
    const CardLifetimeGauge gauge = globalCardLifetimeManager().gauge();
    if (!finalGaugeIsZero(gauge)) {
        globalCardLifetimeManager().dumpDomain(this);
        failShutdown("postclose", gauge);
        return;
    }

    emitFinalGauge(gauge);
    globalCardLifetimeManager().unregisterDomainBaseline(this);
    m_shutdownState = ShutdownState::Closed;
    restorePreviousDomain();
}

void RoomRuntime::shutdownForInitFailure()
{
    ShutdownState expected = ShutdownState::Running;
    if (!m_shutdownState.compare_exchange_strong(expected, ShutdownState::Closing))
        return;

    m_ai.shutdown();
    m_lua.shutdown();
    const CardLifetimeRuntimeContext previousContext =
        CardLifetimeManager::setCurrentRuntimeContext(this, nullptr, 0, nullptr);
    m_roomState.clear();
    m_definitions.clear();
    CardLifetimeManager::setCurrentRuntimeContext(previousContext.domain,
                                                  previousContext.identity,
                                                  previousContext.generation,
                                                  previousContext.state);
    releaseShutdownRoots();
    globalCardLifetimeManager().unregisterDomainBaseline(this);
    m_shutdownState = ShutdownState::Failed;
    restorePreviousDomain();
}

bool RoomRuntime::onCanonicalOwner() const
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const QThread *roomThread = m_room ? m_room->QObject::thread() : nullptr;
    const QThread *canonical = manager.ownerThread();
    return canonical != nullptr && QThread::currentThread() == canonical
        && (!roomThread || roomThread == canonical);
}

quint64 RoomRuntime::drainShutdownStage(const char *stage)
{
    Q_ASSERT(onCanonicalOwner());
    QList<QPointer<QObject>> retiredObjects;
    const quint64 retired = globalCardLifetimeManager().drainDomain(
        this, &retiredObjects);
    if (QCoreApplication::instance()) {
        // A process-wide flush can re-enter another Room's deferred destructor.
        for (const QPointer<QObject> &object : std::as_const(retiredObjects))
            if (object)
                QCoreApplication::sendPostedEvents(
                    object.data(), QEvent::DeferredDelete);
    }
    std::fprintf(stdout, "CARD_LIFETIME_SHUTDOWN_STAGE %s retired=%llu\n",
                 stage, static_cast<unsigned long long>(retired));
    std::fflush(stdout);
    return retired;
}

void RoomRuntime::releaseShutdownRoots()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    manager.releaseEventPayloads(this);
    manager.releaseVariantTags(this);
    manager.releaseVariantTags(&m_definitions);
    manager.releaseVariantTags(&m_roomState);
    manager.releaseVariantTags(&m_lua);
    manager.releaseVariantTags(&m_ai);
    if (m_room) {
        manager.releaseVariantTags(m_room);
        m_room->tag.clear();
        for (ServerPlayer *player : m_room->getPlayers())
            player->clearTags();
    }
}

void RoomRuntime::restorePreviousDomain()
{
    bool expected = false;
    if (!m_domainRestored.compare_exchange_strong(expected, true))
        return;
    activeRoomRuntimes.removeAll(this);
    for (RoomRuntime *runtime : std::as_const(activeRoomRuntimes)) {
        if (runtime->m_previousDomain == this)
            runtime->m_previousDomain = m_previousDomain;
    }
    const void *current = CardLifetimeManager::setCurrentDomain(nullptr);
    if (current != this) {
        CardLifetimeManager::setCurrentDomain(current);
        return;
    }
    CardLifetimeManager::setCurrentDomain(
        activeRoomRuntimes.isEmpty() ? m_previousDomain
                                     : activeRoomRuntimes.constLast());
}

void RoomRuntime::invalidateOrphanedRuntimeEntries()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const QSet<const void *> currentAddresses = manager.entryAddressesForDomain(this);
    for (const void *address : currentAddresses) {
        const auto token = manager.liveToken(address);
        if (token && (!m_baselineTokenEntries.contains(address)
                      || m_baselineTokenEntries.value(address).get() != token.get())
            && !m_runtimeObservedEntries.contains(address))
            m_runtimeObservedEntries.insert(address, token);
    }
    for (auto observed = m_runtimeObservedEntries.cbegin();
         observed != m_runtimeObservedEntries.cend(); ++observed) {
        const void *address = observed.key();
        const auto &token = observed.value();
        if (!token)
            continue;
        const auto baseline = m_baselineTokenEntries.value(address);
        if ((baseline && baseline.get() == token.get())
            || manager.isBaselineToken(this, token))
            continue;
        const QThread *affinity = manager.affinityThread(token);
        if (affinity && affinity != QThread::currentThread())
            continue;
        manager.invalidateIfObserved(token);
    }
}

bool RoomRuntime::finalGaugeIsZero(const CardLifetimeGauge &) const
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const CardLifetimeGauge domainGauge = manager.gaugeForDomain(this);
    const CardLifetimeGauge gameRuntimeGauge = manager.gaugeForRuntime(
        this, m_gameRuntimeIdentity, m_gameRuntimeGeneration, m_gameRuntimeState);
    const CardLifetimeGauge aiRuntimeGauge = manager.gaugeForRuntime(
        this, m_aiRuntimeIdentity, m_aiRuntimeGeneration, m_aiRuntimeState);
    const auto runtimeHasWork = [](const CardLifetimeGauge &runtimeGauge) {
        return runtimeGauge.managed_live != 0
            || runtimeGauge.factory_unclaimed != 0
            || runtimeGauge.unknown_unclaimed != 0
            || runtimeGauge.pending_delete != 0
            || runtimeGauge.adoption_reserved != 0
            || runtimeGauge.wrapper_leases != 0
            || runtimeGauge.native_leases != 0
            || runtimeGauge.sidecar_edges != 0
            || runtimeGauge.lua_pins != 0;
    };
    if (m_gameRuntimeIdentity && m_aiRuntimeIdentity
        && m_gameRuntimeIdentity == m_aiRuntimeIdentity)
        return false;
    if (runtimeHasWork(gameRuntimeGauge) || runtimeHasWork(aiRuntimeGauge))
        return false;
    if (domainGauge.managed_live != 0
        || domainGauge.factory_unclaimed != 0
        || domainGauge.unknown_unclaimed != 0
        || domainGauge.pending_delete != 0
        || domainGauge.adoption_reserved != 0
        || domainGauge.wrapper_leases != 0
        || domainGauge.native_leases != 0
        || domainGauge.lua_pins != 0
        || domainGauge.sidecar_edges != 0
        || manager.entryCountForDomain(this) != 0
        || manager.activeScopeDepthForDomain(this) != 0)
        return false;
    return true;
}

void RoomRuntime::failShutdown(const char *stage, const CardLifetimeGauge &gauge)
{
    std::fprintf(stderr, "ROOM_RUNTIME_FAIL stage=%s live=%llu pending=%llu reservations=%llu wrappers=%llu leases=%llu pins=%llu entries=%llu\n",
                 stage,
                 static_cast<unsigned long long>(gauge.managed_live),
                 static_cast<unsigned long long>(gauge.pending_delete),
                 static_cast<unsigned long long>(gauge.adoption_reserved),
                 static_cast<unsigned long long>(gauge.wrapper_leases),
                 static_cast<unsigned long long>(gauge.native_leases),
                 static_cast<unsigned long long>(gauge.lua_pins),
                 static_cast<unsigned long long>(globalCardLifetimeManager().entryCount()));
    m_shutdownState = ShutdownState::Failed;
    restorePreviousDomain();
    QJsonObject details;
    details.insert(QStringLiteral("managed_live"), qint64(gauge.managed_live));
    details.insert(QStringLiteral("pending_delete"), qint64(gauge.pending_delete));
    details.insert(QStringLiteral("adoption_reserved"), qint64(gauge.adoption_reserved));
    details.insert(QStringLiteral("wrapper_leases"), qint64(gauge.wrapper_leases));
    details.insert(QStringLiteral("native_leases"), qint64(gauge.native_leases));
    details.insert(QStringLiteral("lua_pins"), qint64(gauge.lua_pins));
    details.insert(QStringLiteral("sidecar_edges"), qint64(gauge.sidecar_edges));
    details.insert(QStringLiteral("entries"), qint64(globalCardLifetimeManager().entryCount()));
    details.insert(QStringLiteral("active_scopes"), qint64(globalCardLifetimeManager().activeScopeDepth()));
    const QByteArray failure = QJsonDocument(details).toJson(QJsonDocument::Compact);
    std::fprintf(stderr, "CARD_LIFETIME_SHUTDOWN_FAILED stage=%s %s\n",
                 stage, failure.constData());
    qFatal("RoomRuntime shutdown failed with non-zero Card lifetime gauges");
}

void RoomRuntime::emitFinalGauge(const CardLifetimeGauge &gauge)
{
    if (m_finalMarkerEmitted)
        return;
    m_finalMarkerEmitted = true;
    CardLifetimeManager &manager = globalCardLifetimeManager();
    const CardLifetimeGauge domainGauge = manager.gaugeForDomain(this);
    const CardLifetimeGauge gameRuntimeGauge = manager.gaugeForRuntime(
        this, m_gameRuntimeIdentity, m_gameRuntimeGeneration, m_gameRuntimeState);
    const CardLifetimeGauge aiRuntimeGauge = manager.gaugeForRuntime(
        this, m_aiRuntimeIdentity, m_aiRuntimeGeneration, m_aiRuntimeState);
    const auto runtimeGaugeObject = [](const void *identity, quint64 generation,
                                       lua_State *state,
                                       const CardLifetimeGauge &runtimeGauge) {
        QJsonObject result;
        result.insert(QStringLiteral("identity"),
                      QString::number(static_cast<qulonglong>(
                          reinterpret_cast<quintptr>(identity)), 16));
        result.insert(QStringLiteral("generation"), qint64(generation));
        result.insert(QStringLiteral("state_present"), state != nullptr);
        result.insert(QStringLiteral("managed_live"), qint64(runtimeGauge.managed_live));
        result.insert(QStringLiteral("pending_delete"), qint64(runtimeGauge.pending_delete));
        result.insert(QStringLiteral("adoption_reserved"), qint64(runtimeGauge.adoption_reserved));
        result.insert(QStringLiteral("wrapper_leases"), qint64(runtimeGauge.wrapper_leases));
        result.insert(QStringLiteral("native_leases"), qint64(runtimeGauge.native_leases));
        result.insert(QStringLiteral("sidecar_edges"), qint64(runtimeGauge.sidecar_edges));
        result.insert(QStringLiteral("lua_pins"), qint64(runtimeGauge.lua_pins));
        return result;
    };
    QJsonObject marker;
    marker.insert(QStringLiteral("event"), QStringLiteral("[CardLifetime] FINAL_GAUGE"));
    marker.insert(QStringLiteral("managed_live"), qint64(domainGauge.managed_live));
    marker.insert(QStringLiteral("pending_delete"), qint64(domainGauge.pending_delete));
    marker.insert(QStringLiteral("adoption_reserved"), qint64(domainGauge.adoption_reserved));
    marker.insert(QStringLiteral("wrapper_leases"), qint64(domainGauge.wrapper_leases));
    marker.insert(QStringLiteral("native_leases"), qint64(domainGauge.native_leases));
    marker.insert(QStringLiteral("lua_pins"), qint64(domainGauge.lua_pins));
    marker.insert(QStringLiteral("sidecar_edges"), qint64(domainGauge.sidecar_edges));
    marker.insert(QStringLiteral("entries"), qint64(manager.entryCountForDomain(this)));
    marker.insert(QStringLiteral("active_scopes"), qint64(manager.activeScopeDepthForDomain(this)));
    marker.insert(QStringLiteral("runtime_delta_complete"), true);
    marker.insert(QStringLiteral("game_runtime_delta"), runtimeGaugeObject(
        m_gameRuntimeIdentity, m_gameRuntimeGeneration, m_gameRuntimeState, gameRuntimeGauge));
    marker.insert(QStringLiteral("ai_runtime_delta"), runtimeGaugeObject(
        m_aiRuntimeIdentity, m_aiRuntimeGeneration, m_aiRuntimeState, aiRuntimeGauge));
    marker.insert(QStringLiteral("clone_created_delta"), qint64(gauge.clone_created - m_baselineCloneCreated));
    marker.insert(QStringLiteral("factory_unclaimed_delta"), qint64(gauge.factory_unclaimed - m_baselineFactoryUnclaimed));
    marker.insert(QStringLiteral("unknown_unclaimed_delta"), qint64(gauge.unknown_unclaimed - m_baselineUnknownUnclaimed));
    marker.insert(QStringLiteral("actually_destroyed_delta"), qint64(gauge.actually_destroyed - m_baselineActuallyDestroyed));
    const CardLifetimeMutexProfile mutexProfile = manager.mutexProfile();
    if (mutexProfile.enabled) {
        QJsonObject mutexProfileObject;
        mutexProfileObject.insert(QStringLiteral("scope"), QStringLiteral("process_cumulative"));
        mutexProfileObject.insert(QStringLiteral("lock_count"), qint64(mutexProfile.lock_count));
        mutexProfileObject.insert(QStringLiteral("contended_count"), qint64(mutexProfile.contended_count));
        mutexProfileObject.insert(QStringLiteral("wait_ns"), mutexProfile.wait_ns);
        mutexProfileObject.insert(QStringLiteral("max_wait_ns"), mutexProfile.max_wait_ns);
        marker.insert(QStringLiteral("mutex_profile"), mutexProfileObject);
    }
    const QByteArray markerJson = QJsonDocument(marker).toJson(QJsonDocument::Compact);
    std::fprintf(stdout, "CARD_LIFETIME_ZERO %s\n", markerJson.constData());
    std::fflush(stdout);
}

bool RoomRuntime::initialize(QString *error)
{
    if (shutdownState() != ShutdownState::Running) {
        if (error)
            *error = QStringLiteral("Room runtime is closing");
        return false;
    }
    if (!m_lua.initialize(error))
        return false;
    m_gameRuntimeIdentity = &m_lua;
    m_gameRuntimeGeneration = m_lua.generation();
    m_gameRuntimeState = m_lua.rawState();
    LuaRuntime::Binding luaBinding(m_lua);
    if (!m_lua.addPackagePath(QStringLiteral("./lua/?.lua"), error)
        || !m_lua.addPackagePath(QStringLiteral("./lua/?/init.lua"), error))
        return false;
    EngineRuntimeContextScope contextScope(*Sanguosha, this);
    m_loadingDefinitions = true;
    const bool loaded = m_lua.loadScript(QStringLiteral("lua/config.lua"), error)
        && m_lua.loadScript(QStringLiteral("lua/sanguosha.lua"), error);
    m_loadingDefinitions = false;
    if (!loaded)
        return false;
    if (!m_lua.loadScript(QStringLiteral("lua/ai/smart-ai.lua"), error))
        return false;

    const QSet<const void *> currentAddresses =
        globalCardLifetimeManager().entryAddressesForDomain(this);
    for (const void *address : currentAddresses)
        if (const auto token = globalCardLifetimeManager().liveToken(address))
            if ((!m_baselineTokenEntries.contains(address)
                 || m_baselineTokenEntries.value(address).get() != token.get())
                && !m_runtimeObservedEntries.contains(address))
                m_runtimeObservedEntries.insert(address, token);

    QString aiError;
    m_aiRuntimeIdentity = &m_ai.lua();
    if (!m_ai.initialize(&aiError))
        qWarning().noquote() << "AI Lua runtime disabled:" << aiError;
    else {
        m_aiRuntimeGeneration = m_ai.lua().generation();
        m_aiRuntimeState = m_ai.lua().rawState();
    }
    return true;
}

void RoomRuntime::seedRandom(quint64 seed)
{
    if (shutdownState() != ShutdownState::Running) {
        qWarning() << "Ignoring random seed after Room runtime shutdown began";
        return;
    }
    m_rng.seed(seed);
    m_lua.setSeed(seed);
    m_ai.seed(seed);
}

void RoomRuntime::advanceStateRevision(StateMutation mutation)
{
    Q_UNUSED(mutation);
    ++m_stateRevision;
    if (m_stateRevision == 0)
        ++m_stateRevision;
}

void RoomRuntime::addPackage(Package *package)
{
    m_definitions.addPackage(package);
}

void RoomRuntime::setPackage(Package *package)
{
    m_definitions.setPackage(package);
}

void RoomRuntime::addSkills(const QList<const Skill *> &skills)
{
    m_definitions.addSkills(skills);
}

void RoomRuntime::addTranslationEntry(const QString &key, const QString &value)
{
    m_definitions.addTranslationEntry(key, value);
}

QString RoomRuntime::translate(const QString &key, bool initial) const
{
    return m_definitions.translate(key, initial);
}

bool RoomRuntime::hasTranslation(const QString &key) const
{
    return m_definitions.hasTranslation(key);
}

const Package *RoomRuntime::package(const QString &name) const
{
    return m_definitions.package(name);
}

Package *RoomRuntime::packageOverlay(const Package *basePackage)
{
    return m_definitions.packageOverlay(basePackage);
}

QList<const Package *> RoomRuntime::packages() const
{
    return m_definitions.packages();
}

const General *RoomRuntime::general(const QString &name) const
{
    return m_definitions.general(name);
}

QList<const General *> RoomRuntime::generals() const
{
    return m_definitions.generals();
}

const Skill *RoomRuntime::skill(const QString &name) const
{
    return m_definitions.skill(name);
}

QStringList RoomRuntime::skillNames() const
{
    return m_definitions.skillNames();
}

QList<const Skill *> RoomRuntime::allSkills() const
{
    return m_definitions.allSkills();
}

const Card *RoomRuntime::engineCard(int id) const
{
    return m_definitions.engineCard(id);
}

const Card *RoomRuntime::cardTemplate(const QString &name) const
{
    return m_definitions.cardTemplate(name);
}

int RoomRuntime::cardCount(int bootstrapCount) const
{
    return m_definitions.cardCount(bootstrapCount);
}

const CardPattern *RoomRuntime::pattern(const QString &name) const
{
    return m_definitions.pattern(name);
}

QStringList RoomRuntime::relatedSkillNames(const QString &name) const
{
    return m_definitions.relatedSkillNames(name);
}

QList<const ProhibitSkill *> RoomRuntime::prohibitSkills() const
{
    return m_definitions.prohibitSkills();
}

QList<const DistanceSkill *> RoomRuntime::distanceSkills() const
{
    return m_definitions.distanceSkills();
}

QList<const MaxCardsSkill *> RoomRuntime::maxCardsSkills() const
{
    return m_definitions.maxCardsSkills();
}

QList<const TargetModSkill *> RoomRuntime::targetModSkills() const
{
    return m_definitions.targetModSkills();
}

QList<const InvaliditySkill *> RoomRuntime::invaliditySkills() const
{
    return m_definitions.invaliditySkills();
}

QList<const TriggerSkill *> RoomRuntime::globalTriggerSkills() const
{
    return m_definitions.globalTriggerSkills();
}

QList<const AttackRangeSkill *> RoomRuntime::attackRangeSkills() const
{
    return m_definitions.attackRangeSkills();
}

QList<const ViewAsEquipSkill *> RoomRuntime::viewAsEquipSkills() const
{
    return m_definitions.viewAsEquipSkills();
}

QList<const CardLimitSkill *> RoomRuntime::cardLimitSkills() const
{
    return m_definitions.cardLimitSkills();
}

QList<const ProhibitPindianSkill *> RoomRuntime::prohibitPindianSkills() const
{
    return m_definitions.prohibitPindianSkills();
}

void RoomRuntime::registerLuaSkillCard(const QString &name, const LuaSkillCard *card)
{
    m_definitions.registerLuaSkillCard(name, card);
}

const LuaSkillCard *RoomRuntime::luaSkillCard(const QString &name) const
{
    return m_definitions.luaSkillCard(name);
}

QObject *RoomRuntime::runtimeObject()
{
    return m_room;
}
