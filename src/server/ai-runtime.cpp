#include "ai-strategy-round56-connection-cache.h"
#include "ai-strategy-round56-mode-evaluation.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round53-chuli.h"
#include "ai-strategy-round41-response.h"
#include "ai-strategy-round46-inovation.h"
#include "ai-strategy-round50-effects.h"
#include "ai-strategy-round53-collateral.h"
#include "ai-strategy-round29-original.h"
#include "ai-strategy-round29-planning.h"
#include "ai-strategy-round29-choice.h"
#include "ai-strategy-round40-choice.h"
#include "ai-strategy-round30-choice.h"
#include "ai-strategy-round30-planning.h"
#include "ai-strategy-round31-planning.h"
#include "ai-strategy-round26-response.h"
#include "ai-strategy-round26-helpers.h"
#include "ai-strategy-round28-choice.h"
#include "ai-strategy-round24-response.h"
#include "ai-runtime.h"
#include "ai-strategy-round46-planning-slots.h"
#include "ai-strategy-round45-poxi-slot.h"
#include "ai-strategy-round50-slots.h"
#include "ai-strategy-round53-targets.h"
#include "ai-strategy-round53-peiqi.h"
#include "ai-strategy-round53-daoji.h"
#include "ai-strategy-round51-manjuan.h"
#include "ai-strategy-round53-manjuan.h"
#include "ai-strategy-choice-continuity.h"
#include "ai-strategy-round53-registry.h"
#include "ai-strategy-round24-choice.h"
#include "ai-strategy-round25-planning.h"
#include "ai-strategy-round24-clock.h"
#include "ai-strategy-round22-olclan-plan.h"
#include "ai-strategy-round26-planning.h"
#include "ai-strategy-round44-fc-frame.h"
#include "ai-strategy-round45-context.h"
#include "ai-strategy-round27-original.h"
#include "ai-strategy-range-query.h"
#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-round44-routes.h"
#include "ai-strategy-round25-distance.h"
#include "ai-strategy-readonly-card-store.h"
#include "serverplayer.h"

#include "ai-data-store.h"
#include "ai-strategy-history-context.h"
#include "ai-strategy-nested-cardask.h"
#include "ai-strategy-yczh2016.h"
#include "ai-strategy-aliases.h"
#include "roomthread.h"
#include "engine.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "lua-wrapper.h"
#include "room.h"
#include "room-runtime.h"
#include "ai-strategy-round35-choice.h"
#include "ai-strategy-round43-common.h"
#include "ai-strategy-round44-imagine.h"
#include "ai-strategy-round45-inovation-providers.h"
#include "ai-strategy-round45-inovation-state.h"
#include "ai-strategy-round45-goutong.h"
#include "ai-strategy-round36-planning.h"
#include "ai-strategy-round38-manbing.h"
#include "ai-strategy-round37-response-init.h"
#include "ai-strategy-round37-planning.h"
#include "ai-strategy-round36-choice.h"
#include "ai-strategy-round37-choice.h"
#include "ai-strategy-round36-response-wire.h"
#include "settings.h"
#include "skill.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QSet>
#include <QScopeGuard>

#include <cmath>
#include <algorithm>
#include <limits>

