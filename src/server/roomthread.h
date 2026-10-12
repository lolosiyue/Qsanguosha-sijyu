#ifndef _ROOM_THREAD_H
#define _ROOM_THREAD_H

#include "structs.h"
#include "trigger-cascade-break.h"
#include "trigger-dispatch-budget.h"
#include "game-state-contract.h"

#include <QHash>
#include <QSet>
#include <atomic>
#include <functional>

class GameRule;
class ManagedRewindLab;
struct SkillContext;

// Opt-in attached legacy skills use this only around an accepted effect. The
// enclosing dispatcher pins source identity before prompts or nested events.
class LegacySkillActivation
{
public:
    LegacySkillActivation(Room *room, ServerPlayer *owner, const QString &skillName);
    ~LegacySkillActivation() noexcept(false);
    explicit operator bool() const { return m_allowed; }
    static bool isAvailable(Room *room, ServerPlayer *owner, const QString &skillName);
    LegacySkillActivation(const LegacySkillActivation &) = delete;
    LegacySkillActivation &operator=(const LegacySkillActivation &) = delete;

private:
    Room *m_room;
    SkillContext *m_context = nullptr;
    bool m_allowed = true;
    int m_uncaughtExceptions;
};

struct LogMessage
{
    LogMessage();
    QVariant toVariant() const;

    QString type;
    ServerPlayer *from;
    QList<ServerPlayer *> to;
    QString card_str;
    QString arg;
    QString arg2;
    QString arg3;
    QString arg4;
    QString arg5;
};

class EventTriplet
{
public:
    inline EventTriplet(TriggerEvent triggerEvent, Room *room, ServerPlayer *target)
        : _m_event(triggerEvent), _m_room(room), _m_target(target)
    {
    }
    QString toString() const;
    TriggerEvent event() const { return _m_event; }

private:
    TriggerEvent _m_event;
    Room *_m_room;
    ServerPlayer *_m_target;
};

