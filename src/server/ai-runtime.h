#ifndef QSAN_AI_RUNTIME_H
#define QSAN_AI_RUNTIME_H

#include "ai.h"
#include "game-rng.h"
#include "lua-runtime.h"

#include <QHash>

class Room;
struct lua_Debug;

enum AiRoute {
    AiRouteLegacyDirect,
    AiRouteLegacyAdapted,
    AiRouteIsolated,
    AiRouteShadow
};

enum AiShadowComparison {
    AiShadowNotCovered,
    AiShadowMatch,
    AiShadowMismatch,
    AiShadowError
};

struct AiShadowAuditEntry {
    quint64 decisionId = 0;
    QString callbackName;
    QString skillName;
    QString pattern;
    AIResult officialResult;
    AIResult shadowResult;
    AiShadowComparison comparison = AiShadowNotCovered;
};

struct AiShadowAuditSummary {
    quint64 notCovered = 0;
    quint64 matches = 0;
    quint64 mismatches = 0;
    quint64 errors = 0;
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
    void shutdown();
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
    AIResult decideShadow(const AIRequest &request);
    void recordShadowAudit(const AIRequest &request, const QString &callbackName,
                           const QString &skillName, const AIResult &officialResult,
                           const AIResult &shadowResult);
    const QList<AiShadowAuditEntry> &shadowAudits() const { return m_shadowAudits; }
    const AiShadowAuditSummary &shadowAuditSummary() const { return m_shadowAuditSummary; }

private:
    class ExecutionBinding
    {
    public:
        explicit ExecutionBinding(AiLuaRuntime &runtime);
        ~ExecutionBinding();

    private:
        AiLuaRuntime *m_previous;
    };

    static int luaRandom(lua_State *state);
    static int luaRandomSeed(lua_State *state);
    static int luaTraceback(lua_State *state);
    static int luaAiDataRead(lua_State *state);
    static int luaAiDataWrite(lua_State *state);
    static void luaInstructionHook(lua_State *state, lua_Debug *debug);
    static AiLuaRuntime *fromUpvalue(lua_State *state);
    static AiLuaRuntime *current();

    bool installSandbox(QString *error);
    bool loadScriptWithBudget(const QString &path, qint64 instructionBudget, QString *error);
    bool loadConfiguredScripts(QString *error);
    void loadConfiguredRoutes();
    void pushRequest(lua_State *state, const AIRequest &request) const;
    bool parseResult(lua_State *state, AIResult &result) const;

    Room *m_room;
    LuaRuntime m_lua;
    GameRng m_rng;
    AiRouteRegistry m_routes;
    qint64 m_instructionBudget;
    qint64 m_initializationInstructionBudget;
    qint64 m_instructionsRemaining;
    bool m_instructionLimitExceeded;
    int m_shadowAuditLimit;
    QList<AiShadowAuditEntry> m_shadowAudits;
    AiShadowAuditSummary m_shadowAuditSummary;
};

#endif