namespace {

thread_local AiLuaRuntime *currentAiRuntime = nullptr;

const size_t AiSoftMemoryLimit = 64u * 1024u * 1024u;
const size_t AiHardMemoryLimit = 128u * 1024u * 1024u;
const size_t AiMaxResultStringBytes = 64u * 1024u;
const size_t AiMaxSelectedCards = 2048u;
const size_t AiMaxSelectedTargets = 64u;
const int AiMaxIntentionDeltas = 64;

struct AiIntentionDelta {
    QString from;
    QString to;
    lua_Number level;
    bool deferRefresh = false;
    bool deferRefreshPresent = false;
};

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

bool readBoundedString(lua_State *state, int index, QString &value)
{
    size_t size = 0;
    const char *data = lua_tolstring(state, index, &size);
    if (!data || size > AiMaxResultStringBytes)
        return false;
    value = QString::fromUtf8(data, qsizetype(size));
    return true;
}

bool isPlainTable(lua_State *state, int index)
{
    if (!lua_istable(state, index))
        return false;
    if (!lua_getmetatable(state, index))
        return true;
    lua_pop(state, 1);
    return false;
}

int luaFacadeIndex(lua_State *state)
{
    // Exact facade_index lookup over caller-owned Lua tables. This performs
    // no native object lookup and does not cache mutable class/instance data.
    luaL_checktype(state, 1, LUA_TTABLE);
    luaL_checktype(state, 2, LUA_TTABLE);
    lua_settop(state, 3);
    lua_pushvalue(state, 3);
    lua_rawget(state, 1);
    if (!lua_isnil(state, -1)) return 1;
    lua_pop(state, 1);
    lua_pushliteral(state, "_scalar_methods");
    lua_rawget(state, 2);
    if (lua_istable(state, -1)) {
        lua_pushvalue(state, 3);
        lua_gettable(state, -2); // preserve the scalar table's normal __index
        return 1;
    }
    lua_pushnil(state);
    return 1;
}

// Pure bounded bookkeeping, not a policy query. Return the first matching
// existing estimate row; Lua retains all append/count/capacity mutations.
// -1 delegates representations outside this fast path to the original Lua
// scan, including metatables and non-scalar context values. No cached index
// means in-place edits, duplicate rows and branch rollback remain observable.
int luaFindKnowledgeEstimate(lua_State *state)
{
    const int top = lua_gettop(state);
    const auto result = [&](int index) {
        lua_settop(state, top);
        lua_pushinteger(state, index);
        return 1;
    };
    if (!isPlainTable(state, 1) || !isPlainTable(state, 2)) return result(-1);
    const auto scalarRow = [&](int index) {
        index = lua_absindex(state, index);
        if (!isPlainTable(state, index)) return false;
        int count = 0;
        lua_pushnil(state);
        while (lua_next(state, index)) {
            const int valueType = lua_type(state, -1);
            if (++count > 64 || lua_type(state, -2) != LUA_TSTRING
                || (valueType != LUA_TSTRING && valueType != LUA_TNUMBER
                    && valueType != LUA_TBOOLEAN)) {
                lua_pop(state, 2);
                return false;
            }
            lua_pop(state, 1);
        }
        return true;
    };
    if (!scalarRow(2)) return result(-1);
    for (int index = 1; index <= 256; ++index) {
        lua_rawgeti(state, 1, index);
        if (lua_isnil(state, -1)) return result(0); // exactly ipairs' first hole
        const int previous = lua_absindex(state, -1);
        if (!scalarRow(previous)) return result(-1);
        bool same = true;
        lua_pushnil(state);
        while (lua_next(state, 2)) {
            lua_pushvalue(state, -2);
            lua_rawget(state, previous);
            const bool equal = lua_rawequal(state, -1, -2);
            lua_pop(state, 2);
            if (!equal) { lua_pop(state, 1); same = false; break; }
        }
        if (same) {
            lua_pushnil(state);
            while (lua_next(state, previous)) {
                size_t length = 0;
                const char *key = lua_tolstring(state, -2, &length);
                const bool countKey = length == 5 && std::equal(key, key + 5, "count");
                if (!countKey) {
                    lua_pushvalue(state, -2);
                    lua_rawget(state, 2);
                    const bool equal = lua_rawequal(state, -1, -2);
                    lua_pop(state, 1);
                    if (!equal) { lua_pop(state, 2); same = false; break; }
                }
                lua_pop(state, 1);
            }
        }
        if (same) return result(index);
        lua_pop(state, 1);
    }
    lua_rawgeti(state, 1, 257);
    return result(lua_isnil(state, -1) ? 0 : -1);
}

// Bounded pure-value bridge: no metatables, userdata, cycles or mixed tables.
bool readAIJson(lua_State *state, int index, QJsonValue &out, int &budget, int depth = 0,
                bool emptyArray = false)
{
    if (--budget < 0 || depth > 12) return false;
    index = lua_absindex(state, index);
    switch (lua_type(state, index)) {
    case LUA_TNIL: out = QJsonValue(); return true;
    case LUA_TBOOLEAN: out = bool(lua_toboolean(state, index)); return true;
    case LUA_TNUMBER: {
        double value = lua_tonumber(state, index);
        if (!std::isfinite(value)) return false;
        out = value; return true;
    }
    case LUA_TSTRING: {
        QString value; if (!readBoundedString(state, index, value)) return false;
        out = value; return true;
    }
    case LUA_TTABLE: break;
    default: return false;
    }
    if (!isPlainTable(state, index)) return false;
    QJsonObject object; QMap<int, QJsonValue> array;
    lua_pushnil(state);
    while (lua_next(state, index)) {
        QJsonValue child;
        const QString childKey = lua_type(state, -2) == LUA_TSTRING ? QString::fromUtf8(lua_tostring(state, -2)) : QString();
        if (!readAIJson(state, -1, child, budget, depth + 1, childKey == "subcards" || childKey == "targets")) { lua_pop(state, 2); return false; }
        if (lua_type(state, -2) == LUA_TSTRING) {
            QString key; if (!readBoundedString(state, -2, key)) { lua_pop(state, 2); return false; }
            object.insert(key, child);
        } else if (lua_type(state, -2) == LUA_TNUMBER) {
            int key;
            if (!aiResultInteger(lua_tonumber(state, -2), key) || key < 1 || key > 4096) {
                lua_pop(state, 2); return false;
            }
            array.insert(key, child);
        } else { lua_pop(state, 2); return false; }
        lua_pop(state, 1);
    }
    if (!object.isEmpty() && !array.isEmpty()) return false;
    if (!array.isEmpty() || (object.isEmpty() && emptyArray)) {
        QJsonArray result;
        for (auto it = array.cbegin(); it != array.cend(); ++it) {
            if (it.key() != result.size() + 1) return false;
            result.append(it.value());
        }
        out = result;
    } else out = object;
    return true;
}

bool readAIArray(lua_State *state, int index, QJsonArray &out, int maximum)
{
    if (lua_isnil(state, index)) { out = {}; return true; }
    int budget = 8192; QJsonValue value;
    if (!readAIJson(state, index, value, budget, 0, true) || !value.isArray()
        || value.toArray().size() > maximum) return false;
    out = value.toArray(); return true;
}

bool validSpeechSource(const QString &key)
{
    // Fixed-source common speech catalog; package additions need an explicit
    // presentation source declaration rather than an arbitrary prefix.
    static const QSet<QString> allowed{
        "yuri:arkora",
        "tenyear_hc:cixiao",
        "speak:amazing_grace",
        "speak:amazing_grace_female",
        "speak:become_lord",
        "speak:bianshi",
        "speak:bianshi_female",
        "speak:blade",
        "speak:collateral",
        "speak:collateralNoslash",
        "speak:collateralNoslash_female",
        "speak:collateral_female",
        "speak:continuous_slash",
        "speak:continuous_slash_female",
        "speak:discard_many",
        "speak:dismantlement",
        "speak:dismantlement_female",
        "speak:dongzhuxianji",
        "speak:duel",
        "speak:duel_female",
        "speak:eight_diagram",
        "speak:eight_diagramIsGood",
        "speak:equip_removed",
        "speak:equip_removed_female",
        "speak:ex_nihilo",
        "speak:full_hand",
        "speak:game_start",
        "speak:god_equip",
        "speak:guanxing",
        "speak:indulgence",
        "speak:indulgenceIsBad",
        "speak:indulgenceIsBad_female",
        "speak:indulgenceIsGood",
        "speak:indulgence_female",
        "speak:jijiang",
        "speak:jijiang_female",
        "speak:judgeIsGood",
        "speak:kurou",
        "speak:kylin_bow",
        "speak:lightningIsGood",
        "speak:lord_in_danger",
        "speak:low_hp_kill",
        "speak:low_hp_kill_female",
        "speak:luoyi",
        "speak:multi_jink",
        "speak:noJink",
        "speak:noJink_female",
        "speak:no_peach",
        "speak:no_peach_female",
        "speak:null_card",
        "speak:quhu",
        "speak:snatch",
        "speak:snatch_female",
        "speak:spear",
        "speak:supply_shortage",
        "speak:supply_shortageIsBad",
        "speak:supply_shortageIsGood",
        "speak:turn_end",
        "speak:turn_end_female",
        "speak:turn_start",
        "speak:turn_start_female",
        "speak:yiji",
        "speak:zhujinqiyuan",
        "speak:zhujinqiyuan_female",
        "chat:Death:stupid_lord",
        "chat:Dying:fuck_renegade",
        "chat:DamageForseen:kylin_bow",
        "chat:CardFinished:analeptic",
        "chat:PreCardUsed:CardUse",
        "chat:EventPhaseStart:luanwu",
        "chat:ChoiceMade:state",
        "chat:DrawNCards:screen",
        "chat:StartJudge:isBad",
        "chat:FinishJudge:delayed_trick",
        "chat:TargetSpecified:continuous_slash",
        "chat:CardOffset:multi_jink",
        "chat:Death:low_hp_kill",
        "chat:GameStart:greeting",
        "chat:EventPhaseStart:turn_end",
        "chat:EventPhaseStart:turn_start",
        "chat:GameStart:lord_identity",
        "chat:EventPhaseStart:full_hand",
        "chat:CardsMoveOneTime:god_equip",
        "chat:Damaged:lord_in_danger_call",
        "chat:CardsMoveOneTime:equip_removed",
        "chat:EventPhaseStart:discard_many",
        "personality:skill_invoke",
        "personality:single_peach",
        "single_peach:self_refusal",
        "single_peach:humanized_wait",
        "cardask:slash-jink",
        "mode:updateIntention",
    };
    return allowed.contains(key);
}

void publishPresentation(Room *room, const QJsonArray &effects)
{
    if (!room) return;
    for (const auto &value : effects) {
        const auto effect = value.toObject();
        if (effect.value("kind") == "speech")
            room->publishAIStrategySpeech(room->findPlayerByObjectName(effect.value("player").toString(), true),
                effect.value("text").toString());
        else if (effect.value("kind") == "delay" && room->getThread())
            room->getThread()->delay(effect.value("milliseconds").toInt());
    }
}

bool validateStrategyEffects(const QJsonArray &effects, const AIWorldView &world, int deltaCount)
{
    QSet<QString> players{world.self.objectName};
    for (const auto &player : world.players) players.insert(player.objectName);
    players.remove(QString());
    int previous = 0;
    QSet<QString> dummyNoJinkIdentities;
    for (const auto &value : effects) {
        if (!value.isObject()) return false;
        const QJsonObject row = value.toObject();
        const QString kind = row.value("kind").toString();
        int after = -1;
        if (!row.value("after_delta").isDouble()
            || !aiResultInteger(row.value("after_delta").toDouble(), after)
            || after < previous || after > deltaCount) return false;
        previous = after;
        QSet<QString> allowed{"kind", "after_delta"};
        if (kind == "srkill_choice_flag") {
            allowed.unite({"source_key", "player", "name", "value"});
            const auto witness=world.decisionContext.value("round26_choice").toObject().value("flag_witness").toObject();
            if (after != 0 || !row.value("value").isBool() || witness.isEmpty()) return false;
            for (const QString &key : {QStringLiteral("source_key"),QStringLiteral("player"),QStringLiteral("name"),QStringLiteral("value")})
                if (!witness.contains(key) || row.value(key) != witness.value(key)) return false;
        } else if (kind == "speech" || kind == "delay") {
            allowed.insert("source_key");
            if (!validSpeechSource(row.value("source_key").toString())) return false;
            if (row.value("source_key") == "yuri:arkora" && (kind != "delay"
                || !world.strategyFacts.value("yuri_arkora_delay").toBool())) return false;
            if (row.value("source_key") == "tenyear_hc:cixiao"
                && (kind != "speech" || !world.strategyFacts.value("round25_cixiao_speech").toBool()
                    || row.value("player") != world.self.objectName
                    || row.value("text") != QStringLiteral("伤害不高，但侮辱性极强"))) return false;
            if (kind == "speech") {
                allowed.unite({"player", "text"});
                const QString speaker = row.value("player").toString();
                if (!players.contains(speaker) || !row.value("text").isString()
                    || row.value("text").toString().toUtf8().size() > 4096) return false;
                if (speaker != world.self.objectName) {
                    bool robot = false;
                    for (const auto &player : world.players)
                        if (player.objectName == speaker) robot = player.playerState == "robot";
                    if (!robot) return false;
                }
            } else {
                allowed.insert("milliseconds");
                int delay = -1;
                if (!row.value("milliseconds").isDouble()
                    || !aiResultInteger(row.value("milliseconds").toDouble(), delay) || delay < 0 || delay > 60000) return false;
            }
        } else if (kind == "speech_counter") {
            allowed.unite({"player", "name", "value", "source_key"});
            const QString name = row.value("name").toString();
            const QString key = row.value("source_key").toString();
            const bool jink = name == "jink_count-Clear" && key == "chat:CardOffset:multi_jink";
            bool slash = false;
            for (const QString &from : players)
                if (name == "continuous_slash_count" + from + "-Clear"
                    && key == "chat:TargetSpecified:continuous_slash") slash = true;
            int count = -1;
            if (!players.contains(row.value("player").toString()) || (!jink && !slash)
                || !row.value("value").isDouble() || !aiResultInteger(row.value("value").toDouble(), count)
                || count < 0 || count > 1000000) return false;
        } else if (kind == "phase_clear") {
            allowed.insert("player");
            if (row.value("player") != world.self.objectName) return false;
        } else if (kind == "ai_data") {
            allowed.unite({"revision", "data"});
            if (!row.value("revision").isString() || !row.value("data").isObject()
                || !world.strategyFacts.value("ai_data_persistent").toBool()
                || row.value("revision") != world.strategyFacts.value("ai_data_revision")) return false;
        } else if (kind == "personality_target") {
            allowed.insert("player");
            if (row.value("player") != world.self.objectName) return false;
        } else if (kind == "ai_player_hint") {
            allowed.unite({"source", "player", "name"});
            static const QSet<QString> names{"jiaozhaoAI", "jiyuAI", "kuangbiAI"};
            if (row.value("source") != world.self.objectName
                || !players.contains(row.value("player").toString())
                || !names.contains(row.value("name").toString())) return false;
        } else if (kind == "cardask_no_jink") {
            allowed.unite({"player", "amount"});
            if (row.value("player") != world.self.objectName || row.value("amount").toInt() != 1) return false;
            if (row.contains("source_key")) {
                allowed.unite({"source_key", "scope", "ordinal"});
                const auto witness = world.strategyFacts.value("aocai_nojink_scope");
                int ordinal = 0;
                if (row.value("source_key") != "aocai:slash-jink" || !witness.isString()
                    || witness.toString().isEmpty() || row.value("scope") != witness
                    || !row.value("ordinal").isDouble()
                    || !aiResultInteger(row.value("ordinal").toDouble(), ordinal)
                    || ordinal < 1 || ordinal > 64) return false;
                const QString identity = witness.toString() + ':' + QString::number(ordinal);
                if (dummyNoJinkIdentities.contains(identity)) return false;
                dummyNoJinkIdentities.insert(identity);
            } else if (row.contains("card_token")) {
                allowed.insert("card_token");
                const auto witness = world.strategyFacts.value("response_effect_nojink_token");
                const auto card = world.strategyFacts.value("response_effect_card").toObject();
                const QString token = row.value("card_token").toString();
                if (!witness.isString() || row.value("card_token") != witness
                    || !card.value("no_cost_virtual").toBool() || card.value("legacy_identity_token") != witness
                    || token.size() > 128
                    || !QRegularExpression("^native:[0-9a-f-]{36}:[0-9]+$").match(token).hasMatch()) return false;
            } else {
                allowed.insert("card_id");
                int id = 0;
                if (!row.value("card_id").isDouble() || !aiResultInteger(row.value("card_id").toDouble(), id)) return false;
                const auto card = world.strategyFacts.value("response_effect_card").toObject();
                if (id < 0 || card.value("no_cost_virtual").toBool()
                    || !card.value("effective_id").isDouble() || card.value("effective_id").toInt() != id) return false;
            }
        } else if (kind == "role_refresh") {
            // Explicit source updateIntentions refresh boundary; no player fields.
            allowed.unite({"adjust", "force", "refresh"});
            if ((row.contains("adjust") && !row.value("adjust").isBool())
                || (row.contains("force") && !row.value("force").isBool())
                || (row.contains("refresh") && !row.value("refresh").isBool())
                || (row.value("adjust").toBool()
                    && (row.value("force").toBool() || row.value("refresh").toBool()))) return false;
        } else if (kind == "dongmanbao_dapo_role") {
            allowed.unite({"player", "source_player", "role"});
            static const QSet<QString> roles{"lord","loyalist","rebel","renegade"};
            const auto witness=world.strategyFacts.value("dongmanbao_dapo_role_witness").toObject();
            bool found=false;
            for (const auto &value:witness.value("sources").toArray()) {
                const auto proof=value.toObject();
                if (proof.value("player")==row.value("source_player") && proof.value("role_known")==QJsonValue(true)
                    && proof.value("role")==row.value("role")) {found=true;break;}
            }
            if (!found || witness.value("target_role_known")!=QJsonValue(true)
                || witness.value("player")!=row.value("player")
                || !players.contains(row.value("player").toString())
                || !players.contains(row.value("source_player").toString())
                || !roles.contains(row.value("role").toString())) return false;
        } else if (kind == "azirshenji_role") {
            allowed.unite({"player", "role"});
            static const QSet<QString> roles{"lord","loyalist","rebel","renegade"};
            const auto witness=world.strategyFacts.value("azirshenji_role_witness").toObject();
            bool found=false;
            for (const auto &value:witness.value("rows").toArray()) {
                const auto proof=value.toObject();
                if (proof.value("player")==row.value("player") && proof.value("head_known")==QJsonValue(true)
                    && proof.value("solar")==QJsonValue(true) && proof.value("role_known")==QJsonValue(true)
                    && proof.value("role")==row.value("role")) {found=true;break;}
            }
            if (!found || !players.contains(row.value("player").toString())
                || !roles.contains(row.value("role").toString())) return false;
        } else if (kind == "role_row_update") {
            allowed.unite({"player", "role", "assigned_role", "assigned_value", "source_skill"});
            static const QSet<QString> roles{"lord", "loyalist", "rebel", "renegade"};
            const QString source = row.value("source_skill").toString();
            const auto witness = world.strategyFacts.value("role_row_update_witness").toObject();
            const QString role = row.value("role").toString();
            const bool mercenary = source == "Cjiyongbing";
            if (!roles.contains(role) || !players.contains(row.value("player").toString())
                || !witness.value(source).toArray().contains(row.value("player"))
                || row.value("assigned_role") != (mercenary ? role : role == "rebel" ? QString("loyalist") : role)
                || row.value("assigned_value") != (mercenary ? 1000 : 0)) return false;
        } else if (kind == "loong_feilong_role") {
            allowed.unite({"actor", "player", "role"});
            const auto witness = world.strategyFacts.value("loong_feilong_role_witness").toObject();
            static const QSet<QString> roles{"lord", "loyalist", "rebel", "renegade"};
            if (row.value("actor") != world.self.objectName
                || row.value("actor") != witness.value("actor")
                || row.value("role") != witness.value("role")
                || !roles.contains(row.value("role").toString())
                || !players.contains(row.value("player").toString())
                || !witness.value("targets").toArray().contains(row.value("player"))) return false;
        } else if (kind == "du_tongque_role") {
            allowed.insert("player");
            const auto witness = world.strategyFacts.value("du_tongque_role_witness").toObject();
            if (!players.contains(row.value("player").toString())
                || row.value("player") != witness.value("player")) return false;
        } else if (kind == "kaiyuan_role" || kind == "quanxiang_role" || kind == "sfofl_longmu_role") {
            allowed.unite({"player", "source_player", "source_role", "target_role"});
            const auto witness = world.strategyFacts.value(kind + "_witness").toObject();
            static const QSet<QString> roles{"lord", "loyalist", "rebel", "renegade"};
            if (!players.contains(row.value("player").toString())
                || row.value("player") != witness.value("player")
                || !players.contains(row.value("source_player").toString())
                || !witness.value("sources").toArray().contains(row.value("source_player"))
                || !roles.contains(row.value("source_role").toString())
                || !roles.contains(row.value("target_role").toString())) return false;
        } else if (kind == "role_copy" || kind == "peach_renegade" || kind == "intention") {
            allowed.unite({"from", "to"});
            if (kind == "intention") {
                allowed.insert("level");
                if (!row.value("level").isDouble()) return false;
            }
            if (!players.contains(row.value("from").toString())
                || !players.contains(row.value("to").toString())
                || row.value("from") == row.value("to")) return false;
        } else {
            allowed.insert("player");
            if (!players.contains(row.value("player").toString())) return false;
            if (kind == "role_value_add") {
                allowed.unite({"role", "amount"});
                if (row.value("role") != "renegade" || !row.value("amount").isDouble()) return false;
            } else if (kind == "strategy_flag") {
                allowed.unite({"name", "value"});
                const QString name = row.value("name").toString();
                const QString weaponFlag = QStringLiteral("weapon_done_") + world.self.objectName
                    + QStringLiteral("_") + row.value("player").toString();
                bool standardFlag = name == "AI_NosXuanhuoTarget"
                    || (name=="willSkipPlayPhase" && row.value("player").toString()==world.self.objectName
                        && world.strategyFacts.value("original_guanxing_flag_write")==QJsonValue(true));
                if (row.value("player").toString() == world.self.objectName) {
                    standardFlag = standardFlag || name == "NosKurou_toDie" || name == "AI_doNotInvoke_qianxi" || name == "AI_doNotInvoke_luoshen"
                        || name == "AI_Luoshen_Conflict_With_Guanxing" || name == "stack_overflow_jijiang"
                        || name == "should_luawu" || name == "AI_doInvoke_luawu"
                        || name == "vecter_friend" || name == "vecter_slash" || name == "AI_XiechanUsing";
                    for (const QString &prefix : {QStringLiteral("nostuxi_isfriend_"), QStringLiteral("tuxi_isfriend_"), QStringLiteral("jieyin_isenemy_"), QStringLiteral("AI_XiansiToFriend_"), QStringLiteral("sp_qiangxi_isfriend_"), QStringLiteral("luajiejiang_isfriend_"), QStringLiteral("AI_TuxiToFriend_")})
                        standardFlag = standardFlag || (name.startsWith(prefix) && players.contains(name.mid(prefix.size())));
                    for (const auto &target : players) {
                        const QString prefix = target + QStringLiteral("yj_zhengyuUse");
                        if (!name.startsWith(prefix)) continue;
                        bool valid = false; const int id = name.mid(prefix.size()).toInt(&valid);
                        if (!valid || name != prefix + QString::number(id)) continue;
                        for (const auto &card : world.handCards) if (card.effectiveId == id) standardFlag = true;
                        for (const auto &card : world.self.equips) if (card.effectiveId == id) standardFlag = true;
                    }
                    for (const auto &pattern : world.strategyFacts.value("pattern_names").toArray())
                        standardFlag = standardFlag || name == pattern.toString() + QStringLiteral("nosguhuoFailed");
                }
                if (!row.value("value").isBool() || !(name == "AI_doNotSave"
                    || name == "AIGlobal_NeedToWake" || name == "needCrossbow" || name == "AI_HuangtianPindian" || name == "AI_MizhaoTarget"
                    || name == weaponFlag || standardFlag
                    || (name.startsWith("duelTo_") && players.contains(name.mid(7))))) return false;
            } else if (kind == "strategy_mark") {
                allowed.unite({"name", "amount"});
                const QString name = row.value("name").toString(); int amount;
                const QJsonObject focus = world.strategyFacts.value("imagine_focus_event").toObject();
                const bool focusAllowed = (name == "FocusDamageCount" || name == "FocusRecoverCount")
                    && focus.value("name") == row.value("name") && focus.value("player") == row.value("player")
                    && focus.value("amount") == row.value("amount") && focus.value("baseline").isDouble();
                if (!row.value("amount").isDouble() || !aiResultInteger(row.value("amount").toDouble(), amount)
                    || !(focusAllowed || name == "roleRobot" || ((name == "yhpodi_to-PlayClear" || ((name == "AI_fangjian-Clear" || name == "AI_do_not_invoke_s2_longwei-Clear") && row.value("player").toString() == world.self.objectName)) && amount == 1) || (name.startsWith("Intention") && players.contains(name.mid(9)))
                        || (name.startsWith("revenge") && players.contains(name.mid(7))
                            && row.value("player").toString() == world.self.objectName))) return false;
            } else if (kind != "strategy_flags_clear") return false;
        }
        for (auto it = row.begin(); it != row.end(); ++it) if (!allowed.contains(it.key())) return false;
    }
    return true;
}

bool parseIntentionDeltas(lua_State *state, const AIWorldView &world,
                         QList<AiIntentionDelta> &deltas)
{
    const int resultIndex = lua_gettop(state);
    if (!isPlainTable(state, resultIndex))
        return false;
    const size_t count = lua_rawlen(state, resultIndex);
    if (count > size_t(AiMaxIntentionDeltas))
        return false;
    QSet<QString> players;
    players.insert(world.self.objectName);
    for (const AIPlayerView &player : world.players)
        players.insert(player.objectName);
    players.remove(QString());
    // Reject sparse arrays, named fields and all metatables before any commit.
    int entries = 0;
    lua_pushnil(state);
    while (lua_next(state, resultIndex) != 0) {
        int index = 0;
        const bool valid = lua_type(state, -2) == LUA_TNUMBER
            && aiResultInteger(lua_tonumber(state, -2), index)
            && index >= 1 && size_t(index) <= count;
        lua_pop(state, 1);
        if (!valid || ++entries > AiMaxIntentionDeltas) {
            lua_settop(state, resultIndex);
            return false;
        }
    }
    if (size_t(entries) != count)
        return false;
    for (size_t index = 1; index <= count; ++index) {
        lua_rawgeti(state, resultIndex, int(index));
        const int deltaIndex = lua_gettop(state);
        if (!isPlainTable(state, deltaIndex)) {
            lua_settop(state, resultIndex);
            return false;
        }
        AiIntentionDelta delta;
        int fields = 0;
        unsigned required = 0;
        bool valid = true;
        lua_pushnil(state);
        while (lua_next(state, deltaIndex) != 0) {
            QString key;
            valid = lua_type(state, -2) == LUA_TSTRING
                && readBoundedString(state, -2, key);
            if (valid && (key == QStringLiteral("from") || key == QStringLiteral("to"))) {
                QString &name = key == QStringLiteral("from") ? delta.from : delta.to;
                valid = lua_type(state, -1) == LUA_TSTRING
                    && readBoundedString(state, -1, name) && players.contains(name);
                required |= key == QStringLiteral("from") ? 1u : 2u;
            } else if (valid && key == QStringLiteral("level")) {
                delta.level = lua_tonumber(state, -1);
                valid = lua_type(state, -1) == LUA_TNUMBER
                    && std::isfinite(double(delta.level));
                required |= 4u;
            } else if (valid && key == QStringLiteral("defer_refresh")) {
                valid = lua_type(state, -1) == LUA_TBOOLEAN;
                delta.deferRefresh = lua_toboolean(state, -1);
                delta.deferRefreshPresent = true;
            } else {
                valid = false;
            }
            lua_pop(state, 1);
            if (!valid || ++fields > 4)
                break;
        }
        if (!valid || required != 7u || delta.from == delta.to) {
            lua_settop(state, resultIndex);
            return false;
        }
        deltas.append(delta);
        lua_pop(state, 1);
    }
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
    if (!card.legacyIdentityToken.isEmpty()) setStringField(state, "legacy_identity_token", card.legacyIdentityToken);
    if (!card.legacyStringKey.isNull()) setStringField(state, "legacy_string_key", card.legacyStringKey);
    if (!card.legacySortKey.isNull()) setStringField(state, "legacy_sort_key", card.legacySortKey);
    if (card.nameLength >= 0) setIntegerField(state, "name_length", card.nameLength);
    setStringField(state, "name", card.objectName);
    setStringField(state, "class_name", card.className);
    lua_pushboolean(state, card.skillNamesComplete);
    lua_setfield(state, -2, "skill_names_complete");
    if (card.skillNamesComplete) {
        lua_createtable(state, card.skillNames.size(), 0);
        for (int i = 0; i < card.skillNames.size(); ++i) {
            pushQString(state, card.skillNames.at(i)); lua_rawseti(state, -2, i + 1);
        }
        lua_setfield(state, -2, "skill_names");
    }
    lua_createtable(state, card.equipOccupyLocations.size(), 0);
    for (int i = 0; i < card.equipOccupyLocations.size(); ++i) {
        lua_pushinteger(state, card.equipOccupyLocations.at(i)); lua_rawseti(state, -2, i + 1);
    }
    lua_setfield(state, -2, "equip_occupy_locations");
    lua_createtable(state, 0, card.targetResidue.size());
    for (auto it = card.targetResidue.constBegin(); it != card.targetResidue.constEnd(); ++it) {
        lua_pushinteger(state, it.value()); lua_setfield(state, -2, it.key().toUtf8().constData());
    }
    lua_setfield(state, -2, "target_residue");
    if (!card.engineClassName.isEmpty())
        setStringField(state, "engine_class_name", card.engineClassName);
    if (card.ownerStateKnown || card.sourceSkillName == card.skillName)
        setStringField(state, "source_skill", card.sourceSkillName);
    lua_pushboolean(state, card.willThrow);
    lua_setfield(state, -2, "will_throw");
    lua_createtable(state, 0, card.knownFlags.size());
    for (auto it = card.knownFlags.constBegin(); it != card.knownFlags.constEnd(); ++it) {
        lua_pushboolean(state, it.value().toBool()); lua_setfield(state, -2, it.key().toUtf8().constData());
    }
    lua_setfield(state, -2, "known_flags");
    lua_createtable(state, 0, card.knownTips.size());
    for (auto it = card.knownTips.constBegin(); it != card.knownTips.constEnd(); ++it) {
        lua_pushboolean(state, it.value().toBool()); lua_setfield(state, -2, it.key().toUtf8().constData());
    }
    lua_setfield(state, -2, "known_tips");
    lua_pushboolean(state, card.flagsComplete);
    lua_setfield(state, -2, "flags_complete");
    if (card.flagsComplete) setStringField(state, "flags_complete_scope", "owner_non_visibility");
    lua_pushboolean(state, card.canRecast);
    lua_setfield(state, -2, "can_recast");
    if (card.ownerStateKnown) {
        lua_pushboolean(state, card.usingCard);
        lua_setfield(state, -2, "using");
        lua_pushboolean(state, card.killOff);
        lua_setfield(state, -2, "kill_off");
        lua_pushboolean(state, card.locked);
        lua_setfield(state, -2, "locked");
        lua_pushboolean(state, card.jilei);
        lua_setfield(state, -2, "jilei");
    }
    lua_pushinteger(state, card.suit);
    lua_setfield(state, -2, "suit");
    lua_pushinteger(state, card.number);
    lua_setfield(state, -2, "number");
    setStringField(state, "skill_name", card.skillName);
    lua_pushinteger(state, card.typeId);
    lua_setfield(state, -2, "type_id");
    lua_pushinteger(state, card.equipSlot);
    lua_setfield(state, -2, "equip_slot");
    if (card.weaponRange >= 0) {
        lua_pushinteger(state, card.weaponRange);
        lua_setfield(state, -2, "weapon_range");
    }
    lua_pushinteger(state, card.handlingMethod);
    lua_setfield(state, -2, "handling_method");
    lua_pushboolean(state, card.virtualCard);
    lua_setfield(state, -2, "virtual_card");
    lua_pushboolean(state, card.targetFixed);
    lua_setfield(state, -2, "target_fixed");
    lua_pushboolean(state, card.singleTarget); lua_setfield(state, -2, "single_target");
    lua_pushboolean(state, card.damageCard);
    lua_setfield(state, -2, "damage_card");
    lua_createtable(state, int(card.subcardIds.size()), 0);
    for (int index = 0; index < card.subcardIds.size(); ++index) {
        lua_pushinteger(state, card.subcardIds.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "subcards");
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
    lua_createtable(state, int(skill.skillClasses.size()), 0);
    for (int index = 0; index < skill.skillClasses.size(); ++index) {
        pushQString(state, skill.skillClasses.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "skill_classes");
    lua_pushinteger(state, skill.frequency);
    lua_setfield(state, -2, "frequency");
    lua_pushboolean(state, skill.hasViewAsSkill);
    lua_setfield(state, -2, "has_view_as_skill");
    setStringField(state, "description", skill.description);
    lua_createtable(state, skill.triggerEvents.size(), 0);
    for (int i = 0; i < skill.triggerEvents.size(); ++i) { lua_pushinteger(state, skill.triggerEvents[i]); lua_rawseti(state, -2, i+1); }
    lua_setfield(state, -2, "trigger_events");
    lua_pushboolean(state, skill.loseEquipDescriptionTrigger); lua_setfield(state, -2, "lose_equip_description_trigger");
    lua_pushboolean(state, skill.retrialTrigger);
    lua_setfield(state, -2, "retrial_trigger");
    lua_pushboolean(state, skill.lordSkill);
    lua_setfield(state, -2, "lord_skill");
    lua_pushboolean(state, skill.attachedLordSkill);
    lua_setfield(state, -2, "attached_lord_skill");
    lua_pushboolean(state, skill.lordSkillEffective);
    lua_setfield(state, -2, "lord_skill_effective");
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
    lua_pushboolean(state, player.skillsComplete);
    lua_setfield(state, -2, "skills_complete");
    setIntegerField(state, "hand_pile_count", player.handPileCount);
    setStringField(state, "state", player.playerState);
    if (player.privateFlagsVisible) { pushAIJsonValue(state, player.cardRules); lua_setfield(state, -2, "card_rules"); }
    pushAIJsonValue(state, player.usedHistory); lua_setfield(state, -2, "used_history");
    if (player.privateFlagsVisible) { pushAIJsonValue(state, player.canDiscard); lua_setfield(state, -2, "can_discard"); }
    if (player.privateFlagsVisible) {
        lua_pushboolean(state, player.inYinniState);
        lua_setfield(state, -2, "in_yinni_state");
        lua_pushboolean(state, player.aiNoTo); lua_setfield(state, -2, "ai_no_to");
        lua_pushboolean(state, player.shuangxiongColors); lua_setfield(state, -2, "shuangxiong_colors");
    }
    lua_pushinteger(state, player.maxCards);
    lua_setfield(state, -2, "max_cards");
    lua_pushinteger(state, player.hujia);
    lua_setfield(state, -2, "hujia");
    lua_pushinteger(state, player.attackRange);
    lua_setfield(state, -2, "attack_range");
    setIntegerField(state, "attack_range_without_weapon", player.attackRangeWithoutWeapon);
    lua_pushinteger(state, player.gender);
    lua_setfield(state, -2, "gender");
    lua_pushboolean(state, player.lord);
    lua_setfield(state, -2, "lord");
    lua_createtable(state, 0, int(player.equipSlots.size()));
    for (auto slot = player.equipSlots.constBegin(); slot != player.equipSlots.constEnd(); ++slot) {
        lua_pushinteger(state, slot.value());
        lua_rawseti(state, -2, slot.key() + 1);
    }
    lua_setfield(state, -2, "equip_slots");
    setStringField(state, "kingdom", player.kingdom);
    setStringField(state, "role", player.role);
    setStringField(state, "controller", player.controller);
    lua_pushboolean(state, player.roleRevealed);
    lua_setfield(state, -2, "role_revealed");
    lua_pushboolean(state, player.roleVisible);
    lua_setfield(state, -2, "role_visible");
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
    lua_pushboolean(state, player.marksComplete); lua_setfield(state, -2, "marks_complete");
    pushAISkills(state, player.skills);
    lua_setfield(state, -2, "skills");
    pushAICards(state, player.knownCards);
    lua_setfield(state, -2, "known_cards");
    lua_pushboolean(state, player.handVisible);
    lua_setfield(state, -2, "hand_visible");
    if (player.armorEffectKnown)
        setStringField(state, "active_armor_name", player.activeArmorName);
    if (player.privateFlagsVisible) {
        lua_createtable(state, int(player.privateFlags.size()), 0);
        for (int index = 0; index < player.privateFlags.size(); ++index) {
            lua_pushstring(state, player.privateFlags.at(index).toUtf8().constData());
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "flags");
        lua_createtable(state, 0, int(player.skippedPhases.size()));
        for (auto phase = player.skippedPhases.constBegin(); phase != player.skippedPhases.constEnd(); ++phase) {
            lua_pushboolean(state, phase.value());
            lua_rawseti(state, -2, phase.key());
        }
        lua_setfield(state, -2, "skipped_phases");
    }
    lua_createtable(state, int(player.piles.size()), 0);
    for (int index = 0; index < player.piles.size(); ++index) {
        const AICardPileView &pile = player.piles.at(index);
        lua_createtable(state, 0, 5);
        setStringField(state, "name", pile.name);
        lua_pushinteger(state, pile.count);
        lua_setfield(state, -2, "count");
        lua_pushboolean(state, pile.open);
        lua_setfield(state, -2, "open");
        lua_pushboolean(state, pile.handPile);
        lua_setfield(state, -2, "hand_pile");
        // A closed pile has no id list at all: absent means unknown, not empty.
        if (pile.open) {
            lua_createtable(state, int(pile.cardIds.size()), 0);
            for (int cardIndex = 0; cardIndex < pile.cardIds.size(); ++cardIndex) {
                lua_pushinteger(state, pile.cardIds.at(cardIndex));
                lua_rawseti(state, -2, cardIndex + 1);
            }
            lua_setfield(state, -2, "card_ids");
            pushAICards(state, pile.cards);
            lua_setfield(state, -2, "cards");
        }
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "piles");
    // Public board enumerates every named pile, including closed pile counts.
    lua_pushboolean(state, true); lua_setfield(state, -2, "piles_complete");
    lua_createtable(state, int(player.displayCards.size()), 0);
    for (int index = 0; index < player.displayCards.size(); ++index) {
        lua_pushinteger(state, player.displayCards.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "display_cards");
}

void pushAIEvent(lua_State *state, const AIEventView &event)
{
    lua_createtable(state, 0, 22);
    lua_pushinteger(state, lua_Integer(event.sequence));
    lua_setfield(state, -2, "sequence");
    setStringField(state, "revision", QString::number(event.revision));
    lua_pushinteger(state, event.triggerEvent);
    lua_setfield(state, -2, "trigger_event");
    setStringField(state, "kind", event.kind);
    setStringField(state, "from", event.from);
    setStringField(state, "to", event.to);
    setStringField(state, "card_name", event.cardName);
    setStringField(state, "reason", event.reason);
    lua_pushinteger(state, event.amount);
    lua_setfield(state, -2, "amount");
    lua_pushinteger(state, event.nature);
    lua_setfield(state, -2, "nature");
    lua_pushinteger(state, event.place);
    lua_setfield(state, -2, "place");
    lua_pushboolean(state, event.good);
    lua_setfield(state, -2, "good");
    lua_createtable(state, int(event.targets.size()), 0);
    for (int targetIndex = 0; targetIndex < event.targets.size(); ++targetIndex) {
        pushQString(state, event.targets.at(targetIndex));
        lua_rawseti(state, -2, targetIndex + 1);
    }
    lua_setfield(state, -2, "targets");
    QList<int> ids = event.cardIds;
    ids << event.privateCardIds;
    lua_createtable(state, int(ids.size()), 0);
    for (int idIndex = 0; idIndex < ids.size(); ++idIndex) {
        lua_pushinteger(state, ids.at(idIndex));
        lua_rawseti(state, -2, idIndex + 1);
    }
    lua_setfield(state, -2, "card_ids");
    setStringField(state, "card_class", event.cardClass);
    setStringField(state, "card_skill", event.cardSkill);
    lua_pushboolean(state, event.chain);
    lua_setfield(state, -2, "chain");
    lua_pushboolean(state, event.transfer);
    lua_setfield(state, -2, "transfer");
    lua_pushboolean(state, event.byUser);
    lua_setfield(state, -2, "by_user");
    if (event.details.value("intention_suppression_known").toBool()) {
        lua_pushboolean(state, event.intentionSuppressed);
        lua_setfield(state, -2, "intention_suppressed");
    }
    pushAIJsonValue(state, event.details);
    lua_setfield(state, -2, "details");
}

void pushAIWorldView(lua_State *state, const AIWorldView &world)
{
    lua_createtable(state, 0, 11);
    setStringField(state, "mode_id", world.modeId);
    setStringField(state, "distance_scope", world.distanceScope);
    lua_pushboolean(state, world.customRoles);
    lua_setfield(state, -2, "custom_roles");
    pushAIJsonValue(state, world.modePolicy);
    lua_setfield(state, -2, "mode_policy");
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
    pushAICards(state, world.discardPile);
    lua_setfield(state, -2, "discard_pile");
    // IDs only: RoomView resolves these against this request's visible players.
    lua_createtable(state, int(world.playerOrder.size()), 0);
    for (int index = 0; index < world.playerOrder.size(); ++index) {
        pushQString(state, world.playerOrder.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "player_order");
    lua_createtable(state, int(world.alivePlayerOrder.size()), 0);
    for (int index = 0; index < world.alivePlayerOrder.size(); ++index) {
        pushQString(state, world.alivePlayerOrder.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "alive_player_order");
    lua_createtable(state, 0, int(world.distances.size()));
    for (auto from = world.distances.constBegin(); from != world.distances.constEnd(); ++from) {
        lua_createtable(state, 0, int(from.value().size()));
        for (auto to = from.value().constBegin(); to != from.value().constEnd(); ++to) {
            lua_pushinteger(state, to.value());
            const QByteArray key = to.key().toUtf8();
            lua_setfield(state, -2, key.constData());
        }
        const QByteArray key = from.key().toUtf8();
        lua_setfield(state, -2, key.constData());
    }
    lua_setfield(state, -2, "distances");
    lua_createtable(state, int(world.events.size()), 0);
    for (int index = 0; index < world.events.size(); ++index) {
        pushAIEvent(state, world.events.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "events");
    setStringField(state, "current_player", world.currentPlayer);
    lua_pushinteger(state, world.currentPhase);
    lua_setfield(state, -2, "current_phase");
    lua_pushinteger(state, world.turnCount);
    lua_setfield(state, -2, "turn_count");
    lua_pushboolean(state, world.enableHegemony); lua_setfield(state, -2, "enable_hegemony");
    lua_pushboolean(state, world.aiHumanized); lua_setfield(state, -2, "ai_humanized");
    lua_pushboolean(state, world.aiChatEnabled); lua_setfield(state, -2, "ai_chat_enabled");
    setStringField(state, "duel_rule", world.duelRule);
    if (world.roleCountsKnown) { pushAIJsonValue(state, world.roleCounts); lua_setfield(state, -2, "role_counts"); }
    pushAIJsonValue(state, world.strategyFacts); lua_setfield(state, -2, "strategy_facts");
    pushAIJsonValue(state, world.evaluationCards); lua_setfield(state, -2, "evaluation_cards");
    pushAIJsonValue(state, world.decisionContext); lua_setfield(state, -2, "decision_context");
    pushAIJsonValue(state, world.responsePolicy);
    lua_setfield(state, -2, "response_policy");
}

}

void AiLuaRuntime::pushWorldView(lua_State *state, const AIWorldView &world)
{
    pushAIWorldView(state, world);
}

void AiLuaRuntime::evaluateModePolicy(LuaRuntime &runtime, AIWorldView &world, bool viewerOnly)
{
    world.modePolicy = QJsonObject{{"managed", world.customRoles},
        {"relations", QJsonObject()}, {"objectives", QJsonObject()}};
    if (!world.self.objectName.isEmpty()) {
        world.modePolicy.insert("relations", QJsonObject{{world.self.objectName,
            QJsonObject{{world.self.objectName, "friend"}}}});
        world.modePolicy.insert("objectives", QJsonObject{{world.self.objectName, -3}});
    }
    world.modePolicy.insert("relation_scope", viewerOnly ? "viewer" : "full");
    if (!runtime.rawState()) {
        world.modePolicy.insert("error", "runtime_unavailable");
        return;
    }
    LuaRuntime::Binding binding(runtime);
    LuaRuntime::LuaInvocationScope invocation(runtime);
    lua_State *state = runtime.rawState();
    // Before the registry guard or world allocation, including refusal paths.
    Room *modeRoom = Sanguosha ? Sanguosha->currentRoom() : nullptr;
    std::unique_ptr<AIRound56ModeEvaluationScope> sourceScope;
    if (modeRoom && modeRoom->roomRuntime() && &modeRoom->roomRuntime()->lua() == &runtime)
        sourceScope.reset(new AIRound56ModeEvaluationScope(*modeRoom, state, world.modeId));
    const int top = lua_gettop(state);
    // Reentrancy is tracked in this Room's registry, never a process-global AI table.
    lua_getfield(state, LUA_REGISTRYINDEX, "qsan.mode_ai.active");
    const bool active = lua_toboolean(state, -1);
    lua_pop(state, 1);
    if (active) {
        world.modePolicy.insert("managed", true);
        world.modePolicy.insert("error", "policy_reentrant");
        return;
    }
    lua_getglobal(state, "sgs");
    if (!lua_istable(state, -1)) {
        world.modePolicy.insert("error", "policy_unavailable");
        lua_settop(state, top); return;
    }
    lua_getfield(state, -1, "evaluateModeAI");
    if (!lua_isfunction(state, -1)) {
        world.modePolicy.insert("error", "policy_unavailable");
        lua_settop(state, top); return;
    }
    lua_pushboolean(state, true);
    lua_setfield(state, LUA_REGISTRYINDEX, "qsan.mode_ai.active");
    if (sourceScope && sourceScope->accepted()) {
        AIRound56ModeWorldSource worldSource(*modeRoom, state);
        pushAIWorldView(state, world);
    } else {
        pushAIWorldView(state, world);
    }
    lua_pushboolean(state, viewerOnly);
    const int status = LuaRuntime::protectedCall(state, 2, 1, 0);
    lua_pushboolean(state, false);
    lua_setfield(state, LUA_REGISTRYINDEX, "qsan.mode_ai.active");
    if (status != 0 || !lua_istable(state, -1)) {
        world.modePolicy.insert("managed", true);
        world.modePolicy.insert("error", "policy_failed");
        lua_settop(state, top);
        return;
    }
    const int result = lua_gettop(state);
    for (const char *field : {"strategy_flags", "strategy_flags_cleared", "strategy_marks", "role_values"}) {
        lua_getfield(state, result, field);
        if (!lua_isnil(state, -1)) {
            QJsonValue value; int budget = 8192;
            if (!readAIJson(state, -1, value, budget) || !value.isObject()) {
                world.modePolicy.insert("managed", true); world.modePolicy.insert("error", "invalid_strategy_state");
                lua_settop(state, top); return;
            }
            world.modePolicy.insert(QString::fromLatin1(field), value);
        }
        lua_pop(state, 1);
    }
    for (const char *field : {"explicit_renegade", "standard_identity", "legacy_managed", "legacy_base_update"}) {
        lua_getfield(state, result, field);
        if (lua_isboolean(state, -1)) world.modePolicy.insert(QString::fromLatin1(field), bool(lua_toboolean(state, -1)));
        lua_pop(state, 1);
    }
    lua_getfield(state, result, "estimated_branches");
    QJsonArray estimatedBranches;
    bool validEstimates = readAIArray(state, -1, estimatedBranches, 256);
    for (const auto &value : estimatedBranches) if (!value.isString()) validEstimates = false;
    lua_pop(state, 1);
    if (!validEstimates) {
        world.modePolicy.insert("managed", true); world.modePolicy.insert("error", "invalid_estimation_state");
        lua_settop(state, top); return;
    }
    world.modePolicy.insert("estimated_branches", estimatedBranches);
    lua_getfield(state, result, "error");
    QString policyError;
    if (lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, policyError))
        world.modePolicy.insert("error", policyError);
    lua_pop(state, 1);
    lua_getfield(state, result, "managed");
    world.modePolicy.insert("managed", world.customRoles || bool(lua_toboolean(state, -1)));
    lua_pop(state, 1);
    lua_getfield(state, result, "predictable");
    world.modePolicy.insert("predictable", bool(lua_toboolean(state, -1)));
    lua_pop(state, 1);
    QStringList names{world.self.objectName};
    for (const AIPlayerView &player : world.players) names << player.objectName;
    QJsonObject estimates;
    lua_getfield(state, result, "role_estimates");
    if (lua_istable(state, -1)) {
        for (const QString &name : names) {
            lua_getfield(state, -1, name.toUtf8().constData());
            QString value;
            if (lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, value)
                && QStringList{"lord", "loyalist", "rebel", "renegade", "neutral", "unknown"}.contains(value))
                estimates.insert(name, value);
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    world.modePolicy.insert("role_estimates", estimates);
    QJsonObject relations;
    lua_getfield(state, result, "relations");
    if (lua_istable(state, -1)) {
        const QStringList sources = viewerOnly ? QStringList{world.self.objectName} : names;
        for (const QString &from : sources) {
            lua_getfield(state, -1, from.toUtf8().constData());
            QJsonObject row;
            if (lua_istable(state, -1)) {
                for (const QString &to : names) {
                    lua_getfield(state, -1, to.toUtf8().constData());
                    QString relation;
                    if (lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, relation)
                        && (relation == "friend" || relation == "enemy"
                            || relation == "neutral" || relation == "unknown"))
                        row.insert(to, relation);
                    lua_pop(state, 1);
                }
            }
            relations.insert(from, row);
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    world.modePolicy.insert("relations", relations);
    QJsonObject objectives;
    lua_getfield(state, result, "objectives");
    if (lua_istable(state, -1)) {
        for (const QString &name : names) {
            lua_getfield(state, -1, name.toUtf8().constData());
            const double value = lua_tonumber(state, -1);
            if (lua_type(state, -1) == LUA_TNUMBER && std::isfinite(value)
                && (world.modePolicy.value("standard_identity").toBool()
                    || (value >= -5 && value <= 5))) objectives.insert(name, value);
            lua_pop(state, 1);
        }
    }
    lua_pop(state, 1);
    world.modePolicy.insert("objectives", objectives);
    lua_getfield(state, result, "game_process");
    const double process = lua_tonumber(state, -1);
    if (lua_type(state, -1) == LUA_TNUMBER && std::isfinite(process))
        world.modePolicy.insert("game_process", process);
    lua_pop(state, 1);
    lua_getfield(state, result, "process_label");
    QString label;
    if (lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, label))
        world.modePolicy.insert("process_label", label);
    lua_settop(state, top);
}

AiRouteRegistry::AiRouteRegistry()
    : m_frozen(false)
{
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
    return m_decisionRoutes.value(int(kind), AiRouteIsolated);
}

QString AiRouteRegistry::callbackKey(const QString &callbackName,
                                     const QString &skillName)
{
    return callbackName + QChar(0x1f) + skillName;
}

bool AiRouteRegistry::hasLegacyRoutes() const
{
    for (AiRoute route : m_decisionRoutes) {
        if (route != AiRouteIsolated)
            return true;
    }
    for (AiRoute route : m_callbackRoutes) {
        if (route != AiRouteIsolated)
            return true;
    }
    return false;
}

AiLuaRuntime::AiLuaRuntime(Room *room)
    : m_room(room), m_lua(LuaRuntime::Auxiliary), m_instructionBudget(2000000),
      m_initializationInstructionBudget(500000), m_instructionsRemaining(0),
      m_instructionLimitExceeded(false)
{
}

AiLuaRuntime::~AiLuaRuntime()
{
    shutdown();
}

bool AiLuaRuntime::initialize(QString *error)
{
    if (m_room) clearAIStrategyRound24Response(*m_room);
    if(m_room)invalidateAIRound44RangeDecision(*m_room);
    if(m_room)clearAIRound23OlClanObservations(*m_room);
    if(m_room)clearAIRound41FaqiPlans(*m_room);
    if(m_room)clearAIRound26PlanningPublicUses(*m_room);
    if(m_room){clearAIRound44FCUseFrames(*m_room);clearAIRound45NewgeneralsFrames(*m_room);}
    if(m_room)clearAIRound26DelegateObservations(*m_room);
    if(m_room)clearAIRound28ChoiceState(*m_room);
    if(m_room)clearAIRound27OriginalViews(*m_room);
    if(m_room)clearAIRound29ShicaiPreview(*m_room);
    clearObservationLedgers();
    // Room startup has finished loading the legacy registries. Capture audited
    // function identities once, before the isolated VM can make a decision.
    if (m_room) captureAIStrategyAliases(*m_room);
    if (m_room) {
        captureAIRound29ChoicePriorities(*m_room);
        captureAIRound30TargetCache(*m_room);
        initializeAIRound30TargetRevises(*m_room);
        initializeAIRound36KurouShadow(*m_room);
        initializeAIRound38Manbing(*m_room);
        initializeAIRound37OriginalTurnUse(*m_room);
        initializeAIRound45InovationProviders(*m_room);
        initializeAIRound37CrossbowShadow(*m_room);
    }
    if (m_room) captureAIRound24ChoiceRegistry(*m_room);
    if (m_room) captureAIRound25PlanningMetadata(*m_room);
    if (m_room) captureAIRound31PlanningMetadata(*m_room);
    if (m_room) captureAIRound26DelegateMetadata(*m_room);
    if (m_room) { clearAIStrategyRound26Response(*m_room); captureAIStrategyRound26Response(*m_room); }
    const int softMiB = qMax(1, Config.value(QStringLiteral("AiLuaSoftMemoryMiB"),
                                             int(AiSoftMemoryLimit / 1024u / 1024u)).toInt());
    const int hardMiB = qMax(softMiB, Config.value(QStringLiteral("AiLuaHardMemoryMiB"),
                                                   int(AiHardMemoryLimit / 1024u / 1024u)).toInt());
    m_instructionBudget = qMax<qint64>(10000,
        Config.value(QStringLiteral("AiLuaInstructionBudget"), 2000000).toLongLong());
    m_initializationInstructionBudget = qMax<qint64>(10000,
        Config.value(QStringLiteral("AiLuaInitializationInstructionBudget"), 500000).toLongLong());
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
    if(m_room)initializeAIRound41ChoiceTables(*m_room,m_lua.state(),true);
    loadConfiguredRoutes();
    m_routes.freeze();
    return true;
}

void AiLuaRuntime::shutdown()
{
    if (m_room) clearAIStrategyRound24Response(*m_room);
    if(m_room)invalidateAIRound44RangeDecision(*m_room);
    m_sourceCompatibility.reset();
    if(m_room)clearAIRound23OlClanObservations(*m_room);
    if(m_room)clearAIRound41FaqiPlans(*m_room);
    if(m_room)clearAIRound26PlanningPublicUses(*m_room);
    if(m_room){clearAIRound44FCUseFrames(*m_room);clearAIRound45NewgeneralsFrames(*m_room);}
    if(m_room)clearAIRound26DelegateObservations(*m_room);
    if(m_room)clearAIRound28ChoiceState(*m_room);
    if(m_room)clearAIRound27OriginalViews(*m_room);
    if(m_room)clearAIRound29ShicaiPreview(*m_room);
    if(m_room) clearAIStrategyRound26Response(*m_room);
    clearObservationLedgers();
    m_readonlyCards.reset();
    m_lua.shutdown();
}

QString AiLuaRuntime::issueReadonlyCard(const AIRequest &request, ServerPlayer *viewer,
                                      std::unique_ptr<Card> card, const QString &sourceKey)
{
    if (current()!=this || m_activeRequest!=&request || m_activeEventRequest || !m_room || !m_readonlyCards) return {};
    return m_readonlyCards->issue(viewer,request,std::move(card),sourceKey);
}

QString AiLuaRuntime::issueBorrowedReadonlyCard(const AIRequest &request, ServerPlayer *viewer,
                                               const Card *card, const QString &sourceKey)
{
    if (current()!=this || m_activeRequest!=&request || m_activeEventRequest || !m_room || !m_readonlyCards) return {};
    return m_readonlyCards->issueBorrowed(viewer,request,card,sourceKey);
}

const Card *AiLuaRuntime::readonlyCard(const AIRequest &request, ServerPlayer *viewer, const QString &key) const
{
    if (current()!=this || m_activeRequest!=&request || m_activeEventRequest || !m_readonlyCards) return nullptr;
    return m_readonlyCards->lookup(viewer,request,key);
}

QString AiLuaRuntime::readonlyCardSource(const AIRequest &request, ServerPlayer *viewer, const QString &key) const
{
    if (current()!=this || m_activeRequest!=&request || m_activeEventRequest || !m_readonlyCards) return {};
    return m_readonlyCards->sourceKey(viewer,request,key);
}

AIReadonlyCardLease AiLuaRuntime::retainReadonlyCard(const AIRequest &request, ServerPlayer *viewer, const QString &key) const
{
    if (current()!=this || m_activeRequest!=&request || m_activeEventRequest || !m_readonlyCards) return {};
    return m_readonlyCards->retain(viewer,request,key);
}

// Requests may outlive a decision (and have shared copies). Track their
// native-only product custody weakly so Room teardown releases every graph
// even when a diagnostic caller still holds an AIRequest.
void AiLuaRuntime::trackObservationLedger(const QSharedPointer<AIConversionLedger> &ledger)
{
    if (!ledger) return;
    for (auto it=m_observationLedgers.begin(); it!=m_observationLedgers.end();) {
        const auto live=it->toStrongRef();
        if (!live) it=m_observationLedgers.erase(it);
        else { if (live==ledger) return; ++it; }
    }
    m_observationLedgers.append(ledger.toWeakRef());
}

void AiLuaRuntime::clearObservationLedgers()
{
    for (const auto &weak:m_observationLedgers)
        if (const auto ledger=weak.toStrongRef()) ledger->observedProducts.clear();
    m_observationLedgers.clear();
}

void AiLuaRuntime::seed(quint64 seed)
{
    const quint64 aiSeed = seed ^ Q_UINT64_C(0x9e3779b97f4a7c15);
    m_rng.seed(aiSeed);
    m_lua.setSeed(aiSeed);
}

GameRng::State AiLuaRuntime::exportRngState() const
{
    return m_rng.exportState();
}

bool AiLuaRuntime::restoreRngState(const GameRng::State &state, QString *error)
{
    // math.random is sandboxed to m_rng, so restoring this generator is
    // sufficient even while the auxiliary Lua VM is already initialized.
    // Do not restore SmartAI Lua globals: takeover intentionally creates a
    // fresh AI mind and only resumes its deterministic random stream.
    return m_rng.restoreState(state, error);
}

void AiLuaRuntime::commitPreparedRng(GameRng &prepared) noexcept
{
    m_rng.swapState(prepared);
}

AIResult AiLuaRuntime::decideIsolated(const AIRequest &request)
{
    if(m_room)observeAIChoiceContinuityDecision(*m_room,request);
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
        lua_pushnil(state);
        lua_setglobal(state, "ai_last_decision_outcome");
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
        m_activeRequest = &request;
        if(m_room)beginAIRound44RangeDecision(*m_room,request,lua_topointer(state,-1));
        const auto finishRangeDecision=qScopeGuard([&]{if(m_room)finishAIRound44RangeDecision(*m_room,request);});
        m_originalDiscardResultReady=false;
        m_originalDiscardResultIds.clear();
        const auto clearOriginalDiscard=qScopeGuard([this]{m_originalDiscardResultReady=false;m_originalDiscardResultIds.clear();});
        m_originalSuitResultReady=false;
        const auto clearOriginalSuit=qScopeGuard([this]{m_originalSuitResultReady=false;});
        if(m_room) {
            beginAIRound29ChoicePolicy(*m_room,request.viewerObjectName);
            beginAIRound30TargetRevises(*m_room,request.viewerObjectName);
        }
        m_sourceCompatibility.reset(m_room ? new AIRound24SourceSession(*m_room,request) : nullptr);
        const auto clearSourceCompatibility=qScopeGuard([this]{m_sourceCompatibility.reset();});
        m_readonlyCards.reset(m_room ? new AIOwnerReadonlyCardStore(*m_room,request) : nullptr);
        const auto clearRequest = qScopeGuard([this] { m_readonlyCards.reset(); m_activeRequest = nullptr; });
        const int status = lua_pcall(state, 1, 1, 0);
        m_readonlyCards.reset();
        m_activeRequest = nullptr;
        lua_sethook(state, nullptr, 0, 0);
        // A nested policy pcall can catch the hook error. Exhaustion remains
        // a runtime failure even if that policy subsequently returns a value.
        if (status != 0 || m_instructionLimitExceeded) {
            result.errorCode = m_instructionLimitExceeded
                ? QStringLiteral("AI_INSTRUCTION_LIMIT")
                : status == LUA_ERRMEM ? QStringLiteral("AI_MEMORY_LIMIT")
                                       : QStringLiteral("AI_RUNTIME_ERROR");
            lua_pop(state, 1);
            rebuild = status == LUA_ERRMEM || m_instructionLimitExceeded;
        } else {
            if (!lua_isnil(state, -1)) {
                if (!parseResult(state, result, request)
                    || (result.kind == AIResult::Answer && result.action.hasCardSpec
                        && request.kind != AIRequest::RespondCard)) {
                    result.errorCode = QStringLiteral("AI_INVALID_RESULT");

                } else if (request.hasSkillActionContext) {
                    result.action.hasSkillActionContext = true;
                    result.action.skillActionContext = request.skillActionContext;
                }
            }
            lua_pop(state, 1);
            if (!result.handled && result.errorCode.isEmpty()) {
                lua_getglobal(state, "ai_last_decision_outcome");
                if (lua_type(state, -1) == LUA_TSTRING) {
                    const QString outcome = QString::fromUtf8(lua_tostring(state, -1));
                    if (outcome == QStringLiteral("unsupported")) result.errorCode = QStringLiteral("AI_UNSUPPORTED");
                    else if (outcome == QStringLiteral("native-rejected")) result.errorCode = QStringLiteral("AI_NATIVE_REJECTED");
                }
                lua_pop(state, 1);
            }
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

bool AiLuaRuntime::validateDecisionEffects(const AIWorldView &world, const QJsonArray &effects)
{
    return effects.size() <= 64 && validateStrategyEffects(effects, world, 0);
}

bool AiLuaRuntime::commitStrategyEffects(const AIWorldView &world, const QJsonArray &effects,
                                         LuaRuntime &modeRuntime, QString *error)
{
    if (error) error->clear();
    if (effects.isEmpty()) return true;
    const auto fail = [error](const char *message) { if (error) *error = QString::fromLatin1(message); return false; };
    if (current() || !modeRuntime.rawState() || effects.size() > 64
        || !validateStrategyEffects(effects, world, 0)) return fail("AI_STRATEGY_INVALID_EFFECTS");
    for (const auto &effect : effects)
        if (effect.toObject().value("kind") != "strategy_flag"
            && effect.toObject().value("kind") != "intention"
            && effect.toObject().value("kind") != "role_refresh"
            && effect.toObject().value("kind") != "speech"
            && effect.toObject().value("kind") != "delay"
            && effect.toObject().value("kind") != "personality_target"
            && effect.toObject().value("kind") != "cardask_no_jink") return fail("AI_STRATEGY_INVALID_EFFECTS");
    if (m_room && !aiRound44RangeWorldCurrent(*m_room,world)) return fail("AI_STRATEGY_STALE");
    LuaRuntime::Binding binding(modeRuntime);
    ExecutionBinding executionBinding(*this);
    lua_State *state = modeRuntime.state();
    const int top = lua_gettop(state);
    const auto restore = qScopeGuard([state, top] { lua_settop(state, top); });
    lua_getglobal(state, "sgs");
    if (!lua_istable(state, -1)) return fail("AI_STRATEGY_MODE_UNAVAILABLE");
    lua_getfield(state, -1, "commitModeAIIntentions");
    lua_getfield(state, -2, "prepareModeAIIntentions");
    lua_remove(state, top + 1);
    if (!lua_isfunction(state, -1) || !lua_isfunction(state, -2)) return fail("AI_STRATEGY_MODE_UNAVAILABLE");
    pushAIWorldView(state, world);
    lua_newtable(state);
    pushAIJsonValue(state, effects);
    const auto oldHook = lua_gethook(state); const int oldMask = lua_gethookmask(state), oldCount = lua_gethookcount(state);
    m_instructionsRemaining = m_instructionBudget; m_instructionLimitExceeded = false;
    lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
    const int status = LuaRuntime::protectedCall(state, 3, 2, 0);
    lua_sethook(state, oldHook, oldMask, oldCount);
    QJsonArray presentation;
    if (status || m_instructionLimitExceeded || !readAIArray(state, -1, presentation, 128))
        return fail("AI_STRATEGY_MODE_ERROR");
    lua_pop(state, 1);
    if (!lua_istable(state, -1) || !validateStrategyEffects(presentation, world, 0))
        return fail("AI_STRATEGY_MODE_ERROR");
    if (m_room && !aiRound44RangeWorldCurrent(*m_room,world)) return fail("AI_STRATEGY_STALE");
    if (LuaRuntime::protectedCall(state, 1, 1, 0) != 0 || !lua_isnil(state, -1)) return fail("AI_STRATEGY_COMMIT_ERROR");
    publishPresentation(m_room, presentation);
    return true;
}

bool AiLuaRuntime::processEvent(const AIWorldView &world, const AIEventView &event,
                                LuaRuntime &modeRuntime, QString *error)
{
    if(m_room)observeAIChoiceContinuityEvent(*m_room,world.self.objectName,event.details.value("player").toString());
    if (error)
        error->clear();
    const auto fail = [error](const QString &code) {
        if (error)
            *error = code;
        return false;
    };
    // A mode hook may enter native gameplay code and synchronously emit another
    // event. Reject that event before an ExecutionBinding or budget is replaced.
    if (current() != nullptr)
        return fail(QStringLiteral("AI_EVENT_REENTRANT"));
    if (!m_lua.rawState() || !modeRuntime.rawState())
        return fail(QStringLiteral("AI_EVENT_RUNTIME_UNAVAILABLE"));

    m_instructionsRemaining = m_instructionBudget;
    m_instructionLimitExceeded = false;
    int eventQueryAbort = LUA_NOREF;
    bool eventQuerySession = false;
    const auto abortQueries = qScopeGuard([&] {
        m_activeEventRequest = nullptr;
        m_activePeiqiEvent = nullptr;
        m_activeEventMode = nullptr;
        if (eventQueryAbort == LUA_NOREF) return;
        LuaRuntime::Binding queryBinding(modeRuntime);
        lua_State *queryState = modeRuntime.state();
        const int queryTop = lua_gettop(queryState);
        lua_rawgeti(queryState, LUA_REGISTRYINDEX, eventQueryAbort);
        if (lua_isfunction(queryState, -1)) LuaRuntime::protectedCall(queryState, 0, 0, 0);
        lua_settop(queryState, queryTop);
        luaL_unref(queryState, LUA_REGISTRYINDEX, eventQueryAbort);
    });
    AIRequest eventRequest;
    eventRequest.viewerObjectName = world.self.objectName;
    eventRequest.stateRevision = world.revision;
    eventRequest.worldView = world;
    if (world.modePolicy.value("standard_identity").toBool()
        || world.modePolicy.value("legacy_managed").toBool()
        || world.modePolicy.value("legacy_base_update").toBool()) {
        LuaRuntime::Binding queryBinding(modeRuntime);
        ExecutionBinding executionBinding(*this);
        lua_State *queryState = modeRuntime.state();
        const int queryTop = lua_gettop(queryState);
        const auto queryStack = qScopeGuard([&] { lua_settop(queryState, queryTop); });
        lua_getglobal(queryState, "sgs");
        if (!lua_istable(queryState, -1)) return fail(QStringLiteral("AI_EVENT_MODE_QUERY_UNAVAILABLE"));
        lua_getfield(queryState, -1, "abortStandardEventQueries");
        if (!lua_isfunction(queryState, -1)) return fail(QStringLiteral("AI_EVENT_MODE_QUERY_UNAVAILABLE"));
        eventQueryAbort = luaL_ref(queryState, LUA_REGISTRYINDEX);
        lua_getfield(queryState, -1, "beginStandardEventQueries");
        if (!lua_isfunction(queryState, -1)) return fail(QStringLiteral("AI_EVENT_MODE_QUERY_UNAVAILABLE"));
        pushAIWorldView(queryState, world);
        const lua_Hook oldHook = lua_gethook(queryState);
        const int oldMask = lua_gethookmask(queryState), oldCount = lua_gethookcount(queryState);
        lua_sethook(queryState, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
        const int queryStatus = LuaRuntime::protectedCall(queryState, 1, 1, 0);
        lua_sethook(queryState, oldHook, oldMask, oldCount);
        if (queryStatus || !lua_isboolean(queryState, -1) || !lua_toboolean(queryState, -1))
            return fail(QStringLiteral("AI_EVENT_MODE_QUERY_START_ERROR"));
        eventQuerySession = true;
        eventRequest.viewerObjectName = world.self.objectName;
        eventRequest.stateRevision = world.revision;
        eventRequest.worldView = world;
    }

    QList<AiIntentionDelta> deltas;
    QJsonArray effects, eventEstimates, callbackDiagnostics;
    QString failure;
    bool rebuild = false;
    int memoryCommit = LUA_NOREF, memoryAbort = LUA_NOREF;
    // Keep the exact trusted hooks across policy callbacks and both VM phases.
    const auto finishMemory = [&](bool commit) {
        LuaRuntime::Binding memoryBinding(m_lua);
        lua_State *memoryState = m_lua.state();
        const int memoryTop = lua_gettop(memoryState);
        lua_rawgeti(memoryState, LUA_REGISTRYINDEX, commit ? memoryCommit : memoryAbort);
        const bool ok = lua_isfunction(memoryState, -1)
            && LuaRuntime::protectedCall(memoryState, 0, 0, 0) == 0;
        lua_settop(memoryState, memoryTop);
        luaL_unref(memoryState, LUA_REGISTRYINDEX, memoryCommit);
        luaL_unref(memoryState, LUA_REGISTRYINDEX, memoryAbort);
        memoryCommit = memoryAbort = LUA_NOREF;
        return ok;
    };
    {
        LuaRuntime::Binding binding(m_lua);
        ExecutionBinding executionBinding(*this);
        lua_State *state = m_lua.state();
        const int top = lua_gettop(state);
        // Capture the trusted bounded diagnostic getter before callbacks can
        // replace globals. It returns audit data only, never strategy effects.
        lua_getglobal(state, "ai_event_callback_diagnostics");
        const int diagnosticsRef = lua_isfunction(state, -1)
            ? luaL_ref(state, LUA_REGISTRYINDEX) : LUA_NOREF;
        if (diagnosticsRef == LUA_NOREF) lua_pop(state, 1);
        const auto releaseDiagnostics = qScopeGuard([&] {
            if (diagnosticsRef != LUA_NOREF) luaL_unref(state, LUA_REGISTRYINDEX, diagnosticsRef);
        });
        lua_getglobal(state, "ai_event_commit");
        if (!lua_isfunction(state, -1)) { lua_settop(state, top); return fail(QStringLiteral("AI_EVENT_MEMORY_UNAVAILABLE")); }
        memoryCommit = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_getglobal(state, "ai_event_abort");
        if (!lua_isfunction(state, -1)) {
            luaL_unref(state, LUA_REGISTRYINDEX, memoryCommit);
            lua_settop(state, top); return fail(QStringLiteral("AI_EVENT_MEMORY_UNAVAILABLE"));
        }
        memoryAbort = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_getglobal(state, "ai_event");
        if (!lua_isfunction(state, -1)) {
            lua_settop(state, top);
            finishMemory(false);
            return fail(QStringLiteral("AI_EVENT_UNHANDLED"));
        }
        pushAIWorldView(state, world);
        pushAIEvent(state, event);
        lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
        eventRequest.choiceOptions.context.insert("target_recommend_event_actor", event.details.value("player"));
        eventRequest.choiceOptions.context.insert("target_recommend_event_death", event.triggerEvent == int(Death));
        eventRequest.choiceOptions.context.insert("r40_oljijiang_event",aiRound40OLJijiangEvent(event));
        const auto chuliEvent=aiRound53ChuliEvent(event);
        if(!chuliEvent.isEmpty())eventRequest.choiceOptions.context.insert("r53_chuli_event",chuliEvent);
        m_activeEventRequest = &eventRequest;
        m_activePeiqiEvent = &event;
        if(m_room) {
            beginAIRound29ChoicePolicy(*m_room,eventRequest.viewerObjectName);
            beginAIRound30TargetRevises(*m_room,eventRequest.viewerObjectName);
        }
        m_sourceCompatibility.reset(m_room ? new AIRound24SourceSession(*m_room,eventRequest,event.triggerEvent,event.kind) : nullptr);
        const auto clearSourceCompatibility=qScopeGuard([this]{m_sourceCompatibility.reset();});
        m_activeEventMode = eventQuerySession ? &modeRuntime : nullptr;
        const int status = LuaRuntime::protectedCall(state, 2, 3, 0);
        m_activeEventRequest = nullptr;
        m_activePeiqiEvent = nullptr;
        m_activeEventMode = nullptr;
        lua_sethook(state, nullptr, 0, 0);
        if (status != 0 || m_instructionLimitExceeded) {
            failure = m_instructionLimitExceeded ? QStringLiteral("AI_EVENT_INSTRUCTION_LIMIT")
                : status == LUA_ERRMEM ? QStringLiteral("AI_EVENT_MEMORY_LIMIT")
                                      : QStringLiteral("AI_EVENT_RUNTIME_ERROR");
            if (status != 0 && !m_instructionLimitExceeded && lua_istable(state, -1)
                && lua_getmetatable(state, -1)) {
                lua_getglobal(state, "AIUnsupported");
                if (lua_rawequal(state, -1, -2)) failure = QStringLiteral("AI_EVENT_UNSUPPORTED");
                lua_pop(state, 2);
            }
            rebuild = status == LUA_ERRMEM || m_instructionLimitExceeded;
        } else {
            bool validEstimates = readAIArray(state, -1, eventEstimates, 256);
            for (const auto &value : eventEstimates) if (!value.isString()) validEstimates = false;
            lua_pop(state, 1);
            const bool validEffects = validEstimates && readAIArray(state, -1, effects, 64);
            lua_pop(state, 1);
            if (lua_isnil(state, -1)) failure = QStringLiteral("AI_EVENT_UNHANDLED");
            else if (!validEffects || !parseIntentionDeltas(state, world, deltas)
                || !validateStrategyEffects(effects, world, deltas.size()))
                failure = QStringLiteral("AI_EVENT_INVALID_RESULT");
        }
        if (diagnosticsRef != LUA_NOREF && status != LUA_ERRMEM && !m_instructionLimitExceeded) {
            const int resultTop = lua_gettop(state);
            lua_rawgeti(state, LUA_REGISTRYINDEX, diagnosticsRef);
            bool validDiagnostics = LuaRuntime::protectedCall(state, 0, 1, 0) == 0
                && readAIArray(state, -1, callbackDiagnostics, 64);
            for (const auto &entry : callbackDiagnostics) {
                const auto row = entry.toObject();
                const QString classification = row.value("classification").toString();
                validDiagnostics = validDiagnostics && entry.isObject()
                    && row.value("kind") == QJsonValue("event_callback_failure")
                    && (classification == "ordinary_error" || classification == "typed_unsupported"
                        || classification == "resource_limit")
                    && row.value("callback").isString() && row.value("callback").toString().size() <= 160
                    && row.value("reason").isString() && row.value("reason").toString().size() <= 512
                    && row.value("after_delta").isDouble() && row.value("after_effect").isDouble();
            }
            lua_settop(state, resultTop);
            if (!validDiagnostics && failure.isEmpty()) failure = QStringLiteral("AI_EVENT_INVALID_DIAGNOSTICS");
        }
        lua_settop(state, top);
        if (m_lua.exceedsSoftMemoryLimit()) {
            lua_gc(state, LUA_GCCOLLECT, 0);
            if (m_lua.exceedsSoftMemoryLimit()) {
                failure = QStringLiteral("AI_EVENT_MEMORY_LIMIT");
                rebuild = true;
            }
        }
    }
    if (!callbackDiagnostics.isEmpty())
        qWarning().noquote() << "AI event callback diagnostics" << event.kind << "viewer" << world.self.objectName
            << "lua_outcome" << (failure.isEmpty() ? QStringLiteral("ready_for_commit") : failure)
            << "callbacks" << callbackDiagnostics;
    if (!failure.isEmpty()) finishMemory(false);
    if (rebuild) {
        // Rebuild once for future work, never replay this event or commit its deltas.
        shutdown();
        QString rebuildError;
        if (!initialize(&rebuildError))
            failure += QStringLiteral(":REBUILD_FAILED");
    }
    if (!failure.isEmpty())
        return fail(failure);
    if (!eventEstimates.isEmpty() && Config.value(QStringLiteral("AiDecisionTrace"), false).toBool())
        qInfo().noquote() << "AI event" << event.kind << "viewer" << world.self.objectName
            << "outcome estimated" << "estimate_count" << eventEstimates.size()
            << "branches" << eventEstimates;
    std::unique_ptr<AIStrategyHistoryWrite> historyWrite;
    for (const auto &value : effects) {
        const auto effect = value.toObject();
        if (effect.value("kind") != "ai_data") continue;
        if (historyWrite || !m_room) {
            finishMemory(false);
            return fail(QStringLiteral("AI_HISTORY_INVALID_BATCH"));
        }
        QString historyError;
        historyWrite = prepareAIStrategyHistory(world, *m_room, effect.value("revision").toString(),
            effect.value("data").toObject(), &historyError);
        if (!historyWrite) {
            if (historyError == "AI_HISTORY_BUSY" || historyError == "AI_HISTORY_STALE"
                || historyError == "AI_HISTORY_STAGE_FAILED" || historyError == "AI_HISTORY_READ_FAILED") {
                // Like SetAiData's failed file open, a cache write failure must
                // not replay or discard an otherwise valid policy event.
                qWarning().noquote() << "AI history persistence skipped:" << historyError;
            } else {
                finishMemory(false);
                return fail(historyError);
            }
        }
    }
    if (deltas.isEmpty() && effects.isEmpty() && !eventQuerySession)
        return finishMemory(true) || fail(QStringLiteral("AI_EVENT_MEMORY_COMMIT_ERROR"));

    LuaRuntime::Binding binding(modeRuntime);
    ExecutionBinding executionBinding(*this);
    lua_State *state = modeRuntime.state();
    const int top = lua_gettop(state);
    lua_getglobal(state, "sgs");
    if (!lua_istable(state, -1)) {
        lua_settop(state, top);
        finishMemory(false);
        return fail(QStringLiteral("AI_EVENT_MODE_UNAVAILABLE"));
    }
    // Keep the commit function below the preparation call. A policy callback
    // cannot replace the function the host will invoke after its budget stops.
    lua_getfield(state, -1, "commitModeAIIntentions");
    if (!lua_isfunction(state, -1)) {
        lua_settop(state, top);
        finishMemory(false);
        return fail(QStringLiteral("AI_EVENT_MODE_UNAVAILABLE"));
    }
    lua_getfield(state, -2, "prepareModeAIIntentions");
    lua_remove(state, top + 1); // sgs; leave commit, prepare on the stack.
    if (!lua_isfunction(state, -1)) {
        lua_settop(state, top);
        finishMemory(false);
        return fail(QStringLiteral("AI_EVENT_MODE_UNAVAILABLE"));
    }
    pushAIWorldView(state, world);
    lua_createtable(state, deltas.size(), 0);
    for (int index = 0; index < deltas.size(); ++index) {
        const AiIntentionDelta &delta = deltas.at(index);
        lua_createtable(state, 0, 3);
        setStringField(state, "from", delta.from);
        setStringField(state, "to", delta.to);
        lua_pushnumber(state, delta.level);
        lua_setfield(state, -2, "level");
        if (delta.deferRefreshPresent) {
            lua_pushboolean(state, delta.deferRefresh);
            lua_setfield(state, -2, "defer_refresh");
        }
        lua_rawseti(state, -2, index + 1);
    }
    pushAIJsonValue(state, effects);
    // Only preparation shares the remaining event budget. It returns an opaque
    // Room-VM token without changing the mind, even if interrupted at its return.
    const lua_Hook previousHook = lua_gethook(state);
    const int previousMask = lua_gethookmask(state);
    const int previousCount = lua_gethookcount(state);
    lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
    const int status = LuaRuntime::protectedCall(state, 3, 2, 0);
    lua_sethook(state, nullptr, 0, 0);
    bool accepted = false;
    QJsonArray presentation;
    const bool validPresentation = status == 0 && readAIArray(state, -1, presentation, 128)
        && validateStrategyEffects(presentation, world, deltas.size());
    if (status == 0) lua_pop(state, 1);
    if (validPresentation && lua_istable(state, -1) && !m_instructionLimitExceeded) {
        if (m_room && m_room->roomRuntime()
            && m_room->roomRuntime()->stateRevision() != world.revision) {
            // A mode hook that entered native gameplay cannot commit an inference
            // derived from the board that existed before its mutation.
            failure = QStringLiteral("AI_EVENT_MODE_STALE");
        } else {
            // Stack is commit, prepared token. Commit contains no policy callbacks
            // or loops, and cannot time out after replacing a successfully staged mind.
            const int commitStatus = LuaRuntime::protectedCall(state, 1, 1, 0);
            accepted = commitStatus == 0 && lua_isnil(state, -1);
        }
    }
    lua_sethook(state, previousHook, previousMask, previousCount);
    if (!accepted && failure.isEmpty()) {
        failure = m_instructionLimitExceeded ? QStringLiteral("AI_EVENT_MODE_INSTRUCTION_LIMIT")
                                            : QStringLiteral("AI_EVENT_MODE_ERROR");
    }
    lua_settop(state, top);
    if (!finishMemory(accepted)) {
        if (accepted) return fail(QStringLiteral("AI_EVENT_MEMORY_COMMIT_ERROR"));
    }
    if (accepted) {
        if (event.details.value("strategy_marks_cleared").isArray()) m_speechCounters = {};
        for (const auto &value : effects) {
            const auto effect = value.toObject();
            if (effect.value("kind") != "speech_counter") continue;
            const QString player = effect.value("player").toString();
            auto row = m_speechCounters.value(player).toObject();
            row.insert(effect.value("name").toString(), effect.value("value"));
            m_speechCounters.insert(player, row);
        }
        if (historyWrite) {
            QString historyError;
            if (!historyWrite->commit(&historyError))
                qWarning().noquote() << "AI history persistence failed after event commit:" << historyError;
        }
        publishPresentation(m_room, presentation);
    }
    return accepted || fail(failure);
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

int AiLuaRuntime::luaResolveConversion(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "conversion authority unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128) {
        pushAIJsonValue(state, QJsonObject{{"error", "conversion query budget exceeded"}, {"key", "conversion_policy"}});
        return 1;
    }
    QString className; int scope;
    if ((lua_gettop(state) != 3 && lua_gettop(state) != 4) || lua_type(state, 1) != LUA_TSTRING
        || !readBoundedString(state, 1, className) || lua_type(state, 2) != LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(state, 2), scope) || scope < 0)
        return luaL_error(state, "invalid conversion query");
    int budget = 4096; QJsonValue spec;
    if (!readAIJson(state, 3, spec, budget) || !spec.isObject())
        return luaL_error(state, "conversion spec must be plain values");
    if (runtime->m_room && runtime->m_room->roomRuntime()
        && !aiRound44RangeRequestCurrent(*runtime->m_room,request))
        return luaL_error(state, "stale conversion query");
    QJsonObject proposal{{"class_name", className}, {"scope_id", scope}, {"spec", spec}};
    if (lua_gettop(state)==4) {
        QJsonArray selected;
        if (!readAIArray(state,4,selected,128)) return luaL_error(state,"invalid selected material array");
        proposal.insert("selected_materials",selected);
    }
    pushAIJsonValue(state, runtime->m_conversionResolver(request,proposal));
    return 1;
}

int AiLuaRuntime::luaEvaluateSkillSeed(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "strategy seed unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    int scope; QString className;
    const int argc = lua_gettop(state);
    if ((argc != 2 && argc != 3 && argc != 4) || lua_type(state, 1) != LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(state, 1), scope) || scope < 1
        || lua_type(state, 2) != LUA_TSTRING || !readBoundedString(state, 2, className))
        return luaL_error(state, "invalid strategy seed query");
    QJsonObject proposal{{"kind", "evaluate_skill_seed"}, {"scope_id", scope}, {"class_name", className}};
    if (argc >= 3) {
        QJsonArray costs;
        if (!readAIArray(state, 3, costs, 128)) return luaL_error(state, "invalid readonly seed materials");
        proposal.insert("subcards", costs);
        if (argc == 4) {
            QString declaration;
            if (lua_type(state, 4) != LUA_TSTRING || !readBoundedString(state, 4, declaration))
                return luaL_error(state, "invalid readonly seed declaration");
            proposal.insert("user_string", declaration);
        }
    }
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "strategy seed budget exceeded");
    pushAIJsonValue(state, runtime->m_conversionResolver(request, proposal));
    return 1;
}

int AiLuaRuntime::luaQiuxinTargetFilter(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver
        || lua_gettop(state) != 3 || lua_type(state, 1) != LUA_TSTRING || lua_type(state, 3) != LUA_TSTRING)
        return luaL_error(state, "Qiuxin target query requires the active request");
    QString recipe, target;
    QJsonArray prefix;
    if (!readBoundedString(state, 1, recipe) || recipe.isEmpty()
        || !readBoundedString(state, 3, target) || target.isEmpty() || !readAIArray(state, 2, prefix, 128))
        return luaL_error(state, "invalid Qiuxin target query");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "Qiuxin target query budget exceeded");
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "qiuxin_target_filter"}, {"recipe_key", recipe}, {"prefix", prefix}, {"target", target}}));
    return 1;
}