class RoomThread : public QThread
{
    Q_OBJECT

public:
    explicit RoomThread(Room *room);
    // Recoverable operations (draw/card use/damage) own a cascade only when
    // there is no enclosing operation. Judge joins; it must never manufacture
    // a successful JudgeStruct after cancellation.
    class CascadeScope {
    public:
        explicit CascadeScope(RoomThread &thread, bool recoverable = true);
        ~CascadeScope();
        CascadeScope(const CascadeScope &) = delete;
        CascadeScope &operator=(const CascadeScope &) = delete;
        bool ownsRoot() const { return m_ownsRoot; }
        quint64 id() const { return m_id; }
        bool cancelled() const;
        void checkpoint() const;
    private:
        RoomThread &m_thread;
        bool m_ownsRoot = false;
        quint64 m_id = 0;
        TriggerDispatchBudget m_previousBudget;
        quint64 m_previousGeneration = 0;
        quint64 m_previousId = 0;
        bool m_joinOnly = false;
        bool m_holdsBudget = false;
    };
    // A dying/death frame keeps its original native cursor after an event
    // cancellation. Only the cancelled stage is discarded; the saved owning
    // cascade remains cancelled when this continuation finishes.
    class DyingContinuationScope {
    public:
        explicit DyingContinuationScope(RoomThread &thread, bool isolateCancelled = true,
                                        quint64 retainedOwner = 0);
        ~DyingContinuationScope();
        DyingContinuationScope(const DyingContinuationScope &) = delete;
        DyingContinuationScope &operator=(const DyingContinuationScope &) = delete;
        bool cancelled() const { return bool(m_state); }
        quint64 cancelledCascadeId() const;
        void rethrowCancelled() const;
    private:
        friend class RoomThread;
        struct State;
        std::unique_ptr<State> capture(const TriggerCascadeBreak &cancel) const;
        void resume(const TriggerCascadeBreak &cancel);
        RoomThread &m_thread;
        std::unique_ptr<State> m_state;
        std::unique_ptr<State> m_unownedEntry;
    };
    bool invokeDyingEvent(DyingContinuationScope &continuation, TriggerEvent event,
                         ServerPlayer *target, QVariant &data, bool *nativeEnteredResult = nullptr);
    bool invokeDyingCallback(DyingContinuationScope &continuation,
                            const std::function<void()> &callback);
    class MandatoryCleanupScope {
    public:
        explicit MandatoryCleanupScope(RoomThread &thread) : m_thread(thread) { ++m_thread.m_mandatoryCleanupDepth; }
        ~MandatoryCleanupScope() { --m_thread.m_mandatoryCleanupDepth; }
        MandatoryCleanupScope(const MandatoryCleanupScope &) = delete;
        MandatoryCleanupScope &operator=(const MandatoryCleanupScope &) = delete;
    private:
        RoomThread &m_thread;
    };
    // Cancellation unwinds an author callback, then canonical physical commit
    // completes without more optional dispatch. Checkpoint after this scope.
    class NativeCommitScope {
    public:
        explicit NativeCommitScope(RoomThread &thread) : m_thread(thread) {
            if (thread.m_dispatchBudget.depth() == 0 && !thread.isMandatoryCleanup()) {
                thread.beginTriggerDispatch(NonTrigger, nullptr, false);
                m_holdsBudget = true;
                m_generation = thread.m_budgetGeneration;
            }
            ++m_thread.m_nativeCommitDepth;
            m_thread.m_commitAuthorDepths << m_thread.m_authorCallbackDepth;
        }
        ~NativeCommitScope() {
            m_thread.m_commitAuthorDepths.removeLast();
            --m_thread.m_nativeCommitDepth;
            if (m_holdsBudget) m_thread.leaveTriggerDispatch(m_generation);
        }
        NativeCommitScope(const NativeCommitScope &) = delete;
        NativeCommitScope &operator=(const NativeCommitScope &) = delete;
    private:
        RoomThread &m_thread;
        bool m_holdsBudget = false;
        quint64 m_generation = 0;
    };
    quint64 cascadeId() const { return m_cascadeId; }
    quint64 cascadeParentId() const { return m_operationRootActive ? m_operationParentId : 0; }
    bool hasActiveCascade() const { return m_cascadeId && m_dispatchBudget.depth() != 0; }
    bool isCascadeCancelled() const { return m_dispatchBudget.aborted(); }
    bool isMandatoryCleanup() const { return m_mandatoryCleanupDepth != 0; }
    bool isNativeCommitActive() const { return m_nativeCommitDepth != 0; }
    bool isSettlementBudgetExhausted() const;
    bool isDyingEventActive() const { return !m_dyingRuleFrames.isEmpty(); }
    bool settlementCursorCheckpoint(ServerPlayer *player);
    void settlementStepCheckpoint(ServerPlayer *player);
    bool triggerMandatoryGameRule(TriggerEvent event, ServerPlayer *target, QVariant &data);
    bool invokeStructuralCallback(const std::function<void()> &callback,
                                  const void *definition = nullptr, const QString &site = QString(),
                                  ServerPlayer *target = nullptr);
    void retireCancelledCallbackOrigins(quint64 owner);
    void recordDeferredAnytime(ServerPlayer *player, const QString &skill);
    void recordDeferredReveal(ServerPlayer *player, const QString &slot);
    void finishDeferredCascade(quint64 cascadeId, bool cancelled);
    quint64 deferredCreatorCascadeId() const;
    void checkCascadeCancellation() const;
    void constructTriggerTable();
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data);
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target);
    // Initialize only newly granted sources through the ordinary V2 lifecycle.
    bool triggerSkillSources(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data,
                             const QList<SkillInstanceRef> &sources);
    // Diagnostic snapshot; call from the RoomThread or after it has stopped.
    QVariantMap triggerDispatchProfile() const;

    // Invalidates only the client-facing distanceTo_* synchronization cache.
    // Server-side game rules continue to call Player::distanceTo() directly.
    void markDistanceCacheDirty();
    // Initial skill grants share the same deferred presentation boundary.
    void preparePlayers();
    // Coalesce presentation work inside triggers; requests flush before input.
    bool deferPlayerUiState(ServerPlayer *player);
    void flushPlayerUiState();
    // Preserve interrupted lifecycle IDs while TurnBroken cleanup unwinds.
    void rememberInterruptedTurn(qint64 eventId);
    qint64 interruptedTurn() const;
    void clearInterruptedTurn();
    qint64 takeInterruptedTurn();
    void rememberInterruptedPhase(qint64 eventId);
    qint64 interruptedPhase() const;
    void clearInterruptedPhase();
    qint64 takeInterruptedPhase();
    void markSkillDescriptionsDirty();
    void refreshSkillDescriptions();

    // A nested card can continue an accepted legacy effect without consuming
    // the same attached source a second time.
    bool isLegacySkillActivationActive(const SkillInstanceRef &source) const;

    void addPlayerSkills(ServerPlayer *player, bool invoke_game_start = false);

    void addTriggerSkill(const TriggerSkill *skill);
    void delay(long msecs = -1);
    ServerPlayer *find3v3Next(QList<ServerPlayer *> &first, QList<ServerPlayer *> &second);
    void run3v3(QList<ServerPlayer *> &first, QList<ServerPlayer *> &second, GameRule *game_rule, ServerPlayer *current);
    void actionHulaoPass(ServerPlayer *shenlvbu, QList<ServerPlayer *> league, GameRule *game_rule, int stage);
    ServerPlayer *findHulaoPassNext(ServerPlayer *shenlvbu, QList<ServerPlayer *> league, int stage);
    void actionNormal(GameRule *game_rule);

    // Opt-in audited normal games only. Configuration is frozen on enrollment.
    bool enableManagedTurns(const GameState::ProviderRegistry &registry, QString *error = nullptr);
    bool setManagedLuaProviders(const QString &gameProvider, const QString &aiProvider);
    bool requestManagedRestore(GameTimeline::AnchorKind kind, QString *error = nullptr);
    // The production loop and focused tests both execute this entire native turn.
    bool stepNormalTurn(GameRule *gameRule, QString *error = nullptr, bool executeTurn = true);
    bool isManagedTurnBoundary() const;
    void invalidateManagedTurns(const QString &reason);
    using TimelineCommitObserver = std::function<void(const GameTimeline &, const GameTimeline::Anchor &)>;
    void setTimelineCommitObserver(TimelineCommitObserver observer);

    const QList<EventTriplet> *getEventStack() const;

