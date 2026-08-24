#include "ai-runtime.h"

#include "ai-data-store.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "settings.h"

#include <QDebug>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QMetaEnum>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace {

thread_local AiLuaRuntime *currentAiRuntime = nullptr;

const size_t AiSoftMemoryLimit = 64u * 1024u * 1024u;
const size_t AiHardMemoryLimit = 128u * 1024u * 1024u;
const size_t AiMaxResultStringBytes = 64u * 1024u;
const size_t AiMaxSelectedCards = 2048u;
const size_t AiMaxSelectedTargets = 64u;
const int AiAuditListLimit = 32;
const int AiAuditStringLimit = 512;

bool aiResultInteger(lua_Number number, int &value)
{
    if (!std::isfinite(double(number))
        || number < lua_Number(std::numeric_limits<int>::min())
        || number > lua_Number(std::numeric_limits<int>::max()))
        return false;
    const int converted = static_cast<int>(number);
    if (lua_Number(converted) != number)
        return false;
    value = converted;
    return true;
}

bool sameAiResult(const AIResult &first, const AIResult &second)
{
    return first.kind == second.kind
        && first.handled == second.handled
        && first.errorCode == second.errorCode
        && first.action.legacyCardString == second.action.legacyCardString
        && first.action.selectedCardIds == second.action.selectedCardIds
        && first.action.selectedTargetNames == second.action.selectedTargetNames
        && first.action.userString == second.action.userString
        && first.action.hasSkillActionContext == second.action.hasSkillActionContext
        && (!first.action.hasSkillActionContext
            || (first.action.skillActionContext.activationRef
                    == second.action.skillActionContext.activationRef
                && first.action.skillActionContext.sourceRef
                    == second.action.skillActionContext.sourceRef));
}

const char *shadowComparisonName(AiShadowComparison comparison)
{
    switch (comparison) {
    case AiShadowNotCovered:
        return "not_covered";
    case AiShadowMatch:
        return "match";
    case AiShadowMismatch:
        return "mismatch";
    case AiShadowError:
        return "error";
    }
    return "unknown";
}

QString auditString(const QString &value)
{
    if (value.size() <= AiAuditStringLimit)
        return value;
    const QByteArray bytes = value.toUtf8();
    return QStringLiteral("sha256:%1 bytes:%2")
        .arg(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()))
        .arg(bytes.size());
}

AIResult auditResult(const AIResult &source)
{
    AIResult result;
    result.kind = source.kind;
    result.handled = source.handled;
    result.decisionId = source.decisionId;
    result.stateRevision = source.stateRevision;
    result.errorCode = auditString(source.errorCode);
    result.action.legacyCardString = auditString(source.action.legacyCardString);
    result.action.userString = auditString(source.action.userString);
    result.action.selectedCardIds = source.action.selectedCardIds.mid(0, AiAuditListLimit);
    foreach (const QString &target, source.action.selectedTargetNames.mid(0, AiAuditListLimit))
        result.action.selectedTargetNames << auditString(target);
    result.action.hasSkillActionContext = source.action.hasSkillActionContext;
    result.action.skillActionContext = source.action.skillActionContext;
    return result;
}

bool readBoundedString(lua_State *state, int index, QString &value)
{
    size_t size = 0;
    const char *data = lua_tolstring(state, index, &size);
    if (!data || size > AiMaxResultStringBytes)
        return false;
    value = QString::fromUtf8(data, qsizetype(size));
    return true;
}

void pushQString(lua_State *state, const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    lua_pushlstring(state, utf8.constData(), size_t(utf8.size()));
}

void setStringField(lua_State *state, const char *name, const QString &value)
{
    pushQString(state, value);
    lua_setfield(state, -2, name);
}

void setIntegerField(lua_State *state, const char *name, int value)
{
    lua_pushinteger(state, value);
    lua_setfield(state, -2, name);
}