int AiLuaRuntime::luaEvaluateOwnerRangeCard(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver
        || (!AIOwnerRangeQueryScope::isActive() && !AIRound25OwnerDistanceScope::active()
            && !aiRound44RangeContinuation(*runtime->m_room,*runtime->m_activeRequest))
        || lua_gettop(state) != 1 || !lua_istable(state, 1))
        return luaL_error(state, "owner range card requires an active hypothesis");
    const AIRequest &request = *runtime->m_activeRequest;
    QJsonValue spec; int budget = 2048;
    if (!readAIJson(state, 1, spec, budget) || !spec.isObject())
        return luaL_error(state, "invalid owner range card spec");
    QJsonObject object = spec.toObject();
    const bool attest=object.size()==1 && object.value("session_current")==true;
    if (!request.conversionLedger || (!attest && ++request.conversionLedger->queryCount > 128))
        return luaL_error(state,"owner range card budget exceeded");
    for (const char *key : {"flags", "target_prefix"}) {
        lua_getfield(state, 1, key);
        if (!lua_isnil(state, -1)) {
            QJsonArray values;
            if (!readAIArray(state, -1, values, 128)) {
                lua_pop(state, 1);
                return luaL_error(state, "invalid range array");
            }
            object.insert(QString::fromLatin1(key), values);
        }
        lua_pop(state, 1);
    }
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "evaluate_owner_range_card"}, {"spec", object}}));
    return 1;
}

int AiLuaRuntime::luaEvaluatePreviewCard(lua_State *state)
{
    auto *runtime=fromUpvalue(state); int id;
    if (!runtime || current()!=runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver
        || lua_gettop(state)!=1 || lua_type(state,1)!=LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(state,1),id) || id<0)
        return luaL_error(state,"preview evaluation requires current disclosed card");
    const auto &request=*runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount>128)
        return luaL_error(state,"preview query budget exceeded");
    pushAIJsonValue(state,runtime->m_conversionResolver(request,{{"kind","evaluate_preview_card"},{"card_id",id}}));
    return 1;
}

int AiLuaRuntime::luaBindPreviewPhysical(lua_State *state)
{
    auto *runtime=fromUpvalue(state); int scope,id;
    if (!runtime || current()!=runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver
        || lua_gettop(state)!=2 || lua_type(state,1)!=LUA_TNUMBER || lua_type(state,2)!=LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(state,1),scope) || scope<1
        || !aiResultInteger(lua_tonumber(state,2),id) || id<0)
        return luaL_error(state,"preview binding requires current source and card");
    const auto &request=*runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount>128)
        return luaL_error(state,"preview binding budget exceeded");
    pushAIJsonValue(state,runtime->m_conversionResolver(request,
        {{"kind","bind_preview_physical"},{"scope_id",scope},{"card_id",id}}));
    return 1;
}

int AiLuaRuntime::luaOriginalSDChoice(lua_State *L)
{
 auto *runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_room||!runtime->m_activeRequest||runtime->m_activeEventRequest
  ||lua_gettop(L)!=1)return luaL_error(L,"Original SD host requires one pure request under active decision");
 QJsonValue value;int budget=256;
 if(!readAIJson(L,1,value,budget)||!value.isObject())return luaL_error(L,"Original SD operation must be a bounded value object");
 const auto&q=*runtime->m_activeRequest;auto*p=runtime->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound37SD(*runtime->m_room,q,p,value.toObject()));return 1;
}

int AiLuaRuntime::luaOriginalCardChosenResult(lua_State *L)
{
 auto *runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest
  ||lua_gettop(L)!=1||lua_type(L,1)!=LUA_TNUMBER)
  return luaL_error(L,"Original CardChosen result requires its native numeric source result");
 const auto&q=*runtime->m_activeRequest;
 if(q.kind!=AIRequest::CardChosen||q.choiceOptions.question!="askForCardChosen"||!aiRound35OriginalQuestion(q))
  return luaL_error(L,"Original CardChosen result inquiry mismatch");
 // Exact fixed swig/ai.i461 conversion, including integral doubles and the
 // Lua API's own result for fractional/nonfinite numbers. No ID authority.
 lua_pushinteger(L,int(lua_tointeger(L,1)));return 1;
}

int AiLuaRuntime::luaOriginalGuanxingShadow(lua_State *L)
{
 auto *runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_room||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)
  ||lua_gettop(L)>1||(lua_gettop(L)==1&&!lua_isboolean(L,1)))return luaL_error(L,"Original Guanxing shadow requires active owner policy and optional boolean");
 const auto&q=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
 auto*p=runtime->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 QJsonObject result;
 if(lua_gettop(L)==1){
  if(runtime->m_activeEventRequest)return luaL_error(L,"Original Guanxing shadow event write rejected");
  result=writeAIRound36GuanxingShadow(*runtime->m_room,q,p,lua_toboolean(L,1));
 }else result=readAIRound36GuanxingShadow(*runtime->m_room,q,p);
 pushAIJsonValue(L,result);return 1;
}

int AiLuaRuntime::luaOriginalCrossbowShadow(lua_State *L)
{
 auto *runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_room||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)||lua_gettop(L)!=1||!lua_istable(L,1))
  return luaL_error(L,"Original Crossbow shadow requires an active owner policy and finite object");
 int budget=32;QJsonValue input;
 if(!readAIJson(L,1,input,budget)||!input.isObject())return luaL_error(L,"Original Crossbow input is not a finite object");
 const auto spec=input.toObject();const auto op=spec.value("op").toString();
 const auto &q=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
 auto *p=runtime->m_room->findPlayerByObjectName(q.viewerObjectName,true);QJsonObject result;
 if(op==QStringLiteral("read")&&spec.size()==1)result=readAIRound37CrossbowShadow(*runtime->m_room,q,p);
 else if(op==QStringLiteral("write")&&spec.size()==2&&spec.value("source").isObject()&&!runtime->m_activeEventRequest)
  result=writeAIRound37CrossbowShadow(*runtime->m_room,q,p,spec.value("source").toObject());
 else return luaL_error(L,"Original Crossbow operation or scope is not admitted");
 pushAIJsonValue(L,result);return 1;
}

