#ifndef QSAN_AI_RUNTIME_H
#define QSAN_AI_RUNTIME_H

#include "ai.h"
#include "ai-strategy-readonly-card-store.h"
#include "game-rng.h"
#include "lua-runtime.h"

#include <QHash>
#include <functional>

class Room;
class Card;
class ServerPlayer;
class AIOwnerReadonlyCardStore;
class AIRound24SourceSession;
class RoomManagedState;
struct lua_Debug;

enum AiRoute {
    AiRouteLegacyDirect,
    AiRouteLegacyAdapted,
    AiRouteIsolated
};

class AiRouteRegistry
{
public:
    AiRouteRegistry();

    bool setDecisionRoute(AIRequest::DecisionKind kind, AiRoute route);
    bool setCallbackRoute(const QString &callbackName, const QString &skillName, AiRoute route);
    AiRoute routeFor(AIRequest::DecisionKind kind, const QString &callbackName = QString(),
                     const QString &skillName = QString()) const;
    void freeze() { m_frozen = true; }
    bool isFrozen() const { return m_frozen; }
    bool hasLegacyRoutes() const;

private:
    static QString callbackKey(const QString &callbackName, const QString &skillName);

    bool m_frozen;
    QHash<int, AiRoute> m_decisionRoutes;
    QHash<QString, AiRoute> m_callbackRoutes;
};

class AiLuaRuntime
{
public:
    explicit AiLuaRuntime(Room *room);
    ~AiLuaRuntime();

    bool initialize(QString *error = nullptr);
    static bool requiresLegacyRuntime();
    static void pushWorldView(lua_State *state, const AIWorldView &world);
    static void evaluateModePolicy(LuaRuntime &runtime, AIWorldView &world,
                                   bool viewerOnly = false);
    void shutdown();
    void projectDefenseHistory(AIWorldView &world);
    // Exact live processEvent request; never a zero-ID decision exemption.
    bool currentOLJijiangInvokeEvent(const AIRequest &, const ServerPlayer *) const;
    bool currentOLJijiangResponseEvent(const AIRequest &, const ServerPlayer *, const QString &payload) const;
    bool currentOLJijiangPlanningRequest(const AIRequest &, const ServerPlayer *) const;
    bool currentXufaChoiceRequest(const AIRequest &, const ServerPlayer *) const;
    // Native audited producers transfer the exact detached product, never a Lua spec.
    QString issueReadonlyCard(const AIRequest &, ServerPlayer *, std::unique_ptr<Card>, const QString &sourceKey);
    QString issueBorrowedReadonlyCard(const AIRequest &, ServerPlayer *, const Card *, const QString &sourceKey);
    const Card *readonlyCard(const AIRequest &, ServerPlayer *, const QString &key) const;
    QString readonlyCardSource(const AIRequest &, ServerPlayer *, const QString &key) const;
    void trackObservationLedger(const QSharedPointer<AIConversionLedger> &);
    AIReadonlyCardLease retainReadonlyCard(const AIRequest &, ServerPlayer *, const QString &key) const;
    void seed(quint64 seed);

    // AI decisions use an RNG independent from the gameplay/runtime RNG.  The
    // state is deliberately exposed as the stable GameRng::State DTO so a
    // takeover snapshot can resume AI random choices without restoring the
    // SmartAI Lua heap (the historical AI mind is intentionally fresh).
    GameRng::State exportRngState() const;
    GameRng::State rngState() const { return exportRngState(); }
    bool restoreRngState(const GameRng::State &state, QString *error = nullptr);
    bool restore(const GameRng::State &state, QString *error = nullptr)
    {
        return restoreRngState(state, error);
    }

    LuaRuntime &lua() { return m_lua; }
    const LuaRuntime &lua() const { return m_lua; }
    AiRouteRegistry &routes() { return m_routes; }
    const AiRouteRegistry &routes() const { return m_routes; }
    using ConversionResolver = std::function<QJsonObject(const AIRequest &, const QJsonObject &)>;
    void setConversionResolver(ConversionResolver resolver) { m_conversionResolver = std::move(resolver); }
    AIResult decideIsolated(const AIRequest &request);
    static bool validateDecisionEffects(const AIWorldView &world, const QJsonArray &effects);
    bool commitStrategyEffects(const AIWorldView &world, const QJsonArray &effects,
                               LuaRuntime &modeRuntime, QString *error = nullptr);
    QJsonObject speechCounters() const { return m_speechCounters; }
    bool processEvent(const AIWorldView &world, const AIEventView &event,
                      LuaRuntime &modeRuntime, QString *error = nullptr);

private:
    friend class RoomManagedState;