bool setMetaEnumFields(lua_State *state, const QMetaObject &metaObject,
                       const char *enumeratorName, const char *fieldPrefix)
{
    const int enumeratorIndex = metaObject.indexOfEnumerator(enumeratorName);
    if (enumeratorIndex < 0)
        return false;

    const QMetaEnum enumerator = metaObject.enumerator(enumeratorIndex);
    for (int index = 0; index < enumerator.keyCount(); ++index) {
        const QByteArray fieldName = QByteArray(fieldPrefix) + enumerator.key(index);
        setIntegerField(state, fieldName.constData(), enumerator.value(index));
    }
    return true;
}

void pushAICardView(lua_State *state, const AICardView &card)
{
    lua_createtable(state, 0, 10);
    lua_pushinteger(state, card.cardId);
    lua_setfield(state, -2, "id");
    lua_pushinteger(state, card.effectiveId);
    lua_setfield(state, -2, "effective_id");
    setStringField(state, "name", card.objectName);
    setStringField(state, "class_name", card.className);
    lua_pushinteger(state, card.suit);
    lua_setfield(state, -2, "suit");
    lua_pushinteger(state, card.number);
    lua_setfield(state, -2, "number");
    setStringField(state, "skill_name", card.skillName);
    lua_pushboolean(state, card.red);
    lua_setfield(state, -2, "red");
    lua_pushboolean(state, card.black);
    lua_setfield(state, -2, "black");
    lua_createtable(state, int(card.kindOfNames.size()), 0);
    for (int index = 0; index < card.kindOfNames.size(); ++index) {
        pushQString(state, card.kindOfNames.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "kind_of");
}

void pushAICards(lua_State *state, const QList<AICardView> &cards)
{
    lua_createtable(state, int(cards.size()), 0);
    for (int index = 0; index < cards.size(); ++index) {
        pushAICardView(state, cards.at(index));
        lua_rawseti(state, -2, index + 1);
    }
}

void pushAIJsonValue(lua_State *state, const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        lua_pushnil(state);
        break;
    case QJsonValue::Bool:
        lua_pushboolean(state, value.toBool());
        break;
    case QJsonValue::Double: {
        const double number = value.toDouble();
        const lua_Integer integer = lua_Integer(number);
        if (double(integer) == number)
            lua_pushinteger(state, integer);
        else
            lua_pushnumber(state, lua_Number(number));
        break;
    }
    case QJsonValue::String:
        pushQString(state, value.toString());
        break;
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        lua_createtable(state, array.size(), 0);
        for (int index = 0; index < array.size(); ++index) {
            pushAIJsonValue(state, array.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        break;
    }
    case QJsonValue::Object: {
        const QJsonObject object = value.toObject();
        lua_createtable(state, 0, object.size());
        for (auto item = object.constBegin(); item != object.constEnd(); ++item) {
            pushAIJsonValue(state, item.value());
            const QByteArray key = item.key().toUtf8();
            lua_setfield(state, -2, key.constData());
        }
        break;
    }
    }
}

void pushAISkillView(lua_State *state, const AISkillView &skill)
{
    lua_createtable(state, 0, 8);
    setStringField(state, "name", skill.skillName);
    lua_pushinteger(state, skill.instanceId);
    lua_setfield(state, -2, "instance_id");
    lua_pushinteger(state, skill.source);
    lua_setfield(state, -2, "source");
    lua_pushboolean(state, skill.invalid);
    lua_setfield(state, -2, "invalid");
    lua_pushboolean(state, skill.hasAmountOverride);
    lua_setfield(state, -2, "has_amount_override");
    lua_pushinteger(state, skill.amount);
    lua_setfield(state, -2, "amount");
    if (skill.hasPrivateState) {
        pushAIJsonValue(state, skill.state);
        lua_setfield(state, -2, "state");
    }
    pushAIJsonValue(state, skill.correctState);
    lua_setfield(state, -2, "correct_state");
}

void pushAISkills(lua_State *state, const QList<AISkillView> &skills)
{
    lua_createtable(state, int(skills.size()), 0);
    for (int index = 0; index < skills.size(); ++index) {
        pushAISkillView(state, skills.at(index));
        lua_rawseti(state, -2, index + 1);
    }
}

void pushAIPlayerView(lua_State *state, const AIPlayerView &player)
{
    lua_createtable(state, 0, 23);
    setStringField(state, "object_name", player.objectName);
    lua_pushinteger(state, player.seat);
    lua_setfield(state, -2, "seat");
    lua_pushinteger(state, player.hp);
    lua_setfield(state, -2, "hp");
    lua_pushinteger(state, player.maxHp);
    lua_setfield(state, -2, "max_hp");
    lua_pushinteger(state, player.handcardCount);
    lua_setfield(state, -2, "handcard_count");
    lua_pushinteger(state, player.phase);
    lua_setfield(state, -2, "phase");
    lua_pushboolean(state, player.alive);
    lua_setfield(state, -2, "alive");
    lua_pushboolean(state, player.dead);
    lua_setfield(state, -2, "dead");
    lua_pushboolean(state, player.removed);
    lua_setfield(state, -2, "removed");
    lua_pushboolean(state, player.kongcheng);
    lua_setfield(state, -2, "kongcheng");
    lua_pushboolean(state, player.wounded);
    lua_setfield(state, -2, "wounded");
    lua_pushboolean(state, player.faceUp);
    lua_setfield(state, -2, "face_up");
    lua_pushboolean(state, player.chained);
    lua_setfield(state, -2, "chained");
    setStringField(state, "kingdom", player.kingdom);
    setStringField(state, "role", player.role);
    setStringField(state, "general", player.generalName);
    setStringField(state, "general2", player.general2Name);
    pushAICards(state, player.equips);
    lua_setfield(state, -2, "equips");
    pushAICards(state, player.judgingArea);
    lua_setfield(state, -2, "judging_area");
    lua_createtable(state, 0, int(player.publicMarks.size()));
    for (auto mark = player.publicMarks.constBegin(); mark != player.publicMarks.constEnd(); ++mark) {
        lua_pushinteger(state, mark.value());
        const QByteArray key = mark.key().toUtf8();
        lua_setfield(state, -2, key.constData());
    }
    lua_setfield(state, -2, "public_marks");
    pushAISkills(state, player.skills);
    lua_setfield(state, -2, "skills");
}

void pushAIWorldView(lua_State *state, const AIWorldView &world)
{
    lua_createtable(state, 0, 6);
    setStringField(state, "revision", QString::number(world.revision));
    pushAIPlayerView(state, world.self);
    lua_setfield(state, -2, "self");
    lua_createtable(state, int(world.players.size()), 0);
    for (int index = 0; index < world.players.size(); ++index) {
        pushAIPlayerView(state, world.players.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "players");
    pushAICards(state, world.handCards);
    lua_setfield(state, -2, "hand_cards");
    setStringField(state, "current_player", world.currentPlayer);
    lua_pushinteger(state, world.currentPhase);
    lua_setfield(state, -2, "current_phase");
}

}

AiRouteRegistry::AiRouteRegistry()
    : m_frozen(false)
{
    m_decisionRoutes.insert(int(AIRequest::Activate), AiRouteLegacyAdapted);
    m_decisionRoutes.insert(int(AIRequest::UseCard), AiRouteShadow);
}

bool AiRouteRegistry::setDecisionRoute(AIRequest::DecisionKind kind, AiRoute route)
{
    if (m_frozen)
        return false;
    m_decisionRoutes.insert(int(kind), route);
    return true;
}

bool AiRouteRegistry::setCallbackRoute(const QString &callbackName,
                                       const QString &skillName, AiRoute route)
{
    if (m_frozen || callbackName.isEmpty())
        return false;
    m_callbackRoutes.insert(callbackKey(callbackName, skillName), route);
    return true;
}

AiRoute AiRouteRegistry::routeFor(AIRequest::DecisionKind kind,
                                  const QString &callbackName,
                                  const QString &skillName) const
{
    if (!callbackName.isEmpty()) {
        const QString exact = callbackKey(callbackName, skillName);
        if (m_callbackRoutes.contains(exact))
            return m_callbackRoutes.value(exact);
        const QString callbackDefault = callbackKey(callbackName, QString());
        if (m_callbackRoutes.contains(callbackDefault))
            return m_callbackRoutes.value(callbackDefault);
    }
    return m_decisionRoutes.value(int(kind), AiRouteLegacyDirect);
}

QString AiRouteRegistry::callbackKey(const QString &callbackName,
                                     const QString &skillName)
{
    return callbackName + QChar(0x1f) + skillName;
}

AiLuaRuntime::AiLuaRuntime(Room *room)
    : m_room(room), m_lua(LuaRuntime::Auxiliary), m_instructionBudget(2000000),
      m_initializationInstructionBudget(500000), m_instructionsRemaining(0),
      m_instructionLimitExceeded(false), m_shadowAuditLimit(256)
{
}

AiLuaRuntime::~AiLuaRuntime()
{
    shutdown();
}

bool AiLuaRuntime::initialize(QString *error)
{
    const int softMiB = qMax(1, Config.value(QStringLiteral("AiLuaSoftMemoryMiB"),
                                             int(AiSoftMemoryLimit / 1024u / 1024u)).toInt());
    const int hardMiB = qMax(softMiB, Config.value(QStringLiteral("AiLuaHardMemoryMiB"),
                                                   int(AiHardMemoryLimit / 1024u / 1024u)).toInt());
    m_instructionBudget = qMax<qint64>(10000,
        Config.value(QStringLiteral("AiLuaInstructionBudget"), 2000000).toLongLong());
    m_initializationInstructionBudget = qMax<qint64>(10000,
        Config.value(QStringLiteral("AiLuaInitializationInstructionBudget"), 500000).toLongLong());
    m_shadowAuditLimit = qBound(1,
        Config.value(QStringLiteral("AiShadowAuditLimit"), 256).toInt(), 4096);
    m_lua.setMemoryLimits(size_t(softMiB) * 1024u * 1024u,
                          size_t(hardMiB) * 1024u * 1024u);
    if (!m_lua.initialize(error))
        return false;
    LuaRuntime::Binding binding(m_lua);
    if (!m_lua.addPackagePath(QStringLiteral("./lua/?.lua"), error)
        || !m_lua.addPackagePath(QStringLiteral("./lua/?/init.lua"), error)
        || !loadScriptWithBudget(QStringLiteral("lua/ai/isolated-bootstrap.lua"),
                                 m_initializationInstructionBudget, error)
        || !installSandbox(error)
        || !loadScriptWithBudget(QStringLiteral("lua/ai/isolated-facades.lua"),
                                 m_initializationInstructionBudget, error)
        || !loadConfiguredScripts(error)) {
        shutdown();
        return false;
    }
    loadConfiguredRoutes();
    m_routes.freeze();
    return true;
}

void AiLuaRuntime::shutdown()
{
    m_lua.shutdown();
}

void AiLuaRuntime::seed(quint64 seed)
{
    const quint64 aiSeed = seed ^ Q_UINT64_C(0x9e3779b97f4a7c15);
    m_rng.seed(aiSeed);
    m_lua.setSeed(aiSeed);
}

AIResult AiLuaRuntime::decideShadow(const AIRequest &request)
{
    AIResult result;
    result.decisionId = request.decisionId;
    result.stateRevision = request.stateRevision;
    if (!m_lua.rawState())
        return result;

    bool rebuild = false;
    {
        LuaRuntime::Binding luaBinding(m_lua);
        ExecutionBinding executionBinding(*this);
        lua_State *state = m_lua.state();
        lua_getglobal(state, "ai_decide");
        if (!lua_isfunction(state, -1)) {
            lua_pop(state, 1);
            return result;
        }
        pushRequest(state, request);
        m_instructionsRemaining = m_instructionBudget;
        m_instructionLimitExceeded = false;
        lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
        LuaRuntime::LuaInvocationScope invocation(m_lua);
        const int status = lua_pcall(state, 1, 1, 0);
        lua_sethook(state, nullptr, 0, 0);
        if (status != 0) {
            result.errorCode = m_instructionLimitExceeded
                ? QStringLiteral("AI_INSTRUCTION_LIMIT")
                : status == LUA_ERRMEM ? QStringLiteral("AI_MEMORY_LIMIT")
                                       : QStringLiteral("AI_RUNTIME_ERROR");
            lua_pop(state, 1);
            rebuild = status == LUA_ERRMEM || m_instructionLimitExceeded;
        } else {
            if (!lua_isnil(state, -1)) {
                if (!parseResult(state, result)) {
                    result.errorCode = QStringLiteral("AI_INVALID_RESULT");
                } else if (request.hasSkillActionContext) {
                    result.action.hasSkillActionContext = true;
                    result.action.skillActionContext = request.skillActionContext;
                }
            }
            lua_pop(state, 1);
            if (m_lua.exceedsSoftMemoryLimit()) {
                lua_gc(state, LUA_GCCOLLECT, 0);
                if (m_lua.exceedsSoftMemoryLimit()) {
                    result = AIResult();
                    result.decisionId = request.decisionId;
                    result.stateRevision = request.stateRevision;
                    result.errorCode = QStringLiteral("AI_MEMORY_LIMIT");
                    rebuild = true;
                }
            }
        }
    }
    if (rebuild) {
        shutdown();
        QString error;
        if (!initialize(&error))
            qWarning().noquote() << "Unable to rebuild AI Lua runtime:" << error;
    }
    return result;
}

void AiLuaRuntime::recordShadowAudit(const AIRequest &request,
                                     const QString &callbackName,
                                     const QString &skillName,
                                     const AIResult &officialResult,
                                     const AIResult &shadowResult)
{
    AiShadowAuditEntry entry;
    entry.decisionId = request.decisionId;
    entry.callbackName = callbackName;
    entry.skillName = auditString(skillName);
    entry.pattern = auditString(request.pattern);
    entry.officialResult = auditResult(officialResult);
    entry.shadowResult = auditResult(shadowResult);
    if (!officialResult.errorCode.isEmpty() || !shadowResult.errorCode.isEmpty()) {
        entry.comparison = AiShadowError;
        ++m_shadowAuditSummary.errors;
    } else if (!shadowResult.handled) {
        entry.comparison = AiShadowNotCovered;
        ++m_shadowAuditSummary.notCovered;
    } else if (sameAiResult(officialResult, shadowResult)) {
        entry.comparison = AiShadowMatch;
        ++m_shadowAuditSummary.matches;
    } else {
        entry.comparison = AiShadowMismatch;
        ++m_shadowAuditSummary.mismatches;
    }
    while (m_shadowAudits.size() >= m_shadowAuditLimit)
        m_shadowAudits.removeFirst();
    m_shadowAudits << entry;
    if (Config.value(QStringLiteral("AiShadowAuditLog"), false).toBool()) {
        qDebug().noquote() << "AiShadowAudit"
            << QStringLiteral("decision=%1 callback=%2 skill=%3 pattern=%4 comparison=%5 "
                              "official=%6/%7 shadow=%8/%9 totals=%10/%11/%12/%13")
                .arg(entry.decisionId).arg(callbackName, entry.skillName, entry.pattern)
                .arg(QString::fromLatin1(shadowComparisonName(entry.comparison)))
                .arg(int(officialResult.kind)).arg(officialResult.errorCode)
                .arg(int(shadowResult.kind)).arg(shadowResult.errorCode)
                .arg(m_shadowAuditSummary.notCovered).arg(m_shadowAuditSummary.matches)
                .arg(m_shadowAuditSummary.mismatches).arg(m_shadowAuditSummary.errors);
    }
}

AiLuaRuntime::ExecutionBinding::ExecutionBinding(AiLuaRuntime &runtime)
    : m_previous(currentAiRuntime)
{
    Q_ASSERT(!currentAiRuntime);
    currentAiRuntime = &runtime;
}

AiLuaRuntime::ExecutionBinding::~ExecutionBinding()
{
    currentAiRuntime = m_previous;
}

int AiLuaRuntime::luaRandom(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime)
        return luaL_error(state, "AI random used outside a decision");
    const int count = lua_gettop(state);
    if (count == 0) {
        const lua_Number value = lua_Number(runtime->m_rng.bounded(0x7fffffff))
            / lua_Number(0x7fffffff);
        lua_pushnumber(state, value);
        return 1;
    }
    const int lower = count == 1 ? 1 : int(luaL_checkinteger(state, 1));
    const int upper = int(luaL_checkinteger(state, count == 1 ? 1 : 2));
    if (lower > upper)
        return luaL_error(state, "invalid AI random interval");
    const qint64 span = qint64(upper) - qint64(lower) + 1;
    if (span <= 0 || span > 0x7fffffff)
        return luaL_error(state, "AI random interval is too large");
    lua_pushinteger(state, lower + runtime->m_rng.bounded(int(span)));
    return 1;
}