int AiLuaRuntime::luaOriginalTurnUsePresence(lua_State *L)
{
 auto*runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_room||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)
  ||lua_gettop(L)>1||(lua_gettop(L)==1&&lua_type(L,1)!=LUA_TSTRING))return luaL_error(L,"Original toUse requires active owner policy and optional finite state");
 const auto&q=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
 auto*p=runtime->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 QString write;
 if(lua_gettop(L)==1){if(runtime->m_activeEventRequest)return luaL_error(L,"Original toUse event write rejected");size_t n=0;const char*s=lua_tolstring(L,1,&n);write=QString::fromUtf8(s,int(n));if(write!="empty"&&write!="nil"&&write!="unknown")return luaL_error(L,"invalid Original toUse state");}
 pushAIJsonValue(L,queryAIRound37OriginalTurnUse(*runtime->m_room,q,p,write));return 1;
}

int AiLuaRuntime::luaManbingFlag(lua_State *L)
{
 auto*runtime=fromUpvalue(L);
 const int argc=lua_gettop(L);
 if(!runtime||current()!=runtime||!runtime->m_room
  ||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)
  ||argc<1||argc>2||lua_type(L,1)!=LUA_TSTRING
  ||(argc==2&&(!lua_isboolean(L,2)||!lua_toboolean(L,2))))
  return luaL_error(L,"Manbing flag requires an active owner policy, target and optional true write");
 QString target;if(!readBoundedString(L,1,target)||target.isEmpty())return luaL_error(L,"invalid Manbing target");
 const auto&q=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
 const auto*p=runtime->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound38Manbing(*runtime->m_room,q,p,target,argc==2));return 1;
}

int AiLuaRuntime::luaOriginalKurouShadow(lua_State *L)
{
 auto*runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_room||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)||lua_gettop(L)>1||(lua_gettop(L)==1&&!lua_isboolean(L,1)))return luaL_error(L,"Original Kurou shadow requires active owner policy and optional boolean");
 const auto&q=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
 auto*p=runtime->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 QJsonObject result;
 if(lua_gettop(L)==1){if(runtime->m_activeEventRequest)return luaL_error(L,"Original Kurou event write rejected");result=writeAIRound36KurouShadow(*runtime->m_room,q,p,lua_toboolean(L,1));}
 else result=readAIRound36KurouShadow(*runtime->m_room,q,p);
 pushAIJsonValue(L,result);return 1;
}

int AiLuaRuntime::luaLianliCardaskError(lua_State *L)
{
    auto *runtime = fromUpvalue(L);
    if (!runtime || current() != runtime || !runtime->m_room || !runtime->m_activeRequest
        || runtime->m_activeEventRequest || lua_gettop(L) != 1)
        return luaL_error(L, "Lianli diagnostic requires active question");
    QString message;
    if (lua_type(L, 1) != LUA_TSTRING || !readBoundedString(L, 1, message))
        return luaL_error(L, "invalid Lianli source diagnostic");
    const auto &request = *runtime->m_activeRequest;
    auto *viewer = runtime->m_room->findPlayerByObjectName(request.viewerObjectName, true);
    pushAIJsonValue(L, reportAILianliCardaskError(*runtime->m_room, viewer, request, message));
    return 1;
}

int AiLuaRuntime::luaInovationXinyang(lua_State *L)
{
 auto*r=fromUpvalue(L);
 if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest
  ||lua_gettop(L)!=2||lua_type(L,1)!=LUA_TSTRING||!lua_isinteger(L,2))
  return luaL_error(L,"Xinyang source tag requires its actual current question");
 QString op;if(!readBoundedString(L,1,op))return luaL_error(L,"invalid Xinyang operation");
 const lua_Integer id=lua_tointeger(L,2);if(id<-1||id>std::numeric_limits<int>::max())return luaL_error(L,"invalid Xinyang material");
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound45Xinyang(*r->m_room,q,p,op,int(id)));return 1;
}

int AiLuaRuntime::luaSourceImmediateEffect(lua_State*L){
 auto*r=fromUpvalue(L);
 if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest||lua_gettop(L)!=1)
  return luaL_error(L,"source effect requires actual current question");
 QJsonValue input;int budget=48;
 if(!readAIJson(L,1,input,budget)||!input.isObject())return luaL_error(L,"invalid finite source effect schema");
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,input.toObject().value("source")=="yohane"
  ?queryAIRound53Yohane(*r->m_room,q,p,input.toObject()):input.toObject().value("source")=="goodatwep"
  ?queryAIRound53Goodatwep(*r->m_room,q,p,input.toObject()):input.toObject().value("source")=="ordinary_collateral"
  ?queryAIRound53Collateral(*r->m_room,q,p,input.toObject()):queryAIRound50Effects(*r->m_room,q,p,input.toObject()));return 1;
}

int AiLuaRuntime::luaInovationRemaining(lua_State *L)
{
 auto*r=fromUpvalue(L);
 if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest
  ||lua_gettop(L)!=2||lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING)
  return luaL_error(L,"Inovation finite source operation requires actual current question");
 QString op,name;if(!readBoundedString(L,1,op)||!readBoundedString(L,2,name))return luaL_error(L,"invalid Inovation finite source operation");
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound46Inovation(*r->m_room,q,p,op,name));return 1;
}

int AiLuaRuntime::luaInovationGoutong(lua_State *L)
{
 auto*r=fromUpvalue(L);if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest||lua_gettop(L)!=1||lua_type(L,1)!=LUA_TSTRING)
  return luaL_error(L,"Goutong source prefix requires the actual current request");
 QString op;if(!readBoundedString(L,1,op)||(op!="write"&&op!="read"))return luaL_error(L,"invalid Goutong operation");
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,op=="write"?writeAIRound45GoutongMove(*r->m_room,q,p):readAIRound45GoutongMove(*r->m_room,q,p));return 1;
}

int AiLuaRuntime::luaInovationShoushi(lua_State *L)
{
 auto*r=fromUpvalue(L);
 if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest
  ||lua_gettop(L)!=2||lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING)
  return luaL_error(L,"Shoushi source slots require their actual current question");
 QString op,name;if(!readBoundedString(L,1,op)||!readBoundedString(L,2,name))return luaL_error(L,"invalid Shoushi operation");
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound45Shoushi(*r->m_room,q,p,op,name));return 1;
}

int AiLuaRuntime::luaManjuanSource(lua_State *L)
{
 auto*r=fromUpvalue(L);if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest||lua_gettop(L)!=2||lua_type(L,1)!=LUA_TSTRING)return luaL_error(L,"Manjuan source requires actual owner request");
 QString op;QJsonValue input;int budget=64;
 if(!readBoundedString(L,1,op)||!readAIJson(L,2,input,budget)||!input.isObject())return luaL_error(L,"Manjuan finite argument shape invalid");
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,op.startsWith("printed_")?queryAIRound53ManjuanPrinted(*r->m_room,q,p,op,input.toObject()):queryAIRound51Manjuan(*r->m_room,q,p,op,input.toObject()));return 1;
}

int AiLuaRuntime::luaPeiqiSourceRecord(lua_State *L)
{
 auto*r=fromUpvalue(L);
 if(!r||current()!=r||!r->m_room||(!r->m_activeRequest&&!r->m_activeEventRequest)||lua_gettop(L)!=3||lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING)
  return luaL_error(L,"Peiqi source record requires current owner policy");
 if(bool(r->m_activeEventRequest)!=bool(r->m_activePeiqiEvent))return luaL_error(L,"Peiqi actual event lifetime missing");
 QString op,key;QJsonValue value;int budget=32;
 if(!readBoundedString(L,1,op)||!readBoundedString(L,2,key)||!readAIJson(L,3,value,budget)||!value.isObject())return luaL_error(L,"Peiqi source argument shape invalid");
 const auto&q=*(r->m_activeEventRequest?r->m_activeEventRequest:r->m_activeRequest);
 auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound53PeiqiRecord(*r->m_room,q,p,r->m_activePeiqiEvent,op,key,value.toObject()));return 1;
}

int AiLuaRuntime::luaDaojiSourceCard(lua_State *L)
{
 auto*r=fromUpvalue(L);if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest||lua_gettop(L)!=2||lua_type(L,1)!=LUA_TSTRING)return luaL_error(L,"Daoji source Card requires actual owner inquiry");
 QString op;QJsonValue value;int budget=256;
 if(!readBoundedString(L,1,op)||!readAIJson(L,2,value,budget)||!value.isObject())return luaL_error(L,"Daoji source arguments malformed");
 auto input=value.toObject();if(input.value("ids").isObject()&&input.value("ids").toObject().isEmpty())input.insert("ids",QJsonArray{});
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound53DaojiCard(*r->m_room,q,p,op,input));return 1;
}

int AiLuaRuntime::luaCurrentSourceSlot(lua_State *L)
{
 auto*r=fromUpvalue(L);if(!r||current()!=r||!r->m_room||(!r->m_activeRequest&&!r->m_activeEventRequest)||lua_gettop(L)!=3||lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING)return luaL_error(L,"source slot requires actual owner request");
 QString key,op;QJsonValue value;int budget=8192;
 if(!readBoundedString(L,1,key)||!readBoundedString(L,2,op)||!readAIJson(L,3,value,budget)||!value.isObject())return luaL_error(L,"source slot argument shape invalid");
 if(key=="yuanrongIds"&&op=="write"&&value.toObject().value("ids").isObject()&&value.toObject().value("ids").toObject().isEmpty()){auto o=value.toObject();o.insert("ids",QJsonArray{});value=o;}
 if(r->m_activeEventRequest&&key!="chuli_id_choice")return luaL_error(L,"source slot event operation outside finite Chuli reader");
 const auto&q=*(r->m_activeEventRequest?r->m_activeEventRequest:r->m_activeRequest);auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 if(key=="chuli_id_choice"){pushAIJsonValue(L,queryAIRound53ChuliMap(*r->m_room,q,p,op,value.toObject()));return 1;}
 if((key=="cl_to"||key=="youlong_to"||key=="secondjinzhi_to"||key=="fengying_to"||key=="fuping_to"||key=="jiusiTo"||key=="fuhui_to"||key=="jg_to"||key=="mx_to"||key=="shouli_to")&&op=="write"&&value.toObject().value("type")=="players"&&value.toObject().value("value").isObject()&&value.toObject().value("value").toObject().isEmpty()){auto o=value.toObject();o.insert("value",QJsonArray{});value=o;}
 if(key=="luochong_to"||key=="cl_to"||key=="diting_to"||key=="youlong_to"||key=="secondjinzhi_to"||key=="xiaoxinf_to"||key=="fengying_to"||key=="fuping_to"||key=="jiusiTo"||key=="fuhui_to"||key=="jg_to"||key=="mx_to"||key=="shouli_to"||key=="tj_to"||key=="shouxingTo"||key=="geassTarget"||key=="tychendeTo")pushAIJsonValue(L,queryAIRound53TargetSlot(*r->m_room,q,p,key,op,value.toObject()));
 else pushAIJsonValue(L,queryAIRound50SourceSlot(*r->m_room,q,p,key,op,value.toObject()));return 1;
}

int AiLuaRuntime::luaPoxiSourceSlot(lua_State *L)
{
 auto*r=fromUpvalue(L);
 if(!r||current()!=r||!r->m_room||!r->m_activeRequest||r->m_activeEventRequest
  ||lua_gettop(L)!=2||lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING)
  return luaL_error(L,"Poxi source slot requires actual current owner question");
 QString op,name;
 if(!readBoundedString(L,1,op)||!readBoundedString(L,2,name))return luaL_error(L,"invalid Poxi source slot arguments");
 const auto&q=*r->m_activeRequest;auto*p=r->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound45PoxiSlot(*r->m_room,q,p,op,name));return 1;
}

int AiLuaRuntime::luaInovationSourceDiagnostic(lua_State *L)
{
 auto*runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_room||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)||lua_gettop(L)!=1)
  return luaL_error(L,"Inovation diagnostic requires active policy");
 QString key;if(lua_type(L,1)!=LUA_TSTRING||!readBoundedString(L,1,key))return luaL_error(L,"invalid Inovation diagnostic key");
 const auto&request=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
 auto*viewer=runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true);
 if(key=="shoushi_missing"){
  if(!runtime->m_activeRequest||runtime->m_activeEventRequest){lua_pushboolean(L,false);return 1;}
  const auto row=queryAIRound45Shoushi(*runtime->m_room,request,viewer,"read",QString());
  if(row.value("known")!=QJsonValue(true)||row.value("value")!=QJsonValue(false)){lua_pushboolean(L,false);return 1;}
  runtime->m_room->output(QStringLiteral("shoushi player chosen error!!"));qWarning("%s","shoushi player chosen error!!");lua_pushboolean(L,true);return 1;
 }
 const QString method=key=="jijiang_traceback"?QStringLiteral("getJijiangSlashNum"):
  ((key=="lijian_looking"||key=="lijian_unfound")?QStringLiteral("findLijianTarget"):QString());
 if(method.isEmpty()||!aiRound44RangeRequestCurrent(*runtime->m_room,request,viewer)
  ||!aiRound45InovationProviderKnown(*runtime->m_room,viewer,method)){lua_pushboolean(L,false);return 1;}
 if(key=="jijiang_traceback"){
  // Same traceback operation / console sink order; this VM's file and line
  // necessarily differ and remain an explicit diagnostic-only boundary.
  luaL_traceback(L,L,nullptr,1);const QString text=QString::fromUtf8(lua_tostring(L,-1));lua_pop(L,1);
  runtime->m_room->output(text);qWarning("%s",qPrintable(text));
 }else runtime->m_room->output(key=="lijian_looking"?QStringLiteral("Looking for the friend!"):QStringLiteral("unfound"));
 lua_pushboolean(L,true);return 1;
}

int AiLuaRuntime::luaImagineSourceDiagnostic(lua_State *L)
{
    auto *runtime=fromUpvalue(L);
    if(!runtime||current()!=runtime||!runtime->m_room
        ||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)||lua_gettop(L)!=2)
        return luaL_error(L,"Imagine diagnostic requires active policy");
    QString site,message;
    if(lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING
        ||!readBoundedString(L,1,site)||!readBoundedString(L,2,message))
        return luaL_error(L,"invalid Imagine diagnostic arguments");
    const auto &request=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
    auto *viewer=runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true);
    lua_pushboolean(L,emitAIRound44ImagineDiagnostic(*runtime->m_room,request,viewer,site,message));
    return 1;
}

int AiLuaRuntime::luaOriginalSourceWarning(lua_State *L)
{
    auto *runtime=fromUpvalue(L);
    if(!runtime||current()!=runtime||!runtime->m_room||!runtime->m_activeRequest
        ||runtime->m_activeEventRequest||lua_gettop(L)!=2)return luaL_error(L,"Original diagnostic requires active question");
    QString source,message;
    if(lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING
        ||!readBoundedString(L,1,source)||!readBoundedString(L,2,message))
        return luaL_error(L,"invalid Original diagnostic text");
    const auto &request=*runtime->m_activeRequest;
    auto *viewer=runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true);
    lua_pushboolean(L,source=="inovation.jizhanshiz"
        ? emitAIRound43InovationWarning(*runtime->m_room,request,viewer,source,message)
        : emitAIRound35SourceWarning(*runtime->m_room,request,viewer,source,message));
    return 1;
}

int AiLuaRuntime::luaTargetRecommendCache(lua_State *L)
{
    auto *runtime = fromUpvalue(L);
    if (!runtime || current() != runtime || !runtime->m_room
        || (!runtime->m_activeRequest && !runtime->m_activeEventRequest) || lua_gettop(L) != 1)
        return luaL_error(L, "target recommendation cache requires active policy");
    QJsonValue value; int budget = 24;
    if (!readAIJson(L, 1, value, budget) || !value.isObject())
        return luaL_error(L, "invalid target recommendation cache proposal");
    const QJsonObject proposal = value.toObject();
    const auto &request = *(runtime->m_activeEventRequest ? runtime->m_activeEventRequest : runtime->m_activeRequest);
    auto *viewer = runtime->m_room->findPlayerByObjectName(request.viewerObjectName, true);
    if(proposal.value("op")=="source_connections"||proposal.value("op")=="source_alive"||proposal.value("op")=="source_skills") {
        pushAIJsonValue(L,runtime->m_activeEventRequest?QJsonObject{{"applicable",false}}
            :queryAIRound56ConnectionCache(*runtime->m_room,request,viewer,proposal));
        return 1;
    }
    if (runtime->m_activeEventRequest && proposal.value("op") != "epoch"
        && request.choiceOptions.context.value("target_recommend_event_actor").toString() != request.viewerObjectName) {
        pushAIJsonValue(L, QJsonObject{{"known",false},{"reason","target cache event observer is not the source filterEvent actor"}});
        return 1;
    }
    if (proposal.value("op") == "death_clear") {
        if (proposal.size() != 1 || !runtime->m_activeEventRequest
            || !request.choiceOptions.context.value("target_recommend_event_death").toBool())
            return luaL_error(L,"target recommendation Death clear requires source Death phase");
        resetAIRound30TargetCache(*runtime->m_room);
        pushAIJsonValue(L, queryAIRound30TargetCache(*runtime->m_room, request, viewer, {{"op","read"}}));
        return 1;
    }
    pushAIJsonValue(L, queryAIRound30TargetCache(*runtime->m_room, request, viewer, proposal));
    return 1;
}

int AiLuaRuntime::luaTargetReviseProperty(lua_State *L)
{
    auto *runtime = fromUpvalue(L);
    if (!runtime || current() != runtime || !runtime->m_room
        || (!runtime->m_activeRequest && !runtime->m_activeEventRequest) || lua_gettop(L) != 1)
        return luaL_error(L, "target revise property requires active policy");
    QJsonValue value; int budget = 24;
    if (!readAIJson(L, 1, value, budget) || !value.isObject())
        return luaL_error(L, "invalid target revise property proposal");
    const auto &request = *(runtime->m_activeEventRequest ? runtime->m_activeEventRequest : runtime->m_activeRequest);
    auto *viewer = runtime->m_room->findPlayerByObjectName(request.viewerObjectName, true);
    pushAIJsonValue(L, queryAIRound30TargetRevises(*runtime->m_room, request, viewer, value.toObject()));
    return 1;
}

int AiLuaRuntime::luaEvaluateResponsePolicy(lua_State *L)
{
    auto *runtime=fromUpvalue(L); int scope;
    QString className, registry, hookKey;
    if (!runtime || current()!=runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver
        || lua_gettop(L)!=6 || lua_type(L,1)!=LUA_TSTRING || lua_type(L,2)!=LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(L,2),scope) || scope<1 || lua_type(L,3)!=LUA_TSTRING
        || lua_type(L,4)!=LUA_TSTRING || lua_type(L,6)!=LUA_TBOOLEAN
        || !readBoundedString(L,1,className) || !readBoundedString(L,3,registry) || !readBoundedString(L,4,hookKey))
        return luaL_error(L,"response policy parser requires exact active callback scope");
    QJsonValue spec; int budget=2048;
    if (!readAIJson(L,5,spec,budget)) return luaL_error(L,"response parser input is not a bounded value");
    const AIRequest &request=*runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount>128)
        return luaL_error(L,"response parser query budget exceeded");
    pushAIJsonValue(L,runtime->m_conversionResolver(request,{{"kind","evaluate_response_policy"},
        {"class_name",className},{"scope_id",scope},{"registry",registry},{"hook_key",hookKey},
        {"spec",spec},{"attach_request",bool(lua_toboolean(L,6))}}));
    return 1;
}

int AiLuaRuntime::luaChoiceContinuity(lua_State *state)
{
 auto *runtime=fromUpvalue(state);
 if(!runtime||current()!=runtime||!runtime->m_room||!runtime->m_activeRequest
    ||runtime->m_activeEventRequest||lua_gettop(state)!=1)
  return luaL_error(state,"choice continuity requires one finite current-decision operation");
 int budget=32;QJsonValue value;
 if(!readAIJson(state,1,value,budget)||!value.isObject())return luaL_error(state,"invalid choice continuity operation");
 const AIRequest &request=*runtime->m_activeRequest;
 auto *viewer=runtime->m_room->findPlayerByObjectName(request.viewerObjectName);
 const auto operation=value.toObject();
 if(operation.value("key")=="xufa"){
  EngineRuntimeContextScope engine(*Sanguosha,runtime->m_room);
  QJsonObject out{{"known",false}};const auto op=operation.value("op").toString();
  if(op=="read"&&operation.size()==2)out=readAIRound53XufaChoice(*runtime->m_room,viewer,request);
  else if(op=="record"&&operation.size()==3&&operation.value("row").isObject()){
   const auto row=operation.value("row").toObject();
   if(row.size()==3&&row.value("known")==QJsonValue(true)&&row.value("present")==QJsonValue(true)&&row.value("value").isString())
    out=writeAIRound53XufaChoice(*runtime->m_room,viewer,request,row.value("value").toString());
  }
  pushAIJsonValue(state,out);return 1;
 }
 pushAIJsonValue(state,queryAIChoiceContinuity(*runtime->m_room,viewer,request,operation));return 1;
}

int AiLuaRuntime::luaResponseCatalogue(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver
        || (lua_gettop(state) != 1 && lua_gettop(state) != 3) || lua_type(state, 1) != LUA_TSTRING)
        return luaL_error(state, "response catalogue requires a current decision");
    QString key;
    if (!readBoundedString(state, 1, key)) return luaL_error(state, "invalid response catalogue key");
    QJsonObject query{{"kind","response_catalogue"},{"key",key}};
    const bool printedEffect=key=="sfofl_zhengan_basic_set"||key=="sfofl_zhengan_basic_available";
    if(printedEffect) {
        QString sequence;
        if(lua_gettop(state)!=3||lua_type(state,2)!=LUA_TSTRING||!readBoundedString(state,2,sequence)
           ||!lua_isinteger(state,3)||lua_tointeger(state,3)<1||lua_tointeger(state,3)>100000)
            return luaL_error(state,"printed source effect requires its issued sequence and ordinal");
        query.insert("sequence",sequence);query.insert("index",int(lua_tointeger(state,3)));
    } else if(lua_gettop(state)!=1)return luaL_error(state,"catalogue does not accept source-effect arguments");
    // The catalogue provider charges the same native query budget exactly once.
    pushAIJsonValue(state, runtime->m_conversionResolver(*runtime->m_activeRequest,
        query));
    return 1;
}

int AiLuaRuntime::luaWithOwnerRange(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_room || !runtime->m_activeRequest
        || runtime->m_activeEventRequest || (lua_gettop(state) != 3 && lua_gettop(state) != 4)
        || lua_type(state, 1) != LUA_TSTRING || lua_type(state, 2) != LUA_TSTRING
        || lua_type(state, 3) != LUA_TFUNCTION)
        return luaL_error(state, "owner range hypothesis requires a current decision and callback");
    const int arguments=lua_gettop(state);
    int status = LUA_OK;
    bool changed = false;
    {
        const AIRequest &request = *runtime->m_activeRequest;
        QString flag, sourceKey;
        if (!readBoundedString(state, 1, flag) || !readBoundedString(state, 2, sourceKey))
            return luaL_error(state, "owner range hypothesis name is invalid");
        ServerPlayer *viewer = runtime->m_room->findPlayerByObjectName(request.viewerObjectName, true);
        AIOwnerRangeQueryScope::Flag kind;
        if (!validateAIStrategyOwnerRange(*runtime->m_room, viewer, request, flag, sourceKey, kind))
            return luaL_error(state, "owner range hypothesis source/viewer/revision is invalid");
        if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
            return luaL_error(state, "owner range hypothesis budget exceeded");
        if(aiRound44RangeSource(flag,sourceKey)) {
            if(arguments!=4 || !aiRound44RangeRoute(*runtime->m_room,request,viewer,flag,sourceKey,state,4))
                return luaL_error(state,"range source closure/current inquiry is not authenticated");
            QString failure;QJsonObject projection;
            const auto transition=[&](bool value,bool project) {
                AIOwnerRangeQueryScope sourceScope(viewer,kind,true,sourceKey,true);
                EngineRuntimeContextScope engineContext(*Sanguosha,runtime->m_room);
                LuaRuntime::Binding nativeBinding(runtime->m_room->roomRuntime()->lua(),false);
                if(!writeAIRound44RangeFlag(*runtime->m_room,request,viewer,flag,sourceKey,value,&failure))return false;
                if(project)projection=projectAIStrategyOwnerRange(*runtime->m_room,viewer,flag,sourceKey);
                if(!aiRound44RangeRequestCurrent(*runtime->m_room,request,viewer)) {
                    failure=QStringLiteral("range projection changed game state");return false;
                }
                return true;
            };
            bool okay=transition(true,true);
            if(okay) {
                // Lua allocation occurs outside native RAII scopes.
                lua_pushvalue(state,3);pushAIJsonValue(state,projection);
                {
                    AIOwnerRangeQueryScope sourceScope(viewer,kind,true,sourceKey,true);
                    EngineRuntimeContextScope engineContext(*Sanguosha,runtime->m_room);
                    status=lua_pcall(state,1,LUA_MULTRET,0);
                }
                // Source errors retain true; normal nil/false still clear.
                if(status==LUA_OK)okay=transition(false,false);
            }
            if(!okay){lua_pushstring(state,failure.toUtf8().constData());status=LUA_ERRRUN;}
        } else {
        const QStringList originalFlags = viewer->getFlagList();
        QJsonObject projection;
        {
            EngineRuntimeContextScope engineContext(*Sanguosha, runtime->m_room);
            LuaRuntime::Binding nativeBinding(runtime->m_room->roomRuntime()->lua(), false);
            AIOwnerRangeQueryScope scope(viewer, kind);
            projection = projectAIStrategyOwnerRange(*runtime->m_room, viewer, flag, sourceKey);
        }
        if (request.stateRevision != runtime->m_room->roomRuntime()->stateRevision()
            || viewer->getFlagList() != originalFlags)
            return luaL_error(state, "owner range projection changed game state");
        // Allocate Lua arguments outside the RAII scope. lua_pcall catches all
        // callback errors (including budget/OOM); no longjmp skips its destructor.
        lua_pushvalue(state, 3);
        pushAIJsonValue(state, projection);
        {
            EngineRuntimeContextScope engineContext(*Sanguosha, runtime->m_room);
            AIOwnerRangeQueryScope scope(viewer, kind);
            status = lua_pcall(state, 1, LUA_MULTRET, 0);
        }
        changed = request.stateRevision != runtime->m_room->roomRuntime()->stateRevision()
            || viewer->getFlagList() != originalFlags;
        }
    }
    // Preserve the original error object, including branded AIUnsupported.
    if (status != LUA_OK) return lua_error(state);
    if (changed) return luaL_error(state, "owner range callback changed game state");
    return lua_gettop(state) - arguments;
}

int AiLuaRuntime::luaWithOwnerDistance(lua_State *state)
{
    auto *runtime=fromUpvalue(state);int delta;
    if(!runtime||current()!=runtime||!runtime->m_room||!runtime->m_activeRequest
        ||runtime->m_activeEventRequest||lua_gettop(state)!=3||lua_type(state,1)!=LUA_TSTRING
        ||lua_type(state,2)!=LUA_TNUMBER||!aiResultInteger(lua_tonumber(state,2),delta)
        ||lua_type(state,3)!=LUA_TFUNCTION)
        return luaL_error(state,"owner distance requires a current decision and callback");
    int status=LUA_OK;bool changed=false;
    {
        const AIRequest &request=*runtime->m_activeRequest;QString source;
        if(!readBoundedString(state,1,source))return luaL_error(state,"invalid distance source");
        auto *viewer=runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true);
        if(!validateAIRound25OwnerDistance(*runtime->m_room,viewer,request,source,delta))
            return luaL_error(state,"distance source/viewer/revision/delta is invalid");
        if(!request.conversionLedger||++request.conversionLedger->queryCount>128)
            return luaL_error(state,"owner distance query budget exceeded");
        const QStringList flags=viewer->getFlagList();
        const int mark=viewer->getMark("ExtraBfDistanceFrom-Clear");QJsonObject projection;
        {
            EngineRuntimeContextScope engineContext(*Sanguosha,runtime->m_room);
            LuaRuntime::Binding nativeBinding(runtime->m_room->roomRuntime()->lua(),false);
            AIRound25OwnerDistanceScope scope(viewer,delta);
            projection=projectAIRound25OwnerDistance(*runtime->m_room,viewer,source,delta);
        }
        if(request.stateRevision!=runtime->m_room->roomRuntime()->stateRevision()
            ||viewer->getFlagList()!=flags||viewer->getMark("ExtraBfDistanceFrom-Clear")!=mark)
            return luaL_error(state,"distance projection changed game state");
        lua_pushvalue(state,3);pushAIJsonValue(state,projection);
        {
            EngineRuntimeContextScope engineContext(*Sanguosha,runtime->m_room);
            AIRound25OwnerDistanceScope scope(viewer,delta);
            status=lua_pcall(state,1,LUA_MULTRET,0);
        }
        changed=request.stateRevision!=runtime->m_room->roomRuntime()->stateRevision()
            ||viewer->getFlagList()!=flags||viewer->getMark("ExtraBfDistanceFrom-Clear")!=mark;
    }
    if(status!=LUA_OK)return lua_error(state);
    if(changed)return luaL_error(state,"distance callback changed game state");
    return lua_gettop(state)-3;
}

int AiLuaRuntime::luaEvaluateTianyiSlash(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "Tianyi evaluation unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    int id;
    if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(state, 1), id)) return luaL_error(state, "invalid Tianyi card identity");
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "Tianyi evaluation budget exceeded");
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "evaluate_tianyi_slash"}, {"card_id", id}}));
    return 1;
}

int AiLuaRuntime::luaEvaluateZenhuiCard(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver
        || lua_gettop(state) != 0) return luaL_error(state, "Zenhui evaluation outside its decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "Zenhui evaluation budget exceeded");
    pushAIJsonValue(state, runtime->m_conversionResolver(request, {{"kind", "evaluate_zenhui_card"}}));
    return 1;
}

int AiLuaRuntime::luaConsumePlayerHint(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    QString name;
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_room
        || lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING
        || !readBoundedString(state, 1, name)) return luaL_error(state, "player hint outside active decision");
    const AIRequest &request = *runtime->m_activeRequest;
    auto *viewer = runtime->m_room->findPlayerByObjectName(request.viewerObjectName, true);
    pushAIJsonValue(state, consumeAIStrategyPlayerHint(*runtime->m_room, request, viewer, name));
    return 1;
}

int AiLuaRuntime::luaCardTargetFilter(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "target filter outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    int id = 0, budget = 512; QJsonValue prefix; QString target, recipe;
    const bool readonlyRecipe = lua_type(state,1) == LUA_TSTRING;
    const QString operation=lua_gettop(state)==4 && lua_type(state,4)==LUA_TSTRING ? QString::fromUtf8(lua_tostring(state,4)) : QString();
    const bool specialized=operation=="is_prohibited" || operation=="can_slash";
    if (lua_gettop(state) != 3+int(specialized) || (readonlyRecipe ? (!readBoundedString(state,1,recipe) || recipe.isEmpty())
            : (lua_type(state,1) != LUA_TNUMBER || !aiResultInteger(lua_tonumber(state,1),id))) || !readAIJson(state,2,prefix,budget)
        || lua_type(state,3) != LUA_TSTRING || !readBoundedString(state,3,target))
        return luaL_error(state, "invalid target filter query");
    if (prefix.isObject() && prefix.toObject().isEmpty()) prefix = QJsonArray();
    if (!prefix.isArray()) return luaL_error(state, "invalid target prefix");
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "target filter budget exceeded");
    QJsonObject query{{"kind", "card_target_filter"}, {"prefix",prefix}, {"target",target}};
    if (specialized) query.insert("operation",operation);
    query.insert(readonlyRecipe ? "recipe_key" : "card_id",
        readonlyRecipe ? QJsonValue(recipe) : QJsonValue(id));
    pushAIJsonValue(state, runtime->m_conversionResolver(request, query));
    return 1;
}

