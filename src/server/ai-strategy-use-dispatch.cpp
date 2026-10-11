#include "ai-strategy-use-dispatch.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "lua.hpp"
#include <QScopeGuard>
#include <limits>

namespace {
bool rawField(lua_State *state, int table, const QByteArray &key)
{
    table = lua_absindex(state, table);
    if (!lua_istable(state, table)) { lua_pushnil(state); return false; }
    lua_pushlstring(state, key.constData(), size_t(key.size())); lua_rawget(state, table);
    if (!lua_isnil(state, -1)) return true;
    if (!lua_getmetatable(state, table)) return true;
    lua_pushliteral(state, "__index"); lua_rawget(state, -2);
    const bool known = lua_isnil(state, -1); lua_pop(state, 2);
    return known;
}
QJsonObject origin(lua_State *state, int function)
{
    if (!lua_isfunction(state, function)) return {};
    lua_pushvalue(state, function); lua_Debug info{};
    if (!lua_getinfo(state, ">S", &info)) return {};
    return {{"source", QString::fromUtf8(info.source ? info.source : "")}, {"line", info.linedefined}};
}
bool disclosedId(const AIRequest &request, int id)
{
    const auto cardsHave = [id](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId == id) return true;
        return false;
    };
    const auto playerHas = [&](const AIPlayerView &player) {
        if (cardsHave(player.knownCards) || cardsHave(player.equips) || cardsHave(player.judgingArea)) return true;
        for (const auto &pile : player.piles) if (cardsHave(pile.cards)) return true;
        return false;
    };
    if (cardsHave(request.worldView.handCards) || cardsHave(request.worldView.discardPile)
        || cardsHave(request.choiceOptions.cards) || playerHas(request.worldView.self)) return true;
    for (const auto &player : request.worldView.players) if (playerHas(player)) return true;
    return false;
}
QJsonObject useRow(lua_State *state, int registry, bool registryKnown, const QString &key, const AIRequest &request, bool originalEntry)
{
    const int top = lua_gettop(state);
    auto restore = qScopeGuard([&] { lua_settop(state, top); });
    QJsonObject row{{"key", key}, {"known", false}};
    if (!rawField(state, registry, key.toUtf8()) || !registryKnown) return row;
    int function = lua_absindex(state, -1);
    row.insert("known", true);
    row.insert("present", !lua_isnil(state, function));
    if (lua_isnil(state, function)) return row;
    row.insert("truthy", bool(lua_toboolean(state, function)));
    row.insert("value_type", QString::fromLatin1(lua_typename(state, lua_type(state, function))));
    if (!lua_isfunction(state, function)) { row.insert("kind", "nonfunction"); return row; }
    auto info = origin(state, function);
    if (info.value("source") == "@lua/ai/original-hegemony/heg-loader-ai.lua"
        && info.value("line") == QJsonValue(92)) {
        // One audited loader wrapper captures only its provider function. Its
        // dynamic normalizer must still be the exact fixed body. No callback,
        // arbitrary upvalue, __index, userdata or card face is evaluated here.
        const int before = lua_gettop(state);
        lua_pushglobaltable(state);
        bool normalizerKnown = rawField(state, -1, "sgs");
        normalizerKnown = rawField(state, -1, "originalHegemonyCardWire") && normalizerKnown;
        const auto wireOrigin = normalizerKnown ? origin(state, -1) : QJsonObject();
        lua_settop(state, before);
        if (!originalEntry || wireOrigin.value("source") != "@lua/ai/original-hegemony/heg-common-ai.lua"
            || wireOrigin.value("line") != QJsonValue(32)) {
            row.insert("known", false); row.insert("reason", "Original use-card wire normalizer is not the audited provider");
            return row;
        }
        row.insert("wrapper_source", info.value("source"));
        row.insert("wrapper_line", info.value("line"));
        bool found = false;
        for (int i = 1; const char *name = lua_getupvalue(state, function, i); ++i) {
            if (QByteArray(name) == "original" && lua_isfunction(state, -1)) {
                function = lua_absindex(state, -1); info = origin(state, function);
                found = true; break;
            }
            lua_pop(state, 1);
        }
        if (!found) {
            row.insert("known", false); row.insert("reason", "Original use-card wrapper provider is unavailable");
            return row;
        }
        row.insert("original_wire_wrapper", true);
    }
    row.insert("source", info.value("source")); row.insert("line", info.value("line"));
    row.insert("kind", "callback");
    if (info.value("source") != "@lua/ai/smart-ai.lua") return row;
    const int line = info.value("line").toInt();
    const char *wanted = line == 3896 || line == 3922 ? "cn" : line == 3958 ? "id" : nullptr;
    if (!wanted) return row;
    // Read only the one audited scalar closure capture. Never execute a hook,
    // index a userdata, or expose arbitrary upvalues.
    bool found = false;
    for (int i = 1; const char *name = lua_getupvalue(state, function, i); ++i) {
        if (QByteArray(name) == wanted) {
            if (*wanted == 'c' && lua_type(state, -1) == LUA_TSTRING) {
                const QString className = QString::fromUtf8(lua_tostring(state, -1));
                if (!className.isEmpty()) { row.insert("kind", "class"); row.insert("class_name", className); found = true; }
            } else if (*wanted == 'i' && lua_isinteger(state, -1)) {
                const lua_Integer value = lua_tointeger(state, -1);
                if (value >= 0 && value <= std::numeric_limits<int>::max() && disclosedId(request, int(value))) {
                    row.insert("kind", "physical"); row.insert("card_id", int(value)); found = true;
                }
            }
        }
        lua_pop(state, 1);
        if (found) break;
    }
    if (!found) { row.insert("known", false); row.insert("reason", "generated handler scalar or disclosed card is unavailable"); }
    return row;
}
}