int AiLuaRuntime::luaRandomSeed(lua_State *state)
{
    Q_UNUSED(state);
    return 0;
}

int AiLuaRuntime::luaTraceback(lua_State *state)
{
    const char *message = lua_isnoneornil(state, 1) ? nullptr : lua_tostring(state, 1);
    const int level = int(luaL_optinteger(state, 2, 1));
    luaL_traceback(state, state, message, level);
    return 1;
}

int AiLuaRuntime::luaAiDataRead(lua_State *state)
{
    const QByteArray data = AiDataStore::read().toUtf8();
    if (data.isEmpty())
        lua_pushnil(state);
    else
        lua_pushlstring(state, data.constData(), data.size());
    return 1;
}

int AiLuaRuntime::luaAiDataWrite(lua_State *state)
{
    size_t size = 0;
    const char *data = luaL_checklstring(state, 1, &size);
    QString error;
    const bool written = AiDataStore::write(
        QString::fromUtf8(data, qsizetype(size)), &error);
    lua_pushboolean(state, written);
    if (written)
        return 1;
    lua_pushstring(state, error.toUtf8().constData());
    return 2;
}

void AiLuaRuntime::luaInstructionHook(lua_State *state, lua_Debug *debug)
{
    Q_UNUSED(debug);
    AiLuaRuntime *runtime = current();
    if (!runtime)
        return;
    runtime->m_instructionsRemaining -= 1000;
    if (runtime->m_instructionsRemaining <= 0) {
        runtime->m_instructionLimitExceeded = true;
        luaL_error(state, "AI instruction limit exceeded");
    }
}