int AiLuaRuntime::luaOriginalAocaiPattern(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_room
        || (!runtime->m_activeRequest && !runtime->m_activeEventRequest) || lua_gettop(state) != 0)
        return luaL_error(state, "Aocai literal query outside active policy scope");
    QJsonObject result{{"kind", "original_aocai_pattern"}, {"known", false}};
    // Observers never inherit a prior decision's captured response context.
    if (runtime->m_activeEventRequest) {
        pushAIJsonValue(state, result);
        return 1;
    }
    const AIRequest &request = *(runtime->m_activeRequest ? runtime->m_activeRequest : runtime->m_activeEventRequest);
    if (request.stateRevision != runtime->m_room->roomRuntime()->stateRevision()
        || request.worldView.self.objectName != request.viewerObjectName)
        return luaL_error(state, "stale or mismatched Aocai literal query");
    const auto &captured = request.originalAocaiPattern;
    if (!runtime->m_activeEventRequest && runtime->m_activeRequest
        && (request.kind == AIRequest::RespondCard || request.kind == AIRequest::UseCard)
        && captured.value("viewer").toString() == request.viewerObjectName
        && captured.value("decision").toString() == request.getDecisionId()
        && captured.value("revision").toString() == request.getStateRevision()
        && captured.value("slash").isBool() && captured.value("jink").isBool()) {
        result.insert("known", true);
        result.insert("slash", captured.value("slash"));
        result.insert("jink", captured.value("jink"));
    }
    pushAIJsonValue(state, result);
    return 1;
}

int AiLuaRuntime::luaMatchOwnerDiscardPattern(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "owner discard pattern query unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "owner discard pattern query budget exceeded");
    if (lua_gettop(state) != 2 || lua_type(state, 1) != LUA_TSTRING || !lua_isinteger(state, 2))
        return luaL_error(state, "owner discard pattern requires string and physical ID");
    size_t length = 0;
    const char *pattern = lua_tolstring(state, 1, &length);
    const lua_Integer id = lua_tointeger(state, 2);
    if (!length || length > 1024 || id < 0 || id > std::numeric_limits<int>::max())
        return luaL_error(state, "owner discard pattern input outside bounded domain");
    const QJsonObject result = runtime->m_conversionResolver(request,
        {{"kind", "match_owner_discard_pattern"}, {"pattern", QString::fromUtf8(pattern, qsizetype(length))},
         {"card_id", int(id)}});
    if (result.value("matched").isBool()) lua_pushboolean(state, result.value("matched").toBool());
    else pushAIJsonValue(state, result);
    return 1;
}

int AiLuaRuntime::luaInspectNestedCardaskString(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "nested card parser unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "nested card parser budget exceeded");
    if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING)
        return luaL_error(state, "nested card parser requires one string");
    size_t length = 0;
    const char *text = lua_tolstring(state, 1, &length);
    if (length > 16384) {
        pushAIJsonValue(state, QJsonObject{{"error", "AI_UNSUPPORTED"},
            {"reason", "nested parser input exceeds bounded text domain"}, {"key", "nested_askForCard.parse"}});
        return 1;
    }
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "inspect_nested_cardask_string"}, {"value", QString::fromUtf8(text, qsizetype(length))}}));
    return 1;
}

int AiLuaRuntime::luaEvaluateNengchenPreview(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "Nengchen evaluation unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "Nengchen evaluation budget exceeded");
    if (lua_gettop(state) != 1 || !lua_isinteger(state, 1))
        return luaL_error(state, "invalid Nengchen material identity");
    const lua_Integer id = lua_tointeger(state, 1);
    if (id < 0 || id > std::numeric_limits<int>::max()) return luaL_error(state, "invalid Nengchen material identity");
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "evaluate_nengchen_preview"}, {"card_id", int(id)}}));
    return 1;
}

int AiLuaRuntime::luaRushBPhysicalSkill(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    if(!runtime || current()!=runtime || !runtime->m_room || !runtime->m_activeRequest || runtime->m_activeEventRequest || !runtime->m_conversionResolver)
        return luaL_error(state,"RushB physical mutation requires active owner decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger || ++request.conversionLedger->queryCount>128)
        return luaL_error(state,"RushB physical mutation query budget exhausted");
    QJsonValue value;int budget=64;
    if(lua_gettop(state)!=2 || !lua_istable(state,2)
        || !aiRound44RangeLuaRequestCurrent(*runtime->m_room,request,lua_topointer(state,2))
        || !readAIJson(state,1,value,budget) || !value.isObject())return luaL_error(state,"RushB physical mutation requires bounded values");
    auto proposal=value.toObject();if(proposal.contains("kind"))return luaL_error(state,"RushB physical mutation cannot supply host kind");
    const auto refresh=proposal.take("refresh");
    if(!refresh.isUndefined()&&!refresh.isBool())return luaL_error(state,"RushB refresh discriminator invalid");
    proposal.insert("kind",refresh.toBool()?"rushb_physical_skill_read":"rushb_physical_skill");
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaEvaluateNotifiedCard(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "notified card evaluation unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "notified card evaluation budget exceeded");
    if (lua_gettop(state) != 1 || !lua_isinteger(state, 1))
        return luaL_error(state, "invalid notified card material identity");
    const lua_Integer id = lua_tointeger(state, 1);
    if (id < 0 || id > std::numeric_limits<int>::max()) return luaL_error(state, "invalid notified card material identity");
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "evaluate_notified_card"}, {"card_id", int(id)}}));
    return 1;
}

int AiLuaRuntime::luaRecordNestedDelegateUse(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const QString &message){pushAIJsonValue(state,QJsonObject{{"error",message},{"key","nested-temporary-delegates"}});return 1;};
    if(!runtime || current()!=runtime || !runtime->m_room || !runtime->m_activeRequest || runtime->m_activeEventRequest)
        return fail(QStringLiteral("delegate write unavailable outside active decision"));
    QString key;
    if(lua_gettop(state)!=1 || lua_type(state,1)!=LUA_TSTRING || !readBoundedString(state,1,key)
       || (key!="mobilebenxi"&&key!="fanyin"&&key!="@@jieyingh"&&key!="ov_zenhui"&&key!="ov_yizhu"&&key!="ov_ronggui"&&key!="ov_qirang:yb_fujia2_data"&&key!="ov_qirang:yb_zhuzhan2_data"))
        return fail(QStringLiteral("delegate source key is outside finite family"));
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger || ++request.conversionLedger->queryCount>128)
        return fail(QStringLiteral("delegate observation query budget exceeded"));
    ServerPlayer *viewer=runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true);
    QString error;
    {
        EngineRuntimeContextScope engineScope(*Sanguosha,runtime->m_room);
        LuaRuntime::Binding gameBinding(runtime->m_room->roomRuntime()->lua(),false);
        if(!recordAIRound26DelegateUse(request,viewer,key,&error))return fail(error);
    }
    // Source writes before calling the child and does not undo on child error.
    // This is observer custody only, not an accepted-answer gameplay effect.
    lua_pushboolean(state,true);return 1;
}

int AiLuaRuntime::luaXuanzhuActualSlot(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const char *why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","xuanzhuUse"}});return 1;};
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)
        return fail("Xuanzhu observation requires active owner decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("Xuanzhu query budget exceeded");
    QString mode;QJsonValue payload;int budget=512;
    if(lua_gettop(state)!=2||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,mode)
        ||(mode!="capture"&&mode!="construct"&&mode!="read"&&mode!="first_construct"&&mode!="first_select"&&mode!="saved_select")||!lua_istable(state,2)
        ||!readAIJson(state,2,payload,budget)||!payload.isObject())return fail("Xuanzhu finite query shape malformed");
    QJsonObject proposal=payload.toObject();if(proposal.contains("kind")||proposal.contains("mode"))return fail("Xuanzhu query discriminator supplied twice");
    // readAIJson cannot infer the array shape of an empty plain Lua table.
    // Normalize only the array field declared by this exact Xuanzhu operation.
    const char *arrayKey=(mode=="capture"||mode=="saved_select")?"targets":nullptr;
    if(arrayKey){
        QJsonValue values=proposal.value(arrayKey);
        if(values.isObject()&&values.toObject().isEmpty())values=QJsonArray();
        if(!values.isArray())return fail("Xuanzhu declared array field malformed");
        proposal.insert(arrayKey,values);
    }
    proposal.insert("kind","xuanzhu_actual_slot");proposal.insert("mode",mode);
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaXufaActualSlot(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const char *why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","xufaUse"}});return 1;};
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)
        return fail("Xufa observation requires active owner decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("Xufa query budget exceeded");
    QString mode;QJsonValue payload;int budget=512;
    if(lua_gettop(state)!=2||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,mode)
        ||(mode!="capture"&&mode!="construct"&&mode!="read"&&mode!="first_construct"&&mode!="first_select"&&mode!="saved_select")||!lua_istable(state,2)
        ||!readAIJson(state,2,payload,budget)||!payload.isObject())return fail("Xufa finite query shape malformed");
    QJsonObject proposal=payload.toObject();if(proposal.contains("kind")||proposal.contains("mode"))return fail("Xufa query discriminator supplied twice");
    // readAIJson cannot infer the array shape of an empty plain Lua table.
    // Normalize only the array field declared by this exact Xufa operation.
    const char *arrayKey=(mode=="construct"||mode=="first_construct")?"costs":(mode=="capture"||mode=="saved_select")?"targets":nullptr;
    if(arrayKey){
        QJsonValue values=proposal.value(arrayKey);
        if(values.isObject()&&values.toObject().isEmpty())values=QJsonArray();
        if(!values.isArray())return fail("Xufa declared array field malformed");
        proposal.insert(arrayKey,values);
    }
    proposal.insert("kind","xufa_actual_slot");proposal.insert("mode",mode);
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaOlClanSavedSlot(lua_State *state)
{
 AiLuaRuntime*runtime=fromUpvalue(state);
 auto fail=[&](const char*why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","olClan.saved_plan"}});return 1;};
 if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)return fail("saved plan requires active owner inquiry");
 const AIRequest&request=*runtime->m_activeRequest;
 if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("saved plan query budget exceeded");
 QString slot,op;QJsonValue value;int budget=1024;
 if(lua_gettop(state)!=3||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,slot)||lua_type(state,2)!=LUA_TSTRING||!readBoundedString(state,2,op)
  ||(slot!="kezufuxunUse"&&slot!="zuchengqiUse"&&slot!="zulianzhuEp"&&slot!="zujieliTo"&&slot!="kezumingjieUse"&&slot!="kezulilunUse"&&slot!="zujiewusuit"&&slot!="lianzhanUse"&&slot!="tenyearmingfa_c"&&slot!="gengduUse"&&slot!="zhuning_d"&&slot!="diouUse"&&slot!="tyfenwuUse"&&slot!="tyqingkouUse"&&slot!="tychendeUse")||(op!="write"&&op!="read"&&!(slot=="tenyearmingfa_c"&&op=="query")&&!(slot=="diouUse"&&op=="select"))
  ||!lua_istable(state,3)||!readAIJson(state,3,value,budget)||!value.isObject())return fail("finite saved-plan query malformed");
 // Only these named source protocols declare targets as an array here.
 // They permit the empty SPlayerList of a target-fixed hypothetical card.
 if((slot=="diouUse"||slot=="tyfenwuUse"||slot=="tyqingkouUse"||slot=="tychendeUse")&&op=="write"){
  auto payload=value.toObject();auto targets=payload.value("targets");
  if(targets.isObject()&&targets.toObject().isEmpty())payload.insert("targets",QJsonArray());
  value=payload;
 }
 if(slot=="diouUse"&&op=="select"){auto payload=value.toObject();const auto targets=payload.value("targets");if(targets.isObject()&&targets.toObject().isEmpty())payload.insert("targets",QJsonArray());value=payload;}
 if(slot=="zhuning_d"&&op=="write"){
  auto payload=value.toObject();const auto targets=payload.value("targets");
  if(targets.isObject()&&targets.toObject().isEmpty())payload.insert("targets",QJsonArray());
  value=payload;
 }
 if(slot=="gengduUse"&&op=="write"){
  auto payload=value.toObject();const auto targets=payload.value("targets");
  if(targets.isObject()&&targets.toObject().isEmpty())payload.insert("targets",QJsonArray());
  value=payload;
 }
 pushAIJsonValue(state,runtime->m_conversionResolver(request,QJsonObject{{"kind","olclan_saved_slot"},{"slot",slot},{"operation",op},{"payload",value}}));return 1;
}

int AiLuaRuntime::luaNewgeneralsSavedSlot(lua_State *state)
{
 AiLuaRuntime*runtime=fromUpvalue(state);
 auto fail=[&](const char*why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","newgenerals.saved_plan"}});return 1;};
 if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)return fail("saved plan requires active owner inquiry");
 const AIRequest&request=*runtime->m_activeRequest;
 if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("saved plan query budget exceeded");
 QString slot,op;QJsonValue value;int budget=1024;
 if(lua_gettop(state)!=3||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,slot)||lua_type(state,2)!=LUA_TSTRING||!readBoundedString(state,2,op)
  ||(slot!="xdyifuvsTo"&&slot!="xdyifuvsUse"&&slot!="godshenji_use")||(op!="write"&&op!="read")
  ||!lua_istable(state,3)||!readAIJson(state,3,value,budget)||!value.isObject())return fail("finite saved-plan query malformed");
 pushAIJsonValue(state,runtime->m_conversionResolver(request,QJsonObject{{"kind","newgenerals_saved_slot"},{"slot",slot},{"operation",op},{"payload",value}}));return 1;
}

int AiLuaRuntime::luaHeqiaSavedPlan(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const char *why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","heqia_use"}});return 1;};
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)
        return fail("Heqia observation requires active owner decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("Heqia query budget exceeded");
    QString mode;QJsonValue payload;int budget=512;
    if(lua_gettop(state)!=2||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,mode)
        ||(mode!="capture"&&mode!="construct"&&mode!="select"&&mode!="read"&&mode!="first")||!lua_istable(state,2)
        ||!readAIJson(state,2,payload,budget)||!payload.isObject())return fail("Heqia finite query shape malformed");
    QJsonObject proposal=payload.toObject();if(proposal.contains("kind")||proposal.contains("mode"))return fail("Heqia query discriminator supplied twice");
    if(mode=="capture"||mode=="select"||mode=="first"){
        auto targets=proposal.value("targets");if(targets.isObject()&&targets.toObject().isEmpty())targets=QJsonArray();
        if(!targets.isArray())return fail("Heqia target sequence malformed");proposal.insert("targets",targets);
    }
    proposal.insert("kind","heqia_saved_plan");proposal.insert("mode",mode);
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaLeiluanSavedPlan(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const char *why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","leiluanUse"}});return 1;};
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)
        return fail("Leiluan observation requires active owner decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("Leiluan query budget exceeded");
    QString mode;QJsonValue payload;int budget=512;
    if(lua_gettop(state)!=2||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,mode)
        ||(mode!="capture"&&mode!="construct"&&mode!="select")||!lua_istable(state,2)
        ||!readAIJson(state,2,payload,budget)||!payload.isObject())return fail("Leiluan finite query shape malformed");
    QJsonObject proposal=payload.toObject();if(proposal.contains("kind")||proposal.contains("mode"))return fail("Leiluan query discriminator supplied twice");
    if(mode=="capture"){
        auto targets=proposal.value("targets");if(targets.isObject()&&targets.toObject().isEmpty())targets=QJsonArray();
        if(!targets.isArray())return fail("Leiluan target sequence malformed");proposal.insert("targets",targets);
    }
    proposal.insert("kind","leiluan_saved_plan");proposal.insert("mode",mode);
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaJianmanSavedPlan(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const char *why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","jianmanUse"}});return 1;};
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)
        return fail("Jianman observation requires active owner decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("Jianman query budget exceeded");
    QString mode;QJsonValue payload;int budget=512;
    if(lua_gettop(state)!=2||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,mode)
        ||(mode!="capture"&&mode!="construct"&&mode!="select")||!lua_istable(state,2)
        ||!readAIJson(state,2,payload,budget)||!payload.isObject())return fail("Jianman finite query shape malformed");
    QJsonObject proposal=payload.toObject();if(proposal.contains("kind")||proposal.contains("mode"))return fail("Jianman query discriminator supplied twice");
    if(mode=="capture"){
        auto targets=proposal.value("targets");if(targets.isObject()&&targets.toObject().isEmpty())targets=QJsonArray();
        if(!targets.isArray())return fail("Jianman target sequence malformed");proposal.insert("targets",targets);
    }
    proposal.insert("kind","jianman_saved_plan");proposal.insert("mode",mode);
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaFaqiSavedPlan(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const char *why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","faqiUse"}});return 1;};
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||runtime->m_activeEventRequest||!runtime->m_conversionResolver)
        return fail("Faqi observation requires active owner decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128)return fail("Faqi query budget exceeded");
    QString mode;QJsonValue payload;int budget=512;
    if(lua_gettop(state)!=2||lua_type(state,1)!=LUA_TSTRING||!readBoundedString(state,1,mode)
        ||(mode!="capture"&&mode!="construct"&&mode!="select")||!lua_istable(state,2)
        ||!readAIJson(state,2,payload,budget)||!payload.isObject())return fail("Faqi finite query shape malformed");
    QJsonObject proposal=payload.toObject();if(proposal.contains("kind")||proposal.contains("mode"))return fail("Faqi query discriminator supplied twice");
    proposal.insert("kind","faqi_saved_plan");proposal.insert("mode",mode);
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaOlClanPlanObservation(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    auto fail=[&](const char *message){pushAIJsonValue(state,QJsonObject{{"error",message},{"key","olClan.saved_plan"}});return 1;};
    if(!runtime || current()!=runtime || !runtime->m_activeRequest || runtime->m_activeEventRequest || !runtime->m_conversionResolver)
        return fail("OlClan saved observation unavailable outside decision");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger || ++request.conversionLedger->queryCount>128)
        return fail("OlClan observation query budget exceeded");
    if(lua_isinteger(state,lua_upvalueindex(2)) && lua_tointeger(state,lua_upvalueindex(2))==2){
        if(lua_gettop(state)!=2||!lua_isinteger(state,1)||lua_tointeger(state,1)<1||lua_tointeger(state,1)>std::numeric_limits<int>::max())
            return fail("OlClan source policy allocation expects one issued conversion ID and finite form");
        QString form;if(lua_type(state,2)!=LUA_TSTRING||!readBoundedString(state,2,form)||(form!="ordinary"&&form!="legacy_response"))
            return fail("OlClan source policy allocation form is outside audited constructors");
        pushAIJsonValue(state,runtime->m_conversionResolver(request,QJsonObject{{"kind","allocate_olclan_policy_card"},{"conversion_id",int(lua_tointeger(state,1))},{"policy_form",form}}));
        return 1;
    }
    QString slot;
    if(lua_gettop(state)!=2 || lua_type(state,1)!=LUA_TSTRING || !readBoundedString(state,1,slot))
        return fail("OlClan observation expects a slot and one selector");
    const bool capture=lua_toboolean(state,lua_upvalueindex(2));
    QJsonObject proposal{{"kind",capture?"capture_olclan_plan":"restore_olclan_plan"},{"slot",slot}};
    if(capture){
        QJsonValue selector;int budget=32;
        if(!lua_istable(state,2)||!readAIJson(state,2,selector,budget)||!selector.isObject())return fail("OlClan observation selector malformed");
        proposal.insert("selector",selector);
    }else{
        QString token;
        if(lua_type(state,2)!=LUA_TSTRING||!readBoundedString(state,2,token))return fail("OlClan observation token malformed");
        proposal.insert("token",token);
    }
    pushAIJsonValue(state,runtime->m_conversionResolver(request,proposal));return 1;
}

int AiLuaRuntime::luaEvaluateSkillCard(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "skill evaluation unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return luaL_error(state, "skill evaluation budget exceeded");
    if (lua_gettop(state) == 3 && lua_type(state, 1) == LUA_TSTRING && lua_isinteger(state, 2)
        && lua_type(state, 3) == LUA_TSTRING) {
        QString className, declaration;
        const lua_Integer scope = lua_tointeger(state, 2);
        if (!readBoundedString(state, 1, className) || !readBoundedString(state, 3, declaration)
            || scope < 0 || scope > std::numeric_limits<int>::max())
            return luaL_error(state, "invalid Tongyi policy evaluation");
        pushAIJsonValue(state, runtime->m_conversionResolver(request,
            {{"kind", "evaluate_tongyi_policy"}, {"scope_id", int(scope)},
             {"class_name", className}, {"declaration", declaration}}));
        return 1;
    }
    if (lua_gettop(state) == 1 && lua_istable(state, 1)) {
        QJsonValue spec; int budget = 1024;
        if (!readAIJson(state, 1, spec, budget) || !spec.isObject()) return luaL_error(state, "invalid owner view-as evaluation");
        pushAIJsonValue(state, runtime->m_conversionResolver(request,
            {{"kind", "evaluate_owner_view_as"}, {"spec", spec}}));
        return 1;
    }
    QString skill, name; QJsonValue costs; int budget = 4096;
    if (lua_gettop(state) != 3 || lua_type(state, 1) != LUA_TSTRING || lua_type(state, 2) != LUA_TSTRING
        || !readBoundedString(state, 1, skill) || !readBoundedString(state, 2, name)
        || !readAIJson(state, 3, costs, budget)) return luaL_error(state, "invalid skill evaluation");
    if (costs.isObject() && costs.toObject().isEmpty()) costs = QJsonArray();
    if (!costs.isArray()) return luaL_error(state, "invalid evaluation costs");
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "evaluate_skill_card"}, {"skill", skill}, {"name", name}, {"subcards", costs}}));
    return 1;
}

int AiLuaRuntime::luaObserveOverseasLihuo(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    if(lua_gettop(state)!=0||!runtime||current()!=runtime||!runtime->m_activeRequest||!runtime->m_conversionResolver)
        return luaL_error(state,"Lihuo owner source observation requires an active inquiry and no arguments");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128){
        pushAIJsonValue(state,QJsonObject{{"error","Lihuo query budget exceeded"},{"key","ov_lihuo.hypothesis"}});return 1;
    }
    pushAIJsonValue(state,runtime->m_conversionResolver(request,{{"kind","observe_overseas_lihuo"}}));return 1;
}

int AiLuaRuntime::luaSelectSijyuFengyiPhysical(lua_State *state)
{
    auto *runtime=fromUpvalue(state);
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||!runtime->m_conversionResolver
        ||lua_gettop(state)!=1||!lua_isinteger(state,1)||lua_tointeger(state,1)<0
        ||lua_tointeger(state,1)>std::numeric_limits<int>::max())
        return luaL_error(state,"Fengyi selected physical answer requires one current integer card ID");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128){
        pushAIJsonValue(state,QJsonObject{{"error","Fengyi selected query budget exceeded"},{"native_rejected",true}});return 1;
    }
    pushAIJsonValue(state,runtime->m_conversionResolver(request,
        {{"kind","select_sijyu_fengyi_physical"},{"card_id",int(lua_tointeger(state,1))}}));
    return 1;
}

int AiLuaRuntime::luaBindOverseasNiwoSelected(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||!runtime->m_conversionResolver||lua_gettop(state)!=1)
        return luaL_error(state,"Niwo selected binding requires one active response cost list");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128){
        pushAIJsonValue(state,QJsonObject{{"error","Niwo query budget exceeded"},{"key","ov_niwo.selected"}});return 1;
    }
    QJsonValue costs;int budget=2048;
    if(!readAIJson(state,1,costs,budget))return luaL_error(state,"Niwo costs are malformed");
    if(costs.isObject()&&costs.toObject().isEmpty())costs=QJsonArray();
    if(!costs.isArray())return luaL_error(state,"Niwo costs must be a dense array");
    pushAIJsonValue(state,runtime->m_conversionResolver(request,{{"kind","bind_overseas_niwo_selected"},{"subcards",costs}}));return 1;
}

int AiLuaRuntime::luaUseValueFlag(lua_State *state)
{
    auto *runtime=fromUpvalue(state);
    if(!runtime || current()!=runtime || !runtime->m_room || (!runtime->m_activeRequest && !runtime->m_activeEventRequest)
        || lua_gettop(state)>1 || (lua_gettop(state)==1 && lua_type(state,1)!=LUA_TBOOLEAN))
        return luaL_error(state,"valuation flag requires active owner policy and optional boolean");
    const AIRequest &request=*(runtime->m_activeEventRequest ? runtime->m_activeEventRequest : runtime->m_activeRequest);
    auto *owner=runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true);
    QString error; bool value=false;
    if(lua_gettop(state)==1) {
        value=lua_toboolean(state,1);
        if(!writeAIRound29UseValueFlag(*runtime->m_room,owner,request,value,&error))return luaL_error(state,"%s",qPrintable(error));
    } else if(!readAIRound29UseValueFlag(*runtime->m_room,owner,request,value,&error))return luaL_error(state,"%s",qPrintable(error));
    lua_pushboolean(state,value);return 1;
}
int AiLuaRuntime::luaEvaluateExtraShicai(lua_State *state)
{
    auto *runtime=fromUpvalue(state);
    if(!runtime || current()!=runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver || lua_gettop(state)!=0)
        return luaL_error(state,"Shicai preview requires active question and no arguments");
    const AIRequest &request=*runtime->m_activeRequest;
    if(!request.conversionLedger || ++request.conversionLedger->queryCount>128)return luaL_error(state,"Shicai preview budget exceeded");
    pushAIJsonValue(state,runtime->m_conversionResolver(request,{{"kind","evaluate_extra_shicai"}}));return 1;
}

bool AiLuaRuntime::currentOLJijiangInvokeEvent(const AIRequest &q, const ServerPlayer *p) const
{
 // The request and event must be the exact pair installed by processEvent,
 // still under its ExecutionBinding. Copies/retired events have no authority.
 if(current()!=this||!m_room||!p||p->getRoom()!=m_room||m_activeRequest
  ||m_activeEventRequest!=&q||!m_activePeiqiEvent)return false;
 const auto &e=*m_activePeiqiEvent;
 const auto fields=e.details.value("prompt_list").toArray();
 const auto projected=q.choiceOptions.context.value("r40_oljijiang_event").toObject();
 return q.decisionId==0&&q.kind==AIRequest::UseCard
  &&q.reason==CardUseStruct::CARD_USE_REASON_UNKNOWN&&q.handlingMethod==Card::MethodUse
  &&q.prompt.isEmpty()&&q.pattern.isEmpty()
  &&q.viewerObjectName==p->objectName()&&q.worldView.self.objectName==p->objectName()
  &&q.stateRevision==m_room->roomRuntime()->stateRevision()&&q.worldView.revision==q.stateRevision
  &&e.sequence!=0&&e.revision==q.stateRevision&&e.triggerEvent==int(ChoiceMade)
  &&e.kind=="choice"&&e.reason=="oljijiang"&&e.from==p->objectName()&&e.to==p->objectName()
  // recordEvent verifies the recipient, then clears privateViewer before processEvent.
  // The exact active projected event retains privateEvent and the actor/viewer checks above.
  &&e.privateEvent&&e.privateViewer.isEmpty()
  &&e.details.value("player")==p->objectName()&&e.details.value("choice_kind")=="skillInvoke"
  &&e.details.value("answer")=="yes"&&fields==QJsonArray({"skillInvoke","oljijiang","yes"})
  &&projected==QJsonObject({{"writer","invoke"},{"actor",p->objectName()},{"sequence",QString::number(e.sequence)}});
}

int AiLuaRuntime::luaOLJijiangState(lua_State *L)
{
 auto *runtime=fromUpvalue(L);
 if(!runtime||current()!=runtime||!runtime->m_room||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest)
  ||lua_gettop(L)<1||lua_gettop(L)>2||lua_type(L,1)!=LUA_TSTRING)
  return luaL_error(L,"OLJijiang continuity needs an active policy and finite operation");
 QString op,value;if(!readBoundedString(L,1,op)||(lua_gettop(L)==2&&!lua_isnil(L,2)&&!readBoundedString(L,2,value)))return luaL_error(L,"invalid OLJijiang primitive operation");
 const auto&q=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
 auto *viewer=runtime->m_room->findPlayerByObjectName(q.viewerObjectName,true);
 pushAIJsonValue(L,queryAIRound40OLJijiang(*runtime->m_room,q,viewer,op,value));return 1;
}

int AiLuaRuntime::luaOverseasPriority(lua_State *L)
{
    auto *runtime=fromUpvalue(L);
    if(!runtime||current()!=runtime||!runtime->m_room||(!runtime->m_activeRequest&&!runtime->m_activeEventRequest))
        return luaL_error(L,"priority continuity requires active policy request");
    const int argc=lua_gettop(L);
    if(argc<2||argc>3||lua_type(L,1)!=LUA_TSTRING||lua_type(L,2)!=LUA_TSTRING)
        return luaL_error(L,"invalid priority operation");
    const QString op=QString::fromUtf8(lua_tostring(L,1));QString key;
    if(!readBoundedString(L,2,key)||(op!="read"&&op!="write")||(op=="read"&&argc!=2))return luaL_error(L,"invalid priority key/operation");
    QJsonObject spec{{"kind","overseas_priority"},{"op",op},{"key",key}};
    if(op=="write") {
        const bool removed=argc==2||lua_isnil(L,3);
        if(!removed&&(lua_type(L,3)!=LUA_TNUMBER||!std::isfinite(lua_tonumber(L,3))))return luaL_error(L,"invalid priority scalar");
        spec.insert("removed",removed);spec.insert("value",removed?QJsonValue(QJsonValue::Null):QJsonValue(lua_tonumber(L,3)));
    }
    const auto &request=*(runtime->m_activeEventRequest?runtime->m_activeEventRequest:runtime->m_activeRequest);
    auto *viewer=runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true);
    pushAIJsonValue(L,queryAIRound29ChoicePriority(*runtime->m_room,request,viewer,spec));return 1;
}
int AiLuaRuntime::luaOverseasXiafengFlag(lua_State *L)
{
    auto *runtime=fromUpvalue(L);
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||!runtime->m_conversionResolver||lua_gettop(L)!=2
        ||lua_type(L,1)!=LUA_TNUMBER||lua_type(L,2)!=LUA_TBOOLEAN)
        return luaL_error(L,"invalid Xiafeng owner flag request");
    const double id=lua_tonumber(L,1);
    if(!std::isfinite(id)||id<0||id!=std::floor(id)||id>std::numeric_limits<int>::max())return luaL_error(L,"invalid Xiafeng card id");
    pushAIJsonValue(L,runtime->m_conversionResolver(*runtime->m_activeRequest,
        {{"kind","overseas_xiafeng_flag"},{"card_id",int(id)},{"enabled",bool(lua_toboolean(L,2))}}));return 1;
}

int AiLuaRuntime::luaBindZhifengQuestion(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    if (!runtime || current()!=runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver || lua_gettop(state)!=1)
        return luaL_error(state,"Zhifeng binding requires one selected recipe in an active question");
    const AIRequest &request=*runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount>128)
        return luaL_error(state,"Zhifeng question budget exceeded");
    QJsonValue spec;int budget=128;
    if (!readAIJson(state,1,spec,budget) || !spec.isObject())return luaL_error(state,"invalid Zhifeng selected recipe");
    pushAIJsonValue(state,runtime->m_conversionResolver(request,{{"kind","bind_zhifeng_question"},{"spec",spec}}));return 1;
}

