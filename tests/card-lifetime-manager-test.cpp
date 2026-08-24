#include "card-lifetime-manager.h"
#include "card-lifetime-test-check.h"
#include "card.h"
#include "engine-bootstrap.h"
#include "lua-runtime.h"
#include "room-test-access.h"
#include "wrapped-card.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QStringList>
#include "lua.hpp"
#include <cstdio>
#include <limits>
#include <new>

// An unregistered Card-bearing metatype: not part of the N8.3 frozen matrix, so
// the manager must reject it instead of guessing at its layout.
struct UnregisteredCardPayload
{
    const Card *card = nullptr;
};
Q_DECLARE_METATYPE(UnregisteredCardPayload)

namespace {
int liveObject = 0;
int otherObject = 0;

class DispatcherProbe final : public QObject
{
public:
    explicit DispatcherProbe(int *destroyed) : m_destroyed(destroyed) { }
    ~DispatcherProbe() override { ++*m_destroyed; }
private:
    int *m_destroyed;
};

bool probeCanonicalOwner()
{
    QThread *caller = QThread::currentThread();
    QThread owner;
    owner.start();
    QObject context;
    context.moveToThread(&owner);
    bool dispatcherAlive = false;
    int destroyed = 0;
    QMetaObject::invokeMethod(&context, [&] {
        dispatcherAlive = QThread::currentThread()->eventDispatcher() != nullptr;
        auto *probe = new DispatcherProbe(&destroyed);
        probe->moveToThread(QThread::currentThread());
        QTimer::singleShot(0, probe, &QObject::deleteLater);
    }, Qt::BlockingQueuedConnection);
    QThread::msleep(10);
    QMetaObject::invokeMethod(&context, [&] {
        context.moveToThread(caller);
    }, Qt::BlockingQueuedConnection);
    owner.quit();
    owner.wait();
    return dispatcherAlive && destroyed == 1;
}

bool probeRoomMode(const QString &mode)
{
    Room room(nullptr, mode);
    RoomTestAccess::attachThread(room);
    QThread *owner = RoomTestAccess::canonicalThread(room);
    if (owner == nullptr)
        return false;
    const bool dispatcherAlive = owner->thread()->eventDispatcher() != nullptr;
    return dispatcherAlive && room.getMode() == mode;
}

bool probeWrappedCardCanonicalOwner(const QString &mode)
{
    Room room(nullptr, mode);
    RoomTestAccess::attachThread(room);
    auto *inner = new DummyCard;
    bool result = false;
    {
        WrappedCard outer(inner);
        QThread *canonical = room.QObject::thread();
        result = outer.thread() == canonical && inner->thread() == canonical;
    }
    return result;
}

bool probeBlockers12()
{
    CardLifetimeManager manager(CardLifetimeMode::ManagedReclaim);

    auto *retiredCard = new DummyCard;
    const auto retiredToken = manager.observeCard(retiredCard);
    const bool retiredRequested = retiredToken && manager.requestNativeDelete(retiredToken);
    const quint64 retiredCount = manager.drain();
    const auto reobservedRetired = manager.observeLive(retiredCard);
    const bool retiredRefused = retiredRequested && retiredCount == 1
        && !reobservedRetired && manager.state(retiredToken) == CardLifetimeState::Retired;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const bool retiredDestroyed = !manager.isLive(retiredToken)
        && manager.state(retiredToken) == CardLifetimeState::Dead;

    auto *repeatedCard = new DummyCard;
    const auto repeatedToken = manager.observeCard(repeatedCard);
    const auto repeatedAgain = manager.observeCard(repeatedCard);
    const auto repeatedThird = manager.observeCard(repeatedCard);
    const quint64 destroyedBeforeRepeated = manager.gauge().actually_destroyed;
    delete repeatedCard;
    const bool repeatedObservationIdempotent = repeatedToken && repeatedAgain == repeatedToken
        && repeatedThird == repeatedToken
        && manager.gauge().actually_destroyed == destroyedBeforeRepeated + 1;

    void *storage = ::operator new(sizeof(DummyCard));
    int wrapperIdentity = 0;
    auto *firstCard = new (storage) DummyCard;
    const auto firstToken = manager.observeCard(firstCard);
    const bool firstWrapperRetained = firstToken && manager.retainWrapper(firstToken);
    const bool firstWrapperBound = firstWrapperRetained
        && manager.bindWrapper(&wrapperIdentity, firstToken);
    const bool firstNativeRetained = firstWrapperBound
        && manager.retainNativeLease(firstToken);
    firstCard->~DummyCard();
    auto *secondCard = new (storage) DummyCard;
    const auto secondToken = manager.observeCard(secondCard);
    const auto staleWrapperBinding = manager.wrapperBinding(&wrapperIdentity);
    const bool reusedAddressGetsNewGeneration = firstToken && secondToken
        && firstToken->generation != secondToken->generation
        && !manager.isLive(firstToken) && manager.isLive(secondToken)
        && staleWrapperBinding == firstToken;
    const auto releasedWrapperBinding = manager.releaseWrapperBinding(&wrapperIdentity);
    const bool releasedOldNativeLease = manager.releaseNativeLease(firstToken);
    const bool releasingOldGenerationLeavesNewLive = releasedWrapperBinding == firstToken
        && releasedOldNativeLease && !manager.isLive(firstToken)
        && manager.isLive(secondToken)
        && manager.gauge().wrapper_leases == 0
        && manager.gauge().native_leases == 0;
    secondCard->~DummyCard();
    const bool resetAfterCompleteReuseCleanup = manager.resetForTest();
    ::operator delete(storage);

    return retiredRefused && retiredDestroyed && repeatedObservationIdempotent
        && firstWrapperRetained && firstWrapperBound && firstNativeRetained
        && reusedAddressGetsNewGeneration && releasingOldGenerationLeavesNewLive
        && resetAfterCompleteReuseCleanup;
}

bool probeResidualGaugeProducersAndDomainIsolation()
{
    CardLifetimeManager isolation(CardLifetimeMode::ManagedReclaim);
    static int domainA = 0;
    static int domainB = 0;
    int pendingA = 0;
    const void *previousDomain = CardLifetimeManager::setCurrentDomain(&domainA);
    const auto pendingToken = isolation.observeLive(&pendingA);
    const bool pendingRequested = pendingToken && isolation.requestNativeDelete(pendingToken);
    CardLifetimeManager::setCurrentDomain(&domainB);
    isolation.enterScope();
    isolation.enterLuaPin();
    const quint64 isolatedDrain = isolation.drain();
    isolation.leaveLuaPin();
    isolation.leaveScope();
    CardLifetimeManager::setCurrentDomain(previousDomain);
    const bool crossDomainIsolation = pendingRequested && isolatedDrain == 1
        && isolation.state(pendingToken) == CardLifetimeState::Retired;

    CardLifetimeManager producer(CardLifetimeMode::ManagedReclaim);
    int wrapperIdentity = 0;
    auto *factoryCard = new DummyCard;
    const auto factoryToken = producer.recordFactoryClone(factoryCard);
    const CardLifetimeGauge factoryGauge = producer.gauge();
    const bool factoryProduced = factoryToken && factoryGauge.clone_created == 1
        && factoryGauge.factory_unclaimed == 1;
    producer.recordOwningFactoryResult(factoryToken);
    const bool factoryClaimed = producer.retainWrapper(factoryToken)
        && producer.bindWrapper(&wrapperIdentity, factoryToken, true)
        && producer.gauge().factory_unclaimed == 0
        && producer.gauge().unknown_unclaimed == 0;
    producer.releaseWrapperBinding(&wrapperIdentity);
    CARD_LIFETIME_CHECK(producer.requestNativeDelete(factoryToken));
    CARD_LIFETIME_CHECK(producer.drain() == 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    auto *unknownCard = new DummyCard;
    const auto unknownToken = producer.observeCard(unknownCard);
    producer.recordOwningFactoryResult(unknownToken);
    const bool unknownProduced = unknownToken && producer.gauge().unknown_unclaimed == 1;
    CARD_LIFETIME_CHECK(producer.retainWrapper(unknownToken));
    CARD_LIFETIME_CHECK(producer.bindWrapper(&wrapperIdentity, unknownToken, true));
    const bool unknownClaimed = producer.gauge().unknown_unclaimed == 0;
    producer.releaseWrapperBinding(&wrapperIdentity);
    CARD_LIFETIME_CHECK(producer.requestNativeDelete(unknownToken));
    CARD_LIFETIME_CHECK(producer.drain() == 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const quint64 bypassBefore = producer.gauge().card_delete_bypass;
    auto *bypassCard = new DummyCard;
    CARD_LIFETIME_CHECK(producer.observeCard(bypassCard));
    producer.recordDeferredDelete(bypassCard);
    const bool bypassProduced = producer.gauge().card_delete_bypass == bypassBefore + 1;
    delete bypassCard;

    const quint64 rawBefore = producer.gauge().unapproved_card_raw_delete;
    auto *rawCard = new DummyCard;
    CARD_LIFETIME_CHECK(producer.observeCard(rawCard));
    delete rawCard;
    const bool rawDeleteProduced = producer.gauge().unapproved_card_raw_delete
        == rawBefore + 1;

    const quint64 externalBefore = producer.gauge().external_direct_destroy;
    auto *externalCard = new DummyCard;
    const auto externalToken = producer.observeCard(externalCard);
    CARD_LIFETIME_CHECK(externalToken && producer.retainNativeLease(externalToken));
    delete externalCard;
    const bool externalDestroyProduced = producer.gauge().external_direct_destroy
        == externalBefore + 1;
    CARD_LIFETIME_CHECK(producer.releaseNativeLease(externalToken));

    fprintf(stdout,
            "CARD_LIFETIME_RESIDUAL_DETAIL isolated=%d factory=%d claimed=%d unknown=%d unknown_claimed=%d bypass=%d raw=%d external=%d\n",
            crossDomainIsolation, factoryProduced, factoryClaimed, unknownProduced,
            unknownClaimed, bypassProduced, rawDeleteProduced, externalDestroyProduced);

    return crossDomainIsolation && factoryProduced && factoryClaimed
        && unknownProduced && unknownClaimed && bypassProduced
        && rawDeleteProduced && externalDestroyProduced;
}

bool probeBlocker4CorePins()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    CardLifetimeManager unrelated(CardLifetimeMode::ObserveOnly);
    static int runtimeDomain = 0;
    static int unrelatedDomain = 0;
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    runtime.setLifetimeDomain(&runtimeDomain);
    if (!runtime.initialize())
        return false;
    const quint64 runtimeGeneration = runtime.generation();
    lua_State *runtimeState = runtime.rawState();

    unrelated.registerRuntimeDomain(&unrelatedDomain, &runtime, runtimeGeneration,
                                    runtimeState);
    bool managerOwnerDiffers = false;
    bool workerRan = false;
    bool outerPin = false;
    bool nestedDepth = false;
    bool nestedPin = false;
    bool errorReturned = false;
    bool pinSurvivedError = false;
    bool pinSurvivedInnerExit = false;
    bool pinReleased = false;
    bool unrelatedUnpinned = false;
    QThread *worker = QThread::create([&] {
        LuaRuntime::Binding binding(runtime);
        managerOwnerDiffers = manager.ownerThread() != QThread::currentThread();
        workerRan = runtime.state() != nullptr && LuaRuntime::current() == &runtime;
        {
            LuaRuntime::LuaInvocationScope outer(runtime);
            const auto outerGauge = manager.gaugeForRuntime(
                runtime.lifetimeDomain(), &runtime, runtime.generation(), runtime.rawState());
            outerPin = runtime.invocationDepth() == 1 && outerGauge.lua_pins == 1;
            unrelatedUnpinned = unrelated.gauge().lua_pins == 0;
            {
                LuaRuntime::LuaInvocationScope nested(runtime);
                const auto nestedGauge = manager.gaugeForRuntime(
                    runtime.lifetimeDomain(), &runtime, runtime.generation(), runtime.rawState());
                nestedDepth = runtime.invocationDepth() == 2;
                nestedPin = nestedGauge.lua_pins == 1;
                lua_State *state = runtime.state();
                if (state && luaL_loadstring(state, "error('blocker4 nested error')") == 0) {
                    errorReturned = lua_pcall(state, 0, 0, 0) != 0;
                    lua_pop(state, 1);
                }
                pinSurvivedError = manager.gaugeForRuntime(
                    runtime.lifetimeDomain(), &runtime, runtime.generation(), runtime.rawState())
                    .lua_pins == 1;
            }
            pinSurvivedInnerExit = manager.gaugeForRuntime(
                runtime.lifetimeDomain(), &runtime, runtime.generation(), runtime.rawState())
                .lua_pins == 1;
        }
        pinReleased = manager.gaugeForRuntime(
            runtime.lifetimeDomain(), &runtime, runtime.generation(), runtime.rawState())
            .lua_pins == 0;
    });
    worker->start();
    worker->wait();
    delete worker;
    unrelated.unregisterRuntimeDomain(&unrelatedDomain, &runtime, runtimeGeneration,
                                      runtimeState);
    {
        LuaRuntime::Binding binding(runtime);
        runtime.shutdown();
    }
    fprintf(stdout,
            "CARD_LIFETIME_BLOCKER_4_CORE_DETAIL owner_diff=%d worker=%d outer=%d depth=%d nested=%d error=%d survived_error=%d survived_inner=%d released=%d unrelated=%d\n",
            managerOwnerDiffers, workerRan, outerPin, nestedDepth, nestedPin, errorReturned,
            pinSurvivedError, pinSurvivedInnerExit, pinReleased, unrelatedUnpinned);
    return managerOwnerDiffers && workerRan && outerPin && nestedDepth && nestedPin
        && errorReturned && pinSurvivedError && pinSurvivedInnerExit && pinReleased
        && unrelatedUnpinned;
}

bool probeRoomStateCanonicalReset()
{
    Room room(nullptr, QStringLiteral("03_1v2"));
    RoomTestAccess::attachThread(room);
    RoomState *state = room.getRoomState();
    state->reset();
    Card *outer = state->getCard(0);
    if (!outer || outer->thread() != room.QObject::thread())
        return false;
    state->resetCard(0);
    return outer->thread() == room.QObject::thread()
        && qobject_cast<WrappedCard *>(outer) != nullptr;
}

struct LifetimeChildReceipt
{
    QStringList arguments;
    QByteArray standardOutput;
    QByteArray standardError;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    int exitCode = std::numeric_limits<int>::min();
    bool started = false;
    bool finished = false;
    bool timedOut = false;
    bool terminateSent = false;
    bool killSent = false;
    bool cleanupComplete = false;
    QList<QString> shutdownStages;
    QList<QString> shutdownFailureStages;
    QList<QJsonObject> shutdownFailures;

    QByteArray combinedOutput() const
    {
        return standardOutput + standardError;
    }

    int markerCount(const QByteArray &marker) const
    {
        return combinedOutput().count(marker);
    }
};

void terminateLifetimeChild(QProcess &child, LifetimeChildReceipt &receipt)
{
    if (child.state() == QProcess::NotRunning) {
        receipt.cleanupComplete = true;
        return;
    }
    const qint64 childPid = child.processId();
    child.terminate();
    receipt.terminateSent = true;
    if (!child.waitForFinished(1000)) {
#ifdef _WIN32
        QProcess taskkill;
        taskkill.start(QStringLiteral("taskkill"),
                       {QStringLiteral("/PID"), QString::number(childPid),
                        QStringLiteral("/T"), QStringLiteral("/F")});
        taskkill.waitForFinished(5000);
#endif
        child.kill();
        receipt.killSent = true;
        child.waitForFinished(5000);
    }
    receipt.cleanupComplete = child.state() == QProcess::NotRunning;
}

void parseLifetimeReceipt(LifetimeChildReceipt &receipt)
{
    const QByteArray output = receipt.combinedOutput();
    const QList<QByteArray> lines = output.split('\n');
    const QRegularExpression stagePattern(
        QStringLiteral("CARD_LIFETIME_SHUTDOWN_STAGE\\s+([^\\s]+)\\s+retired=(\\d+)"));
    const QRegularExpression failurePattern(
        QStringLiteral("CARD_LIFETIME_SHUTDOWN_FAILED\\s+stage=([^\\s]+)\\s+(\\{.*\\})"));
    for (const QByteArray &line : lines) {
        const QString text = QString::fromUtf8(line).trimmed();
        const QRegularExpressionMatch stage = stagePattern.match(text);
        if (stage.hasMatch())
            receipt.shutdownStages.push_back(stage.captured(1));
        const QRegularExpressionMatch failure = failurePattern.match(text);
        if (failure.hasMatch()) {
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(
                failure.captured(2).toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && document.isObject()) {
                receipt.shutdownFailureStages.push_back(failure.captured(1));
                receipt.shutdownFailures.push_back(document.object());
            }
        }
    }
}

LifetimeChildReceipt runLifetimeChild(const QStringList &arguments, int timeoutMs = 15000)
{
    LifetimeChildReceipt receipt;
    receipt.arguments = arguments;
    QProcess child;
    child.setProcessChannelMode(QProcess::SeparateChannels);
    child.start(QCoreApplication::applicationFilePath(), arguments);
    receipt.started = child.waitForStarted(5000);
    if (!receipt.started) {
        receipt.standardError = child.errorString().toUtf8();
        receipt.cleanupComplete = child.state() == QProcess::NotRunning;
        return receipt;
    }
    if (!child.waitForFinished(timeoutMs)) {
        receipt.timedOut = true;
        terminateLifetimeChild(child, receipt);
    } else {
        receipt.cleanupComplete = child.state() == QProcess::NotRunning;
    }
    receipt.standardOutput = child.readAllStandardOutput();
    receipt.standardError = child.readAllStandardError();
    receipt.finished = child.state() == QProcess::NotRunning && !receipt.timedOut;
    receipt.exitStatus = child.exitStatus();
    if (receipt.finished || receipt.timedOut)
        receipt.exitCode = child.exitCode();
    parseLifetimeReceipt(receipt);
    return receipt;
}

QJsonObject lifetimeChildSummary(const LifetimeChildReceipt &receipt)
{
    QJsonArray stages;
    for (const QString &stage : receipt.shutdownStages)
        stages.append(stage);
    QJsonArray failureStages;
    for (const QString &stage : receipt.shutdownFailureStages)
        failureStages.append(stage);
    QJsonArray failures;
    for (const QJsonObject &failure : receipt.shutdownFailures)
        failures.append(failure);
    QJsonArray arguments;
    for (const QString &argument : receipt.arguments)
        arguments.append(argument);
    QJsonObject summary;
    summary.insert(QStringLiteral("arguments"), arguments);
    summary.insert(QStringLiteral("started"), receipt.started);
    summary.insert(QStringLiteral("finished"), receipt.finished);
    summary.insert(QStringLiteral("timed_out"), receipt.timedOut);
    summary.insert(QStringLiteral("cleanup_complete"), receipt.cleanupComplete);
    summary.insert(QStringLiteral("terminate_sent"), receipt.terminateSent);
    summary.insert(QStringLiteral("kill_sent"), receipt.killSent);
    summary.insert(QStringLiteral("exit_status"), receipt.exitStatus == QProcess::NormalExit
                      ? QStringLiteral("normal") : QStringLiteral("crash"));
    summary.insert(QStringLiteral("exit_status_code"), static_cast<int>(receipt.exitStatus));
    summary.insert(QStringLiteral("exit_code_valid"),
                   receipt.finished && !receipt.timedOut);
    summary.insert(QStringLiteral("exit_code"), receipt.exitCode);
    summary.insert(QStringLiteral("stdout_bytes"), receipt.standardOutput.size());
    summary.insert(QStringLiteral("stderr_bytes"), receipt.standardError.size());
    summary.insert(QStringLiteral("zero_marker_count"),
                   receipt.markerCount("CARD_LIFETIME_ZERO"));
    summary.insert(QStringLiteral("room_runtime_fail_count"),
                   receipt.markerCount("ROOM_RUNTIME_FAIL"));
    summary.insert(QStringLiteral("shutdown_failed_marker_count"),
                   receipt.markerCount("CARD_LIFETIME_SHUTDOWN_FAILED"));
    summary.insert(QStringLiteral("shutdown_stage_count"), receipt.shutdownStages.size());
    summary.insert(QStringLiteral("shutdown_stages"), stages);
    summary.insert(QStringLiteral("shutdown_failure_count"), receipt.shutdownFailures.size());
    summary.insert(QStringLiteral("shutdown_failure_stages"), failureStages);
    summary.insert(QStringLiteral("shutdown_failure_json"), failures);
    return summary;
}

bool normalLifetimeChild(const LifetimeChildReceipt &receipt)
{
    return receipt.started && receipt.finished && receipt.cleanupComplete
        && !receipt.timedOut && receipt.exitStatus == QProcess::NormalExit
        && receipt.exitCode == 0;
}

bool expectedShutdownFailure(const LifetimeChildReceipt &receipt, const QString &stage,
                             const QString &invariantKey)
{
#ifdef _WIN32
    constexpr int expectedFatalExitCode = -1073740791;
#endif
    const bool stageSequenceMatches = stage == QLatin1String("worker-final")
        ? receipt.shutdownStages.isEmpty()
        : receipt.shutdownStages == QList<QString>{QStringLiteral("preclose"),
              QStringLiteral("lua-close"), QStringLiteral("postclose")};
    return receipt.started && receipt.finished && receipt.cleanupComplete
        && !receipt.timedOut && receipt.exitStatus == QProcess::CrashExit
#ifdef _WIN32
        && receipt.exitCode == expectedFatalExitCode
#endif
        && receipt.standardError.count("ROOM_RUNTIME_FAIL") == 1
        && receipt.markerCount("CARD_LIFETIME_ZERO") == 0
        && receipt.markerCount("CARD_LIFETIME_SHUTDOWN_FAILED") == 1
        && receipt.standardError.contains(
            (QByteArray("ROOM_RUNTIME_FAIL stage=") + stage.toUtf8()).constData())
        && receipt.shutdownFailureStages == QList<QString>{stage}
        && receipt.shutdownFailures.size() == 1
        && stageSequenceMatches
        && receipt.shutdownFailures.constFirst().value(invariantKey).toInteger() > 0;
}
}

int runCardLifetimeLegacyRedTests();

int runCardLifetimeTests()
{
    QString bootstrapError;
    CARD_LIFETIME_CHECK(EngineBootstrap::initialize(false, &bootstrapError));
    const bool defaultModeRegression =
        defaultCardLifetimeMode() == CardLifetimeMode::ManagedReclaim;
    const CardLifetimeGauge defaultGauge = globalCardLifetimeManager().gauge();
    const bool blocker12Regression = probeBlockers12();
    fprintf(stdout, "CARD_LIFETIME_BLOCKERS_1_2 %s\n",
            blocker12Regression ? "PASS" : "FAIL");
    const bool residualGaugeRegression = probeResidualGaugeProducersAndDomainIsolation();
    fprintf(stdout, "CARD_LIFETIME_RESIDUAL_GAUGES %s\n",
            residualGaugeRegression ? "PASS" : "FAIL");
    const bool blocker4CoreRegression = probeBlocker4CorePins();
    fprintf(stdout, "CARD_LIFETIME_BLOCKER_4_CORE %s\n",
            blocker4CoreRegression ? "PASS" : "FAIL");
    CARD_LIFETIME_CHECK(defaultGauge.unknown_unclaimed == 0);
    CARD_LIFETIME_CHECK(defaultGauge.unknown_qvariant_card_payload == 0);
    CARD_LIFETIME_CHECK(defaultGauge.change_list_self_cycle == 0);
    CARD_LIFETIME_CHECK(defaultGauge.change_list_cycles == 0);
    CARD_LIFETIME_CHECK(defaultGauge.change_list_reuse_reconnect == 0);
    CARD_LIFETIME_CHECK(defaultGauge.unapproved_card_raw_delete == 0);
    CARD_LIFETIME_CHECK(defaultGauge.card_delete_bypass == 0);
    CARD_LIFETIME_CHECK(defaultGauge.adoption_failed == 0);
    CARD_LIFETIME_CHECK(defaultGauge.affinity_transfer_failed == 0);
    QProcess legacy;
    legacy.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--suite"),
                                                            QStringLiteral("card-lifetime-legacy-red")});
    CARD_LIFETIME_CHECK(legacy.waitForFinished(15000));
    CARD_LIFETIME_CHECK(legacy.exitCode() == 0);
    const QJsonDocument legacyReport = QJsonDocument::fromJson(legacy.readAllStandardOutput());
    CARD_LIFETIME_CHECK(legacyReport.object().value(QStringLiteral("classification")).toString() == QLatin1String("RED"));
    CARD_LIFETIME_CHECK(legacyReport.object().value(QStringLiteral("normalized_exit")).toInt() == 70);
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    CARD_LIFETIME_CHECK(runtime.invocationDepth() == 0);
    {
        LuaRuntime::LuaInvocationScope invocation(runtime);
        CARD_LIFETIME_CHECK(runtime.invocationDepth() == 1);
        {
            LuaRuntime::LuaInvocationScope nested(runtime);
            CARD_LIFETIME_CHECK(runtime.invocationDepth() == 2);
        }
        CARD_LIFETIME_CHECK(runtime.invocationDepth() == 1);
    }
    CARD_LIFETIME_CHECK(runtime.invocationDepth() == 0);
    {
        static int runtimeDomainA = 0;
        static int runtimeDomainB = 0;
        static int runtimeIdentityA = 0;
        static int runtimeIdentityB = 0;
        static int runtimeStateA = 0;
        static int runtimeStateB = 0;
        CardLifetimeManager identityManager(CardLifetimeMode::ObserveOnly);
        int reusedAddress = 0;
        int wrapperAddress = 0;
        lua_State *stateA = reinterpret_cast<lua_State *>(&runtimeStateA);
        lua_State *stateB = reinterpret_cast<lua_State *>(&runtimeStateB);
        const CardLifetimeRuntimeContext previous =
            CardLifetimeManager::setCurrentRuntimeContext(
                &runtimeDomainA, &runtimeIdentityA, 1, stateA);
        const auto first = identityManager.observeLive(&reusedAddress);
        CARD_LIFETIME_CHECK(first);
        CARD_LIFETIME_CHECK(identityManager.retainWrapper(first));
        CARD_LIFETIME_CHECK(identityManager.bindWrapper(&wrapperAddress, first));
        CardLifetimeManager::setCurrentRuntimeContext(previous.domain, previous.identity,
                                                       previous.generation, previous.state);
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainB, &runtimeIdentityB, 1, stateB);
        CARD_LIFETIME_CHECK(!identityManager.wrapperBinding(&wrapperAddress));
        CARD_LIFETIME_CHECK(!identityManager.releaseWrapperBinding(&wrapperAddress));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainA, &runtimeIdentityA, 1, stateA);
        CARD_LIFETIME_CHECK(identityManager.releaseWrapperBinding(&wrapperAddress));
        CARD_LIFETIME_CHECK(identityManager.invalidateIfObserved(&reusedAddress));
        CARD_LIFETIME_CHECK(!identityManager.bindWrapper(&wrapperAddress, first));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainB, &runtimeIdentityB, 2, stateB);
        const auto second = identityManager.observeLive(&reusedAddress);
        CARD_LIFETIME_CHECK(second && second->generation != first->generation);
        CARD_LIFETIME_CHECK(identityManager.retainWrapper(second));
        CARD_LIFETIME_CHECK(identityManager.bindWrapper(&wrapperAddress, second));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainA, &runtimeIdentityA, 1, stateA);
        CARD_LIFETIME_CHECK(!identityManager.releaseWrapperBinding(&wrapperAddress));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainB, &runtimeIdentityB, 2, stateB);
        CARD_LIFETIME_CHECK(identityManager.releaseWrapperBinding(&wrapperAddress));
        CARD_LIFETIME_CHECK(identityManager.invalidateIfObserved(&reusedAddress));
        CardLifetimeManager::setCurrentRuntimeContext(previous.domain, previous.identity,
                                                       previous.generation, previous.state);
        CARD_LIFETIME_CHECK(identityManager.resetForTest());
        CardLifetimeManager baselineManager(CardLifetimeMode::ObserveOnly);
        int baselineAddress = 0;
        const auto baselineToken = baselineManager.observeLive(&baselineAddress);
        CARD_LIFETIME_CHECK(baselineToken);
        baselineManager.setDomainBaseline(&runtimeDomainA, {&baselineAddress});
        CARD_LIFETIME_CHECK(!baselineManager.resetForTest());
        CARD_LIFETIME_CHECK(baselineManager.invalidateIfObserved(&baselineAddress));
        CARD_LIFETIME_CHECK(baselineManager.resetForTest());
    }
    {
        LuaRuntime guardedRuntime(LuaRuntime::Auxiliary);
        LuaRuntime otherRuntime(LuaRuntime::Auxiliary);
        CARD_LIFETIME_CHECK(guardedRuntime.initialize());
        CARD_LIFETIME_CHECK(otherRuntime.initialize());
        {
            LuaRuntime::Binding binding(guardedRuntime);
            LuaRuntime::LuaInvocationScope invocation(guardedRuntime);
            CARD_LIFETIME_CHECK(guardedRuntime.invocationDepth() == 1);
            CARD_LIFETIME_CHECK(globalCardLifetimeManager().gaugeForRuntime(
                       guardedRuntime.lifetimeDomain(), &guardedRuntime,
                       guardedRuntime.generation(), guardedRuntime.rawState()).lua_pins == 1);
            guardedRuntime.shutdown();
            CARD_LIFETIME_CHECK(guardedRuntime.lifecycle() == LuaRuntime::Lifecycle::Running);
            CARD_LIFETIME_CHECK(guardedRuntime.rawState() != nullptr);
        }
        CARD_LIFETIME_CHECK(globalCardLifetimeManager().gaugeForRuntime(
                   guardedRuntime.lifetimeDomain(), &guardedRuntime,
                   guardedRuntime.generation(), guardedRuntime.rawState()).lua_pins == 0);
        guardedRuntime.shutdown();
        CARD_LIFETIME_CHECK(guardedRuntime.lifecycle() == LuaRuntime::Lifecycle::Closed);
        guardedRuntime.shutdown();
        CARD_LIFETIME_CHECK(guardedRuntime.lifecycle() == LuaRuntime::Lifecycle::Closed);
        CARD_LIFETIME_CHECK(otherRuntime.lifecycle() == LuaRuntime::Lifecycle::Running);
        otherRuntime.shutdown();
        CARD_LIFETIME_CHECK(otherRuntime.lifecycle() == LuaRuntime::Lifecycle::Closed);
    }
    {
        static int sharedDomain = 0;
        LuaRuntime gameRuntime(LuaRuntime::Game);
        LuaRuntime aiRuntime(LuaRuntime::Auxiliary);
        gameRuntime.setLifetimeDomain(&sharedDomain);
        aiRuntime.setLifetimeDomain(&sharedDomain);
        CARD_LIFETIME_CHECK(gameRuntime.initialize());
        CARD_LIFETIME_CHECK(aiRuntime.initialize());
        int gameCard = 0;
        int aiCard = 0;
        int gameWrapper = 0;
        int aiWrapper = 0;
        CardLifetimeManager &manager = globalCardLifetimeManager();
        std::shared_ptr<const CardLifetimeToken> gameToken;
        std::shared_ptr<const CardLifetimeToken> aiToken;
        {
            LuaRuntime::Binding binding(gameRuntime);
            gameToken = manager.observeLive(&gameCard);
            CARD_LIFETIME_CHECK(gameToken && manager.retainWrapper(gameToken));
            CARD_LIFETIME_CHECK(manager.bindWrapper(&gameWrapper, gameToken));
        }
        {
            LuaRuntime::Binding binding(aiRuntime);
            aiToken = manager.observeLive(&aiCard);
            CARD_LIFETIME_CHECK(aiToken && manager.retainWrapper(aiToken));
            CARD_LIFETIME_CHECK(manager.bindWrapper(&aiWrapper, aiToken));
        }
        gameRuntime.shutdown();
        {
            LuaRuntime::Binding binding(aiRuntime);
            CARD_LIFETIME_CHECK(manager.wrapperBinding(&aiWrapper));
            CARD_LIFETIME_CHECK(manager.gaugeForRuntime(aiRuntime.lifetimeDomain(), &aiRuntime,
                                           aiRuntime.generation(), aiRuntime.rawState())
                       .wrapper_leases == 1);
        }
        aiRuntime.shutdown();
        CARD_LIFETIME_CHECK(manager.invalidateIfObserved(&gameCard));
        CARD_LIFETIME_CHECK(manager.invalidateIfObserved(&aiCard));
    }
    CARD_LIFETIME_CHECK(probeCanonicalOwner());
    CARD_LIFETIME_CHECK(probeCanonicalOwner());
    CARD_LIFETIME_CHECK(probeCanonicalOwner());
    CARD_LIFETIME_CHECK(probeCanonicalOwner());
    const QStringList roomModes = {QStringLiteral("03_1v2"), QStringLiteral("02_1v1"),
                                   QStringLiteral("06_3v3"), QStringLiteral("06_XMode")};
    for (const QString &mode : roomModes) {
        CARD_LIFETIME_CHECK(probeRoomMode(mode));
        CARD_LIFETIME_CHECK(probeWrappedCardCanonicalOwner(mode));
    }
    CARD_LIFETIME_CHECK(probeRoomStateCanonicalReset());
    CardLifetimeManager observe(CardLifetimeMode::ObserveOnly);
    const auto token = observe.observeLive(&liveObject, false);
    CARD_LIFETIME_CHECK(token);
    CARD_LIFETIME_CHECK(observe.isLive(token));
    CARD_LIFETIME_CHECK(observe.retainWrapper(token));
    QByteArray error;
    CARD_LIFETIME_CHECK(observe.requestLuaDelete(token, &error));
    CARD_LIFETIME_CHECK(observe.gauge().pending_delete == 1);
    CARD_LIFETIME_CHECK(observe.drain() == 0);
    CARD_LIFETIME_CHECK(observe.releaseWrapper(token));
    CARD_LIFETIME_CHECK(observe.invalidateIfObserved(&liveObject));
    CARD_LIFETIME_CHECK(!observe.isLive(token));
    const auto reusedObserved = observe.observeLive(&liveObject);
    CARD_LIFETIME_CHECK(reusedObserved && reusedObserved->generation != token->generation);
    CardLifetimeManager reuseManager(CardLifetimeMode::ObserveOnly);
    int reused = 0;
    const auto firstGeneration = reuseManager.observeLive(&reused);
    CARD_LIFETIME_CHECK(firstGeneration);
    CARD_LIFETIME_CHECK(reuseManager.invalidateIfObserved(&reused));
    CARD_LIFETIME_CHECK(reuseManager.resetForTest());
    const auto secondGeneration = reuseManager.observeLive(&reused);
    CARD_LIFETIME_CHECK(secondGeneration && secondGeneration->generation != firstGeneration->generation);
    CardLifetimeManager secondDomain(CardLifetimeMode::ObserveOnly);
    const auto secondDomainToken = secondDomain.observeLive(&reused);
    CARD_LIFETIME_CHECK(secondDomainToken);
    CARD_LIFETIME_CHECK(secondDomain.isLive(secondDomainToken));
    CARD_LIFETIME_CHECK(!reuseManager.isLive(secondDomainToken));
    CardLifetimeManager managed(CardLifetimeMode::ManagedReclaim);
    bool activeScopeBlocked = false;
    bool luaPinBlocked = false;
    bool reservationBlocked = false;
    bool wrongThreadBlocked = false;
    bool leaseBlocked = false;
    bool realDeadRelease = false;
    quint64 realEligibleCreated = 0;
    quint64 realDestructorCompleted = 0;
    quint64 realActuallyDestroyed = 0;
    {
        DummyCard card;
        CardLifetimeManager &cardManager = globalCardLifetimeManager();
        const auto cardToken = cardManager.observeLive(&card);
        CARD_LIFETIME_CHECK(cardToken);
        CARD_LIFETIME_CHECK(card.lifetimeGeneration() == cardToken->generation);
        CARD_LIFETIME_CHECK(card.lifetimeIsLive());
        CARD_LIFETIME_CHECK(cardManager.invalidateIfObserved(&card));
        CARD_LIFETIME_CHECK(!card.lifetimeIsLive());
    }
    {
        CardLifetimeManager &cardManager = globalCardLifetimeManager();
        auto *card = new DummyCard;
        const auto cardToken = cardManager.observeLive(card);
        CARD_LIFETIME_CHECK(cardToken);
        card->deleteLater();
    CARD_LIFETIME_CHECK(cardManager.gauge().native_delete_requested > 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CARD_LIFETIME_CHECK(!cardManager.isLive(cardToken));
    }
    const auto managedToken = managed.observeLive(&otherObject, false);
    CARD_LIFETIME_CHECK(managedToken);
    {
        CardLifetimeLease lease(managed, managedToken);
        CARD_LIFETIME_CHECK(lease.isValid());
        CARD_LIFETIME_CHECK(managed.gauge().native_leases > 0);
    }
    int adoptedObject = 0;
    const auto adoptedToken = managed.observeLive(&adoptedObject, false);
    CARD_LIFETIME_CHECK(adoptedToken);
    CARD_LIFETIME_CHECK(managed.requestNativeDelete(adoptedToken));
    CARD_LIFETIME_CHECK(managed.markAdopted(adoptedToken));
    error.clear();
    CARD_LIFETIME_CHECK(managed.requestLuaDelete(adoptedToken, &error));
    CARD_LIFETIME_CHECK(error.isEmpty());
    CARD_LIFETIME_CHECK(managed.gauge().adopted_delete_ignored > 0);
    CARD_LIFETIME_CHECK(!managed.resetForTest());
    CARD_LIFETIME_CHECK(managed.requestNativeDelete(adoptedToken));
    CARD_LIFETIME_CHECK(managed.drain() == 0);
    CARD_LIFETIME_CHECK(managed.gauge().adopted_after_delete_request > 0);
    CARD_LIFETIME_CHECK(managed.requestNativeDelete(managedToken));
    CARD_LIFETIME_CHECK(managed.state(managedToken) == CardLifetimeState::PendingDelete);
    CARD_LIFETIME_CHECK(managed.drain() == 1);
    CARD_LIFETIME_CHECK(managed.state(managedToken) == CardLifetimeState::Retired);

    int scopedObject = 0;
    const auto scopedToken = managed.observeLive(&scopedObject);
    CARD_LIFETIME_CHECK(scopedToken && managed.requestNativeDelete(scopedToken));
    {
        CardLifetimeScope scope(managed);
        activeScopeBlocked = managed.drain() == 0;
        CARD_LIFETIME_CHECK(activeScopeBlocked);
        CARD_LIFETIME_CHECK(managed.state(scopedToken) == CardLifetimeState::PendingDelete);
    }
    CARD_LIFETIME_CHECK(managed.drain() == 1);
    int reservedObject = 0;
    const auto reservedToken = managed.observeLive(&reservedObject);
    CARD_LIFETIME_CHECK(reservedToken && managed.requestNativeDelete(reservedToken));
    CARD_LIFETIME_CHECK(managed.reserveAdoption(reservedToken));
    reservationBlocked = managed.drain() == 0;
    CARD_LIFETIME_CHECK(reservationBlocked);
    managed.cancelAdoption(reservedToken);
    CARD_LIFETIME_CHECK(managed.drain() == 1);
    QThread foreignOwner;
    CardLifetimeManager affinity(CardLifetimeMode::ManagedReclaim, &foreignOwner);
    int foreignObject = 0;
    const auto foreignToken = affinity.observeLive(&foreignObject);
    CARD_LIFETIME_CHECK(foreignToken && affinity.requestNativeDelete(foreignToken));
    wrongThreadBlocked = affinity.drain() == 0;
    CARD_LIFETIME_CHECK(wrongThreadBlocked);
    CARD_LIFETIME_CHECK(affinity.state(foreignToken) == CardLifetimeState::PendingDelete);
    CARD_LIFETIME_CHECK(affinity.invalidateIfObserved(&foreignObject));
    CARD_LIFETIME_CHECK(affinity.resetForTest());

    CardLifetimeManager deadLeaseManager(CardLifetimeMode::ManagedReclaim);
    auto *deadLeaseCard = new DummyCard;
    const auto deadLeaseToken = deadLeaseManager.observeCard(deadLeaseCard);
    CARD_LIFETIME_CHECK(deadLeaseToken);
    CARD_LIFETIME_CHECK(deadLeaseManager.retainWrapper(deadLeaseToken));
    CARD_LIFETIME_CHECK(deadLeaseManager.retainNativeLease(deadLeaseToken));
    CARD_LIFETIME_CHECK(deadLeaseManager.requestNativeDelete(deadLeaseToken));
    realEligibleCreated = deadLeaseManager.gauge().pending_delete;
    CARD_LIFETIME_CHECK(deadLeaseManager.drain() == 0);
    leaseBlocked = deadLeaseManager.gauge().pending_delete == realEligibleCreated
        && deadLeaseManager.gauge().native_leases > 0
        && deadLeaseManager.gauge().wrapper_leases > 0;
    CARD_LIFETIME_CHECK(leaseBlocked);
    delete deadLeaseCard;
    realDestructorCompleted = deadLeaseManager.gauge().actually_destroyed;
    realActuallyDestroyed = deadLeaseManager.gauge().actually_destroyed;
    const auto wrongGeneration = std::make_shared<CardLifetimeToken>(*deadLeaseToken);
    ++wrongGeneration->generation;
    CARD_LIFETIME_CHECK(!deadLeaseManager.releaseNativeLease(wrongGeneration));
    const bool noResurrection = !deadLeaseManager.isLive(deadLeaseToken);
    const bool releasedWrapper = deadLeaseManager.releaseWrapper(deadLeaseToken);
    const bool releasedNative = deadLeaseManager.releaseNativeLease(deadLeaseToken);
    realDeadRelease = noResurrection && releasedWrapper && releasedNative
        && deadLeaseManager.gauge().native_leases == 0
        && deadLeaseManager.gauge().wrapper_leases == 0
        && deadLeaseManager.entryCount() == 0
        && realDestructorCompleted == realActuallyDestroyed;
    CARD_LIFETIME_CHECK(realDeadRelease);

    int luaPinnedObject = 0;
    CardLifetimeManager &luaPinManager = globalCardLifetimeManager();
    std::shared_ptr<const CardLifetimeToken> luaPinnedToken;
    LuaRuntime pinnedRuntime(LuaRuntime::Auxiliary);
    const bool pinnedRuntimeInitialized = pinnedRuntime.initialize();
    CARD_LIFETIME_CHECK(pinnedRuntimeInitialized);
    {
        LuaRuntime::Binding binding(pinnedRuntime);
        luaPinnedToken = luaPinManager.observeLive(&luaPinnedObject);
        CARD_LIFETIME_CHECK(luaPinnedToken
            && luaPinManager.requestNativeDelete(luaPinnedToken));
        LuaRuntime::LuaInvocationScope invocation(pinnedRuntime);
        CARD_LIFETIME_CHECK(pinnedRuntime.invocationDepth() == 1);
        luaPinBlocked = luaPinManager.drain() == 0;
        CARD_LIFETIME_CHECK(luaPinBlocked);
    }
    CARD_LIFETIME_CHECK(luaPinManager.drain() == 1);
    pinnedRuntime.shutdown();

    const auto definition = managed.observeLive(&liveObject, true);
    CARD_LIFETIME_CHECK(definition);
    error.clear();
    CARD_LIFETIME_CHECK(managed.requestLuaDelete(definition, &error));
    CARD_LIFETIME_CHECK(error.isEmpty());
    CARD_LIFETIME_CHECK(managed.invalidateIfObserved(&adoptedObject) == true);
    DummyCard *physical = new DummyCard;
    const auto physicalToken = managed.observeCard(physical);
    CARD_LIFETIME_CHECK(physicalToken);
    CARD_LIFETIME_CHECK(managed.requestNativeDelete(physicalToken));
    const auto destroyedBefore = managed.gauge().actually_destroyed;
    const auto eligible = managed.drain();
    CARD_LIFETIME_CHECK(eligible == 1);
    CARD_LIFETIME_CHECK(managed.gauge().actually_destroyed == destroyedBefore);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CARD_LIFETIME_CHECK(managed.gauge().actually_destroyed == destroyedBefore + eligible);
    CARD_LIFETIME_CHECK(!managed.isLive(physicalToken));
    CARD_LIFETIME_CHECK(managed.state(physicalToken) == CardLifetimeState::Dead);
    CARD_LIFETIME_CHECK(managed.gauge().actually_destroyed >= 1);


    DummyCard *lateCard = nullptr;
    {
        CardLifetimeManager teardownManager(CardLifetimeMode::ManagedReclaim);
        lateCard = new DummyCard;
        const auto lateToken = teardownManager.observeCard(lateCard);
        CARD_LIFETIME_CHECK(lateToken);
    }
    delete lateCard;
    auto *unassociatedCard = new DummyCard;
    delete unassociatedCard;

    const auto before = managed.gauge().unknown_card_delete;
    error.clear();
    CARD_LIFETIME_CHECK(!managed.requestLuaDelete({}, &error));
    CARD_LIFETIME_CHECK(error == CardLifetimeManager::unknownOwnershipError());
    CARD_LIFETIME_CHECK(managed.gauge().unknown_card_delete == before + 1);
    error.clear();
    CARD_LIFETIME_CHECK(!managed.requestLuaDelete(reinterpret_cast<const void *>(static_cast<quintptr>(0xdeadbeef)), &error));
    CARD_LIFETIME_CHECK(error == CardLifetimeManager::unknownOwnershipError());
    CARD_LIFETIME_CHECK(managed.gauge().unknown_card_delete == before + 2);
    const auto opaqueBefore = managed.gauge().unknown_qvariant_card_payload;
    error.clear();
    CARD_LIFETIME_CHECK(!managed.rejectOpaqueVariant(&error));
    CARD_LIFETIME_CHECK(error == CardLifetimeManager::opaqueVariantError());
    CARD_LIFETIME_CHECK(managed.gauge().unknown_qvariant_card_payload == opaqueBefore + 1);
    CARD_LIFETIME_CHECK(QByteArray(CardLifetimeManager::opaqueVariantError())
           == QByteArray("Card lifetime error: rejected opaque QVariant Card payload"));
    DummyCard cycle;
    DummyCard cycleTarget;
    auto &globalManager = globalCardLifetimeManager();
    const auto cycleBefore = globalManager.gauge();
    cycle.addChange(&cycle);
    cycle.addChange(&cycleTarget);
    cycle.addChange(&cycleTarget);
    cycleTarget.addChange(&cycle);
    CARD_LIFETIME_CHECK(cycle.change_cards.size() == 1);
    const auto cycleAfter = globalManager.gauge();
    CARD_LIFETIME_CHECK(cycleAfter.change_list_self_cycle > cycleBefore.change_list_self_cycle);
    CARD_LIFETIME_CHECK(cycleAfter.change_list_reuse_reconnect + cycleAfter.change_list_cycles
           > cycleBefore.change_list_reuse_reconnect + cycleBefore.change_list_cycles);
    CARD_LIFETIME_CHECK(cycleAfter.sidecar_edges == cycleBefore.sidecar_edges + 1);
    globalManager.removeChangeEdges(&cycle);
    globalManager.removeChangeEdges(&cycleTarget);
    CARD_LIFETIME_CHECK(globalManager.gauge().sidecar_edges == cycleBefore.sidecar_edges);
    DummyCard tagSource;
    DummyCard tagTarget;
    const auto leaseBefore = globalManager.gauge().native_leases;
    tagSource.setTag(QStringLiteral("cardLease"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
    CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == leaseBefore + 1);
    tagSource.setTag(QStringLiteral("cardLease"), QVariant(42));
    CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == leaseBefore);
    {
        DamageStruct damage(&tagTarget, nullptr, nullptr);
        DamageStruct damageCopy(damage);
        DamageStruct damageAssigned;
        damageAssigned = damageCopy;
        CardEffectStruct effect;
        effect.card = &tagTarget;
        CardEffectStruct effectCopy(effect);
        SlashEffectStruct slash;
        slash.slash = &tagTarget;
        SlashEffectStruct slashCopy(slash);
        RecoverStruct recover(nullptr, &tagTarget);
        RecoverStruct recoverCopy(recover);
        CardUseStruct use(&tagTarget, nullptr);
        CardUseStruct useCopy(use);
        CardResponseStruct response(&tagTarget, false);
        CardResponseStruct responseCopy(response);
        CARD_LIFETIME_CHECK(globalManager.gauge().native_leases > leaseBefore);
    }
    CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == leaseBefore);
    {
        QVariantMap nested;
        nested.insert(QStringLiteral("card"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
        QVariantList list;
        list << nested;
        QByteArray variantError;
        CARD_LIFETIME_CHECK(globalManager.retainVariantPayload(&tagSource, list, &variantError));
        CARD_LIFETIME_CHECK(globalManager.gauge().native_leases > leaseBefore);
        globalManager.releaseEventPayload(&tagSource);
    CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == leaseBefore);
    }
    {
        CardMoveReason reason;
        reason.m_extraData = QVariant::fromValue(static_cast<Card *>(&tagTarget));
        const auto moveLeaseBefore = globalManager.gauge().native_leases;
        {
            CardsMoveStruct move(QList<int>() << 1, nullptr, Player::DiscardPile, reason);
            CardsMoveStruct moveCopy(move);
            CardsMoveStruct moveAssigned;
            moveAssigned = moveCopy;
            CardsMoveOneTimeStruct one;
            one.reason = reason;
            CardsMoveOneTimeStruct oneCopy(one);
            CardsMoveOneTimeStruct oneAssigned;
            oneAssigned = oneCopy;
            CARD_LIFETIME_CHECK(globalManager.gauge().native_leases > moveLeaseBefore);
        }
        CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == moveLeaseBefore);

        CardMoveReason valid;
        valid.m_extraData = QVariant::fromValue(static_cast<Card *>(&tagTarget));
        const QVariant validPayload = valid.m_extraData;
        CardMoveReason opaque;
        opaque.m_extraData = QVariant::fromValue(UnregisteredCardPayload());
        valid = opaque;
        CARD_LIFETIME_CHECK(valid.m_extraData == validPayload);

        ServerPlayer tagPlayer(nullptr);
        const auto playerLeaseBefore = globalManager.gauge().native_leases;
        tagPlayer.setTag(QStringLiteral("nestedCard"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
        CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == playerLeaseBefore + 1);
        tagPlayer.setTag(QStringLiteral("nestedCard"), QVariant(7));
        CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == playerLeaseBefore);
        tagPlayer.setTag(QStringLiteral("nestedCard"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
        ServerPlayer copiedPlayer(nullptr);
        copiedPlayer.copyFrom(&tagPlayer);
        CARD_LIFETIME_CHECK(globalManager.gauge().native_leases >= playerLeaseBefore + 2);
        copiedPlayer.removeTag(QStringLiteral("nestedCard"));
        tagPlayer.removeTag(QStringLiteral("nestedCard"));
        CARD_LIFETIME_CHECK(globalManager.gauge().native_leases == playerLeaseBefore);
    }
    CARD_LIFETIME_CHECK(managed.invalidateIfObserved(&liveObject));
    const CardLifetimeGauge preResetGauge = managed.gauge();
    CARD_LIFETIME_CHECK(managed.resetForTest());
    const CardLifetimeGauge finalGauge = managed.gauge();
    const quint64 finalEntries = managed.entryCount();
    QJsonObject marker;
    marker.insert(QStringLiteral("eligible_created"), static_cast<qint64>(realEligibleCreated));
    marker.insert(QStringLiteral("destructor_completed"), static_cast<qint64>(realDestructorCompleted));
    marker.insert(QStringLiteral("actually_destroyed"), static_cast<qint64>(realActuallyDestroyed));
    marker.insert(QStringLiteral("wrong_thread_blocked"), wrongThreadBlocked);
    marker.insert(QStringLiteral("active_scope_blocked"), activeScopeBlocked);
    marker.insert(QStringLiteral("lua_pin_blocked"), luaPinBlocked);
    marker.insert(QStringLiteral("lease_blocked"), leaseBlocked);
    marker.insert(QStringLiteral("reservation_blocked"), reservationBlocked);
    marker.insert(QStringLiteral("final_live"), static_cast<qint64>(finalGauge.managed_live));
    marker.insert(QStringLiteral("final_pending"), static_cast<qint64>(finalGauge.pending_delete));
    marker.insert(QStringLiteral("final_wrapper"), static_cast<qint64>(finalGauge.wrapper_leases));
    marker.insert(QStringLiteral("final_native"), static_cast<qint64>(finalGauge.native_leases));
    marker.insert(QStringLiteral("final_lua"), static_cast<qint64>(finalGauge.lua_pins));
    marker.insert(QStringLiteral("final_reservation"), static_cast<qint64>(finalGauge.adoption_reserved));
    marker.insert(QStringLiteral("final_entries"), static_cast<qint64>(finalEntries));
    QJsonObject realRelease;
    realRelease.insert(QStringLiteral("released_exact_generation"), realDeadRelease);
    realRelease.insert(QStringLiteral("no_resurrection"), !deadLeaseManager.isLive(deadLeaseToken));
    fprintf(stdout, "REAL_DEAD_RELEASE %s\n",
            QJsonDocument(realRelease).toJson(QJsonDocument::Compact).constData());
    QJsonObject blockers;
    blockers.insert(QStringLiteral("wrong_thread_blocked"), wrongThreadBlocked);
    blockers.insert(QStringLiteral("active_scope_blocked"), activeScopeBlocked);
    blockers.insert(QStringLiteral("lua_pin_blocked"), luaPinBlocked);
    blockers.insert(QStringLiteral("lease_blocked"), leaseBlocked);
    blockers.insert(QStringLiteral("reservation_blocked"), reservationBlocked);
    fprintf(stdout, "BLOCKER_RESULT %s\n",
            QJsonDocument(blockers).toJson(QJsonDocument::Compact).constData());
    QJsonObject destruction;
    destruction.insert(QStringLiteral("eligible_created"), static_cast<qint64>(realEligibleCreated));
    destruction.insert(QStringLiteral("destructor_completed"), static_cast<qint64>(realDestructorCompleted));
    destruction.insert(QStringLiteral("actually_destroyed"), static_cast<qint64>(realActuallyDestroyed));
    fprintf(stdout, "DESTRUCTION_RESULT %s\n",
            QJsonDocument(destruction).toJson(QJsonDocument::Compact).constData());
    QJsonObject gauges;
    gauges.insert(QStringLiteral("live"), static_cast<qint64>(finalGauge.managed_live));
    gauges.insert(QStringLiteral("pending"), static_cast<qint64>(finalGauge.pending_delete));
    gauges.insert(QStringLiteral("wrapper"), static_cast<qint64>(finalGauge.wrapper_leases));
    gauges.insert(QStringLiteral("native"), static_cast<qint64>(finalGauge.native_leases));
    gauges.insert(QStringLiteral("lua"), static_cast<qint64>(finalGauge.lua_pins));
    gauges.insert(QStringLiteral("reservation"), static_cast<qint64>(finalGauge.adoption_reserved));
    gauges.insert(QStringLiteral("entries"), static_cast<qint64>(finalEntries));
    fprintf(stdout, "FINAL_GAUGE %s\n",
            QJsonDocument(gauges).toJson(QJsonDocument::Compact).constData());
    fprintf(stdout, "CARD_LIFETIME_FINAL %s\n",
            QJsonDocument(marker).toJson(QJsonDocument::Compact).constData());
    const LifetimeChildReceipt eventReceipt = runLifetimeChild(
        {QStringLiteral("--suite"), QStringLiteral("card-lifetime-event-lease")});
    const LifetimeChildReceipt adoptionReceipt = runLifetimeChild(
        {QStringLiteral("--suite"), QStringLiteral("card-lifetime-wrapped-adoption")});
    const LifetimeChildReceipt shutdownReceipt = runLifetimeChild(
        {QStringLiteral("--suite"), QStringLiteral("card-lifetime-shutdown")});
    const bool eventProtocol = normalLifetimeChild(eventReceipt)
        && eventReceipt.standardOutput.contains("CARD_EVENT_LEASE checks=")
        && eventReceipt.standardOutput.contains("failures=0");
    const bool adoptionProtocol = normalLifetimeChild(adoptionReceipt)
        && adoptionReceipt.standardOutput.contains("WRAPPED_ADOPTION checks=")
        && adoptionReceipt.standardOutput.contains("failures=0");
    const bool shutdownProtocol = normalLifetimeChild(shutdownReceipt)
        && shutdownReceipt.markerCount("CARD_LIFETIME_ZERO") == 1
        && shutdownReceipt.markerCount("CARD_LIFETIME_WORKER_FINAL") == 1
        && shutdownReceipt.markerCount("CARD_LIFETIME_SHUTDOWN_STAGE") == 3
        && shutdownReceipt.shutdownStages == QList<QString>{QStringLiteral("preclose"),
            QStringLiteral("lua-close"), QStringLiteral("postclose")}
        && shutdownReceipt.shutdownFailures.isEmpty();
    const QList<QPair<QString, QString>> shutdownCases = {
        {QStringLiteral("worker"), QStringLiteral("managed_live")},
        {QStringLiteral("lease"), QStringLiteral("native_leases")},
        {QStringLiteral("reservation"), QStringLiteral("adoption_reserved")},
        {QStringLiteral("lua-pin"), QStringLiteral("lua_pins")}
    };
    bool adversarialShutdown = true;
    QJsonArray adversarialReceipts;
    for (const auto &shutdownCase : shutdownCases) {
        const LifetimeChildReceipt receipt = runLifetimeChild(
            {QStringLiteral("--suite"), QStringLiteral("card-lifetime-shutdown"),
             shutdownCase.first},
            15000);
        const QString expectedStage = shutdownCase.first == QLatin1String("lua-pin")
            ? QStringLiteral("worker-final") : QStringLiteral("postclose");
        adversarialShutdown = adversarialShutdown
            && expectedShutdownFailure(receipt, expectedStage, shutdownCase.second);
        QJsonObject receiptSummary = lifetimeChildSummary(receipt);
        receiptSummary.insert(QStringLiteral("case"), shutdownCase.first);
        adversarialReceipts.append(receiptSummary);
    }
    QJsonObject integrated;
    integrated.insert(QStringLiteral("event_lease"), eventProtocol);
    integrated.insert(QStringLiteral("wrapped_adoption"), adoptionProtocol);
    integrated.insert(QStringLiteral("shutdown"), shutdownProtocol);
    integrated.insert(QStringLiteral("shutdown_adversarial"), adversarialShutdown);
    integrated.insert(QStringLiteral("normal_receipts"), QJsonArray{
        lifetimeChildSummary(eventReceipt), lifetimeChildSummary(adoptionReceipt),
        lifetimeChildSummary(shutdownReceipt)});
    integrated.insert(QStringLiteral("adversarial_receipts"), adversarialReceipts);
    fprintf(stdout, "CARD_LIFETIME_INTEGRATED %s\n",
            QJsonDocument(integrated).toJson(QJsonDocument::Compact).constData());
    return defaultModeRegression && blocker12Regression && residualGaugeRegression
        && blocker4CoreRegression
        && eventProtocol && adoptionProtocol
        && shutdownProtocol && adversarialShutdown ? 0 : 72;
}

int runCardLifetimeLegacyRedTests()
{
    CardLifetimeManager manager(CardLifetimeMode::ObserveOnly);
    int staleAccess = 0;
    int doubleDelete = 0;
    int adoptionAfterDelete = 0;
    int deferredDelete = 0;
    int directOwnerDestroy = 0;

    int stale = 0;
    const auto staleToken = manager.observeLive(&stale);
    CARD_LIFETIME_CHECK(staleToken);
    CARD_LIFETIME_CHECK(manager.invalidateIfObserved(&stale));
    const auto reusedToken = manager.observeLive(&stale);
    staleAccess = !manager.isLive(staleToken) && reusedToken
        && reusedToken->generation != staleToken->generation;

    int doubleTarget = 0;
    const auto doubleToken = manager.observeLive(&doubleTarget);
    CARD_LIFETIME_CHECK(manager.requestNativeDelete(doubleToken));
    doubleDelete = !manager.requestNativeDelete(doubleToken);

    int adopted = 0;
    const auto adoptedToken = manager.observeLive(&adopted);
    CARD_LIFETIME_CHECK(manager.requestNativeDelete(adoptedToken));
    adoptionAfterDelete = manager.isLive(adoptedToken);

    int deferred = 0;
    const auto deferredToken = manager.observeLive(&deferred);
    CARD_LIFETIME_CHECK(manager.requestNativeDelete(deferredToken));
    deferredDelete = manager.gauge().pending_delete > 0;

    int owner = 0;
    directOwnerDestroy = manager.invalidateIfObserved(&owner) == false;

    QJsonObject result;
    result.insert(QStringLiteral("classification"), QStringLiteral("RED"));
    result.insert(QStringLiteral("stale_access"), staleAccess);
    result.insert(QStringLiteral("double_delete_request"), doubleDelete);
    result.insert(QStringLiteral("adoption_after_delete_request"), adoptionAfterDelete);
    result.insert(QStringLiteral("worker_deferred_delete"), deferredDelete);
    result.insert(QStringLiteral("direct_owner_destroy"), directOwnerDestroy);
    result.insert(QStringLiteral("normalized_exit"), 70);
    fprintf(stdout, "%s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return (staleAccess && doubleDelete && adoptionAfterDelete && deferredDelete && directOwnerDestroy) ? 0 : 70;
}

int runCardLifetimeRoomStateTests()
{
    QString bootstrapError;
    CARD_LIFETIME_CHECK(EngineBootstrap::initialize(false, &bootstrapError));
    const bool ok = probeRoomStateCanonicalReset();
    EngineBootstrap::shutdown();
    return ok ? 0 : 71;
}

int runCardLifetimeSyntheticTests(int actorCount, quint64 seed)
{
    CardLifetimeManager manager(CardLifetimeMode::ManagedReclaim, QThread::currentThread());
    quint64 eligibleCreated = 0;
    if (actorCount == 30) {
        QList<std::shared_ptr<const CardLifetimeToken>> ring;
        ring.reserve(64);
        for (int iteration = 0; iteration < 10000; ++iteration) {
            auto *card = new DummyCard;
            const auto token = manager.observeCard(card);
            CARD_LIFETIME_CHECK(token);
            CARD_LIFETIME_CHECK(manager.requestNativeDelete(token));
            const quint64 drained = manager.drain();
            CARD_LIFETIME_CHECK(drained == 1);
            eligibleCreated += drained;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            ring.push_back(token);
            if (ring.size() > 64)
                ring.pop_front();
        }
        const auto ringGauge = manager.gauge();
        CARD_LIFETIME_CHECK(ringGauge.managed_live == 0);
        CARD_LIFETIME_CHECK(ringGauge.pending_delete == 0);
        CARD_LIFETIME_CHECK(ringGauge.wrapper_leases == 0);
        CARD_LIFETIME_CHECK(ringGauge.peak_managed_cards <= 64);
        CARD_LIFETIME_CHECK(ringGauge.actually_destroyed == eligibleCreated);
    }
    for (int epoch = 0; epoch < 200; ++epoch) {
        QList<std::shared_ptr<const CardLifetimeToken>> tokens;
        tokens.reserve(actorCount);
        for (int actor = 0; actor < actorCount; ++actor) {
            auto *card = new DummyCard;
            const auto token = manager.observeCard(card);
            CARD_LIFETIME_CHECK(token);
            CARD_LIFETIME_CHECK(manager.retainWrapper(token));
            CARD_LIFETIME_CHECK(manager.requestNativeDelete(token));
            tokens.push_back(token);
        }
        const auto gauge = manager.gauge();
        CARD_LIFETIME_CHECK(gauge.managed_live <= static_cast<quint64>(actorCount));
        CARD_LIFETIME_CHECK(gauge.pending_delete <= static_cast<quint64>(actorCount));
        CARD_LIFETIME_CHECK(gauge.wrapper_leases <= static_cast<quint64>(actorCount * 2));
        for (const auto &token : tokens)
            CARD_LIFETIME_CHECK(manager.releaseWrapper(token));
        const quint64 drainedCount = manager.drain();
        CARD_LIFETIME_CHECK(drainedCount == static_cast<quint64>(actorCount));
        eligibleCreated += drainedCount;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto drained = manager.gauge();
        CARD_LIFETIME_CHECK(drained.managed_live == 0);
        CARD_LIFETIME_CHECK(drained.pending_delete == 0);
        CARD_LIFETIME_CHECK(drained.wrapper_leases == 0);
    }
    const auto finalGauge = manager.gauge();
    CARD_LIFETIME_CHECK(finalGauge.actually_destroyed == eligibleCreated);
    CARD_LIFETIME_CHECK(finalGauge.peak_managed_cards <= 64);
    QJsonObject result;
    result.insert(QStringLiteral("actor_count"), actorCount);
    result.insert(QStringLiteral("seed"), QString::number(seed));
    result.insert(QStringLiteral("eligible_created_count"),
                  QString::number(eligibleCreated));
    result.insert(QStringLiteral("actually_destroyed"),
                  QString::number(finalGauge.actually_destroyed));
    result.insert(QStringLiteral("peak_managed_cards"),
                  QString::number(finalGauge.peak_managed_cards));
    fprintf(stdout, "%s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
    return 0;
}

int runCardLifetimeLuaTests()
{
    const QString generatedPath = QDir::currentPath()
        + QStringLiteral("/builds/cmake-vs2026/generated/sanguosha_wrap.cxx");
    QFile generated(generatedPath);
    const QByteArray generatedText = generated.open(QIODevice::ReadOnly)
        ? generated.readAll() : QByteArray();
    const bool wrapperHasCardDispatch = generatedText.contains("qsgsCardNewPointerObj")
        && generatedText.contains("qsgsCardConvertPtr")
        && generatedText.contains("qsgsCardMustGetPtr");
    const QList<QByteArray> owningCloneTypes = {
        "LuaSkillCard", "LuaBasicCard", "LuaTrickCard", "LuaWeapon", "LuaArmor",
        "LuaHorse", "LuaOffensiveHorse", "LuaDefensiveHorse", "LuaTreasure"
    };
    bool generatedOwningClones = !generatedText.isEmpty();
    for (const QByteArray &type : owningCloneTypes) {
        const QByteArray wrapperPrefix = "static int _wrap_" + type + "_clone";
        int searchFrom = 0;
        bool foundCloneBody = false;
        while (generatedOwningClones
               && (searchFrom = generatedText.indexOf(wrapperPrefix, searchFrom)) >= 0) {
            int bodyEnd = generatedText.indexOf("\nstatic int _wrap_",
                                                searchFrom + wrapperPrefix.size());
            if (bodyEnd < 0)
                bodyEnd = generatedText.size();
            const QByteArray body = generatedText.mid(searchFrom, bodyEnd - searchFrom);
            if (body.contains("->clone(")) {
                foundCloneBody = true;
                generatedOwningClones = body.contains("SWIG_NewPointerObj")
                    && body.contains("SWIG_POINTER_OWN");
            }
            searchFrom = bodyEnd;
        }
        generatedOwningClones = generatedOwningClones && foundCloneBody;
    }
    const bool auditedCardRoots = generatedText.contains("\"Horse *\"")
        && generatedText.contains("\"OffensiveHorse *\"")
        && generatedText.contains("\"DefensiveHorse *\"")
        && generatedText.contains("\"Slash *\"")
        && generatedText.contains("\"Analeptic *\"")
        && generatedText.contains("\"DelayedTrick *\"")
        && generatedText.contains("\"Weapon *\"")
        && generatedText.contains("\"Armor *\"")
        && generatedText.contains("\"Treasure *\"")
        && generatedText.contains("\"LuaHorse *\"")
        && generatedText.contains("\"LuaWeapon *\"")
        && generatedText.contains("\"LuaArmor *\"")
        && generatedText.contains("\"LuaOffensiveHorse *\"")
        && generatedText.contains("\"LuaDefensiveHorse *\"")
        && generatedText.contains("\"LuaTreasure *\"");
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    bool luaExecuted = false;
    bool ownerZero = false;
    bool ownerOne = false;
    bool mustGet = false;
    bool cardList = false;
    bool sameGeneration = false;
    bool lightuserdataRejected = false;
    bool aliasMetatables = false;
    bool stockNonCard = false;
    bool duplicateGcReleasedOnce = false;
    bool duplicateGcIdempotent = false;
    bool owningCloneDestroyedOnce = false;
    bool wrapperLeasesReleased = false;
    quint64 wrapperLeasesBeforeClose = 0;
    quint64 wrapperLeasesAfterClose = 0;
    QString luaError;
    const quint64 wrapperLeaseBaseline = globalCardLifetimeManager().gauge().wrapper_leases;
    if (runtime.initialize(&luaError)) {
        const quint64 destroyedBeforeClone =
            globalCardLifetimeManager().gauge().actually_destroyed;
        {
            LuaRuntime::Binding binding(runtime);
            LuaRuntime::LuaInvocationScope invocation(runtime);
            lua_State *state = runtime.state();
            const char *cloneGcScript =
                "local source = sgs.LuaBasicCard(sgs.Card_SuitToBeDecided, 1, "
                "'lua_clone_source', 'LuaBasicCard', 'BasicCard')\n"
                "local clone = source:clone()\n"
                "assert(clone ~= nil)\n"
                "_G.card_lifetime_clone_source = source\n"
                "clone:deleteLater()\n"
                "local gc = debug.getmetatable(clone).__gc\n"
                "gc(clone)\n"
                "gc(clone)\n"
                "clone = nil\n"
                "collectgarbage('collect')\n";
            if (luaL_loadstring(state, cloneGcScript) != 0
                || lua_pcall(state, 0, 0, 0) != 0) {
                luaError = QString::fromUtf8(lua_tostring(state, -1));
                lua_pop(state, 1);
            }
        }
        const quint64 cloneDrain = globalCardLifetimeManager().drain();
        if (QCoreApplication::instance())
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        owningCloneDestroyedOnce = cloneDrain == 1
            && globalCardLifetimeManager().gauge().actually_destroyed
                == destroyedBeforeClone + 1;
        {
            LuaRuntime::Binding binding(runtime);
            LuaRuntime::LuaInvocationScope invocation(runtime);
            lua_State *state = runtime.state();
            const char *script =
                "local c = sgs.LuaBasicCard(sgs.Card_SuitToBeDecided, 1, "
                "'lua_lifetime_probe', 'LuaBasicCard', 'BasicCard')\n"
                "assert(c ~= nil)\n"
                "assert(c == c)\n"
                "local clone = c:clone()\n"
                "assert(clone ~= nil)\n"
                "local cards = sgs.CardList()\n"
                "cards:append(c)\n"
                "assert(cards:length() == 1)\n"
                "local ok = pcall(function() c:objectName() end)\n"
                "assert(ok)\n"
                "local lightOk, lightError = pcall(function() "
                "sgs.CardUseStruct(lightuserdata, nil, sgs.SPlayerList(), false, nil) end)\n"
                "local lightRejected = not lightOk\n"
                "local useOk = pcall(function() sgs.CardUseStruct(c, nil, sgs.SPlayerList(), false, nil) end)\n"
                "local cardGc = debug.getmetatable(c).__gc\n"
                "local aliases = {\n"
                "  OffensiveHorse = sgs.OffensiveHorse(sgs.Card_SuitToBeDecided, 1, -1),\n"
                "  DefensiveHorse = sgs.DefensiveHorse(sgs.Card_SuitToBeDecided, 1, 1),\n"
                "  Weapon = sgs.Weapon(sgs.Card_SuitToBeDecided, 1, 1),\n"
                "  Armor = sgs.Armor(sgs.Card_SuitToBeDecided, 1),\n"
                "  Treasure = sgs.Treasure(sgs.Card_SuitToBeDecided, 1),\n"
                "  LuaWeapon = sgs.LuaWeapon(sgs.Card_SuitToBeDecided, 1, 1, 'lua_weapon_probe', 'LuaWeapon'),\n"
                "  LuaArmor = sgs.LuaArmor(sgs.Card_SuitToBeDecided, 1, 'lua_armor_probe', 'LuaArmor'),\n"
                "  LuaOffensiveHorse = sgs.LuaOffensiveHorse(sgs.Card_SuitToBeDecided, 1, -1, 'lua_offensive_probe', 'LuaOffensiveHorse'),\n"
                "  LuaDefensiveHorse = sgs.LuaDefensiveHorse(sgs.Card_SuitToBeDecided, 1, 1, 'lua_defensive_probe', 'LuaDefensiveHorse'),\n"
                "  LuaTreasure = sgs.LuaTreasure(sgs.Card_SuitToBeDecided, 1, 'lua_treasure_probe', 'LuaTreasure')\n"
                "}\n"
                "local aliasesUseCardGc = true\n"
                "for name, alias in pairs(aliases) do\n"
                "  if alias == nil or debug.getmetatable(alias).__gc ~= cardGc then aliasesUseCardGc = false; break end\n"
                "end\n"
                "local nonCard = sgs.CardUseStruct()\n"
                "local stockNonCardGc = debug.getmetatable(nonCard).__gc ~= cardGc\n"
                "_G.card_lifetime_aliases = aliases\n"
                "_G.card_lifetime_noncard = nonCard\n"
                "return true, cards:length(), c == c, c ~= nil, clone ~= nil, useOk, lightRejected, tostring(lightError), aliasesUseCardGc, stockNonCardGc\n";
            lua_pushlightuserdata(state, static_cast<void *>(&runtime));
            lua_setglobal(state, "lightuserdata");
            if (luaL_loadstring(state, script) == 0 && lua_pcall(state, 0, 10, 0) == 0) {
                luaExecuted = lua_toboolean(state, -10) != 0;
                cardList = lua_tointeger(state, -9) == 1;
                sameGeneration = lua_toboolean(state, -8) != 0;
                ownerZero = lua_toboolean(state, -7) != 0;
                ownerOne = lua_toboolean(state, -6) != 0;
                mustGet = lua_toboolean(state, -5) != 0;
                lightuserdataRejected = lua_toboolean(state, -4) != 0;
                luaError = QString::fromUtf8(lua_tostring(state, -3));
                aliasMetatables = lua_toboolean(state, -2) != 0;
                stockNonCard = lua_toboolean(state, -1) != 0;
                lua_pop(state, 10);
                const quint64 wrapperLeasesBeforeDuplicateGc =
                    globalCardLifetimeManager().gauge().wrapper_leases;
                const char *duplicateGcScript =
                    "local gc = debug.getmetatable(card_lifetime_aliases.LuaOffensiveHorse).__gc\n"
                    "gc(card_lifetime_aliases.LuaOffensiveHorse)\n";
                if (luaL_loadstring(state, duplicateGcScript) == 0
                    && lua_pcall(state, 0, 0, 0) == 0) {
                    const quint64 wrapperLeasesAfterFirstGc =
                        globalCardLifetimeManager().gauge().wrapper_leases;
                    if (luaL_loadstring(state, duplicateGcScript) == 0
                        && lua_pcall(state, 0, 0, 0) == 0) {
                        const quint64 wrapperLeasesAfterSecondGc =
                            globalCardLifetimeManager().gauge().wrapper_leases;
                        duplicateGcReleasedOnce = wrapperLeasesAfterFirstGc + 1
                            == wrapperLeasesBeforeDuplicateGc;
                        duplicateGcIdempotent = wrapperLeasesAfterSecondGc
                            == wrapperLeasesAfterFirstGc;
                    }
                }
                if (lua_gettop(state) > 0)
                    lua_settop(state, 0);
            } else {
                luaError = QString::fromUtf8(lua_tostring(state, -1));
                lua_pop(state, 1);
            }
        }
        wrapperLeasesBeforeClose = globalCardLifetimeManager().gauge().wrapper_leases;
        runtime.shutdown();
        wrapperLeasesAfterClose = globalCardLifetimeManager().gauge().wrapper_leases;
        wrapperLeasesReleased = wrapperLeasesBeforeClose > wrapperLeaseBaseline
            && wrapperLeasesAfterClose == wrapperLeaseBaseline;
    }
    if (!wrapperHasCardDispatch)
        lightuserdataRejected = false;
    QJsonObject marker;
    marker.insert(QStringLiteral("lua_executed"), luaExecuted);
    marker.insert(QStringLiteral("wrapper_dispatch"), wrapperHasCardDispatch);
    marker.insert(QStringLiteral("generated_owning_clones"), generatedOwningClones);
    marker.insert(QStringLiteral("audited_card_roots"), auditedCardRoots);
    marker.insert(QStringLiteral("owner0"), ownerZero);
    marker.insert(QStringLiteral("owner1"), ownerOne);
    marker.insert(QStringLiteral("cardlist"), cardList);
    marker.insert(QStringLiteral("mustget"), mustGet);
    marker.insert(QStringLiteral("lightuserdata"), lightuserdataRejected);
    marker.insert(QStringLiteral("same_generation"), sameGeneration);
    marker.insert(QStringLiteral("alias_metatables"), aliasMetatables);
    marker.insert(QStringLiteral("stock_noncard"), stockNonCard);
    marker.insert(QStringLiteral("duplicate_gc_released_once"), duplicateGcReleasedOnce);
    marker.insert(QStringLiteral("duplicate_gc_idempotent"), duplicateGcIdempotent);
    marker.insert(QStringLiteral("owning_clone_destroyed_once"), owningCloneDestroyedOnce);
    marker.insert(QStringLiteral("wrapper_leases_before_close"),
                  static_cast<qint64>(wrapperLeasesBeforeClose));
    marker.insert(QStringLiteral("wrapper_leases_after_close"),
                  static_cast<qint64>(wrapperLeasesAfterClose));
    marker.insert(QStringLiteral("wrapper_leases_released"), wrapperLeasesReleased);
    marker.insert(QStringLiteral("error"), luaError);
    fprintf(stdout, "LUA_CARD_LIFETIME %s\n",
            QJsonDocument(marker).toJson(QJsonDocument::Compact).constData());
    return luaExecuted && wrapperHasCardDispatch && generatedOwningClones
        && auditedCardRoots && ownerZero && ownerOne
        && cardList && mustGet && sameGeneration && lightuserdataRejected
        && aliasMetatables && stockNonCard && duplicateGcReleasedOnce
        && duplicateGcIdempotent && owningCloneDestroyedOnce
        && wrapperLeasesReleased ? 0 : 71;
}

int runCardLifetimeDerivedCardConversionTests()
{
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    bool initialized = false;
    bool conversionSucceeded = false;
    QString luaError;
    if (runtime.initialize(&luaError)) {
        initialized = true;
        LuaRuntime::Binding binding(runtime);
        LuaRuntime::LuaInvocationScope invocation(runtime);
        lua_State *state = runtime.state();
        const char *script =
            "local horse = sgs.LuaOffensiveHorse(sgs.Card_SuitToBeDecided, 1, -1, "
            "'derived_conversion_probe', 'LuaOffensiveHorse')\n"
            "assert(horse ~= nil)\n"
            "local ok, err = pcall(function() horse:addCharTag('probe') end)\n"
            "return ok, tostring(err)\n";
        if (luaL_loadstring(state, script) == 0 && lua_pcall(state, 0, 2, 0) == 0) {
            conversionSucceeded = lua_toboolean(state, -2) != 0;
            luaError = QString::fromUtf8(lua_tostring(state, -1));
            lua_pop(state, 2);
        } else {
            luaError = QString::fromUtf8(lua_tostring(state, -1));
            lua_pop(state, 1);
        }
    }
    QJsonObject marker;
    marker.insert(QStringLiteral("initialized"), initialized);
    marker.insert(QStringLiteral("conversion_succeeded"), conversionSucceeded);
    marker.insert(QStringLiteral("error"), luaError);
    fprintf(stdout, "DERIVED_CARD_CONVERSION %s\n",
            QJsonDocument(marker).toJson(QJsonDocument::Compact).constData());
    return initialized && conversionSucceeded ? 0 : 73;
}