AiLuaRuntime *AiLuaRuntime::fromUpvalue(lua_State *state)
{
    return static_cast<AiLuaRuntime *>(lua_touserdata(state, lua_upvalueindex(1)));
}

AiLuaRuntime *AiLuaRuntime::current()
{
    return currentAiRuntime;
}

bool AiLuaRuntime::installSandbox(QString *error)
{
    lua_State *state = m_lua.state();
    if (!state) {
        if (error)
            *error = QStringLiteral("AI Lua runtime is not bound");
        return false;
    }

    lua_getglobal(state, "math");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        if (error)
            *error = QStringLiteral("AI Lua math library is unavailable");
        return false;
    }
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaRandom, 1);
    lua_setfield(state, -2, "random");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaRandomSeed, 1);
    lua_setfield(state, -2, "randomseed");
    lua_pop(state, 1);

    lua_newtable(state);
    lua_pushcfunction(state, &AiLuaRuntime::luaTraceback);
    lua_setfield(state, -2, "traceback");
    lua_setglobal(state, "debug");

    lua_newtable(state);
    lua_pushcfunction(state, &AiLuaRuntime::luaAiDataRead);
    lua_setfield(state, -2, "read");
    lua_pushcfunction(state, &AiLuaRuntime::luaAiDataWrite);
    lua_setfield(state, -2, "write");
    lua_setglobal(state, "ai_data");

    const char *blockedGlobals[] = {
        "sgs", "io", "os", "package", "coroutine", "require", "dofile", "loadfile",
        "load", "loadstring", "collectgarbage", nullptr
    };
    for (const char **name = blockedGlobals; *name; ++name) {
        lua_pushnil(state);
        lua_setglobal(state, *name);
    }

    lua_newtable(state);
    if (!setMetaEnumFields(state, Player::staticMetaObject, "Phase", "Player_")
        || !setMetaEnumFields(state, Card::staticMetaObject, "Suit", "Card_")) {
        lua_pop(state, 1);
        if (error)
            *error = QStringLiteral("AI-safe meta enum is unavailable");
        return false;
    }
    lua_setglobal(state, "sgs");
    return true;
}