    // Called only by RoomManagedState after the AI VM is quiescent and a
    // detached RNG candidate has been fully prepared. Keeps m_rng's address
    // stable and performs no allocation or Lua calls.
    void commitPreparedRng(GameRng &prepared) noexcept;

    class ExecutionBinding
    {
    public:
        explicit ExecutionBinding(AiLuaRuntime &runtime);
        ~ExecutionBinding();

    private:
        AiLuaRuntime *m_previous;
    };

    static int luaSourceCompatibility(lua_State *state);
    static int luaOriginalSuitResult(lua_State *state);
    static int luaOriginalDiscardError(lua_State *state);
    static int luaRandom(lua_State *state);
    static int luaResolveConversion(lua_State *state);
    static int luaResolveSkillAction(lua_State *state);
    static int luaRangyiQuestion(lua_State *state);
    static int luaBindJieyinghQuestion(lua_State *state);
    static int luaUseValueFlag(lua_State *state);
    static int luaEvaluateExtraShicai(lua_State *state);
    static int luaOverseasPriority(lua_State *state);
    static int luaOLJijiangState(lua_State *state);
    static int luaOverseasXiafengFlag(lua_State *state);
    static int luaBindZhifengQuestion(lua_State *state);
    static int luaBindOverseasNiwoSelected(lua_State *state);
    static int luaSelectSijyuFengyiPhysical(lua_State *state);
    static int luaObserveOverseasLihuo(lua_State *state);
    static int luaEvaluateSkillCard(lua_State *state);
    static int luaOlClanSavedSlot(lua_State *state);
    static int luaNewgeneralsSavedSlot(lua_State *state);
    static int luaHeqiaSavedPlan(lua_State *state);
    static int luaLeiluanSavedPlan(lua_State *state);
    static int luaJianmanSavedPlan(lua_State *state);
    static int luaFaqiSavedPlan(lua_State *state);
    static int luaXufaActualSlot(lua_State *state);
    static int luaXuanzhuActualSlot(lua_State *state);
    static int luaOlClanPlanObservation(lua_State *state);
    static int luaRecordNestedDelegateUse(lua_State *state);
    static int luaInspectNestedCardaskString(lua_State *state);
    static int luaMatchOwnerDiscardPattern(lua_State *state);
    static int luaEvaluateNengchenPreview(lua_State *state);
    static int luaEvaluateNotifiedCard(lua_State *state);
    static int luaRushBPhysicalSkill(lua_State *state);
    static int luaOriginalAocaiPattern(lua_State *state);
    static int luaEvaluateTianyiSlash(lua_State *state);
    static int luaWithOwnerRange(lua_State *state);
    static int luaWithOwnerDistance(lua_State *state);
    static int luaEvaluateOwnerRangeCard(lua_State *state);
    static int luaQiuxinTargetFilter(lua_State *state);
    static int luaEvaluatePreviewCard(lua_State *state);
    static int luaBindPreviewPhysical(lua_State *state);
    static int luaTargetRecommendCache(lua_State *state);
    static int luaOriginalSourceWarning(lua_State *state);
    static int luaImagineSourceDiagnostic(lua_State *state);
    static int luaInovationSourceDiagnostic(lua_State *state);
    static int luaManjuanSource(lua_State *state);
    static int luaPeiqiSourceRecord(lua_State *state);
    static int luaDaojiSourceCard(lua_State *state);
    static int luaCurrentSourceSlot(lua_State *state);
    static int luaPoxiSourceSlot(lua_State *state);
    static int luaInovationXinyang(lua_State *state);
    static int luaInovationShoushi(lua_State *state);
    static int luaInovationRemaining(lua_State *state);
    static int luaSourceImmediateEffect(lua_State *state);
    static int luaInovationGoutong(lua_State *state);
    static int luaLianliCardaskError(lua_State *state);
    static int luaManbingFlag(lua_State *state);
    static int luaOriginalKurouShadow(lua_State *state);
    static int luaOriginalTurnUsePresence(lua_State *state);
    static int luaOriginalCrossbowShadow(lua_State *state);
    static int luaOriginalGuanxingShadow(lua_State *state);
    static int luaOriginalSDChoice(lua_State *state);
    static int luaOriginalCardChosenResult(lua_State *state);
    static int luaTargetReviseProperty(lua_State *state);
    static int luaEvaluateResponsePolicy(lua_State *state);
    static int luaResponseCatalogue(lua_State *state);
    static int luaChoiceContinuity(lua_State *state);
    static int luaEvaluateZenhuiCard(lua_State *state);
    static int luaCardTargetFilter(lua_State *state);
    static int luaConsumePlayerHint(lua_State *state);
    static int luaEvaluateSkillSeed(lua_State *state);
    static int luaSampleHiddenHand(lua_State *state);
    static int luaSampleOwnHand(lua_State *state);
    static int luaConsumeDeckCountShuffle(lua_State *state);
    static int luaSamplePatternLogName(lua_State *state);
    static int luaModeObjective(lua_State *state);
    static int luaModeRefresh(lua_State *state);
    static int luaModeUpdatePlayers(lua_State *state);
    static int luaRegisterDefenseWriter(lua_State *state);
    static int luaPublishDefenseSnapshot(lua_State *state);
    static int luaRefreshDiscardPreferences(lua_State *state);
    QJsonObject queryStandardMode(LuaRuntime &modeRuntime, const AIRequest &request,
                                 const QString &target, int toUseCount, bool refresh,
                                 const QJsonArray *prefixDeltas = nullptr,
                                 const QJsonArray *prefixEffects = nullptr,
                                 bool updatePlayers = false, bool update = false);
    static int luaRandomSeed(lua_State *state);
    static int luaTraceback(lua_State *state);
    static int luaAiDataRead(lua_State *state);
    static int luaAiDataWrite(lua_State *state);
    static void luaInstructionHook(lua_State *state, lua_Debug *debug);
    static AiLuaRuntime *fromUpvalue(lua_State *state);
    static AiLuaRuntime *current();