int AiLuaRuntime::luaRangyiQuestion(lua_State*state)
{
    auto*runtime=fromUpvalue(state);
    if(!runtime||current()!=runtime||!runtime->m_activeRequest||!runtime->m_conversionResolver||lua_gettop(state)!=1)
        return luaL_error(state,"Rangyi source query requires one input and active question");
    const AIRequest&request=*runtime->m_activeRequest;
    if(!request.conversionLedger||++request.conversionLedger->queryCount>128)
        return luaL_error(state,"Rangyi query budget exceeded");
    QJsonValue input;int budget=64;
    if(!readAIJson(state,1,input,budget)||!input.isObject())return luaL_error(state,"Rangyi query input malformed");
    pushAIJsonValue(state,runtime->m_conversionResolver(request,{{"kind","rangyi_question"},{"input",input}}));return 1;
}

int AiLuaRuntime::luaBindJieyinghQuestion(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (lua_gettop(state) != 0 || !runtime || current() != runtime
        || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "Jieyingh question authority requires an active decision and no arguments");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128) {
        pushAIJsonValue(state, QJsonObject{{"error", "Jieyingh question query budget exceeded"}, {"key", "jieyingh.question"}});
        return 1;
    }
    pushAIJsonValue(state, runtime->m_conversionResolver(request, {{"kind", "bind_jieyingh_question"}}));
    return 1;
}

int AiLuaRuntime::luaResolveSkillAction(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "skill action authority unavailable outside decision");
    const AIRequest &request = *runtime->m_activeRequest;
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128) {
        pushAIJsonValue(state, QJsonObject{{"error", "skill action query budget exceeded"}, {"key", "conversion_policy"}});
        return 1;
    }
    int scope = -1, budget = 4096;
    QJsonValue costs; QString declaration, legacyClass;
    if (lua_gettop(state) < 2 || lua_gettop(state) > 4 || lua_type(state, 1) != LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(state, 1), scope) || scope < 1
        || !readAIJson(state, 2, costs, budget)) return luaL_error(state, "invalid skill action query");
    // The generic JSON reader represents an empty Lua table as an object.
    if (costs.isObject() && costs.toObject().isEmpty()) costs = QJsonArray();
    if (!costs.isArray()) return luaL_error(state, "skill action costs must be a dense array");
    QJsonObject spec{{"subcards", costs}};
    if (lua_gettop(state) >= 3 && !lua_isnil(state, 3)) {
        if (lua_type(state, 3) != LUA_TSTRING || !readBoundedString(state, 3, declaration))
            return luaL_error(state, "invalid skill action declaration");
        spec.insert("user_string", declaration);
    }
    if (lua_gettop(state) == 4 && !lua_isnil(state, 4)) {
        if (lua_type(state, 4) != LUA_TSTRING || !readBoundedString(state, 4, legacyClass))
            return luaL_error(state, "invalid legacy strategy prototype");
        spec.insert("legacy_class", legacyClass);
    }
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "skill_action"}, {"class_name", "@skill_action"}, {"scope_id", scope}, {"spec", spec}}));
    return 1;
}

int AiLuaRuntime::luaConsumeDeckCountShuffle(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_conversionResolver
        || (!runtime->m_activeRequest && !runtime->m_activeEventRequest))
        return luaL_error(state, "deck-count authority unavailable outside policy");
    if (lua_gettop(state) != 0) return luaL_error(state, "deck-count query accepts no arguments");
    const AIRequest &request = *(runtime->m_activeRequest ? runtime->m_activeRequest : runtime->m_activeEventRequest);
    if (runtime->m_room && !aiRound44RangeRequestCurrent(*runtime->m_room,request))
        return luaL_error(state, "stale deck-count query");
    pushAIJsonValue(state, runtime->m_conversionResolver(request, {{"kind", "deck_count_shuffle"}}));
    return 1;
}

int AiLuaRuntime::luaSamplePatternLogName(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_conversionResolver
        || (!runtime->m_activeRequest && !runtime->m_activeEventRequest))
        return luaL_error(state, "pattern-log authority unavailable outside policy");
    QString pattern;
    if (lua_gettop(state) < 1 || lua_gettop(state) > 2 || lua_type(state, 1) != LUA_TSTRING
        || !readBoundedString(state, 1, pattern)
        || (lua_gettop(state) == 2 && lua_type(state, 2) != LUA_TBOOLEAN))
        return luaL_error(state, "invalid pattern-log query");
    const bool derivative = lua_gettop(state) == 2 && lua_toboolean(state, 2);
    const AIRequest &request = *(runtime->m_activeRequest ? runtime->m_activeRequest : runtime->m_activeEventRequest);
    if (runtime->m_room && !aiRound44RangeRequestCurrent(*runtime->m_room,request))
        return luaL_error(state, "stale pattern-log query");
    QJsonObject query{{"kind", "pattern_log_name"}, {"pattern", pattern}};
    if (derivative) query.insert("derivative", true);
    pushAIJsonValue(state, runtime->m_conversionResolver(request, query));
    return 1;
}

int AiLuaRuntime::luaSampleOwnHand(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "owner-hand authority unavailable outside decision");
    if (lua_gettop(state) != 0) return luaL_error(state, "owner-hand query accepts no arguments");
    pushAIJsonValue(state, runtime->m_conversionResolver(*runtime->m_activeRequest, {{"kind", "random_own_hand"}}));
    return 1;
}

int AiLuaRuntime::luaSampleHiddenHand(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "hidden-hand authority unavailable outside decision");
    QString token, target;
    if (lua_gettop(state) != 2 || lua_type(state, 1) != LUA_TSTRING || lua_type(state, 2) != LUA_TSTRING
        || !readBoundedString(state, 1, token) || !readBoundedString(state, 2, target))
        return luaL_error(state, "invalid hidden-hand query");
    const AIRequest &request = *runtime->m_activeRequest;
    if (runtime->m_room && runtime->m_room->roomRuntime()
        && !aiRound44RangeRequestCurrent(*runtime->m_room,request))
        return luaL_error(state, "stale hidden-hand query");
    pushAIJsonValue(state, runtime->m_conversionResolver(request,
        {{"kind", "random_hand_discard"}, {"token", token}, {"target", target}}));
    return 1;
}

QJsonObject AiLuaRuntime::queryStandardMode(LuaRuntime &modeRuntime, const AIRequest &request,
                                            const QString &target, int toUseCount, bool refresh,
                                            const QJsonArray *prefixDeltas, const QJsonArray *prefixEffects,
                                            bool updatePlayers, bool update)
{
    const auto failure = [](const char *reason) {
        return QJsonObject{{"error", QString::fromLatin1(reason)}, {"key", "standard_objective"}};
    };
    const bool identityMode = request.worldView.modePolicy.value("standard_identity").toBool();
    const bool managedUpdate = updatePlayers && request.worldView.modePolicy.value("legacy_managed").toBool();
    const bool baseUpdate = updatePlayers && request.worldView.modePolicy.value("legacy_base_update").toBool();
    if ((!identityMode && !managedUpdate && !baseUpdate)
        || request.viewerObjectName != request.worldView.self.objectName)
        return failure("standard objective viewer is not authorized");
    QSet<QString> players{request.worldView.self.objectName};
    for (const auto &player : request.worldView.players) players.insert(player.objectName);
    if (!refresh && !players.contains(target)) return failure("unknown objective target");
    if (!modeRuntime.rawState()) return failure("mode runtime unavailable");
    LuaRuntime::Binding binding(modeRuntime);
    LuaRuntime::LuaInvocationScope invocation(modeRuntime);
    lua_State *mode = modeRuntime.rawState();
    // Complete the exact ordinary objective or Activate relations prepare/commit
    // graph BEFORE registry guard, hook changes or world allocation. Event and
    // updatePlayers routes remain distinct and receive no refresh scope.
    Room *modeRoom = Sanguosha ? Sanguosha->currentRoom() : nullptr;
    std::unique_ptr<AIRound56ModeEvaluationScope> objectiveSource;
    if (!refresh && !updatePlayers && !prefixDeltas && !prefixEffects
        && !m_activeEventRequest && m_activeRequest == &request && current() == this
        && modeRoom && modeRoom == m_room && modeRoom->roomRuntime()
        && &modeRoom->roomRuntime()->lua() == &modeRuntime
        && inAIRound56ProjectionScope(*modeRoom, mode)
        && aiRound44RangeRequestCurrent(*modeRoom, request))
        objectiveSource.reset(new AIRound56ModeEvaluationScope(*modeRoom, mode,
            request.worldView.modeId, AIRound56ModeEvaluationScope::StandardObjective));
    else if (refresh && !updatePlayers && !prefixDeltas && !prefixEffects
        && !m_activeEventRequest && m_activeRequest == &request && current() == this
        && request.kind == AIRequest::Activate && request.worldView.self.phase == Player::Play
        && identityMode && toUseCount >= -1
        && modeRoom && modeRoom == m_room && modeRoom->roomRuntime()
        && &modeRoom->roomRuntime()->lua() == &modeRuntime
        && aiRound44RangeRequestCurrent(*modeRoom, request))
        objectiveSource.reset(new AIRound56ModeEvaluationScope(*modeRoom, mode,
            request.worldView.modeId, AIRound56ModeEvaluationScope::StandardRelations));
    const int top = lua_gettop(mode);
    const auto restoreStack = qScopeGuard([mode, top] { lua_settop(mode, top); });
    lua_getfield(mode, LUA_REGISTRYINDEX, "qsan.mode_ai.active");
    const bool active = lua_toboolean(mode, -1);
    lua_pop(mode, 1);
    if (active) return failure("mode query is reentrant");
    lua_getglobal(mode, "sgs");
    if (!lua_istable(mode, -1)) return failure("mode policy unavailable");
    lua_getfield(mode, -1, "commitStandardObjective");
    if (!lua_isfunction(mode, -1)) return failure("standard objective commit unavailable");
    lua_getfield(mode, -2, updatePlayers
        ? (prefixDeltas ? "prepareEventStandardUpdatePlayers" : "prepareStandardUpdatePlayers")
        : prefixDeltas ? "prepareEventStandardObjective"
        : refresh ? "prepareStandardRelations" : "prepareStandardObjective");
    if (!lua_isfunction(mode, -1)) return failure("standard objective API unavailable");
    lua_pushboolean(mode, true);
    lua_setfield(mode, LUA_REGISTRYINDEX, "qsan.mode_ai.active");
    const auto clearActive = qScopeGuard([mode] {
        lua_pushboolean(mode, false); lua_setfield(mode, LUA_REGISTRYINDEX, "qsan.mode_ai.active");
    });
    if (objectiveSource && objectiveSource->accepted()) {
        AIRound56ModeWorldSource worldSource(*modeRoom, mode);
        pushAIWorldView(mode, request.worldView);
    } else {
        pushAIWorldView(mode, request.worldView);
    }
    if (refresh) {
        if (toUseCount < 0) lua_pushnil(mode);
        else lua_pushinteger(mode, toUseCount);
    } else pushQString(mode, target);
    if (updatePlayers) lua_pushboolean(mode, update);
    if (prefixDeltas) {
        if (!prefixEffects) return failure("event objective prefix unavailable");
        pushAIJsonValue(mode, *prefixDeltas);
        pushAIJsonValue(mode, *prefixEffects);
    }
    // Nested pure mode work consumes the same decision budget. Restore the Room
    // VM's existing hook even if the policy fails. Prefix intentions preserve
    // their source RNG draws; decoding and query transport add no draws.
    const lua_Hook previousHook = lua_gethook(mode);
    const int previousMask = lua_gethookmask(mode), previousCount = lua_gethookcount(mode);
    LuaRuntime::InstructionBudgetScope budgetOnlyHook(mode, &AiLuaRuntime::luaInstructionHook);
    lua_sethook(mode, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
    const int status = LuaRuntime::protectedCall(mode, (prefixDeltas ? 4 : 2) + (updatePlayers ? 1 : 0), 2, 0);
    lua_sethook(mode, previousHook, previousMask, previousCount);
    if (status != LUA_OK || !isPlainTable(mode, -2) || !isPlainTable(mode, -1)) return failure("standard objective failed");
    const int tokenIndex = lua_gettop(mode), resultIndex = tokenIndex - 1;
    if (objectiveSource) objectiveSource->preparedObjective(resultIndex, tokenIndex);
    QJsonValue decoded; int budget = updatePlayers ? 32768 : 8192;
    if (!readAIJson(mode, resultIndex, decoded, budget) || !decoded.isObject())
        return failure("objective result is not bounded pure values");
    QJsonObject result = decoded.toObject();
    QSet<QString> allowed{"value", "estimated_branches", "strategy_marks", "relations"};
    if (updatePlayers) allowed.unite({"owner_role", "harsh_retain", "friends", "friends_noself", "enemies",
        "role_values", "role_estimates", "explicit_renegade", "game_process", "process_label", "role_counts"});
    if (managedUpdate) allowed.insert("relation_matrix");
    for (auto it = result.constBegin(); it != result.constEnd(); ++it)
        if (!allowed.contains(it.key())) return failure("unknown objective result field");
    if (result.size() != allowed.size() || !result.value("value").isDouble()
        || !std::isfinite(result.value("value").toDouble())
        || !result.value("strategy_marks").isObject() || !result.value("relations").isObject())
        return failure("invalid objective result schema");
    lua_getfield(mode, resultIndex, "estimated_branches");
    QJsonArray estimates;
    const bool estimateArray = readAIArray(mode, -1, estimates, 256);
    lua_pop(mode, 1);
    if (!estimateArray) return failure("invalid objective estimates");
    for (const auto &estimate : estimates)
        if (!estimate.isString() || estimate.toString().isEmpty()) return failure("invalid objective estimate entry");
    result.insert("estimated_branches", estimates);
    const QSet<QString> relations{"friend", "enemy", "neutral", "unknown"};
    const QJsonObject row = result.value("relations").toObject();
    for (auto it = row.constBegin(); it != row.constEnd(); ++it)
        if (!players.contains(it.key()) || !it.value().isString() || !relations.contains(it.value().toString()))
            return failure("invalid objective relation");
    const QJsonObject marks = result.value("strategy_marks").toObject();
    for (auto player = marks.constBegin(); player != marks.constEnd(); ++player) {
        if (!players.contains(player.key()) || !player.value().isObject()) return failure("invalid strategy mark owner");
        const QJsonObject values = player.value().toObject();
        for (auto mark = values.constBegin(); mark != values.constEnd(); ++mark) {
            const QString name = mark.key(); int amount;
            if (!mark.value().isDouble() || !aiResultInteger(mark.value().toDouble(), amount)
                || !(name == "roleRobot" || (name.startsWith("Intention") && players.contains(name.mid(9)))
                    || (name.startsWith("revenge") && players.contains(name.mid(7))
                        && player.key() == request.viewerObjectName)
                    || (player.key() == request.viewerObjectName
                        && (QRegularExpression("^ai_NoJinkCard-?[0-9]+_targeted-Clear$").match(name).hasMatch()
                            || QRegularExpression("^ai_NoJinkDummy[0-9]+_[0-9]+_targeted-Clear$").match(name).hasMatch()
                            || QRegularExpression("^ai_NoJinkVirtualnative:[0-9a-f-]{36}:[0-9]+_targeted-Clear$").match(name).hasMatch())
                        && amount >= 0))) return failure("invalid objective strategy mark");
        }
    }
    if (updatePlayers) {
        QSet<QString> roles{"lord", "loyalist", "rebel", "renegade"};
        if (managedUpdate) {
            roles.insert(request.worldView.self.role);
            for (const auto &player : request.worldView.players)
                if (player.roleVisible && !player.role.isEmpty()) roles.insert(player.role);
        }
        const auto validRole = [&](const QString &role) {
            return !role.isEmpty() && role.size() <= 256
                && (roles.contains(role) || (managedUpdate && !Sanguosha->getRoleAbbreviation(role).isEmpty()));
        };
        const QSet<QString> inferredRoles{"lord", "loyalist", "rebel", "renegade", "neutral", "unknown"};
        const QSet<QString> labels{"loyalist", "loyalish", "rebel", "rebelish", "dilemma", "neutral"};
        if (result.value("value").toDouble() != 0 || !result.value("owner_role").isString()
            || !validRole(result.value("owner_role").toString())
            || result.value("owner_role").toString() != request.worldView.self.role
            || !result.value("harsh_retain").isBool() || !result.value("explicit_renegade").isBool()
            || !result.value("game_process").isDouble() || !std::isfinite(result.value("game_process").toDouble())
            || !result.value("process_label").isString()
            || (managedUpdate ? result.value("process_label").toString().size() > 256
                : !labels.contains(result.value("process_label").toString()))
            || !result.value("role_values").isObject() || !result.value("role_estimates").isObject()
            || !result.value("role_counts").isObject()) return failure("invalid updatePlayers result");
        QSet<QString> alive;
        if (request.worldView.self.alive) alive.insert(request.worldView.self.objectName);
        for (const auto &player : request.worldView.players) if (player.alive) alive.insert(player.objectName);
        QHash<QString, QSet<QString>> lists;
        for (const char *key : {"friends", "friends_noself", "enemies"}) {
            lua_getfield(mode, resultIndex, key);
            QJsonArray values;
            const bool valid = readAIArray(mode, -1, values, 256);
            lua_pop(mode, 1);
            if (!valid) return failure("invalid updatePlayers ordered list");
            QSet<QString> seen;
            for (const auto &value : values) {
                if (!value.isString() || !alive.contains(value.toString()) || seen.contains(value.toString()))
                    return failure("invalid updatePlayers list player");
                seen.insert(value.toString());
            }
            lists.insert(QString::fromLatin1(key), seen);
            result.insert(QString::fromLatin1(key), values);
        }
        auto withoutSelf = lists.value("friends"); withoutSelf.remove(request.viewerObjectName);
        if (withoutSelf != lists.value("friends_noself")
            || lists.value("friends").intersects(lists.value("enemies")))
            return failure("inconsistent updatePlayers lists");
        if (managedUpdate) {
            if (!result.value("relation_matrix").isObject()) return failure("missing managed relation matrix");
            const auto matrix = result.value("relation_matrix").toObject();
            if (matrix.size() != players.size()) return failure("incomplete managed relation matrix");
            for (auto from = matrix.constBegin(); from != matrix.constEnd(); ++from) {
                if (!players.contains(from.key()) || !from.value().isObject()) return failure("invalid managed relation row");
                const auto targets = from.value().toObject();
                if (targets.size() != players.size()) return failure("incomplete managed relation row");
                for (auto to = targets.constBegin(); to != targets.constEnd(); ++to)
                    if (!players.contains(to.key()) || !to.value().isString() || !relations.contains(to.value().toString()))
                        return failure("invalid managed relation entry");
            }
        }
        const auto roleValues = result.value("role_values").toObject();
        for (auto it = roleValues.constBegin(); it != roleValues.constEnd(); ++it) {
            if (!players.contains(it.key()) || !it.value().isObject()) return failure("invalid updatePlayers role owner");
            const auto values = it.value().toObject();
            for (auto value = values.constBegin(); value != values.constEnd(); ++value)
                if ((!validRole(value.key()) && value.key() != "loaylist")
                    || !value.value().isDouble() || !std::isfinite(value.value().toDouble()))
                    return failure("invalid updatePlayers role value");
        }
        const auto roleEstimates = result.value("role_estimates").toObject();
        for (auto it = roleEstimates.constBegin(); it != roleEstimates.constEnd(); ++it)
            if (!players.contains(it.key()) || !it.value().isString() || (!inferredRoles.contains(it.value().toString()) && !validRole(it.value().toString())))
                return failure("invalid updatePlayers role estimate");
        const auto counts = result.value("role_counts").toObject();
        for (const char *key : {"loyalist", "rebel", "renegade"})
            if (!counts.contains(QString::fromLatin1(key))) return failure("missing updatePlayers role count");
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            int amount;
            if ((!validRole(it.key()) && !(managedUpdate && it.key() == "unknown")) || !it.value().isDouble()
                || !aiResultInteger(it.value().toDouble(), amount) || amount < 0 || amount > players.size())
                return failure("invalid updatePlayers role count");
        }
    }
    // Only a schema-validated result may publish this observational state.
    // Capture the original commit closure before prepare; never redispatch a
    // callback-replaced global. The commit itself is a no-allocation table swap.
    lua_pushvalue(mode, top + 2);
    lua_pushvalue(mode, tokenIndex);
    lua_sethook(mode, nullptr, 0, 0);
    const int commitStatus = LuaRuntime::protectedCall(mode, 1, 1, 0);
    lua_sethook(mode, previousHook, previousMask, previousCount);
    if (commitStatus != LUA_OK || !lua_isnil(mode, -1)) return failure("objective commit rejected");
    return result;
}

int AiLuaRuntime::luaModeObjective(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || (!runtime->m_activeRequest && !runtime->m_activeEventRequest))
        return luaL_error(state, "objective authority unavailable outside policy execution");
    const bool eventQuery = runtime->m_activeEventRequest != nullptr;
    QString target;
    if (lua_gettop(state) != (eventQuery ? 3 : 1) || lua_type(state, 1) != LUA_TSTRING || !readBoundedString(state, 1, target))
        return luaL_error(state, "invalid objective target or prefix");
    const AIRequest &request = *(eventQuery ? runtime->m_activeEventRequest : runtime->m_activeRequest);
    LuaRuntime *mode = eventQuery ? runtime->m_activeEventMode : runtime->m_room ? runtime->m_room->luaRuntime() : nullptr;
    if (!mode) return luaL_error(state, "objective mode runtime unavailable");
    if (runtime->m_room && !aiRound44RangeRequestCurrent(*runtime->m_room,request))
        return luaL_error(state, "stale objective query");
    QJsonArray prefixDeltas, prefixEffects;
    if (eventQuery) {
        QList<AiIntentionDelta> parsed;
        lua_pushvalue(state, 2);
        const bool validDeltas = parseIntentionDeltas(state, request.worldView, parsed);
        lua_pop(state, 1);
        if (!validDeltas || !readAIArray(state, 2, prefixDeltas, 64) || !readAIArray(state, 3, prefixEffects, 64)
            || !validateStrategyEffects(prefixEffects, request.worldView, parsed.size()))
            return luaL_error(state, "invalid event objective prefix");
    }
    const QJsonObject result = runtime->queryStandardMode(*mode, request, target, -1, false,
        eventQuery ? &prefixDeltas : nullptr, eventQuery ? &prefixEffects : nullptr);
    pushAIJsonValue(state, result);
    return 1;
}

int AiLuaRuntime::luaModeRefresh(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest)
        return luaL_error(state, "mode refresh unavailable outside decision");
    int count = -1;
    if (lua_gettop(state) > 1 || (!lua_isnoneornil(state, 1)
        && (lua_type(state, 1) != LUA_TNUMBER || !aiResultInteger(lua_tonumber(state, 1), count) || count < 0)))
        return luaL_error(state, "invalid previous owner plan count");
    const AIRequest &request = *runtime->m_activeRequest;
    if (request.kind != AIRequest::Activate)
        return luaL_error(state, "mode refresh requires activate decision");
    if (!runtime->m_room || !runtime->m_room->luaRuntime())
        return luaL_error(state, "mode refresh runtime unavailable");
    if (!aiRound44RangeRequestCurrent(*runtime->m_room,request))
        return luaL_error(state, "stale mode refresh");
    const QJsonObject result = runtime->queryStandardMode(*runtime->m_room->luaRuntime(), request, QString(), count, true);
    pushAIJsonValue(state, result);
    return 1;
}

int AiLuaRuntime::luaModeUpdatePlayers(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || (!runtime->m_activeRequest && !runtime->m_activeEventRequest))
        return luaL_error(state, "updatePlayers unavailable outside policy execution");
    const bool eventQuery = runtime->m_activeEventRequest != nullptr;
    int count;
    if (lua_gettop(state) != (eventQuery ? 4 : 2) || lua_type(state, 1) != LUA_TNUMBER
        || !aiResultInteger(lua_tonumber(state, 1), count) || count < 0 || lua_type(state, 2) != LUA_TBOOLEAN)
        return luaL_error(state, "invalid updatePlayers arguments");
    const AIRequest &request = *(eventQuery ? runtime->m_activeEventRequest : runtime->m_activeRequest);
    LuaRuntime *mode = eventQuery ? runtime->m_activeEventMode : runtime->m_room ? runtime->m_room->luaRuntime() : nullptr;
    if (!mode) return luaL_error(state, "updatePlayers mode runtime unavailable");
    if (runtime->m_room && !aiRound44RangeRequestCurrent(*runtime->m_room,request))
        return luaL_error(state, "stale updatePlayers query");
    QJsonArray prefixDeltas, prefixEffects;
    if (eventQuery) {
        QList<AiIntentionDelta> parsed;
        lua_pushvalue(state, 3);
        const bool validDeltas = parseIntentionDeltas(state, request.worldView, parsed);
        lua_pop(state, 1);
        if (!validDeltas || !readAIArray(state, 3, prefixDeltas, 64) || !readAIArray(state, 4, prefixEffects, 64)
            || !validateStrategyEffects(prefixEffects, request.worldView, parsed.size()))
            return luaL_error(state, "invalid updatePlayers event prefix");
    }
    const QJsonObject result = runtime->queryStandardMode(*mode, request, QString(), count, true,
        eventQuery ? &prefixDeltas : nullptr, eventQuery ? &prefixEffects : nullptr,
        true, lua_toboolean(state, 2));
    pushAIJsonValue(state, result);
    return 1;
}

int AiLuaRuntime::luaRefreshDiscardPreferences(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    if (!runtime || current() != runtime || !runtime->m_activeRequest || !runtime->m_conversionResolver)
        return luaL_error(state, "discard preference authority unavailable outside decision");
    const bool sourceAssignment = lua_gettop(state)==1 && lua_type(state,1)==LUA_TBOOLEAN && lua_toboolean(state,1);
    QJsonObject prefix;
    const bool sourcePrefix = lua_gettop(state)==1 && lua_istable(state,1);
    if (sourcePrefix) {
        QJsonValue value;int budget=16;
        if (!readAIJson(state,1,value,budget) || !value.isObject())
            return luaL_error(state,"ordinary card-choice prefix must be a finite object");
        prefix=value.toObject();
        if (prefix.value("op")=="foreign_keep_trivial" && prefix.value("ids").isObject() && prefix.value("ids").toObject().isEmpty())
            prefix.insert("ids",QJsonArray{});
        const bool increment = prefix.size()==2 && prefix.value("op")=="increment_throw" && prefix.value("key").isString();
        const bool trivialKeep = prefix.size()==3 && prefix.value("op")=="foreign_keep_trivial"
            && prefix.value("owner").isString() && prefix.value("ids").isArray() && prefix.value("ids").toArray().size()<=1;
        if (!increment && !trivialKeep)
            return luaL_error(state,"ordinary card-choice prefix outside audited operation");
    } else if (lua_gettop(state)!=0 && !sourceAssignment)
        return luaL_error(state, "discard preference refresh accepts only true source assignment");
    const AIRequest &request = *runtime->m_activeRequest;
    if (request.viewerObjectName != request.worldView.self.objectName
        || (runtime->m_room && !aiRound44RangeRequestCurrent(*runtime->m_room,request)))
        return luaL_error(state, "stale or invalid discard preference viewer");
    if(sourcePrefix) {
        prefix.insert("kind","ordinary_card_choice_prefix");
        pushAIJsonValue(state,runtime->m_conversionResolver(request,prefix));
    } else pushAIJsonValue(state, runtime->m_conversionResolver(request, {{"kind", sourceAssignment ? "ordinary_card_choice_preferences" : "refresh_discard_preferences"}}));
    return 1;
}