void AiLuaRuntime::loadConfiguredRoutes()
{
    const auto addRoutes = [this](const QString &key, AiRoute route) {
        foreach (const QString &entry, Config.value(key).toStringList()) {
            const QStringList parts = entry.split(QChar(':'));
            const QString callbackName = parts.value(0).trimmed();
            if (callbackName != QStringLiteral("activate")
                && callbackName != QStringLiteral("askForUseCard"))
                continue;
            m_routes.setCallbackRoute(callbackName, parts.value(1).trimmed(), route);
        }
    };
    addRoutes(QStringLiteral("AiLegacyDirectCallbacks"), AiRouteLegacyDirect);
    addRoutes(QStringLiteral("AiLegacyAdaptedCallbacks"), AiRouteLegacyAdapted);
    addRoutes(QStringLiteral("AiIsolatedCallbacks"), AiRouteIsolated);
    addRoutes(QStringLiteral("AiShadowCallbacks"), AiRouteShadow);
}

bool AiLuaRuntime::loadConfiguredScripts(QString *error)
{
    static const QRegularExpression fileNamePattern(
        QStringLiteral("^[A-Za-z0-9_-]+\\.lua$"));
    const QStringList defaultScripts({QStringLiteral("ask-for-use-card.lua"),
                                      QStringLiteral("standard-ai.lua")});
    foreach (const QString &configuredName, Config.value(
                 QStringLiteral("AiIsolatedScripts"), defaultScripts).toStringList()) {
        const QString fileName = configuredName.trimmed();
        if (!fileNamePattern.match(fileName).hasMatch()) {
            if (error)
                *error = QStringLiteral("Invalid isolated AI script name: %1").arg(fileName);
            return false;
        }
        if (!loadScriptWithBudget(QStringLiteral("lua/ai/isolated/%1").arg(fileName),
                                  m_initializationInstructionBudget, error))
            return false;
    }
    return true;
}