void projectAIUseCardDispatch(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (request.kind != AIRequest::UseCard || !viewer || viewer->getRoom() != &room
        || viewer->objectName() != request.viewerObjectName) return;
    QJsonObject result{{"known", false}, {"request_present", request.hasSkillActionContext}};
    if (request.hasSkillActionContext) result.insert("request_valid", AiLegacyRequestView(request, viewer).isValid());
    LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
    lua_State *state = room.roomRuntime()->lua().state();
    if (state) {
        const int top = lua_gettop(state); auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state);
        const int globals = lua_absindex(state, -1);
        bool known = rawField(state, globals, "SmartAI");
        // middleclass explicitly stores methods in this raw dictionary.
        known = rawField(state, -1, "__instanceDict") && known;
        known = rawField(state, -1, "askForUseCard") && known;
        const auto entry = known ? origin(state, -1) : QJsonObject();
        result.insert("entry_source", entry.value("source")); result.insert("entry_line", entry.value("line"));
        QString variant;
        if (entry.value("source") == "@lua/ai/smart-ai.lua" && entry.value("line") == 3978) variant = "ordinary";
        else if (entry.value("source") == "@lua/ai/original-hegemony/heg-common-ai.lua" && entry.value("line") == 2032) variant = "original";
        if (!variant.isEmpty()) { result.insert("entry_variant", variant); result.insert("known", true); }
        lua_settop(state, globals);
        known = rawField(state, globals, "sgs");
        known = rawField(state, -1, "ai_skill_use") && known;
        const int registry = lua_absindex(state, -1);
        result.insert("raw", useRow(state, registry, known, request.pattern, request, variant == "original"));
        result.insert("prompt", useRow(state, registry, known, request.prompt.section(':', 0, 0), request, variant == "original"));
    }
    request.choiceOptions.context.insert("use_card_dispatch", result);
    request.worldView.decisionContext.insert("use_card_dispatch", result);
}

// Aocai's legacy invoke asks askForCard(pattern, prompt, 1), where 1 is
// data and the method is nil. Capture only its actual prompt-hook presence;
// this does not mint a nested response request or execute that callback.
void projectAIAocaiInvokeDispatch(AIRequest &request, Room &room, const QStringList &asked)
{
    if (request.kind != AIRequest::SkillInvoke || request.choiceOptions.reason != "aocai") return;
    QJsonObject result{{"known", true}, {"asked", QJsonArray::fromStringList(asked)}};
    QJsonObject callback{{"known", false}};
    if (asked.size() >= 2) {
        const QString key = asked.at(1).section(':', 0, 0);
        callback.insert("key", key);
        LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
        lua_State *state = room.roomRuntime()->lua().state();
        if (state) {
            const int top = lua_gettop(state); auto restore = qScopeGuard([&] { lua_settop(state, top); });
            lua_pushglobaltable(state);
            bool known = rawField(state, -1, "sgs");
            known = rawField(state, -1, "ai_skill_cardask") && known;
            known = rawField(state, -1, key.toUtf8()) && known;
            callback.insert("known", known);
            if (known) {
                callback.insert("present", lua_isfunction(state, -1));
                callback.insert("value_type", QString::fromLatin1(lua_typename(state, lua_type(state, -1))));
                const auto info = origin(state, -1);
                callback.insert("source", info.value("source")); callback.insert("line", info.value("line"));
            }
        }
    }
    result.insert("cardask", callback);
    request.choiceOptions.context.insert("aocai_invoke", result);
}

// Yitian's actual CardAsked response trigger (yitian.cpp735-766), not an
// activation or an Aocai scope. This is readonly callback metadata only.
void projectAILianliSlashInvokeDispatch(AIRequest &request, Room &room, const QStringList &asked)
{
    if (request.kind != AIRequest::SkillInvoke || request.choiceOptions.reason != "lianli-slash"
        || asked.size() < 3 || asked.first() != "slash" || asked.last() != "response"
        || asked.at(1).contains("lianli-slash")) return;
    QJsonObject result{{"known", true}, {"asked", QJsonArray::fromStringList(asked)}};
    const QString key = asked.at(1).section(':', 0, 0);
    QJsonObject callback{{"known", false}, {"key", key}};
    LuaRuntime::Binding binding(room.roomRuntime()->lua(), false);
    lua_State *state = room.roomRuntime()->lua().state();
    if (state) {
        const int top = lua_gettop(state); auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state);
        bool known = rawField(state, -1, "sgs");
        known = rawField(state, -1, "ai_skill_cardask") && known;
        known = rawField(state, -1, key.toUtf8()) && known;
        callback.insert("known", known);
        if (known) {
            callback.insert("present", lua_isfunction(state, -1));
            callback.insert("value_type", QString::fromLatin1(lua_typename(state, lua_type(state, -1))));
            const auto info = origin(state, -1);
            callback.insert("source", info.value("source")); callback.insert("line", info.value("line"));
        }
    }
    result.insert("cardask", callback);
    request.choiceOptions.context.insert("lianli_slash_invoke", result);
}