int AiLuaRuntime::luaOriginalDiscardError(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    const auto fail=[&](const char*why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","Original.Discard.error_result"}});return 1;};
    if(!runtime || current()!=runtime || !runtime->m_room || !runtime->m_activeRequest
        || runtime->m_activeEventRequest || runtime->m_instructionLimitExceeded
        || runtime->m_originalDiscardResultReady || lua_gettop(state)!=1)
        return fail("Original discard error requires its single active question");
    const AIRequest &r=*runtime->m_activeRequest;
    ServerPlayer *owner=runtime->m_room->findPlayerByObjectName(r.viewerObjectName,true);
    const auto &o=r.choiceOptions;
    const QJsonObject c=o.context;
    if(r.kind!=AIRequest::Discard || !aiRound35OriginalQuestion(r) || !owner
        || r.stateRevision!=runtime->m_room->roomRuntime()->stateRevision()
        || o.minCount<0 || o.maxCount<o.minCount
        || !c.value("include_equip").isBool() || !c.value("exchange").isBool()
        || !c.value("pattern").isString() || c.value("pattern").toString()!=r.pattern
        || !c.value("reason").isString() || c.value("reason").toString()!=o.reason
        || c.value("exchange").toBool()!=owner->hasFlag("Global_AIDiscardExchanging"))
        return fail("Original discard arguments or owner state are not current");
    if(lua_type(state,1)==LUA_TSTRING && lua_rawlen(state,1)>AiMaxResultStringBytes)
        return fail("Original discard diagnostic exceeds bounded result size");
    // Same helper and Room sink as LuaAI::askForDiscard after protectedCall.
    // Only the isolated source filename/stack differs; never call __tostring.
    const QString diagnostic=luaErrorWithTraceback(state);
    runtime->m_room->output(diagnostic);
    if(r.stateRevision!=runtime->m_room->roomRuntime()->stateRevision())
        return fail("Discard source diagnostic changed current gameplay state");
    QList<int> ids;
    {
        LuaRuntime::Binding game(runtime->m_room->roomRuntime()->lua(),false);
        GameRng::Binding gameplay(runtime->m_room->roomRuntime()->rng());
        // Preserve TrustAI's surprising exchange->is_discard argument exactly.
        // forceToDiscard shuffles the complete owner domain before filtering.
        if(!o.optional) ids=owner->forceToDiscard(o.minCount,c.value("include_equip").toBool(),
            owner->hasFlag("Global_AIDiscardExchanging"),r.pattern);
    }
    if(r.stateRevision!=runtime->m_room->roomRuntime()->stateRevision())
        return fail("Native discard fallback changed current gameplay state");
    runtime->m_originalDiscardResultReady=true;
    runtime->m_originalDiscardResultIds=ids;
    runtime->m_originalDiscardResultDecision=r.decisionId;
    runtime->m_originalDiscardResultRevision=r.stateRevision;
    runtime->m_originalDiscardMinimum=o.minCount;
    runtime->m_originalDiscardMaximum=o.maxCount;
    runtime->m_originalDiscardOptional=o.optional;
    runtime->m_originalDiscardIncludeEquip=c.value("include_equip").toBool();
    runtime->m_originalDiscardExchange=c.value("exchange").toBool();
    runtime->m_originalDiscardPattern=r.pattern;
    runtime->m_originalDiscardReason=o.reason;
    runtime->m_originalDiscardViewer=r.viewerObjectName;
    QJsonArray cards;for(int id:ids)cards.append(id);
    pushAIJsonValue(state,QJsonObject{{"kind","original_discard_error_result"},{"cards",cards}});
    return 1;
}

int AiLuaRuntime::luaOriginalSuitResult(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    const auto fail=[&](const char*why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","Original.Suit.result"}});return 1;};
    if (!runtime || current()!=runtime || !runtime->m_room || !runtime->m_activeRequest
        || runtime->m_activeEventRequest || runtime->m_originalSuitResultReady || runtime->m_instructionLimitExceeded)
        return fail("Original Suit result requires its one active question");
    const AIRequest &request=*runtime->m_activeRequest;
    if (request.kind!=AIRequest::Suit || !aiRound35OriginalQuestion(request)
        || request.stateRevision!=runtime->m_room->roomRuntime()->stateRevision()
        || !runtime->m_room->findPlayerByObjectName(request.viewerObjectName,true))
        return fail("Original Suit question identity is unavailable");
    if (lua_gettop(state)!=2 || lua_type(state,2)!=LUA_TBOOLEAN)
        return fail("Original Suit result arguments are malformed");
    if (lua_type(state,1)==LUA_TSTRING && lua_rawlen(state,1)>AiMaxResultStringBytes)
        return fail("Original Suit string result exceeds source boundary");
    const bool ordinaryError=lua_toboolean(state,2);
    if(ordinaryError) {
        // swig/ai.i Suit logs the raw lua_tostring error before TrustAI RNG.
        const char *message=lua_tostring(state,1);
        runtime->m_room->output(QString::fromUtf8(message?message:""));
        if(request.stateRevision!=runtime->m_room->roomRuntime()->stateRevision())
            return fail("Suit diagnostic changed current gameplay state");
    }
    int value;
    if (!ordinaryError && lua_isnumber(state,1)) {
        // Exact fixed swig/ai.i: numeric strings accepted; a nonintegral Lua
        // number follows lua_tointeger's own result. The native int conversion
        // is deliberately the same operation, not a policy-side approximation.
        value=int(lua_tointeger(state,1));
        if (value<int(Card::SuitToBeDecided) || value>int(Card::NoSuit))
            return fail("Source Suit result is outside the defined native enum");
    } else {
        // TrustAI::askForSuit is gameplay RNG, not the isolated AI math stream.
        // No TrustAI object/callback, card authority or legacy Lua is invoked.
        value=int(Card::AllSuits[runtime->m_room->roomRuntime()->rng().bounded(4)]);
    }
    runtime->m_originalSuitResultReady=true;
    runtime->m_originalSuitResultValue=value;
    runtime->m_originalSuitResultDecision=request.decisionId;
    runtime->m_originalSuitResultRevision=request.stateRevision;
    pushAIJsonValue(state,QJsonObject{{"kind","original_suit_result"},{"value",value}});
    return 1;
}

int AiLuaRuntime::luaSourceCompatibility(lua_State *state)
{
    AiLuaRuntime *runtime=fromUpvalue(state);
    const auto fail=[&](const char *why){pushAIJsonValue(state,QJsonObject{{"error",why},{"key","source_compatibility"}});return 1;};
    if(!runtime || current()!=runtime || !runtime->m_sourceCompatibility
        || (!runtime->m_activeRequest && !runtime->m_activeEventRequest))
        return fail("source compatibility requires active policy invocation");
    const int operation=int(lua_tointeger(state,lua_upvalueindex(2)));
    QJsonObject result;
    if(operation==1){
        QString key;
        if(lua_gettop(state)!=1 || lua_type(state,1)!=LUA_TSTRING || !readBoundedString(state,1,key))return fail("source clock key malformed");
        result=runtime->m_sourceCompatibility->clock(key);
    }else if(operation==2){
        int upper;
        if(lua_gettop(state)!=1 || lua_type(state,1)!=LUA_TNUMBER || !aiResultInteger(lua_tonumber(state,1),upper))return fail("source random upper malformed");
        result=runtime->m_sourceCompatibility->gaodaRandom(runtime->m_rng,upper);
    }else if(operation==3){
        QString package,text;
        if(lua_gettop(state)!=2 || lua_type(state,1)!=LUA_TSTRING || lua_type(state,2)!=LUA_TSTRING
            || !readBoundedString(state,1,package) || !readBoundedString(state,2,text))return fail("source numeric text malformed");
        result=runtime->m_sourceCompatibility->numericCosts(package,text);
    }else return fail("source compatibility operation unavailable");
    pushAIJsonValue(state,result);return 1;
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
    lua_pushcfunction(state, luaFindKnowledgeEstimate);
    lua_setglobal(state, "ai_find_knowledge_estimate");
    lua_pushcfunction(state, luaFacadeIndex);
    lua_setglobal(state, "ai_facade_index");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaResolveConversion, 1);
    lua_setglobal(state, "ai_resolve_conversion");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaHeqiaSavedPlan,1);lua_setglobal(state,"ai_heqia_saved_plan");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaLeiluanSavedPlan,1);lua_setglobal(state,"ai_leiluan_saved_plan");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaJianmanSavedPlan,1);lua_setglobal(state,"ai_jianman_saved_plan");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaFaqiSavedPlan,1);lua_setglobal(state,"ai_faqi_saved_plan");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaNewgeneralsSavedSlot,1);lua_setglobal(state,"ai_newgenerals_saved_slot");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaOlClanSavedSlot,1);lua_setglobal(state,"ai_olclan_saved_slot");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaXufaActualSlot,1);lua_setglobal(state,"ai_xufa_actual_slot");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaXuanzhuActualSlot,1);lua_setglobal(state,"ai_xuanzhu_actual_slot");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaResolveSkillAction, 1);
    lua_setglobal(state, "ai_resolve_skill_action");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaBindJieyinghQuestion, 1);
    lua_setglobal(state, "ai_bind_jieyingh_question");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaRangyiQuestion,1);lua_setglobal(state,"ai_rangyi_question");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaUseValueFlag, 1);
    lua_setglobal(state, "ai_use_value_flag");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateExtraShicai, 1);
    lua_setglobal(state, "ai_evaluate_extra_shicai");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaOverseasPriority, 1);
    lua_setglobal(state, "ai_overseas_priority");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaOLJijiangState, 1);
    lua_setglobal(state, "ai_ol_jijiang_state");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaOverseasXiafengFlag, 1);
    lua_setglobal(state, "ai_overseas_xiafeng_flag");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaBindZhifengQuestion, 1);
    lua_setglobal(state, "ai_bind_zhifeng_question");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaBindOverseasNiwoSelected,1);
    lua_setglobal(state,"ai_bind_overseas_niwo_selected");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaSelectSijyuFengyiPhysical,1);
    lua_setglobal(state,"ai_sijyu_fengyi_select");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaObserveOverseasLihuo,1);
    lua_setglobal(state,"ai_observe_overseas_lihuo");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateSkillCard, 1);
    lua_setglobal(state, "ai_evaluate_skill_card");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaInspectNestedCardaskString, 1);
    lua_setglobal(state, "ai_inspect_nested_cardask_string");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaMatchOwnerDiscardPattern, 1);
    lua_setglobal(state, "ai_match_owner_discard_pattern");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateNengchenPreview, 1);
    lua_setglobal(state, "ai_evaluate_nengchen_preview");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateNotifiedCard, 1);
    lua_setglobal(state, "ai_evaluate_notified_card");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaRushBPhysicalSkill, 1);
    lua_setglobal(state, "ai_rushb_physical_skill");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaRecordNestedDelegateUse, 1);
    lua_setglobal(state, "ai_record_nested_delegate_use");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateSkillCard, 1);
    lua_setglobal(state, "ai_evaluate_owner_view_as");
    lua_pushlightuserdata(state,this);lua_pushboolean(state,true);
    lua_pushcclosure(state,&AiLuaRuntime::luaOlClanPlanObservation,2);
    lua_setglobal(state,"ai_capture_olclan_plan");
    lua_pushlightuserdata(state,this);lua_pushboolean(state,false);
    lua_pushcclosure(state,&AiLuaRuntime::luaOlClanPlanObservation,2);
    lua_setglobal(state,"ai_restore_olclan_plan");
    lua_pushlightuserdata(state,this);lua_pushinteger(state,2);
    lua_pushcclosure(state,&AiLuaRuntime::luaOlClanPlanObservation,2);
    lua_setglobal(state,"ai_allocate_olclan_policy_card");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateSkillCard, 1);
    lua_setglobal(state, "ai_evaluate_tongyi_policy");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaOriginalAocaiPattern, 1);
    lua_setglobal(state, "ai_original_aocai_pattern");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateTianyiSlash, 1);
    lua_setglobal(state, "ai_evaluate_tianyi_slash");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaWithOwnerRange, 1);
    lua_setglobal(state, "ai_with_owner_range");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaWithOwnerDistance, 1);
    lua_setglobal(state, "ai_with_owner_distance");
    lua_pushlightuserdata(state,this);
    lua_pushcclosure(state,&AiLuaRuntime::luaOriginalSuitResult,1);
    lua_setglobal(state,"ai_original_suit_result");
    lua_pushlightuserdata(state,this);
    lua_pushcclosure(state,&AiLuaRuntime::luaOriginalDiscardError,1);
    lua_setglobal(state,"ai_original_discard_error");
    const char *sourceNames[]={"ai_source_clock","ai_source_gaoda_random","ai_source_wire_ids"};
    for(int operation=1;operation<=3;++operation){
        lua_pushlightuserdata(state,this);lua_pushinteger(state,operation);
        lua_pushcclosure(state,&AiLuaRuntime::luaSourceCompatibility,2);
        lua_setglobal(state,sourceNames[operation-1]);
    }
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateOwnerRangeCard, 1);
    lua_setglobal(state, "ai_evaluate_owner_range_card");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaQiuxinTargetFilter, 1);
    lua_setglobal(state, "ai_qiuxin_target_filter");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluatePreviewCard, 1);
    lua_setglobal(state, "ai_evaluate_preview_card");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaBindPreviewPhysical, 1);
    lua_setglobal(state, "ai_bind_preview_physical");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaResponseCatalogue, 1);
    lua_setglobal(state, "ai_response_catalogue");
    lua_pushlightuserdata(state,this);
    lua_pushcclosure(state,&AiLuaRuntime::luaChoiceContinuity,1);
    lua_setglobal(state,"ai_response_choice_continuity");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateResponsePolicy, 1);
    lua_setglobal(state, "ai_evaluate_response_policy");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaTargetReviseProperty, 1);
    lua_setglobal(state, "ai_target_revise_property");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaTargetRecommendCache, 1);
    lua_setglobal(state, "ai_target_recommend_cache");
    lua_pushlightuserdata(state,this);
    lua_pushcclosure(state,&AiLuaRuntime::luaOriginalSourceWarning,1);
    lua_setglobal(state,"ai_original_source_warning");
    lua_pushlightuserdata(state,this);
    lua_pushcclosure(state,&AiLuaRuntime::luaImagineSourceDiagnostic,1);
    lua_setglobal(state,"ai_imagine_source_diagnostic");
    lua_pushlightuserdata(state,this);
    lua_pushcclosure(state,&AiLuaRuntime::luaInovationSourceDiagnostic,1);
    lua_setglobal(state,"ai_inovation_source_diagnostic");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaInovationGoutong,1);lua_setglobal(state,"ai_inovation_goutong");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaInovationRemaining,1);lua_setglobal(state,"ai_inovation_remaining");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaSourceImmediateEffect,1);lua_setglobal(state,"ai_source_immediate_effect");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaPoxiSourceSlot,1);lua_setglobal(state,"ai_poxi_source_slot");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaCurrentSourceSlot,1);lua_setglobal(state,"ai_current_source_slot");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaPeiqiSourceRecord,1);lua_setglobal(state,"ai_peiqi_source_record");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaDaojiSourceCard,1);lua_setglobal(state,"ai_daoji_source_card");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaManjuanSource,1);lua_setglobal(state,"ai_manjuan_source");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaInovationXinyang,1);lua_setglobal(state,"ai_inovation_xinyang");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaInovationShoushi,1);lua_setglobal(state,"ai_inovation_shoushi");
    lua_pushlightuserdata(state,this);
    lua_pushcclosure(state,&AiLuaRuntime::luaLianliCardaskError,1);
    lua_setglobal(state,"ai_lianli_cardask_error");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaOriginalKurouShadow,1);
    lua_setglobal(state,"ai_original_kurou_shadow");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaManbingFlag,1);
    lua_setglobal(state,"ai_manbing_flag");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaOriginalTurnUsePresence,1);
    lua_setglobal(state,"ai_original_turn_use_presence");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaOriginalCrossbowShadow,1);
    lua_setglobal(state,"ai_original_crossbow_shadow");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaOriginalGuanxingShadow,1);
    lua_setglobal(state,"ai_original_guanxing_shadow");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaOriginalSDChoice,1);
    lua_setglobal(state,"ai_original_sd_choice");
    lua_pushlightuserdata(state,this);lua_pushcclosure(state,&AiLuaRuntime::luaOriginalCardChosenResult,1);
    lua_setglobal(state,"ai_original_cardchosen_result");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateZenhuiCard, 1);
    lua_setglobal(state, "ai_evaluate_zenhui_card");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaCardTargetFilter, 1);
    lua_setglobal(state, "ai_card_target_filter");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaConsumePlayerHint, 1);
    lua_setglobal(state, "ai_consume_player_hint");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateSkillSeed, 1);
    lua_setglobal(state, "ai_evaluate_skill_seed");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaSampleHiddenHand, 1);
    lua_setglobal(state, "ai_sample_hidden_hand");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaSampleOwnHand, 1);
    lua_setglobal(state, "ai_sample_own_hand");
    // Card::getName reads only the public translation catalog. It does not
    // resolve card IDs, userdata, player state or any gameplay authority.
    lua_pushcfunction(state, [](lua_State *state) -> int {
        QString key;
        if (lua_gettop(state) != 1 || lua_type(state, 1) != LUA_TSTRING
            || !readBoundedString(state, 1, key) || key.size() > 256)
            return luaL_error(state, "invalid public card-name key");
        const QByteArray name = Sanguosha->translate(key).toUtf8();
        lua_pushlstring(state, name.constData(), size_t(name.size()));
        return 1;
    });
    lua_setglobal(state, "ai_translate_card_name");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaModeObjective, 1);
    lua_setglobal(state, "ai_mode_objective");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaModeRefresh, 1);
    lua_setglobal(state, "ai_mode_refresh");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaModeUpdatePlayers, 1);
    lua_setglobal(state, "ai_mode_update_players");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaRefreshDiscardPreferences, 1);
    lua_setglobal(state, "ai_refresh_discard_preferences");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaConsumeDeckCountShuffle, 1);
    lua_setglobal(state, "ai_consume_deck_count_shuffle");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaSamplePatternLogName, 1);
    lua_setglobal(state, "ai_sample_pattern_log_name");
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
        || !setMetaEnumFields(state, Card::staticMetaObject, "Suit", "Card_")
        // Legacy-style callbacks receive the handling method as their third argument.
        || !setMetaEnumFields(state, Card::staticMetaObject, "HandlingMethod", "Card_")
        || !setMetaEnumFields(state, Player::staticMetaObject, "Place", "Player_")
        || !setMetaEnumFields(state, Card::staticMetaObject, "CardType", "Card_")
        || !setMetaEnumFields(state, Skill::staticMetaObject, "Frequency", "Skill_")
        || !setMetaEnumFields(state, General::staticMetaObject, "Gender", "General_")) {
        lua_pop(state, 1);
        if (error)
            *error = QStringLiteral("AI-safe meta enum is unavailable");
        return false;
    }
    lua_pushinteger(state, Room::GuanxingBothSides); lua_setfield(state, -2, "Room_GuanxingBothSides");
    lua_pushinteger(state, Room::GuanxingUpOnly); lua_setfield(state, -2, "Room_GuanxingUpOnly");
    lua_pushinteger(state, Room::GuanxingDownOnly); lua_setfield(state, -2, "Room_GuanxingDownOnly");
    // CardUseStruct likewise has no Q_ENUM. Source strategy branches and the
    // certified nested Play context compare these actual native constants.
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_UNKNOWN", CardUseStruct::CARD_USE_REASON_UNKNOWN);
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_PLAY", CardUseStruct::CARD_USE_REASON_PLAY);
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_RESPONSE", CardUseStruct::CARD_USE_REASON_RESPONSE);
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_RESPONSE_USE", CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    // DamageStruct is a value struct without Q_ENUM. Export its real constants
    // explicitly so damage hooks never rely on duplicated Lua enum numbers.
    setIntegerField(state, "DamageStruct_Normal", DamageStruct::Normal);
    setIntegerField(state, "DamageStruct_Fire", DamageStruct::Fire);
    setIntegerField(state, "DamageStruct_Thunder", DamageStruct::Thunder);
    setIntegerField(state, "DamageStruct_Ice", DamageStruct::Ice);
    setIntegerField(state, "DamageStruct_Poison", DamageStruct::Poison);
    setIntegerField(state, "DamageStruct_God", DamageStruct::God);
    setIntegerField(state, "TargetSpecified", TargetSpecified);
    setIntegerField(state, "TargetConfirmed", TargetConfirmed);
    setIntegerField(state, "DamageInflicted", DamageInflicted);
    setIntegerField(state, "HpRecover", HpRecover);
    setIntegerField(state, "Death", Death);
    setIntegerField(state, "ChoiceMade", ChoiceMade);
    setIntegerField(state, "EventPhaseStart", EventPhaseStart);
    setIntegerField(state, "EventPhaseEnd", EventPhaseEnd);
    setIntegerField(state, "TurnStart", TurnStart);
    setIntegerField(state, "EventPhaseChanging", EventPhaseChanging);
    setIntegerField(state, "CardFinished", CardFinished);
    setIntegerField(state, "Dying", Dying);
    setIntegerField(state, "DamageForseen", DamageForseen);
    setIntegerField(state, "CardOffset", CardOffset);
    setIntegerField(state, "GameStart", GameStart);
    setIntegerField(state, "Damaged", Damaged);
    setIntegerField(state, "FinishJudge", FinishJudge);
    setIntegerField(state, "DrawNCards", DrawNCards);
    setIntegerField(state, "AfterDrawNCards", AfterDrawNCards);
    setIntegerField(state, "GameReady", GameReady);
    setIntegerField(state, "BeforeGameOverJudge", BeforeGameOverJudge);
    setIntegerField(state, "Revived", Revived);
    setIntegerField(state, "RoundStart", RoundStart);
    setIntegerField(state, "CardEffect", CardEffect);
    setIntegerField(state, "PreCardUsed", PreCardUsed);
    setIntegerField(state, "StartJudge", StartJudge);
    setIntegerField(state, "AskForRetrial", AskForRetrial);
    setIntegerField(state, "CardsMoveOneTime", CardsMoveOneTime);
    setIntegerField(state, "ShowCards", ShowCards);
    setIntegerField(state, "CardMoveReason_S_REASON_UNKNOWN", CardMoveReason::S_REASON_UNKNOWN);
    setIntegerField(state, "CardMoveReason_S_REASON_USE", CardMoveReason::S_REASON_USE);
    setIntegerField(state, "CardMoveReason_S_REASON_RESPONSE", CardMoveReason::S_REASON_RESPONSE);
    setIntegerField(state, "CardMoveReason_S_REASON_DISCARD", CardMoveReason::S_REASON_DISCARD);
    setIntegerField(state, "CardMoveReason_S_REASON_RECAST", CardMoveReason::S_REASON_RECAST);
    setIntegerField(state, "CardMoveReason_S_REASON_PINDIAN", CardMoveReason::S_REASON_PINDIAN);
    setIntegerField(state, "CardMoveReason_S_REASON_DRAW", CardMoveReason::S_REASON_DRAW);
    setIntegerField(state, "CardMoveReason_S_REASON_GOTCARD", CardMoveReason::S_REASON_GOTCARD);
    setIntegerField(state, "CardMoveReason_S_REASON_SHOW", CardMoveReason::S_REASON_SHOW);
    setIntegerField(state, "CardMoveReason_S_REASON_TRANSFER", CardMoveReason::S_REASON_TRANSFER);
    setIntegerField(state, "CardMoveReason_S_REASON_PUT", CardMoveReason::S_REASON_PUT);
    setIntegerField(state, "CardMoveReason_S_REASON_LETUSE", CardMoveReason::S_REASON_LETUSE);
    setIntegerField(state, "CardMoveReason_S_REASON_RETRIAL", CardMoveReason::S_REASON_RETRIAL);
    setIntegerField(state, "CardMoveReason_S_REASON_RULEDISCARD", CardMoveReason::S_REASON_RULEDISCARD);
    setIntegerField(state, "CardMoveReason_S_REASON_THROW", CardMoveReason::S_REASON_THROW);
    setIntegerField(state, "CardMoveReason_S_REASON_DISMANTLE", CardMoveReason::S_REASON_DISMANTLE);
    setIntegerField(state, "CardMoveReason_S_REASON_GIVE", CardMoveReason::S_REASON_GIVE);
    setIntegerField(state, "CardMoveReason_S_REASON_EXTRACTION", CardMoveReason::S_REASON_EXTRACTION);
    setIntegerField(state, "CardMoveReason_S_REASON_GOTBACK", CardMoveReason::S_REASON_GOTBACK);
    setIntegerField(state, "CardMoveReason_S_REASON_RECYCLE", CardMoveReason::S_REASON_RECYCLE);
    setIntegerField(state, "CardMoveReason_S_REASON_ROB", CardMoveReason::S_REASON_ROB);
    setIntegerField(state, "CardMoveReason_S_REASON_PREVIEWGIVE", CardMoveReason::S_REASON_PREVIEWGIVE);
    setIntegerField(state, "CardMoveReason_S_REASON_EXCLUSIVE", CardMoveReason::S_REASON_EXCLUSIVE);
    setIntegerField(state, "CardMoveReason_S_REASON_TURNOVER", CardMoveReason::S_REASON_TURNOVER);
    setIntegerField(state, "CardMoveReason_S_REASON_JUDGE", CardMoveReason::S_REASON_JUDGE);
    setIntegerField(state, "CardMoveReason_S_REASON_PREVIEW", CardMoveReason::S_REASON_PREVIEW);
    setIntegerField(state, "CardMoveReason_S_REASON_DEMONSTRATE", CardMoveReason::S_REASON_DEMONSTRATE);
    setIntegerField(state, "CardMoveReason_S_REASON_SWAP", CardMoveReason::S_REASON_SWAP);
    setIntegerField(state, "CardMoveReason_S_REASON_OVERRIDE", CardMoveReason::S_REASON_OVERRIDE);
    setIntegerField(state, "CardMoveReason_S_REASON_EXCHANGE_FROM_PILE", CardMoveReason::S_REASON_EXCHANGE_FROM_PILE);
    setIntegerField(state, "CardMoveReason_S_REASON_NATURAL_ENTER", CardMoveReason::S_REASON_NATURAL_ENTER);
    setIntegerField(state, "CardMoveReason_S_REASON_REMOVE_FROM_PILE", CardMoveReason::S_REASON_REMOVE_FROM_PILE);
    setIntegerField(state, "CardMoveReason_S_REASON_JUDGEDONE", CardMoveReason::S_REASON_JUDGEDONE);
    setIntegerField(state, "CardMoveReason_S_REASON_CHANGE_EQUIP", CardMoveReason::S_REASON_CHANGE_EQUIP);
    setIntegerField(state, "CardMoveReason_S_REASON_SHUFFLE", CardMoveReason::S_REASON_SHUFFLE);
    setIntegerField(state, "CardMoveReason_S_REASON_PUT_END", CardMoveReason::S_REASON_PUT_END);
    setIntegerField(state, "CardMoveReason_S_MASK_BASIC_REASON", CardMoveReason::S_MASK_BASIC_REASON);

    lua_setglobal(state, "sgs");
    return true;
}

// Only the callbacks that actually build an AIRequest can be routed.
static const QStringList &aiRoutableCallbackNames()
{
    static const QStringList names = QStringList()
        << QStringLiteral("activate") << QStringLiteral("askForUseCard")
        << QStringLiteral("askForSkillInvoke") << QStringLiteral("askForChoice")
        << QStringLiteral("askForSuit") << QStringLiteral("askForKingdom")
        << QStringLiteral("askForGeneral") << QStringLiteral("askForDiscard")
        << QStringLiteral("askForAG") << QStringLiteral("askForCardChosen")
        << QStringLiteral("askForYiji") << QStringLiteral("askForPlayerChosen")
        << QStringLiteral("askForPlayersChosen") << QStringLiteral("askForCard")
        << QStringLiteral("askForNullification") << QStringLiteral("askForCardShow")
        << QStringLiteral("askForPindian") << QStringLiteral("askForSinglePeach")
        << QStringLiteral("askForGuanxing") << QStringLiteral("askForTriggerOrder");
    return names;
}

static void loadConfiguredAiRoutes(AiRouteRegistry &routes)
{
    // The imported HEG callbacks run through the room-local legacy adapter.
    // Apply explicit user routes afterward, including per-skill isolated overrides.
    if (Config.EnableHegemony) {
        for (const QString &callback : aiRoutableCallbackNames())
            routes.setCallbackRoute(callback, QString(), AiRouteLegacyAdapted);
    }
    const auto addRoutes = [&routes](const QString &key, AiRoute route) {
        foreach (const QString &entry, Config.value(key).toStringList()) {
            const QStringList parts = entry.split(QChar(':'));
            const QString callbackName = parts.value(0).trimmed();
            if (!aiRoutableCallbackNames().contains(callbackName))
                continue;
            routes.setCallbackRoute(callbackName, parts.value(1).trimmed(), route);
        }
    };
    addRoutes(QStringLiteral("AiLegacyDirectCallbacks"), AiRouteLegacyDirect);
    addRoutes(QStringLiteral("AiLegacyAdaptedCallbacks"), AiRouteLegacyAdapted);
    addRoutes(QStringLiteral("AiIsolatedCallbacks"), AiRouteIsolated);
}

void AiLuaRuntime::loadConfiguredRoutes()
{
    loadConfiguredAiRoutes(m_routes);
}

bool AiLuaRuntime::requiresLegacyRuntime()
{
    // Bootstrap admission uses the same parser and override order as decisions.
    AiRouteRegistry routes;
    loadConfiguredAiRoutes(routes);
    return routes.hasLegacyRoutes();
}

bool AiLuaRuntime::loadIsolatedScript(const QString &fileName, QString *error)
{
    static const QRegularExpression fileNamePattern(
        QStringLiteral("^[A-Za-z0-9_-]+\\.lua$"));
    if (!fileNamePattern.match(fileName).hasMatch()) {
        if (error)
            *error = QStringLiteral("Invalid isolated AI script name: %1").arg(fileName);
        return false;
    }
    return loadScriptWithBudget(QStringLiteral("lua/ai/isolated/%1").arg(fileName),
                                m_initializationInstructionBudget, error);
}

QStringList AiLuaRuntime::declaredCoreScripts(QString *error)
{
    // isolated-bootstrap.lua's ai_isolated_core, the dispatchers without which the
    // runtime cannot answer a single request.  Declaring them in Lua rather than here
    // is the point: the list travels with the AI scripts it names.
    lua_State *state = m_lua.state();
    if (!state) {
        if (error)
            *error = QStringLiteral("AI Lua runtime is not bound");
        return QStringList();
    }
    const int top = lua_gettop(state);
    lua_getglobal(state, "ai_isolated_core");
    if (!lua_istable(state, -1)) {
        lua_settop(state, top);
        if (error)
            *error = QStringLiteral("lua/ai/isolated-bootstrap.lua declares no "
                                    "ai_isolated_core script list");
        return QStringList();
    }
    QStringList names;
    const int length = int(lua_rawlen(state, -1));
    for (int index = 1; index <= length; ++index) {
        lua_rawgeti(state, -1, index);
        if (lua_type(state, -1) != LUA_TSTRING) {
            lua_settop(state, top);
            if (error)
                *error = QStringLiteral("ai_isolated_core entry %1 is not a file name")
                    .arg(index);
            return QStringList();
        }
        names << QString::fromUtf8(lua_tostring(state, -1));
        lua_pop(state, 1);
    }
    lua_settop(state, top);
    if (names.isEmpty() && error)
        *error = QStringLiteral("ai_isolated_core is empty");
    return names;
}

void AiLuaRuntime::loadPackageScripts()
{
    // One handler file per enabled package, found by name the way smart-ai.lua finds
    // <package>-ai.lua in lua/ai: compare case-insensitively, then load the real
    // filename, because package object names and files disagree on case.  Nothing is
    // enumerated in C++ beyond that rule, so a package ships its isolated AI simply by
    // adding lua/ai/isolated/<package>-ai.lua upstream.
    if (!Sanguosha)
        return;
    QHash<QString, QString> available;
    // Qt 5 foreach inspects the expression with decltype; v141 rejects the
    // QStringLiteral lambda there, so materialize the unchanged listing first.
    const QStringList entries = QDir(QStringLiteral("lua/ai/isolated"))
        .entryList(QStringList(QStringLiteral("*.lua")), QDir::Files);
    foreach (const QString &entry, entries)
        available.insert(entry.toLower(), entry);

    // The donor also scans registered scenario names after package handlers.
    // Use the same data-driven filename rule; no policy names live in host code.
    QStringList sourceNames = Sanguosha->getExtensions();
    sourceNames.append(Sanguosha->getModScenarioNames());
    sourceNames.removeDuplicates();
    foreach (const QString &package, sourceNames) {
        const QString fileName =
            available.value(package.toLower() + QStringLiteral("-ai.lua"));
        if (fileName.isEmpty())
            continue;
        // Some legacy common policies are loaded unconditionally by the Lua
        // bootstrap. Do not register them twice if an extension shares a name.
        if (declaredCoreScripts(nullptr).contains(fileName, Qt::CaseInsensitive))
            continue;
        // A broken package handler costs that package its isolated AI and nothing
        // else, matching smart-ai.lua's pcall around each package AI: one damaged file
        // must not leave the whole runtime without a dispatcher.
        QString packageError;
        if (!loadIsolatedScript(fileName, &packageError)) {
            qWarning() << "isolated AI script load failed:" << fileName << packageError;
            m_instructionLimitExceeded = false;
        }
    }
}

