#include "card-lifetime-manager.h"
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

#include <cassert>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QStringList>
#include "lua.hpp"
#include <cstdio>
#include <limits>

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
                   receipt.exitStatus == QProcess::NormalExit);
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
        && receipt.shutdownStages.size() >= 1
        && receipt.shutdownFailures.constFirst().value(invariantKey).toInteger() > 0;
}
}

int runCardLifetimeLegacyRedTests();

int runCardLifetimeTests()
{
    QString bootstrapError;
    assert(EngineBootstrap::initialize(false, &bootstrapError));
    const bool defaultModeRegression =
        defaultCardLifetimeMode() == CardLifetimeMode::ManagedReclaim;
    const CardLifetimeGauge defaultGauge = globalCardLifetimeManager().gauge();
    assert(defaultGauge.unknown_unclaimed == 0);
    assert(defaultGauge.unknown_qvariant_card_payload == 0);
    assert(defaultGauge.change_list_self_cycle == 0);
    assert(defaultGauge.change_list_cycles == 0);
    assert(defaultGauge.change_list_reuse_reconnect == 0);
    assert(defaultGauge.unapproved_card_raw_delete == 0);
    assert(defaultGauge.card_delete_bypass == 0);
    assert(defaultGauge.adoption_failed == 0);
    assert(defaultGauge.affinity_transfer_failed == 0);
    QProcess legacy;
    legacy.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--suite"),
                                                            QStringLiteral("card-lifetime-legacy-red")});
    assert(legacy.waitForFinished(15000));
    assert(legacy.exitCode() == 0);
    const QJsonDocument legacyReport = QJsonDocument::fromJson(legacy.readAllStandardOutput());
    assert(legacyReport.object().value(QStringLiteral("classification")).toString() == QLatin1String("RED"));
    assert(legacyReport.object().value(QStringLiteral("normalized_exit")).toInt() == 70);
    LuaRuntime runtime(LuaRuntime::Auxiliary);
    assert(runtime.invocationDepth() == 0);
    {
        LuaRuntime::LuaInvocationScope invocation(runtime);
        assert(runtime.invocationDepth() == 1);
        {
            LuaRuntime::LuaInvocationScope nested(runtime);
            assert(runtime.invocationDepth() == 2);
        }
        assert(runtime.invocationDepth() == 1);
    }
    assert(runtime.invocationDepth() == 0);
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
        assert(first);
        assert(identityManager.retainWrapper(first));
        assert(identityManager.bindWrapper(&wrapperAddress, first));
        CardLifetimeManager::setCurrentRuntimeContext(previous.domain, previous.identity,
                                                       previous.generation, previous.state);
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainB, &runtimeIdentityB, 1, stateB);
        assert(!identityManager.wrapperBinding(&wrapperAddress));
        assert(!identityManager.releaseWrapperBinding(&wrapperAddress));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainA, &runtimeIdentityA, 1, stateA);
        assert(identityManager.releaseWrapperBinding(&wrapperAddress));
        assert(identityManager.invalidateIfObserved(&reusedAddress));
        assert(!identityManager.bindWrapper(&wrapperAddress, first));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainB, &runtimeIdentityB, 2, stateB);
        const auto second = identityManager.observeLive(&reusedAddress);
        assert(second && second->generation != first->generation);
        assert(identityManager.retainWrapper(second));
        assert(identityManager.bindWrapper(&wrapperAddress, second));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainA, &runtimeIdentityA, 1, stateA);
        assert(!identityManager.releaseWrapperBinding(&wrapperAddress));
        CardLifetimeManager::setCurrentRuntimeContext(
            &runtimeDomainB, &runtimeIdentityB, 2, stateB);
        assert(identityManager.releaseWrapperBinding(&wrapperAddress));
        assert(identityManager.invalidateIfObserved(&reusedAddress));
        CardLifetimeManager::setCurrentRuntimeContext(previous.domain, previous.identity,
                                                       previous.generation, previous.state);
        assert(identityManager.resetForTest());
        CardLifetimeManager baselineManager(CardLifetimeMode::ObserveOnly);
        int baselineAddress = 0;
        const auto baselineToken = baselineManager.observeLive(&baselineAddress);
        assert(baselineToken);
        baselineManager.setDomainBaseline(&runtimeDomainA, {&baselineAddress});
        assert(!baselineManager.resetForTest());
        assert(baselineManager.invalidateIfObserved(&baselineAddress));
        assert(baselineManager.resetForTest());
    }
    {
        LuaRuntime guardedRuntime(LuaRuntime::Auxiliary);
        LuaRuntime otherRuntime(LuaRuntime::Auxiliary);
        assert(guardedRuntime.initialize());
        assert(otherRuntime.initialize());
        {
            LuaRuntime::Binding binding(guardedRuntime);
            LuaRuntime::LuaInvocationScope invocation(guardedRuntime);
            assert(guardedRuntime.invocationDepth() == 1);
            assert(globalCardLifetimeManager().gaugeForRuntime(
                       guardedRuntime.lifetimeDomain(), &guardedRuntime,
                       guardedRuntime.generation(), guardedRuntime.rawState()).lua_pins == 1);
            guardedRuntime.shutdown();
            assert(guardedRuntime.lifecycle() == LuaRuntime::Lifecycle::Running);
            assert(guardedRuntime.rawState() != nullptr);
        }
        assert(globalCardLifetimeManager().gaugeForRuntime(
                   guardedRuntime.lifetimeDomain(), &guardedRuntime,
                   guardedRuntime.generation(), guardedRuntime.rawState()).lua_pins == 0);
        guardedRuntime.shutdown();
        assert(guardedRuntime.lifecycle() == LuaRuntime::Lifecycle::Closed);
        guardedRuntime.shutdown();
        assert(guardedRuntime.lifecycle() == LuaRuntime::Lifecycle::Closed);
        assert(otherRuntime.lifecycle() == LuaRuntime::Lifecycle::Running);
        otherRuntime.shutdown();
        assert(otherRuntime.lifecycle() == LuaRuntime::Lifecycle::Closed);
    }
    {
        static int sharedDomain = 0;
        LuaRuntime gameRuntime(LuaRuntime::Game);
        LuaRuntime aiRuntime(LuaRuntime::Auxiliary);
        gameRuntime.setLifetimeDomain(&sharedDomain);
        aiRuntime.setLifetimeDomain(&sharedDomain);
        assert(gameRuntime.initialize());
        assert(aiRuntime.initialize());
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
            assert(gameToken && manager.retainWrapper(gameToken));
            assert(manager.bindWrapper(&gameWrapper, gameToken));
        }
        {
            LuaRuntime::Binding binding(aiRuntime);
            aiToken = manager.observeLive(&aiCard);
            assert(aiToken && manager.retainWrapper(aiToken));
            assert(manager.bindWrapper(&aiWrapper, aiToken));
        }
        gameRuntime.shutdown();
        {
            LuaRuntime::Binding binding(aiRuntime);
            assert(manager.wrapperBinding(&aiWrapper));
            assert(manager.gaugeForRuntime(aiRuntime.lifetimeDomain(), &aiRuntime,
                                           aiRuntime.generation(), aiRuntime.rawState())
                       .wrapper_leases == 1);
        }
        aiRuntime.shutdown();
        assert(manager.invalidateIfObserved(&gameCard));
        assert(manager.invalidateIfObserved(&aiCard));
    }
    assert(probeCanonicalOwner());
    assert(probeCanonicalOwner());
    assert(probeCanonicalOwner());
    assert(probeCanonicalOwner());
    const QStringList roomModes = {QStringLiteral("03_1v2"), QStringLiteral("02_1v1"),
                                   QStringLiteral("06_3v3"), QStringLiteral("06_XMode")};
    for (const QString &mode : roomModes) {
        assert(probeRoomMode(mode));
        assert(probeWrappedCardCanonicalOwner(mode));
    }
    assert(probeRoomStateCanonicalReset());
    CardLifetimeManager observe(CardLifetimeMode::ObserveOnly);
    const auto token = observe.observeLive(&liveObject, false);
    assert(token);
    assert(observe.isLive(token));
    assert(observe.retainWrapper(token));
    QByteArray error;
    assert(observe.requestLuaDelete(token, &error));
    assert(observe.gauge().pending_delete == 1);
    assert(observe.drain() == 0);
    assert(observe.releaseWrapper(token));
    assert(observe.invalidateIfObserved(&liveObject));
    assert(!observe.isLive(token));
    const auto reusedObserved = observe.observeLive(&liveObject);
    assert(reusedObserved && reusedObserved->generation != token->generation);
    CardLifetimeManager reuseManager(CardLifetimeMode::ObserveOnly);
    int reused = 0;
    const auto firstGeneration = reuseManager.observeLive(&reused);
    assert(firstGeneration);
    assert(reuseManager.invalidateIfObserved(&reused));
    assert(reuseManager.resetForTest());
    const auto secondGeneration = reuseManager.observeLive(&reused);
    assert(secondGeneration && secondGeneration->generation != firstGeneration->generation);
    CardLifetimeManager secondDomain(CardLifetimeMode::ObserveOnly);
    const auto secondDomainToken = secondDomain.observeLive(&reused);
    assert(secondDomainToken);
    assert(secondDomain.isLive(secondDomainToken));
    assert(!reuseManager.isLive(secondDomainToken));
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
        assert(cardToken);
        assert(card.lifetimeGeneration() == cardToken->generation);
        assert(card.lifetimeIsLive());
        assert(cardManager.invalidateIfObserved(&card));
        assert(!card.lifetimeIsLive());
    }
    {
        CardLifetimeManager &cardManager = globalCardLifetimeManager();
        auto *card = new DummyCard;
        const auto cardToken = cardManager.observeLive(card);
        assert(cardToken);
        card->deleteLater();
    assert(cardManager.gauge().native_delete_requested > 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        assert(!cardManager.isLive(cardToken));
    }
    const auto managedToken = managed.observeLive(&otherObject, false);
    assert(managedToken);
    {
        CardLifetimeLease lease(managed, managedToken);
        assert(lease.isValid());
        assert(managed.gauge().native_leases > 0);
    }
    int adoptedObject = 0;
    const auto adoptedToken = managed.observeLive(&adoptedObject, false);
    assert(adoptedToken);
    assert(managed.requestNativeDelete(adoptedToken));
    assert(managed.markAdopted(adoptedToken));
    error.clear();
    assert(managed.requestLuaDelete(adoptedToken, &error));
    assert(error.isEmpty());
    assert(managed.gauge().adopted_delete_ignored > 0);
    assert(!managed.resetForTest());
    assert(managed.requestNativeDelete(adoptedToken));
    assert(managed.drain() == 0);
    assert(managed.gauge().adopted_after_delete_request > 0);
    assert(managed.requestNativeDelete(managedToken));
    assert(managed.state(managedToken) == CardLifetimeState::PendingDelete);
    assert(managed.drain() == 1);
    assert(managed.state(managedToken) == CardLifetimeState::Retired);

    int scopedObject = 0;
    const auto scopedToken = managed.observeLive(&scopedObject);
    assert(scopedToken && managed.requestNativeDelete(scopedToken));
    {
        CardLifetimeScope scope(managed);
        activeScopeBlocked = managed.drain() == 0;
        assert(activeScopeBlocked);
        assert(managed.state(scopedToken) == CardLifetimeState::PendingDelete);
    }
    assert(managed.drain() == 1);
    int reservedObject = 0;
    const auto reservedToken = managed.observeLive(&reservedObject);
    assert(reservedToken && managed.requestNativeDelete(reservedToken));
    assert(managed.reserveAdoption(reservedToken));
    reservationBlocked = managed.drain() == 0;
    assert(reservationBlocked);
    managed.cancelAdoption(reservedToken);
    assert(managed.drain() == 1);
    QThread foreignOwner;
    CardLifetimeManager affinity(CardLifetimeMode::ManagedReclaim, &foreignOwner);
    int foreignObject = 0;
    const auto foreignToken = affinity.observeLive(&foreignObject);
    assert(foreignToken && affinity.requestNativeDelete(foreignToken));
    wrongThreadBlocked = affinity.drain() == 0;
    assert(wrongThreadBlocked);
    assert(affinity.state(foreignToken) == CardLifetimeState::PendingDelete);
    assert(affinity.invalidateIfObserved(&foreignObject));
    assert(affinity.resetForTest());

    CardLifetimeManager deadLeaseManager(CardLifetimeMode::ManagedReclaim);
    auto *deadLeaseCard = new DummyCard;
    const auto deadLeaseToken = deadLeaseManager.observeCard(deadLeaseCard);
    assert(deadLeaseToken);
    assert(deadLeaseManager.retainWrapper(deadLeaseToken));
    assert(deadLeaseManager.retainNativeLease(deadLeaseToken));
    assert(deadLeaseManager.requestNativeDelete(deadLeaseToken));
    realEligibleCreated = deadLeaseManager.gauge().pending_delete;
    assert(deadLeaseManager.drain() == 0);
    leaseBlocked = deadLeaseManager.gauge().pending_delete == realEligibleCreated
        && deadLeaseManager.gauge().native_leases > 0
        && deadLeaseManager.gauge().wrapper_leases > 0;
    assert(leaseBlocked);
    delete deadLeaseCard;
    realDestructorCompleted = deadLeaseManager.gauge().actually_destroyed;
    realActuallyDestroyed = deadLeaseManager.gauge().actually_destroyed;
    const auto wrongGeneration = std::make_shared<CardLifetimeToken>(*deadLeaseToken);
    ++wrongGeneration->generation;
    assert(!deadLeaseManager.releaseNativeLease(wrongGeneration));
    const bool noResurrection = !deadLeaseManager.observeLive(deadLeaseCard);
    const bool releasedWrapper = deadLeaseManager.releaseWrapper(deadLeaseToken);
    const bool releasedNative = deadLeaseManager.releaseNativeLease(deadLeaseToken);
    realDeadRelease = noResurrection && releasedWrapper && releasedNative
        && deadLeaseManager.gauge().native_leases == 0
        && deadLeaseManager.gauge().wrapper_leases == 0
        && deadLeaseManager.entryCount() == 0
        && realDestructorCompleted == realActuallyDestroyed;
    assert(realDeadRelease);

    int luaPinnedObject = 0;
    const auto luaPinnedToken = managed.observeLive(&luaPinnedObject);
    assert(luaPinnedToken && managed.requestNativeDelete(luaPinnedToken));
    LuaRuntime pinnedRuntime(LuaRuntime::Auxiliary);
    {
        LuaRuntime::LuaInvocationScope invocation(pinnedRuntime);
        assert(pinnedRuntime.invocationDepth() == 1);
        luaPinBlocked = managed.drain() == 0;
        assert(luaPinBlocked);
    }
    assert(managed.drain() == 1);

    const auto definition = managed.observeLive(&liveObject, true);
    assert(definition);
    error.clear();
    assert(managed.requestLuaDelete(definition, &error));
    assert(error.isEmpty());
    assert(managed.invalidateIfObserved(&adoptedObject) == true);
    DummyCard *physical = new DummyCard;
    const auto physicalToken = managed.observeCard(physical);
    assert(physicalToken);
    assert(managed.requestNativeDelete(physicalToken));
    const auto destroyedBefore = managed.gauge().actually_destroyed;
    const auto eligible = managed.drain();
    assert(eligible == 1);
    assert(managed.gauge().actually_destroyed == destroyedBefore);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    assert(managed.gauge().actually_destroyed == destroyedBefore + eligible);
    assert(!managed.isLive(physicalToken));
    assert(managed.state(physicalToken) == CardLifetimeState::Dead);
    assert(managed.gauge().actually_destroyed >= 1);


    DummyCard *lateCard = nullptr;
    {
        CardLifetimeManager teardownManager(CardLifetimeMode::ManagedReclaim);
        lateCard = new DummyCard;
        const auto lateToken = teardownManager.observeCard(lateCard);
        assert(lateToken);
    }
    delete lateCard;
    auto *unassociatedCard = new DummyCard;
    delete unassociatedCard;

    const auto before = managed.gauge().unknown_card_delete;
    error.clear();
    assert(!managed.requestLuaDelete({}, &error));
    assert(error == CardLifetimeManager::unknownOwnershipError());
    assert(managed.gauge().unknown_card_delete == before + 1);
    error.clear();
    assert(!managed.requestLuaDelete(reinterpret_cast<const void *>(static_cast<quintptr>(0xdeadbeef)), &error));
    assert(error == CardLifetimeManager::unknownOwnershipError());
    assert(managed.gauge().unknown_card_delete == before + 2);
    const auto opaqueBefore = managed.gauge().unknown_qvariant_card_payload;
    error.clear();
    assert(!managed.rejectOpaqueVariant(&error));
    assert(error == CardLifetimeManager::opaqueVariantError());
    assert(managed.gauge().unknown_qvariant_card_payload == opaqueBefore + 1);
    assert(QByteArray(CardLifetimeManager::opaqueVariantError())
           == QByteArray("Card lifetime error: rejected opaque QVariant Card payload"));
    DummyCard cycle;
    DummyCard cycleTarget;
    auto &globalManager = globalCardLifetimeManager();
    const auto cycleBefore = globalManager.gauge();
    cycle.addChange(&cycle);
    cycle.addChange(&cycleTarget);
    cycle.addChange(&cycleTarget);
    cycleTarget.addChange(&cycle);
    assert(cycle.change_cards.size() == 1);
    const auto cycleAfter = globalManager.gauge();
    assert(cycleAfter.change_list_self_cycle > cycleBefore.change_list_self_cycle);
    assert(cycleAfter.change_list_reuse_reconnect + cycleAfter.change_list_cycles
           > cycleBefore.change_list_reuse_reconnect + cycleBefore.change_list_cycles);
    assert(cycleAfter.sidecar_edges == cycleBefore.sidecar_edges + 1);
    globalManager.removeChangeEdges(&cycle);
    globalManager.removeChangeEdges(&cycleTarget);
    assert(globalManager.gauge().sidecar_edges == cycleBefore.sidecar_edges);
    DummyCard tagSource;
    DummyCard tagTarget;
    const auto leaseBefore = globalManager.gauge().native_leases;
    tagSource.setTag(QStringLiteral("cardLease"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
    assert(globalManager.gauge().native_leases == leaseBefore + 1);
    tagSource.setTag(QStringLiteral("cardLease"), QVariant(42));
    assert(globalManager.gauge().native_leases == leaseBefore);
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
        assert(globalManager.gauge().native_leases > leaseBefore);
    }
    assert(globalManager.gauge().native_leases == leaseBefore);
    {
        QVariantMap nested;
        nested.insert(QStringLiteral("card"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
        QVariantList list;
        list << nested;
        QByteArray variantError;
        assert(globalManager.retainVariantPayload(&tagSource, list, &variantError));
        assert(globalManager.gauge().native_leases > leaseBefore);
        globalManager.releaseEventPayload(&tagSource);
    assert(globalManager.gauge().native_leases == leaseBefore);
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
            assert(globalManager.gauge().native_leases > moveLeaseBefore);
        }
        assert(globalManager.gauge().native_leases == moveLeaseBefore);

        CardMoveReason valid;
        valid.m_extraData = QVariant::fromValue(static_cast<Card *>(&tagTarget));
        const QVariant validPayload = valid.m_extraData;
        CardMoveReason opaque;
        opaque.m_extraData = QVariant::fromValue(UnregisteredCardPayload());
        valid = opaque;
        assert(valid.m_extraData == validPayload);

        ServerPlayer tagPlayer(nullptr);
        const auto playerLeaseBefore = globalManager.gauge().native_leases;
        tagPlayer.setTag(QStringLiteral("nestedCard"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
        assert(globalManager.gauge().native_leases == playerLeaseBefore + 1);
        tagPlayer.setTag(QStringLiteral("nestedCard"), QVariant(7));
        assert(globalManager.gauge().native_leases == playerLeaseBefore);
        tagPlayer.setTag(QStringLiteral("nestedCard"), QVariant::fromValue(static_cast<Card *>(&tagTarget)));
        ServerPlayer copiedPlayer(nullptr);
        copiedPlayer.copyFrom(&tagPlayer);
        assert(globalManager.gauge().native_leases >= playerLeaseBefore + 2);
        copiedPlayer.removeTag(QStringLiteral("nestedCard"));
        tagPlayer.removeTag(QStringLiteral("nestedCard"));
        assert(globalManager.gauge().native_leases == playerLeaseBefore);
    }
    assert(managed.invalidateIfObserved(&liveObject));
    const CardLifetimeGauge preResetGauge = managed.gauge();
    assert(managed.resetForTest());
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
        && shutdownReceipt.markerCount("CARD_LIFETIME_SHUTDOWN_STAGE") == 4
        && shutdownReceipt.shutdownStages == QList<QString>{QStringLiteral("worker-final"),
            QStringLiteral("preclose"), QStringLiteral("lua-close"), QStringLiteral("postclose")}
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
    return defaultModeRegression && eventProtocol && adoptionProtocol
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
    assert(staleToken);
    assert(manager.invalidateIfObserved(&stale));
    const auto reusedToken = manager.observeLive(&stale);
    staleAccess = !manager.isLive(staleToken) && reusedToken
        && reusedToken->generation != staleToken->generation;

    int doubleTarget = 0;
    const auto doubleToken = manager.observeLive(&doubleTarget);
    assert(manager.requestNativeDelete(doubleToken));
    doubleDelete = !manager.requestNativeDelete(doubleToken);

    int adopted = 0;
    const auto adoptedToken = manager.observeLive(&adopted);
    assert(manager.requestNativeDelete(adoptedToken));
    adoptionAfterDelete = manager.isLive(adoptedToken);

    int deferred = 0;
    const auto deferredToken = manager.observeLive(&deferred);
    assert(manager.requestNativeDelete(deferredToken));
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
    assert(EngineBootstrap::initialize(false, &bootstrapError));
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
            assert(token);
            assert(manager.requestNativeDelete(token));
            const quint64 drained = manager.drain();
            assert(drained == 1);
            eligibleCreated += drained;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            ring.push_back(token);
            if (ring.size() > 64)
                ring.pop_front();
        }
        const auto ringGauge = manager.gauge();
        assert(ringGauge.managed_live == 0);
        assert(ringGauge.pending_delete == 0);
        assert(ringGauge.wrapper_leases == 0);
        assert(ringGauge.peak_managed_cards <= 64);
        assert(ringGauge.actually_destroyed == eligibleCreated);
    }
    for (int epoch = 0; epoch < 200; ++epoch) {
        QList<std::shared_ptr<const CardLifetimeToken>> tokens;
        tokens.reserve(actorCount);
        for (int actor = 0; actor < actorCount; ++actor) {
            auto *card = new DummyCard;
            const auto token = manager.observeCard(card);
            assert(token);
            assert(manager.retainWrapper(token));
            assert(manager.requestNativeDelete(token));
            tokens.push_back(token);
        }
        const auto gauge = manager.gauge();
        assert(gauge.managed_live <= static_cast<quint64>(actorCount));
        assert(gauge.pending_delete <= static_cast<quint64>(actorCount));
        assert(gauge.wrapper_leases <= static_cast<quint64>(actorCount * 2));
        for (const auto &token : tokens)
            assert(manager.releaseWrapper(token));
        const quint64 drainedCount = manager.drain();
        assert(drainedCount == static_cast<quint64>(actorCount));
        eligibleCreated += drainedCount;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto drained = manager.gauge();
        assert(drained.managed_live == 0);
        assert(drained.pending_delete == 0);
        assert(drained.wrapper_leases == 0);
    }
    const auto finalGauge = manager.gauge();
    assert(finalGauge.actually_destroyed == eligibleCreated);
    assert(finalGauge.peak_managed_cards <= 64);
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
    bool wrapperLeasesReleased = false;
    quint64 wrapperLeasesBeforeClose = 0;
    quint64 wrapperLeasesAfterClose = 0;
    QString luaError;
    const quint64 wrapperLeaseBaseline = globalCardLifetimeManager().gauge().wrapper_leases;
    if (runtime.initialize(&luaError)) {
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
    marker.insert(QStringLiteral("wrapper_leases_before_close"),
                  static_cast<qint64>(wrapperLeasesBeforeClose));
    marker.insert(QStringLiteral("wrapper_leases_after_close"),
                  static_cast<qint64>(wrapperLeasesAfterClose));
    marker.insert(QStringLiteral("wrapper_leases_released"), wrapperLeasesReleased);
    marker.insert(QStringLiteral("error"), luaError);
    fprintf(stdout, "LUA_CARD_LIFETIME %s\n",
            QJsonDocument(marker).toJson(QJsonDocument::Compact).constData());
    return luaExecuted && wrapperHasCardDispatch && auditedCardRoots && ownerZero && ownerOne
        && cardList && mustGet && sameGeneration && lightuserdataRejected
        && aliasMetatables && stockNonCard && duplicateGcReleasedOnce
        && duplicateGcIdempotent && wrapperLeasesReleased ? 0 : 71;
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