    bool installSandbox(QString *error);
    bool loadScriptWithBudget(const QString &path, qint64 instructionBudget, QString *error);
    bool loadIsolatedScript(const QString &fileName, QString *error);
    QStringList declaredCoreScripts(QString *error);
    void loadPackageScripts();
    bool loadConfiguredScripts(QString *error);
    void loadConfiguredRoutes();
    void pushRequest(lua_State *state, const AIRequest &request) const;
    bool parseResult(lua_State *state, AIResult &result, const AIRequest &request) const;

    QJsonObject m_speechCounters;
    QHash<QString,QJsonObject> m_defenseHistory;
    QJsonObject m_pendingDefenseHistory;
    int m_defenseWriterRef = -2; // LUA_NOREF, retained only for this auxiliary VM

    Room *m_room;
    const AIRequest *m_activeRequest = nullptr;
    bool m_originalDiscardResultReady=false;
    QList<int> m_originalDiscardResultIds;
    quint64 m_originalDiscardResultDecision=0, m_originalDiscardResultRevision=0;
    int m_originalDiscardMinimum=0, m_originalDiscardMaximum=0;
    bool m_originalDiscardOptional=false, m_originalDiscardIncludeEquip=false, m_originalDiscardExchange=false;
    QString m_originalDiscardPattern, m_originalDiscardReason, m_originalDiscardViewer;
    bool m_originalSuitResultReady = false;
    int m_originalSuitResultValue = 0;
    quint64 m_originalSuitResultDecision = 0, m_originalSuitResultRevision = 0;
    std::unique_ptr<AIOwnerReadonlyCardStore> m_readonlyCards;
    std::unique_ptr<AIRound24SourceSession> m_sourceCompatibility;
    QList<QWeakPointer<AIConversionLedger>> m_observationLedgers;
    void clearObservationLedgers();
    const AIRequest *m_activeEventRequest = nullptr;
    const AIEventView *m_activePeiqiEvent = nullptr; // native-only delivered event lifetime
    LuaRuntime *m_activeEventMode = nullptr;
    ConversionResolver m_conversionResolver;
    LuaRuntime m_lua;
    GameRng m_rng;
    AiRouteRegistry m_routes;
    qint64 m_instructionBudget;
    qint64 m_initializationInstructionBudget;
    qint64 m_instructionsRemaining;
    bool m_instructionLimitExceeded;
};

#endif