bool AiLuaRuntime::loadConfiguredScripts(QString *error)
{
    // An explicit AiIsolatedScripts wins and is taken literally, empty list included:
    // that is what the isolation tests use to stand a runtime up with one script, or
    // none.  Without it the runtime is assembled from what the Lua tree declares and
    // which packages are enabled, so no deployment depends on a local config.ini.
    if (Config.contains(QStringLiteral("AiIsolatedScripts"))) {
        const QStringList configuredScripts =
            Config.value(QStringLiteral("AiIsolatedScripts")).toStringList();
        bool trustedChoicePrefix = true;
        for (const QString &configuredName : configuredScripts) {
            const QString name=configuredName.trimmed();
            if (!loadIsolatedScript(name, error))
                return false;
            if (name==QStringLiteral("ask-for-choice.lua")) {
                if (trustedChoicePrefix && m_room) captureAIRound41ChoiceProxies(*m_room,m_lua.state());
                trustedChoicePrefix=false;
            } else if (name!=QStringLiteral("ask-for-use-card.lua")) trustedChoicePrefix=false;
        }
        return true;
    }

    const QStringList coreScripts = declaredCoreScripts(error);
    if (coreScripts.isEmpty())
        return false;
    bool trustedChoicePrefix = true;
    foreach (const QString &fileName, coreScripts) {
        if (!loadIsolatedScript(fileName, error))
            return false;
        // Attest the actual just-loaded dispatcher before package code can forge
        // its source name/lines or replace its closure/backing/metatable objects.
        if (fileName==QStringLiteral("ask-for-choice.lua")) {
            if (trustedChoicePrefix && m_room) captureAIRound41ChoiceProxies(*m_room,m_lua.state());
            trustedChoicePrefix=false;
        } else if (fileName!=QStringLiteral("ask-for-use-card.lua")) trustedChoicePrefix=false;
    }
    loadPackageScripts();
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

static const char *aiDecisionKindName(AIRequest::DecisionKind kind)
{
    switch (kind) {
    case AIRequest::Activate: return "activate";
    case AIRequest::UseCard: return "use_card";
    case AIRequest::SkillInvoke: return "skill_invoke";
    case AIRequest::Choice: return "choice";
    case AIRequest::Suit: return "suit";
    case AIRequest::Kingdom: return "kingdom";
    case AIRequest::General: return "general";
    case AIRequest::Discard: return "discard";
    case AIRequest::AmazingGrace: return "amazing_grace";
    case AIRequest::CardChosen: return "card_chosen";
    case AIRequest::Yiji: return "yiji";
    case AIRequest::PlayerChosen: return "player_chosen";
    case AIRequest::PlayersChosen: return "players_chosen";
    case AIRequest::RespondCard: return "respond_card";
    case AIRequest::Guanxing: return "guanxing";
    case AIRequest::TriggerOrder: return "trigger_order";
    }
    return "unknown";
}

static void pushAiSkillAction(lua_State *state, const AiSkillActionContext &action)
{
    lua_createtable(state, 0, 8);
    setStringField(state, "activation_owner", action.getActivationOwner());
    setStringField(state, "activation_skill", action.getActivationSkillName());
    lua_pushinteger(state, action.getActivationInstanceId());
    lua_setfield(state, -2, "activation_instance");
    setStringField(state, "source_owner", action.getSourceOwner());
    setStringField(state, "source_skill", action.getSourceSkillName());
    lua_pushinteger(state, action.getSourceInstanceID());
    lua_setfield(state, -2, "source_instance");
    lua_pushboolean(state, action.isActivationQuotaAvailable());
    lua_setfield(state, -2, "activation_quota_available");
    lua_pushboolean(state, action.isSourceQuotaAvailable());
    lua_setfield(state, -2, "source_quota_available");
}
void AiLuaRuntime::pushRequest(lua_State *state, const AIRequest &request) const
{
    lua_createtable(state, 0, 10);
    lua_pushstring(state, aiDecisionKindName(request.kind));
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
    const QJsonObject readonlyPlay = request.worldView.strategyFacts.value("readonly_play_context").toObject();
    if (!readonlyPlay.isEmpty()) {
        pushAIJsonValue(state, readonlyPlay);
        lua_setfield(state, -2, "readonly_play_context");
    }
    if (request.kind == AIRequest::Activate || request.kind == AIRequest::UseCard
        || request.kind == AIRequest::RespondCard || !readonlyPlay.isEmpty()) {
        lua_createtable(state, int(request.cardCandidates.size()), 0);
        for (int index = 0; index < request.cardCandidates.size(); ++index) {
            const AICardCandidateView &candidate = request.cardCandidates.at(index);
            lua_createtable(state, 0, 8);
            QJsonObject canUseTargets;
            for (auto it = candidate.canUseTargets.cbegin(); it != candidate.canUseTargets.cend(); ++it) canUseTargets.insert(it.key(), it.value());
            pushAIJsonValue(state, canUseTargets); lua_setfield(state, -2, "can_use_targets");
            QJsonObject extraTargets;
            for (auto it = candidate.extraTargetByPlayer.cbegin(); it != candidate.extraTargetByPlayer.cend(); ++it) extraTargets.insert(it.key(), it.value());
            pushAIJsonValue(state, extraTargets); lua_setfield(state, -2, "extra_target_by_player");
            pushAIJsonValue(state, candidate.historyAvailable); lua_setfield(state, -2, "history_available");
            lua_pushinteger(state, candidate.candidateId);
            lua_setfield(state, -2, "candidate_id");
            lua_pushinteger(state, candidate.cardId);
            lua_setfield(state, -2, "card_id");
            if (candidate.extraTargetKnown) setIntegerField(state, "extra_target_count", candidate.extraTargetCount);
            lua_createtable(state, 0, candidate.targetFilterEmpty.size());
            for (auto it = candidate.targetFilterEmpty.constBegin(); it != candidate.targetFilterEmpty.constEnd(); ++it) {
                lua_pushboolean(state, it.value()); lua_setfield(state, -2, it.key().toUtf8().constData());
            }
            lua_setfield(state, -2, "target_filter_empty");
            lua_pushboolean(state, candidate.available);
            lua_setfield(state, -2, "available");
            lua_pushboolean(state, candidate.limited);
            lua_setfield(state, -2, "limited");
            lua_pushboolean(state, candidate.jilei);
            lua_setfield(state, -2, "jilei");
            lua_pushboolean(state, candidate.targetFixed);
            lua_setfield(state, -2, "target_fixed");
            lua_pushboolean(state, candidate.feasibleWithNoTarget);
            lua_setfield(state, -2, "feasible_with_no_target");
            lua_pushboolean(state, candidate.completeCoverage);
            lua_setfield(state, -2, "complete_coverage");
            if (candidate.affectedTargetsKnown) {
                lua_createtable(state, int(candidate.affectedTargets.size()), 0);
                for (int i = 0; i < candidate.affectedTargets.size(); ++i) {
                    pushQString(state, candidate.affectedTargets.at(i));
                    lua_rawseti(state, -2, i + 1);
                }
                lua_setfield(state, -2, "affected_targets");
            }
            lua_createtable(state, int(candidate.targetCombinations.size()), 0);
            for (int i = 0; i < candidate.targetCombinations.size(); ++i) {
                const QStringList &targets = candidate.targetCombinations.at(i);
                lua_createtable(state, int(targets.size()), 0);
                for (int j = 0; j < targets.size(); ++j) {
                    pushQString(state, targets.at(j));
                    lua_rawseti(state, -2, j + 1);
                }
                lua_rawseti(state, -2, i + 1);
            }
            lua_setfield(state, -2, "target_combinations");
            // Only targets that may be picked more than once appear here, so an absent
            // name means one vote rather than an unknown one.
            if (!candidate.maxVotes.isEmpty()) {
                lua_createtable(state, 0, int(candidate.maxVotes.size()));
                for (auto it = candidate.maxVotes.constBegin();
                     it != candidate.maxVotes.constEnd(); ++it) {
                    lua_pushinteger(state, it.value());
                    lua_setfield(state, -2, it.key().toUtf8().constData());
                }
                lua_setfield(state, -2, "max_votes");
            }
            lua_createtable(state, int(candidate.legalTargets.size()), 0);
            for (int targetIndex = 0; targetIndex < candidate.legalTargets.size(); ++targetIndex) {
                pushQString(state, candidate.legalTargets.at(targetIndex));
                lua_rawseti(state, -2, targetIndex + 1);
            }
            lua_setfield(state, -2, "legal_targets");
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "card_candidates");
        // Authorized conversions. The produced card's name and classification come from
        // the authority's own build, so an author reads them rather than asserting them.
        lua_createtable(state, int(request.cardConversions.size()), 0);
        for (int index = 0; index < request.cardConversions.size(); ++index) {
            const AICardConversionView &conversion = request.cardConversions.at(index);
            lua_createtable(state, 0, 18);
            QJsonObject conversionExtraTargets;
            for (auto it = conversion.extraTargetByPlayer.cbegin(); it != conversion.extraTargetByPlayer.cend(); ++it) conversionExtraTargets.insert(it.key(), it.value());
            pushAIJsonValue(state, conversionExtraTargets); lua_setfield(state, -2, "extra_target_by_player");
            setIntegerField(state, "handling_method", conversion.handlingMethod);
            QJsonObject conversionResidue;
            for (auto it = conversion.targetResidue.cbegin(); it != conversion.targetResidue.cend(); ++it) conversionResidue.insert(it.key(), it.value());
            pushAIJsonValue(state, conversionResidue); lua_setfield(state, -2, "target_residue");
            pushAIJsonValue(state, conversion.cardRules); lua_setfield(state, -2, "evaluation_rules");
            setIntegerField(state, "producer_version", conversion.producerVersion);
            setIntegerField(state, "producer_reason", int(conversion.producerReason));
            setStringField(state, "producer_pattern", conversion.producerPattern);
            setStringField(state, "declared_name", conversion.declaredName);
            setStringField(state, "policy_class", conversion.policyClass);
            lua_pushboolean(state, conversion.sortingOnly); lua_setfield(state, -2, "sorting_only");
            pushAIJsonValue(state, conversion.responsePlanning); lua_setfield(state, -2, "response_planning");
            pushAIJsonValue(state, conversion.responseClassPlanning); lua_setfield(state, -2, "response_class_planning");
            if (!conversion.legacyIdentityToken.isEmpty()) setStringField(state, "legacy_identity_token", conversion.legacyIdentityToken);
            lua_pushinteger(state, conversion.conversionId);
            lua_setfield(state, -2, "conversion_id");
            lua_pushinteger(state, conversion.costCount);
            lua_setfield(state, -2, "cost_count");
            lua_createtable(state, int(conversion.eligibleSubcardIds.size()), 0);
            for (int i = 0; i < conversion.eligibleSubcardIds.size(); ++i) {
                lua_pushinteger(state, conversion.eligibleSubcardIds.at(i));
                lua_rawseti(state, -2, i + 1);
            }
            lua_setfield(state, -2, "eligible_subcards");
            if (!conversion.legacyStringKey.isNull()) setStringField(state, "legacy_string_key", conversion.legacyStringKey);
            if (!conversion.legacySortKey.isNull()) setStringField(state, "legacy_sort_key", conversion.legacySortKey);
            if (conversion.nameLength >= 0) setIntegerField(state, "name_length", conversion.nameLength);
            setStringField(state, "name", conversion.name);
            setStringField(state, "class_name", conversion.className);
            setStringField(state, "skill_name", conversion.skillName);
            if (conversion.cardRules.contains("skill_names")) {
                pushAIJsonValue(state, conversion.cardRules.value("skill_names"));
                lua_setfield(state, -2, "skill_names");
            }
            for (const QString &key : {QStringLiteral("known_flags"), QStringLiteral("flags_complete"), QStringLiteral("skill_names_complete"), QStringLiteral("equip_slot"), QStringLiteral("equip_occupy_locations")}) {
                if (!conversion.cardRules.contains(key)) continue;
                pushAIJsonValue(state, conversion.cardRules.value(key));
                lua_setfield(state, -2, key.toUtf8().constData());
            }
            lua_pushboolean(state, conversion.singleTarget); lua_setfield(state, -2, "single_target");
            lua_pushboolean(state, conversion.damageCard); lua_setfield(state, -2, "damage_card");
            lua_pushboolean(state, conversion.red); lua_setfield(state, -2, "red");
            lua_pushboolean(state, conversion.black); lua_setfield(state, -2, "black");
            if (conversion.extraTargetKnown) { lua_pushinteger(state, conversion.extraTargetCount); lua_setfield(state, -2, "extra_target_count"); }
            lua_createtable(state, 0, conversion.targetFilterEmpty.size());
            for (auto it = conversion.targetFilterEmpty.cbegin(); it != conversion.targetFilterEmpty.cend(); ++it) {
                lua_pushboolean(state, it.value()); lua_setfield(state, -2, it.key().toUtf8().constData());
            }
            lua_setfield(state, -2, "target_filter_empty");
            setIntegerField(state, "type_id", conversion.typeId);
            lua_pushboolean(state, true); lua_setfield(state, -2, "virtual_card");
            lua_pushboolean(state, conversion.canRecast); lua_setfield(state, -2, "can_recast");
            lua_pushboolean(state, conversion.willThrow); lua_setfield(state, -2, "will_throw");
            lua_pushboolean(state, conversion.locked); lua_setfield(state, -2, "locked");
            lua_pushboolean(state, conversion.usingCard); lua_setfield(state, -2, "using");
            lua_pushboolean(state, conversion.containsJinchantuoqiao); lua_setfield(state, -2, "contains_jinchantuoqiao");
            lua_pushboolean(state, conversion.matchesResponsePattern); lua_setfield(state, -2, "matches_response_pattern");
            lua_pushinteger(state, conversion.suit);
            lua_setfield(state, -2, "suit");
            lua_pushinteger(state, conversion.number);
            lua_setfield(state, -2, "number");
            setStringField(state, "activation_owner", conversion.activationRef.ownerObjectName);
            setStringField(state, "activation_skill", conversion.activationRef.key.skillName);
            lua_pushinteger(state, conversion.activationRef.key.instanceID);
            lua_setfield(state, -2, "activation_instance");
            setStringField(state, "source_owner", conversion.sourceRef.ownerObjectName);
            setStringField(state, "source_skill", conversion.sourceRef.key.skillName);
            lua_pushinteger(state, conversion.sourceRef.key.instanceID);
            lua_setfield(state, -2, "source_instance");
            lua_pushboolean(state, conversion.activationQuotaAvailable);
            lua_setfield(state, -2, "activation_quota_available");
            lua_pushboolean(state, conversion.sourceQuotaAvailable);
            lua_setfield(state, -2, "source_quota_available");
            lua_pushboolean(state, conversion.available);
            lua_setfield(state, -2, "available");
            lua_pushboolean(state, conversion.targetFixed);
            lua_setfield(state, -2, "target_fixed");
            lua_pushboolean(state, conversion.feasibleWithNoTarget);
            lua_setfield(state, -2, "feasible_with_no_target");
            lua_pushboolean(state, conversion.completeCoverage);
            lua_setfield(state, -2, "complete_coverage");
            if (conversion.affectedTargetsKnown) {
                lua_createtable(state, int(conversion.affectedTargets.size()), 0);
                for (int i = 0; i < conversion.affectedTargets.size(); ++i) {
                    pushQString(state, conversion.affectedTargets.at(i));
                    lua_rawseti(state, -2, i + 1);
                }
                lua_setfield(state, -2, "affected_targets");
            }
            lua_createtable(state, int(conversion.targetCombinations.size()), 0);
            for (int i = 0; i < conversion.targetCombinations.size(); ++i) {
                const QStringList &targets = conversion.targetCombinations.at(i);
                lua_createtable(state, int(targets.size()), 0);
                for (int j = 0; j < targets.size(); ++j) {
                    pushQString(state, targets.at(j));
                    lua_rawseti(state, -2, j + 1);
                }
                lua_rawseti(state, -2, i + 1);
            }
            lua_setfield(state, -2, "target_combinations");
            lua_createtable(state, int(conversion.kindOfNames.size()), 0);
            for (int nameIndex = 0; nameIndex < conversion.kindOfNames.size(); ++nameIndex) {
                pushQString(state, conversion.kindOfNames.at(nameIndex));
                lua_rawseti(state, -2, nameIndex + 1);
            }
            lua_setfield(state, -2, "kind_of_names");
            lua_createtable(state, int(conversion.subcardIds.size()), 0);
            for (int cardIndex = 0; cardIndex < conversion.subcardIds.size(); ++cardIndex) {
                lua_pushinteger(state, conversion.subcardIds.at(cardIndex));
                lua_rawseti(state, -2, cardIndex + 1);
            }
            lua_setfield(state, -2, "subcards");
            if (!conversion.maxVotes.isEmpty()) {
                lua_createtable(state, 0, int(conversion.maxVotes.size()));
                for (auto it = conversion.maxVotes.constBegin();
                     it != conversion.maxVotes.constEnd(); ++it) {
                    lua_pushinteger(state, it.value());
                    lua_setfield(state, -2, it.key().toUtf8().constData());
                }
                lua_setfield(state, -2, "max_votes");
            }
            lua_createtable(state, int(conversion.legalTargets.size()), 0);
            for (int targetIndex = 0; targetIndex < conversion.legalTargets.size();
                 ++targetIndex) {
                pushQString(state, conversion.legalTargets.at(targetIndex));
                lua_rawseti(state, -2, targetIndex + 1);
            }
            lua_setfield(state, -2, "legal_targets");
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "card_conversions");
        // Whether that list is the whole story. An author must be able to tell
        // "nothing is available" from "this side could not work out what is".
        lua_pushboolean(state, request.conversionsEnumerated);
        lua_setfield(state, -2, "conversions_enumerated");
    }
    pushAIJsonValue(state, request.conversionPolicy); lua_setfield(state, -2, "conversion_policy");
    pushAIWorldView(state, request.worldView);
    lua_setfield(state, -2, "world_view");
    if (request.kind != AIRequest::Activate && request.kind != AIRequest::UseCard) {
        const AIChoiceOptions &choiceOptions = request.choiceOptions;
        lua_createtable(state, 0, 7);
        setStringField(state, "reason", choiceOptions.reason);
        if (!choiceOptions.question.isEmpty())
            setStringField(state, "question", choiceOptions.question);
        lua_createtable(state, int(choiceOptions.choices.size()), 0);
        for (int index = 0; index < choiceOptions.choices.size(); ++index) {
            pushQString(state, choiceOptions.choices.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "choices");
        lua_createtable(state, int(choiceOptions.cardIds.size()), 0);
        for (int index = 0; index < choiceOptions.cardIds.size(); ++index) {
            lua_pushinteger(state, choiceOptions.cardIds.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "card_ids");
        pushAICards(state, choiceOptions.cards);
        lua_setfield(state, -2, "cards");
        lua_pushboolean(state, choiceOptions.candidatesComplete);
        lua_setfield(state, -2, "candidates_complete");
        pushAIJsonValue(state, choiceOptions.context);
        lua_setfield(state, -2, "context");
        lua_createtable(state, int(choiceOptions.playerNames.size()), 0);
        for (int index = 0; index < choiceOptions.playerNames.size(); ++index) {
            pushQString(state, choiceOptions.playerNames.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "players");
        // A missing default is not the empty string: only publish one when it exists.
        if (choiceOptions.hasDefaultChoice)
            setStringField(state, "default_choice", choiceOptions.defaultChoice);
        lua_pushboolean(state, choiceOptions.optional);
        lua_setfield(state, -2, "optional");
        lua_pushinteger(state, choiceOptions.minCount);
        lua_setfield(state, -2, "min_count");
        lua_pushinteger(state, choiceOptions.maxCount);
        lua_setfield(state, -2, "max_count");
        lua_setfield(state, -2, "options");
    }
    lua_createtable(state, int(request.skillActions.size()), 0);
    for (int index = 0; index < request.skillActions.size(); ++index) {
        pushAiSkillAction(state, request.skillActions.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "skill_actions");
    if (request.hasSkillActionContext) {
        pushAiSkillAction(state, request.skillActionContext);
        lua_setfield(state, -2, "skill_action");
    }
}

// Selected cards and targets have the same value shape for a card action and for a
// selection answer, so both kinds read them through these two helpers.
static bool readSelectedCards(lua_State *state, AIResult &result)
{
    lua_getfield(state, -1, "cards");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
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
    lua_pop(state, 1);
    return true;
}

// The bottom pile of a two-pile answer keeps its own order, so it is read separately.
static bool readBottomCards(lua_State *state, AIResult &result)
{
    lua_getfield(state, -1, "bottom_cards");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
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
        if (!validCardId || result.action.bottomCardIds.contains(cardId)
            || result.action.selectedCardIds.contains(cardId)) {
            lua_pop(state, 1);
            return false;
        }
        result.action.bottomCardIds << cardId;
    }
    lua_pop(state, 1);
    return true;
}

static bool readSelectedTargets(lua_State *state, AIResult &result)
{
    lua_getfield(state, -1, "targets");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
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
    lua_pop(state, 1);
    return true;
}

// The value card spec: a description of the card to build, never a built card.
static bool readCardSpec(lua_State *state, AIResult &result)
{
    lua_getfield(state, -1, "card_spec");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return false;
    }
    AICardSpec spec;
    lua_getfield(state, -1, "name");
    const bool nameOk = lua_type(state, -1) == LUA_TSTRING
        && readBoundedString(state, -1, spec.name);
    lua_pop(state, 1);
    if (!nameOk) {
        lua_pop(state, 1);
        return false;
    }
    lua_getfield(state, -1, "suit");
    if (!lua_isnil(state, -1)) {
        int suit = 0;
        if (lua_type(state, -1) != LUA_TNUMBER
            || !aiResultInteger(lua_tonumber(state, -1), suit)
            || suit < int(Card::SuitToBeDecided) || suit > int(Card::NoSuit)) {
            lua_pop(state, 2);
            return false;
        }
        spec.suit = suit;
    }
    lua_pop(state, 1);
    lua_getfield(state, -1, "number");
    if (!lua_isnil(state, -1)) {
        int number = 0;
        if (lua_type(state, -1) != LUA_TNUMBER
            || !aiResultInteger(lua_tonumber(state, -1), number)
            || number < 0 || number > 15) {
            lua_pop(state, 2);
            return false;
        }
        spec.number = number;
    }
    lua_pop(state, 1);
    lua_getfield(state, -1, "skill");
    if (!lua_isnil(state, -1)) {
        if (lua_type(state, -1) != LUA_TSTRING
            || !readBoundedString(state, -1, spec.skillName)) {
            lua_pop(state, 2);
            return false;
        }
    }
    lua_pop(state, 1);
    lua_getfield(state, -1, "conversion_id");
    if (!lua_isnil(state, -1)) {
        int conversionId = 0;
        if (lua_type(state, -1) != LUA_TNUMBER
            || !aiResultInteger(lua_tonumber(state, -1), conversionId)
            || conversionId < 0) {
            lua_pop(state, 2);
            return false;
        }
        spec.conversionId = conversionId;
    }
    lua_pop(state, 1);
    lua_getfield(state, -1, "subcards");
    if (!lua_isnil(state, -1)) {
        if (!lua_istable(state, -1)) {
            lua_pop(state, 2);
            return false;
        }
        const size_t count = lua_rawlen(state, -1);
        if (count > AiMaxSelectedCards) {
            lua_pop(state, 2);
            return false;
        }
        for (size_t index = 1; index <= count; ++index) {
            lua_rawgeti(state, -1, index);
            int cardId = 0;
            const bool validCardId = lua_type(state, -1) == LUA_TNUMBER
                && aiResultInteger(lua_tonumber(state, -1), cardId) && cardId >= 0;
            lua_pop(state, 1);
            if (!validCardId || spec.subcardIds.contains(cardId)) {
                lua_pop(state, 2);
                return false;
            }
            spec.subcardIds << cardId;
        }
    }
    lua_pop(state, 2);
    if (!spec.isValid()) return false;
    result.action.hasCardSpec = true;
    result.action.cardSpec = spec;
    return true;
}

// The answer may name which skill instance it used; the authority re-derives and
// re-checks it, so only the identity crosses.
static bool readResultSkillAction(lua_State *state, AIResult &result)
{
    lua_getfield(state, -1, "skill_action");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        return true;
    }
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return false;
    }
    QString skillName;
    lua_getfield(state, -1, "skill");
    const bool skillOk = lua_type(state, -1) == LUA_TSTRING
        && readBoundedString(state, -1, skillName) && !skillName.isEmpty();
    lua_pop(state, 1);
    int instanceId = 0;
    lua_getfield(state, -1, "instance");
    const bool instanceOk = lua_type(state, -1) == LUA_TNUMBER
        && aiResultInteger(lua_tonumber(state, -1), instanceId) && instanceId > 0;
    lua_pop(state, 1);
    QString owner;
    lua_getfield(state, -1, "owner");
    const bool ownerOk = lua_isnil(state, -1)
        || (lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, owner));
    lua_pop(state, 2);
    if (!skillOk || !instanceOk || !ownerOk)
        return false;
    result.action.hasSkillActionContext = true;
    result.action.skillActionContext.activationRef = SkillInstanceRef(owner,
        SkillInstanceKey(skillName, instanceId));
    return true;
}
bool AiLuaRuntime::parseResult(lua_State *state, AIResult &result, const AIRequest &request) const
{
    if (!isPlainTable(state, -1))
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
    for (const char *field : {"knowledge_estimates", "strategy_effects"}) {
        lua_getfield(state, -1, field);
        QJsonArray &array = QString::fromLatin1(field) == "knowledge_estimates"
            ? result.knowledgeEstimates : result.strategyEffects;
        const bool valid = readAIArray(state, -1, array, 256);
        lua_pop(state, 1);
        if (!valid) return false;
    }
    lua_getfield(state,-1,"original_response_wire");
    const bool originalWirePresent=!lua_isnil(state,-1);
    QString originalWire;
    const bool originalWireString=lua_type(state,-1)==LUA_TSTRING
        && readBoundedString(state,-1,originalWire);
    lua_pop(state,1);
    if(originalWirePresent) {
        int physical=-1;
        if(m_activeEventRequest || !originalWireString || !aiRound36OriginalWireRequest(request)
            || !aiRound36OriginalWireSyntax(originalWire,&physical))return false;
        for(const char *field:{"answer","targets","bottom_cards","card_spec","random_card_choice",
                              "card_id","card","candidate_id","skill_action","original_empty_choice","original_suit_value"}) {
            lua_getfield(state,-1,field);const bool extra=!lua_isnil(state,-1);lua_pop(state,1);
            if(extra)return false;
        }
        lua_getfield(state,-1,"cards");
        if(originalWire==QStringLiteral(".")) {
            const bool valid=kind==QStringLiteral("pass")&&lua_isnil(state,-1);
            lua_pop(state,1);if(!valid)return false;
        }else {
            const bool table=isPlainTable(state,-1)&&lua_rawlen(state,-1)==1;
            if(!table||kind!=QStringLiteral("answer")){lua_pop(state,1);return false;}
            bool keys=true;lua_pushnil(state);
            while(lua_next(state,-2)!=0){
                keys=keys&&lua_type(state,-2)==LUA_TNUMBER&&lua_tonumber(state,-2)==1;
                lua_pop(state,1);
            }
            if(!keys){lua_pop(state,1);return false;}
            lua_rawgeti(state,-1,1);int selected=-1;
            const bool valid=lua_type(state,-1)==LUA_TNUMBER
                && aiResultInteger(lua_tonumber(state,-1),selected)&&selected==physical;
            lua_pop(state,2);if(!valid)return false;
        }
        result.hasOriginalResponseWire=true;
        result.originalResponseWire=originalWire;
    }
    result.handled = true;
    if (kind == QStringLiteral("pass")) {
        result.kind = AIResult::Pass;
        return true;
    }
    if (kind == QStringLiteral("answer")) {
        lua_getfield(state,-1,"original_discard_error");
        const bool hasOriginalDiscard=!lua_isnil(state,-1);
        const bool originalDiscardMarker=lua_type(state,-1)==LUA_TBOOLEAN&&lua_toboolean(state,-1);
        lua_pop(state,1);
        if(hasOriginalDiscard) {
            if(!originalDiscardMarker || !m_originalDiscardResultReady
                || result.decisionId!=m_originalDiscardResultDecision
                || result.stateRevision!=m_originalDiscardResultRevision)return false;
            for(const char*field:{"original_response_wire","answer","original_empty_choice","original_suit_value","targets","bottom_cards",
                                  "card_spec","random_card_choice","card_id","card","candidate_id","skill_action"}){
                lua_getfield(state,-1,field);const bool extra=!lua_isnil(state,-1);lua_pop(state,1);if(extra)return false;
            }
            if(!readSelectedCards(state,result) || result.action.selectedCardIds!=m_originalDiscardResultIds)return false;
            result.kind=AIResult::Answer;
            result.originalDiscardError=true;
            result.originalDiscardMinimum=m_originalDiscardMinimum;
            result.originalDiscardMaximum=m_originalDiscardMaximum;
            result.originalDiscardOptional=m_originalDiscardOptional;
            result.originalDiscardIncludeEquip=m_originalDiscardIncludeEquip;
            result.originalDiscardExchange=m_originalDiscardExchange;
            result.originalDiscardPattern=m_originalDiscardPattern;
            result.originalDiscardReason=m_originalDiscardReason;
            result.originalDiscardViewer=m_originalDiscardViewer;
            return true;
        }

        lua_getfield(state,-1,"original_suit_value");
        const bool hasOriginalSuit=!lua_isnil(state,-1);
        int originalSuit=0;
        const bool validOriginalSuit=lua_type(state,-1)==LUA_TNUMBER
            && aiResultInteger(lua_tonumber(state,-1),originalSuit);
        lua_pop(state,1);
        if(hasOriginalSuit) {
            if(!validOriginalSuit || !m_originalSuitResultReady
                || result.decisionId!=m_originalSuitResultDecision
                || result.stateRevision!=m_originalSuitResultRevision
                || originalSuit!=m_originalSuitResultValue)return false;
            for(const char *field:{"answer","original_empty_choice","cards","targets","bottom_cards",
                                  "card_spec","random_card_choice","card_id","card","candidate_id","skill_action"}) {
                lua_getfield(state,-1,field);const bool extra=!lua_isnil(state,-1);lua_pop(state,1);
                if(extra)return false;
            }
            result.kind=AIResult::Answer;
            result.originalSuitResult=true;
            result.originalSuitValue=originalSuit;
            return true;
        }

        lua_getfield(state,-1,"original_empty_choice");
        const bool originalEmpty=!lua_isnil(state,-1);
        const bool marker=lua_isboolean(state,-1)&&lua_toboolean(state,-1);
        lua_pop(state,1);
        if(originalEmpty && (!marker || m_activeEventRequest
            || request.kind!=AIRequest::Choice
            || request.choiceOptions.question!="askForChoice"
            || !aiRound35OriginalQuestion(request)))return false;
        lua_getfield(state,-1,"answer");
        const bool empty=lua_type(state,-1)==LUA_TSTRING&&lua_rawlen(state,-1)==0;
        lua_pop(state,1);
        if(originalEmpty && !empty)return false;
        if(empty && request.kind==AIRequest::Choice && !originalEmpty)return false;
        if(originalEmpty) {
            for(const char *field:{"cards","targets","bottom_cards","card_spec","random_card_choice",
                                  "card_id","card","candidate_id","skill_action"}) {
                lua_getfield(state,-1,field);const bool extra=!lua_isnil(state,-1);lua_pop(state,1);
                if(extra)return false;
            }
        }
        result.originalEmptyChoice=originalEmpty;
        result.kind = AIResult::Answer;
        lua_getfield(state, -1, "random_card_choice");
        if (!lua_isnil(state, -1)) {
            int budget = 32; QJsonValue value;
            const bool valid = readAIJson(state, -1, value, budget) && value.isObject();
            lua_pop(state, 1);
            if (!valid) return false;
            result.action.randomCardChoice = value.toObject();
            for (const char *field : {"cards", "targets", "bottom_cards", "answer", "card_spec",
                                      "card_id", "card", "candidate_id", "skill_action"}) {
                lua_getfield(state, -1, field); const bool present = !lua_isnil(state, -1); lua_pop(state, 1);
                if (present) return false;
            }
            return !result.action.randomCardChoice.isEmpty();
        }
        lua_pop(state, 1);

        lua_getfield(state, -1, "answer");
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
        if (!readSelectedCards(state, result) || !readSelectedTargets(state, result))
            return false;
        if (!readBottomCards(state, result) || !readCardSpec(state, result))
            return false;
        if (result.action.hasCardSpec) {
            // A converted response has exactly one representation. Do not silently
            // drop an accompanying physical selection or a legacy card string.
            for (const char *field : {"cards", "targets", "bottom_cards", "answer",
                                      "card_id", "card", "candidate_id", "skill_action"}) {
                lua_getfield(state, -1, field);
                const bool present = !lua_isnil(state, -1);
                lua_pop(state, 1);
                if (present) return false;
            }
        }
        return true;
    }
    if (kind != QStringLiteral("use_card"))
        return false;
    result.kind = AIResult::UseCard;

    lua_getfield(state, -1, "card_id");
    if (!lua_isnil(state, -1)) {
        int useCardId = 0;
        if (lua_type(state, -1) != LUA_TNUMBER
            || !aiResultInteger(lua_tonumber(state, -1), useCardId) || useCardId < 0) {
            lua_pop(state, 1);
            return false;
        }
        result.action.useCardId = useCardId;
    }
    lua_pop(state, 1);

    // The candidate ticket. Optional on the wire, because an answer may name the card
    // alone, but a malformed one is a refusal rather than a value quietly dropped.
    lua_getfield(state, -1, "candidate_id");
    if (!lua_isnil(state, -1)) {
        int candidateId = 0;
        if (lua_type(state, -1) != LUA_TNUMBER
            || !aiResultInteger(lua_tonumber(state, -1), candidateId) || candidateId < 0) {
            lua_pop(state, 1);
            return false;
        }
        result.action.candidateId = candidateId;
    }
    lua_pop(state, 1);

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

    if (!readSelectedCards(state, result) || !readSelectedTargets(state, result))
        return false;
    if (!readCardSpec(state, result))
        return false;
    if (!readResultSkillAction(state, result))
        return false;

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