bool AiLuaRuntime::loadScriptWithBudget(const QString &path, qint64 instructionBudget,
                                        QString *error)
{
    lua_State *state = m_lua.state();
    if (!state) {
        if (error)
            *error = QStringLiteral("AI Lua runtime is not bound");
        return false;
    }
    const int stackBase = lua_gettop(state);
    if (luaL_loadfile(state, path.toUtf8().constData()) != 0) {
        if (error)
            *error = QString::fromUtf8(lua_tostring(state, -1));
        lua_settop(state, stackBase);
        return false;
    }
    ExecutionBinding executionBinding(*this);
    m_instructionsRemaining = instructionBudget;
    m_instructionLimitExceeded = false;
    lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
    LuaRuntime::LuaInvocationScope invocation(m_lua);
    const int status = lua_pcall(state, 0, LUA_MULTRET, 0);
    lua_sethook(state, nullptr, 0, 0);
    if (status == 0) {
        lua_settop(state, stackBase);
        return true;
    }
    if (error) {
        *error = m_instructionLimitExceeded
            ? QStringLiteral("AI initialization instruction limit exceeded in %1").arg(path)
            : QString::fromUtf8(lua_tostring(state, -1));
    }
    lua_settop(state, stackBase);
    return false;
}

void AiLuaRuntime::pushRequest(lua_State *state, const AIRequest &request) const
{
    lua_createtable(state, 0, 10);
    lua_pushstring(state, request.kind == AIRequest::Activate ? "activate" : "use_card");
    lua_setfield(state, -2, "kind");
    lua_pushstring(state, request.getDecisionId().toUtf8().constData());
    lua_setfield(state, -2, "decision_id");
    lua_pushstring(state, request.getStateRevision().toUtf8().constData());
    lua_setfield(state, -2, "state_revision");
    lua_pushstring(state, request.viewerObjectName.toUtf8().constData());
    lua_setfield(state, -2, "viewer");
    lua_pushinteger(state, request.reason);
    lua_setfield(state, -2, "reason");
    lua_pushstring(state, request.pattern.toUtf8().constData());
    lua_setfield(state, -2, "pattern");
    lua_pushstring(state, request.prompt.toUtf8().constData());
    lua_setfield(state, -2, "prompt");
    lua_pushinteger(state, request.handlingMethod);
    lua_setfield(state, -2, "handling_method");
    pushAIWorldView(state, request.worldView);
    lua_setfield(state, -2, "world_view");
    if (request.hasSkillActionContext) {
        lua_createtable(state, 0, 8);
        lua_pushstring(state, request.getActivationOwner().toUtf8().constData());
        lua_setfield(state, -2, "activation_owner");
        lua_pushstring(state, request.getActivationSkillName().toUtf8().constData());
        lua_setfield(state, -2, "activation_skill");
        lua_pushinteger(state, request.getActivationInstanceId());
        lua_setfield(state, -2, "activation_instance");
        lua_pushstring(state, request.getSourceOwner().toUtf8().constData());
        lua_setfield(state, -2, "source_owner");
        lua_pushstring(state, request.getSourceSkillName().toUtf8().constData());
        lua_setfield(state, -2, "source_skill");
        lua_pushinteger(state, request.getSourceInstanceID());
        lua_setfield(state, -2, "source_instance");
        lua_pushboolean(state, request.isActivationQuotaAvailable());
        lua_setfield(state, -2, "activation_quota_available");
        lua_pushboolean(state, request.isSourceQuotaAvailable());
        lua_setfield(state, -2, "source_quota_available");
        lua_setfield(state, -2, "skill_action");
    }
}