protected:
    virtual void run();

private:
    friend class LegacySkillActivation;
    friend class ManagedRewindLab;
    ManagedRewindLab *m_managedLab = nullptr;
    bool m_managedRestoredBoundary = false;
    friend struct RoomTestAccess;
    struct LegacyExecutionFrame {
        QString skillName;
        TriggerEvent event;
        ServerPlayer *target = nullptr;
        QVariant *data = nullptr;
        QHash<QString, QList<SkillInstanceRef>> sources;
    };
    QList<LegacyExecutionFrame> m_legacyExecutionFrames;
    QList<SkillInstanceRef> m_activeLegacySources;

    struct TriggerDispatchProfile {
        quint64 triggerCount = 0;
        quint64 priorityRebuildCount = 0;
        quint64 prioritySkillCount = 0;
        quint64 prioritySortCount = 0;
        quint64 v2DispatchCount = 0;
        quint64 v2EmptyDispatchCount = 0;
        quint64 v2CandidateCount = 0;
        quint64 mainTableCandidateVisitCount = 0;
    };

    struct TriggerSkillTraits {
        bool v2 = false;
        bool equipOrRule = false;
        bool gameRule = false;
    };

    void _handleTurnBroken3v3(QList<ServerPlayer *> &first, QList<ServerPlayer *> &second, GameRule *game_rule);
    void _handleTurnBrokenHulaoPass(ServerPlayer *shenlvbu, QList<ServerPlayer *> league, GameRule *game_rule, int stage);
    void _handleTurnBrokenNormal(GameRule *game_rule);
    bool dispatchTrigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data);
    void reclaimCompletedTurn();
    bool triggerV2Skills(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data,
                         const QList<TriggerSkill *> *equipmentGroup = nullptr,
                         const QList<SkillInstanceRef> *allowedSources = nullptr);
    void sortTriggerSkills(TriggerEvent triggerEvent, Room *room, bool includeLose);
    void refreshDistanceCacheIfDirty(Room *room);
    void flushOutermostDeferredWork(Room *room);
    void emitPerfTrace() const;
    void checkTriggerDispatchAbort() const { checkCascadeCancellation(); }
    void beginTriggerDispatch(TriggerEvent event, ServerPlayer *target, bool countEvent = true);
    void triggerDispatchStep(TriggerEvent event, ServerPlayer *target, const QString &skill = QString());
    [[noreturn]] void abortTriggerDispatch(TriggerEvent event, ServerPlayer *target, const QString &skill);
    bool invokeAuthorCallback(const std::function<void()> &callback, bool nativeBoundary = false);
    bool suppressOptionalDispatch() const;
    void noteDyingNativeRule(TriggerEvent event, ServerPlayer *target, QVariant &data);
    bool enterCallbackOrigin(const void *definition, TriggerEvent event, ServerPlayer *target,
                             const QString &site, const QString &source = QString(), int instance = 0);
    bool chargeSettlementWork(bool event, TriggerEvent triggerEvent, ServerPlayer *target);
    void leaveTriggerDispatch(quint64 generation);
    const QByteArray &distancePropertyName(const ServerPlayer *player);

    Room *room;
    std::unique_ptr<GameState::ProviderRegistry> m_managedRegistry;
    bool m_managedBoundary = false;
    bool m_managedRestorePending = false;
    GameTimeline::AnchorKind m_managedRestoreKind = GameTimeline::AnchorKind::PlayerTurn;
    QString m_managedRestoreAnchor;
    quint64 m_managedTurnSerial = 0;
    QString m_managedFailure;
    QString m_managedGameProvider;
    QString m_managedAiProvider;
    TimelineCommitObserver m_timelineCommitObserver;
    TriggerDispatchBudget m_dispatchBudget;
    quint64 m_budgetGeneration = 1;
    quint64 m_nextCascadeId = 0;
    quint64 m_cascadeId = 0;
    quint64 m_operationParentId = 0;
    bool m_operationRootActive = false;
    unsigned m_joinOnlyDepth = 0;
    unsigned m_authorCallbackDepth = 0;
    QSet<int> m_discardOptionalFrames;
    unsigned m_nativeCommitDepth = 0;
    unsigned m_nativeDeathCommitDepth = 0;
    QList<unsigned> m_commitAuthorDepths;
    unsigned m_mandatoryCleanupDepth = 0;
    bool m_dispatchAbortReported = false;
    struct DispatchBreadcrumb {
        int event;
        QString player;
        int phase;
        QString skill;
    };
    QList<DispatchBreadcrumb> m_dispatchRecent;
    struct DyingRuleFrame {
        TriggerEvent event;
        ServerPlayer *target;
        QVariant *data;
        bool *entered;
    };
    QList<DyingRuleFrame> m_dyingRuleFrames;
    QList<QString> m_callbackOrigins;
    QHash<quint64, QSet<QString>> m_cancelledCallbackOrigins;
    quint64 m_settlementOwner = 0;
    struct SettlementEpoch {
        quint64 events = 0, steps = 0;
        bool exhausted = false;
    };
    TriggerDispatchBudget::Limits m_settlementLimits;
    QHash<quint64, SettlementEpoch> m_settlementEpochs;
    using DeferredEntries = QHash<ServerPlayer *, QSet<QString>>;
    QHash<quint64, DeferredEntries> m_deferredAnytime;
    QHash<quint64, DeferredEntries> m_deferredReveals;
    bool m_playerUiStateDirty = false;
    bool m_preparingPlayerUiState = false;
    bool m_flushingPlayerUiState = false;
    QSet<ServerPlayer *> m_pendingPlayerUiState;
    std::atomic_bool m_skillDescriptionsDirty{true};
    bool m_refreshingSkillDescriptions = false;
    bool m_distanceCacheDirty = false;
    bool m_perfTraceEnabled;
    int m_profileRoomId;
    QString m_profileMode;
    TriggerDispatchProfile m_triggerDispatchProfile;
    QHash<const ServerPlayer *, QByteArray> m_distancePropertyNames;
    QHash<const ServerPlayer *, QHash<const ServerPlayer *, int>> m_lastBroadcastDistances;
    qint64 m_interruptedTurnEventId = 0;
    qint64 m_interruptedPhaseEventId = 0;
    QString order;

    QList<TriggerSkill *> skill_table[NumOfEvents];
    quint64 m_triggerTableRevision[NumOfEvents] = {};
    QList<TriggerSkill *> v2_skill_table[NumOfEvents];
    QList<const TriggerSkill *> skillSet;
    QHash<const TriggerSkill *, TriggerSkillTraits> m_triggerSkillTraits;

    QList<EventTriplet> event_stack;
};

#endif