bool AiLuaRuntime::parseResult(lua_State *state, AIResult &result) const
{
    if (!lua_istable(state, -1))
        return false;
    lua_getfield(state, -1, "kind");
    if (lua_type(state, -1) != LUA_TSTRING) {
        lua_pop(state, 1);
        return false;
    }
    QString kind;
    if (!readBoundedString(state, -1, kind)) {
        lua_pop(state, 1);
        return false;
    }
    lua_pop(state, 1);
    result.handled = true;
    if (kind == QStringLiteral("pass")) {
        result.kind = AIResult::Pass;
        return true;
    }
    if (kind != QStringLiteral("use_card"))
        return false;
    result.kind = AIResult::UseCard;

    lua_getfield(state, -1, "card");
    if (!lua_isnil(state, -1)) {
        if (lua_type(state, -1) != LUA_TSTRING) {
            lua_pop(state, 1);
            return false;
        }
        if (!readBoundedString(state, -1, result.action.legacyCardString)) {
            lua_pop(state, 1);
            return false;
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, -1, "cards");
    if (!lua_isnil(state, -1)) {
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            return false;
        }
        const size_t count = lua_rawlen(state, -1);
        if (count > AiMaxSelectedCards) {
            lua_pop(state, 1);
            return false;
        }
        for (size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, index);
            if (lua_type(state, -1) != LUA_TNUMBER) {
                lua_pop(state, 2);
                return false;
            }
            const lua_Number number = lua_tonumber(state, -1);
            int cardId = 0;
            const bool validCardId = aiResultInteger(number, cardId);
            lua_pop(state, 1);
            if (!validCardId || result.action.selectedCardIds.contains(cardId)) {
                lua_pop(state, 1);
                return false;
            }
            result.action.selectedCardIds << cardId;
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, -1, "targets");
    if (!lua_isnil(state, -1)) {
        if (!lua_istable(state, -1)) {
            lua_pop(state, 1);
            return false;
        }
        const size_t count = lua_rawlen(state, -1);
        if (count > AiMaxSelectedTargets) {
            lua_pop(state, 1);
            return false;
        }
        for (size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, index);
            if (lua_type(state, -1) != LUA_TSTRING) {
                lua_pop(state, 2);
                return false;
            }
            QString targetName;
            if (!readBoundedString(state, -1, targetName)) {
                lua_pop(state, 2);
                return false;
            }
            result.action.selectedTargetNames << targetName;
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);

    lua_getfield(state, -1, "user_string");
    if (!lua_isnil(state, -1)) {
        if (lua_type(state, -1) != LUA_TSTRING) {
            lua_pop(state, 1);
            return false;
        }
        if (!readBoundedString(state, -1, result.action.userString)) {
            lua_pop(state, 1);
            return false;
        }
    }
    lua_pop(state, 1);
    return true;
}
