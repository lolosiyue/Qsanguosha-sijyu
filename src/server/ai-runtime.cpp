#include "ai-runtime.h"

#include "ai-data-store.h"
#include "ai-decision-coordinator.h"
#include "engine.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "room.h"
#include "room-runtime.h"
#include "settings.h"
#include "skill.h"
#include "standard.h"
#include "serverplayer.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QSet>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

thread_local AiLuaRuntime *currentAiRuntime = nullptr;
thread_local const AIRequest *currentAiDecision = nullptr;
struct OwnerRangeQuery {
    AiLuaRuntime *runtime;
    const AIRequest *request;
    QString source;
    int depth;
};
thread_local const OwnerRangeQuery *currentOwnerRangeQuery = nullptr;
thread_local int currentAiBudgetScale = 1;

// Read the live stack before luaL_error unwinds it. Do not inspect locals or
// arguments, and finish all C++ temporaries before the hook raises a Lua error.
void logInstructionLimit(lua_State *state)
{
    if (currentAiDecision) {
        qWarning().noquote() << "[AI_LIMIT] decision=" << currentAiDecision->decisionId
            << "player=" << currentAiDecision->viewerObjectName
            << "kind=" << int(currentAiDecision->kind)
            << "reason=" << currentAiDecision->choiceOptions.reason
            << "budget_scale=" << currentAiBudgetScale;
    } else {
        qWarning() << "[AI_LIMIT] callback=event_or_initialization";
    }
    lua_Debug frame;
    for (int level = 0; level < 32; ++level) {
        if (!lua_getstack(state, level, &frame)) return;
        if (lua_getinfo(state, "nSl", &frame))
            qWarning().noquote() << "[AI_LIMIT_STACK] frame=" << level
                << frame.short_src << ":" << frame.currentline
                << (frame.name ? frame.name : "?")
                << "defined=" << frame.linedefined;
    }
    if (lua_getstack(state, 32, &frame))
        qWarning() << "[AI_LIMIT_STACK] truncated after 32 frames";
}

// Error codes alone hide the Lua message. Log each distinct message once per
// thread: one bad event handler otherwise repeats for every observer seat.
void logLuaErrorOnce(const char *code, lua_State *state)
{
    QString message;
    if (lua_type(state, -1) == LUA_TSTRING) {
        message = QString::fromUtf8(lua_tostring(state, -1));
    } else if (lua_istable(state, -1)) {
        // Raw reads only: a __tostring or __index here would run outside any pcall.
        for (const char *field : {"key", "reason"}) {
            lua_pushstring(state, field);
            lua_rawget(state, -2);
            if (lua_type(state, -1) == LUA_TSTRING)
                message += QString::fromLatin1(field) + '=' + QString::fromUtf8(lua_tostring(state, -1)) + ' ';
            lua_pop(state, 1);
        }
        message = QStringLiteral("table ") + message.trimmed();
    } else {
        message = QString::fromLatin1(luaL_typename(state, -1));
    }
    message = message.left(400);
    static thread_local QSet<QString> reported;
    const QString key = QString::fromLatin1(code) + ' ' + message;
    if (reported.size() >= 256 || reported.contains(key)) return;
    reported.insert(key);
    qWarning().noquote() << "[AI_LUA_ERROR]" << code << message;
}

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

// A pure policy value: booleans, finite numbers, bounded strings and plain tables
// (a dense array or a string-keyed object). Anything else is left out.
bool readLuaPolicyValue(lua_State *state, int index, QJsonValue &value, int depth, int &budget,
                         bool strict = false)
{
    index = lua_absindex(state, index);
    if (depth > 6 || --budget < 0)
        return false;
    switch (lua_type(state, index)) {
    case LUA_TBOOLEAN:
        value = bool(lua_toboolean(state, index));
        return true;
    case LUA_TNUMBER: {
        const double number = lua_tonumber(state, index);
        if (!std::isfinite(number)) return false;
        value = number;
        return true;
    }
    case LUA_TSTRING: {
        QString text;
        if (!readBoundedString(state, index, text)) return false;
        value = text;
        return true;
    }
    case LUA_TTABLE: {
        if (!isPlainTable(state, index)) return false;
        const size_t length = lua_rawlen(state, index);
        if (length > 0) {
            if (strict) {
                // Strict queries cannot silently discard dictionary keys or an
                // out-of-range numeric tail on a supposedly dense material list.
                lua_pushnil(state);
                while (lua_next(state, index) != 0) {
                    const lua_Number key = lua_type(state, -2) == LUA_TNUMBER ? lua_tonumber(state, -2) : 0;
                    if (--budget < 0 || !std::isfinite(key) || std::floor(key) != key || key < 1 || key > length) {
                        lua_pop(state, 2);
                        return false;
                    }
                    lua_pop(state, 1);
                }
            }
            QJsonArray array;
            for (size_t i = 1; i <= length; ++i) {
                lua_rawgeti(state, index, lua_Integer(i));
                QJsonValue item;
                const bool ok = readLuaPolicyValue(state, -1, item, depth + 1, budget, strict);
                lua_pop(state, 1);
                if (!ok) return false;
                array.append(item);
            }
            value = array;
            return true;
        }
        QJsonObject object;
        lua_pushnil(state);
        while (lua_next(state, index) != 0) {
            QString key;
            QJsonValue item;
            if (lua_type(state, -2) == LUA_TSTRING && readBoundedString(state, -2, key)
                && readLuaPolicyValue(state, -1, item, depth + 1, budget, strict))
                object.insert(key, item);
            else if (strict) { lua_pop(state, 2); return false; }
            lua_pop(state, 1);
            if (budget < 0) { lua_pop(state, 1); return false; }
        }
        value = object;
        return true;
    }
    default:
        return false;
    }
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
            } else if (valid && key == QStringLiteral("level")) {
                delta.level = lua_tonumber(state, -1);
                valid = lua_type(state, -1) == LUA_TNUMBER
                    && std::isfinite(double(delta.level));
            } else {
                valid = false;
            }
            lua_pop(state, 1);
            if (!valid || ++fields > 3)
                break;
        }
        if (!valid || fields != 3 || delta.from == delta.to) {
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
    setStringField(state, "name", card.objectName);
    setStringField(state, "class_name", card.className);
    if (!card.legacyStringKey.isEmpty()) {
        setStringField(state, "legacy_string_key", card.legacyStringKey);
        setStringField(state, "legacy_sort_key", card.legacyStringKey);
    }
    if (card.nameLength >= 0)
        setIntegerField(state, "name_length", card.nameLength);
    // A borrowed activation can have a private root skill; outside the owner's own
    // cards only an activation that is its own root names it.
    if (card.ownerStateKnown || card.sourceSkillName == card.skillName)
        setStringField(state, "source_skill", card.sourceSkillName);
    lua_pushboolean(state, card.canRecast);
    lua_setfield(state, -2, "can_recast");
    lua_pushboolean(state, card.transferable);
    lua_setfield(state, -2, "transferable");
    lua_pushboolean(state, card.printedTransferable);
    lua_setfield(state, -2, "printed_transferable");
    lua_pushboolean(state, card.willThrow);
    lua_setfield(state, -2, "will_throw");
    lua_pushboolean(state, card.singleTarget);
    lua_setfield(state, -2, "single_target");
    lua_pushboolean(state, card.ownerStateKnown);
    lua_setfield(state, -2, "skill_names_complete");
    if (card.ownerStateKnown) {
        lua_createtable(state, int(card.skillNames.size()), 0);
        for (int index = 0; index < card.skillNames.size(); ++index) {
            pushQString(state, card.skillNames.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "skill_names");
        if (!card.engineClassName.isEmpty())
            setStringField(state, "engine_class_name", card.engineClassName);
        lua_pushboolean(state, card.locked);
        lua_setfield(state, -2, "locked");
        lua_pushboolean(state, card.jilei);
        lua_setfield(state, -2, "jilei");
        lua_pushboolean(state, card.usingCard);
        lua_setfield(state, -2, "using");
        lua_pushboolean(state, card.gift);
        lua_setfield(state, -2, "gift");
    }
    // Issued conversions and evaluations have viewer rules without physical
    // owner state. Each capability is disclosed under its own known flag.
    if (card.ownerRulesKnown) {
        lua_pushboolean(state, card.available);
        lua_setfield(state, -2, "available");
        if (card.analepticStaticKnown) {
            lua_pushboolean(state, card.analepticStaticAvailable);
            lua_setfield(state, -2, "analeptic_static_available");
        }
        lua_pushinteger(state, card.targetModResidue);
        lua_setfield(state, -2, "target_mod_residue");
        lua_pushinteger(state, card.targetModDistance);
        lua_setfield(state, -2, "target_mod_distance");
        lua_pushinteger(state, card.targetModExtra);
        lua_setfield(state, -2, "target_mod_extra");
        const auto pushMethods = [state](const QMap<int, bool> &values, const char *field) {
            if (values.isEmpty()) return;
            lua_createtable(state, 0, int(values.size()));
            for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
                lua_pushboolean(state, it.value());
                lua_setfield(state, -2, QByteArray::number(it.key()).constData());
            }
            lua_setfield(state, -2, field);
        };
        pushMethods(card.limits, "limits");
        pushMethods(card.limitsHand, "limits_hand");
        lua_createtable(state, 0, int(card.prohibited.size()));
        for (auto it = card.prohibited.constBegin(); it != card.prohibited.constEnd(); ++it) {
            lua_pushboolean(state, it.value());
            lua_setfield(state, -2, it.key().toUtf8().constData());
        }
        lua_setfield(state, -2, "prohibited");
        if (!card.targetResidue.isEmpty()) {
            lua_createtable(state, 0, int(card.targetResidue.size()));
            for (auto it = card.targetResidue.constBegin(); it != card.targetResidue.constEnd(); ++it) {
                lua_pushinteger(state, it.value());
                lua_setfield(state, -2, it.key().toUtf8().constData());
            }
            lua_setfield(state, -2, "target_residue");
        }
    }
    lua_createtable(state, int(card.flags.size()), 0);
    for (int index = 0; index < card.flags.size(); ++index) {
        pushQString(state, card.flags.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "flags");
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
    setStringField(state, "limit_mark", skill.limitMark);
    lua_createtable(state, int(skill.triggerEvents.size()), 0);
    for (int index = 0; index < skill.triggerEvents.size(); ++index) {
        lua_pushinteger(state, skill.triggerEvents.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "trigger_events");
    if (skill.viewAsEquipKnown) {
        lua_createtable(state, int(skill.viewAsEquipNames.size()), 0);
        for (int index = 0; index < skill.viewAsEquipNames.size(); ++index) {
            pushQString(state, skill.viewAsEquipNames.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "view_as_equip_names");
    }
    if (skill.viewAsKnown) {
        lua_pushboolean(state, skill.viewAsV2);
        lua_setfield(state, -2, "view_as_v2");
        lua_pushboolean(state, skill.enabledAtPlay);
        lua_setfield(state, -2, "enabled_at_play");
        lua_createtable(state, 0, int(skill.enabledAtResponse.size()));
        for (auto it = skill.enabledAtResponse.constBegin(); it != skill.enabledAtResponse.constEnd(); ++it) {
            lua_pushboolean(state, it.value());
            lua_setfield(state, -2, it.key().toUtf8().constData());
        }
        lua_setfield(state, -2, "enabled_at_response");
    }
    lua_pushboolean(state, skill.lordSkill);
    lua_setfield(state, -2, "lord_skill");
    lua_pushboolean(state, skill.attachedLordSkill);
    lua_setfield(state, -2, "attached_lord_skill");
    lua_pushboolean(state, skill.lordSkillEffective);
    lua_setfield(state, -2, "lord_skill_effective");
    if (skill.hasPrivateState || !skill.state.isEmpty()) {
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
    lua_pushboolean(state, player.inYinniState);
    lua_setfield(state, -2, "in_yinni_state");
    lua_pushboolean(state, player.turnOpen);
    lua_setfield(state, -2, "turn_open");
    lua_pushboolean(state, player.playOpen);
    lua_setfield(state, -2, "play_open");
    lua_pushinteger(state, player.attackRangeWithoutWeapon);
    lua_setfield(state, -2, "attack_range_without_weapon");
    pushAIJsonValue(state, player.prototypeRules);
    lua_setfield(state, -2, "prototype_rules");
    lua_pushboolean(state, player.hasJudgeArea);
    lua_setfield(state, -2, "has_judge_area");
    lua_createtable(state, int(player.equipAreas.size()), 0);
    for (int index = 0; index < player.equipAreas.size(); ++index) {
        lua_pushinteger(state, player.equipAreas.at(index));
        lua_rawseti(state, -2, index + 1);
    }
    lua_setfield(state, -2, "equip_areas");
    lua_createtable(state, 0, int(player.changeSkillStates.size()));
    for (auto entry = player.changeSkillStates.cbegin(); entry != player.changeSkillStates.cend(); ++entry) {
        lua_pushinteger(state, entry.value());
        lua_setfield(state, -2, entry.key().toUtf8().constData());
    }
    lua_setfield(state, -2, "change_skill_states");
    lua_pushboolean(state, player.kongcheng);
    lua_setfield(state, -2, "kongcheng");
    lua_pushboolean(state, player.wounded);
    lua_setfield(state, -2, "wounded");
    lua_pushboolean(state, player.faceUp);
    lua_setfield(state, -2, "face_up");
    lua_pushboolean(state, player.chained);
    lua_setfield(state, -2, "chained");
    setStringField(state, "state", player.playerState);
    lua_pushinteger(state, player.maxCards);
    lua_setfield(state, -2, "max_cards");
    lua_pushinteger(state, player.hujia);
    lua_setfield(state, -2, "hujia");
    lua_pushinteger(state, player.attackRange);
    lua_setfield(state, -2, "attack_range");
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
    lua_pushboolean(state, player.marksComplete);
    lua_setfield(state, -2, "marks_complete");
    pushAISkills(state, player.skills);
    lua_setfield(state, -2, "skills");
    lua_pushboolean(state, player.skillsComplete);
    lua_setfield(state, -2, "skills_complete");
    pushAICards(state, player.knownCards);
    lua_setfield(state, -2, "known_cards");
    lua_pushboolean(state, player.handVisible);
    lua_setfield(state, -2, "hand_visible");
    if (player.armorEffectKnown)
        setStringField(state, "active_armor_name", player.activeArmorName);
    if (player.armorEffectNamesKnown) {
        lua_createtable(state, int(player.armorEffectNames.size()), 0);
        for (int index = 0; index < player.armorEffectNames.size(); ++index) {
            pushQString(state, player.armorEffectNames.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "armor_effect_names");
    }
    if (player.privateFlagsVisible) {
        lua_createtable(state, int(player.privateFlags.size()), 0);
        for (int index = 0; index < player.privateFlags.size(); ++index) {
            lua_pushstring(state, player.privateFlags.at(index).toUtf8().constData());
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "flags");
    }
    // Future phase skips are known to their owner only.
    if (!player.skippedPhases.isEmpty()) {
        lua_createtable(state, 0, int(player.skippedPhases.size()));
        for (auto phase = player.skippedPhases.constBegin(); phase != player.skippedPhases.constEnd(); ++phase) {
            lua_pushboolean(state, phase.value());
            lua_rawseti(state, -2, phase.key());
        }
        lua_setfield(state, -2, "skipped_phases");
    }
    if (player.ownStateVisible) {
        lua_pushboolean(state, player.canSlashWithoutCrossbow);
        lua_setfield(state, -2, "can_slash_without_crossbow");
        lua_createtable(state, 0, int(player.canDiscard.size()));
        for (auto who = player.canDiscard.cbegin(); who != player.canDiscard.cend(); ++who) {
            lua_createtable(state, 0, int(who.value().size()));
            for (auto zone = who.value().cbegin(); zone != who.value().cend(); ++zone) {
                lua_pushboolean(state, zone.value());
                lua_setfield(state, -2, zone.key().toUtf8().constData());
            }
            lua_setfield(state, -2, who.key().toUtf8().constData());
        }
        lua_setfield(state, -2, "can_discard");
        pushAIJsonValue(state, player.tags);
        lua_setfield(state, -2, "tags");
        pushAIJsonValue(state, player.properties);
        lua_setfield(state, -2, "properties");
    }
    if (player.usedHistoryVisible) {
        lua_createtable(state, 0, int(player.usedHistory.size()));
        for (auto entry = player.usedHistory.cbegin(); entry != player.usedHistory.cend(); ++entry) {
            lua_pushinteger(state, entry.value());
            lua_setfield(state, -2, entry.key().toUtf8().constData());
        }
        lua_setfield(state, -2, "used_history");
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
    lua_pushboolean(state, event.intentionSuppressed);
    lua_setfield(state, -2, "intention_suppressed");
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
    if (world.enableHegemony) {
        pushAIJsonValue(state, world.hegemonyFacts);
        lua_setfield(state, -2, "hegemony_facts");
    }
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
    lua_pushinteger(state, world.drawPileCount);
    lua_setfield(state, -2, "draw_pile_count");
    setStringField(state, "current_dying", world.currentDying);
    pushAIJsonValue(state, world.useHistory);
    lua_setfield(state, -2, "use_history");
    lua_pushboolean(state, world.enableHegemony);
    lua_setfield(state, -2, "enable_hegemony");
    lua_pushboolean(state, world.aiHumanized);
    lua_setfield(state, -2, "ai_humanized");
    lua_pushboolean(state, world.aiChatEnabled);
    lua_setfield(state, -2, "ai_chat_enabled");
    if (world.roleCountsKnown) {
        pushAIJsonValue(state, world.roleCounts);
        lua_setfield(state, -2, "role_counts");
    }
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
    pushAIWorldView(state, world);
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
    lua_getfield(state, result, "error");
    QString policyError;
    if (lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, policyError))
        world.modePolicy.insert("error", policyError);
    lua_pop(state, 1);
    lua_getfield(state, result, "managed");
    world.modePolicy.insert("managed", world.customRoles || bool(lua_toboolean(state, -1)));
    lua_pop(state, 1);
    if (!world.modePolicy.value("managed").toBool()) {
        // A Room VM logs this admission failure once, even across AI requests.
        lua_getfield(state, LUA_REGISTRYINDEX, "qsan.mode_ai.unmanaged_logged");
        const bool logged = lua_toboolean(state, -1);
        lua_pop(state, 1);
        if (!logged) {
            qWarning().noquote() << "[AI_LUA_ERROR] MODE_AI_UNMANAGED: active mode" << world.modeId;
            lua_pushboolean(state, true);
            lua_setfield(state, LUA_REGISTRYINDEX, "qsan.mode_ai.unmanaged_logged");
        }
    }
    lua_getfield(state, result, "predictable");
    world.modePolicy.insert("predictable", bool(lua_toboolean(state, -1)));
    lua_pop(state, 1);
    QStringList names{world.self.objectName};
    for (const AIPlayerView &player : world.players) names << player.objectName;
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
                && value >= -5 && value <= 5) objectives.insert(name, value);
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
    lua_pop(state, 1);
    // The rest of the mode mind this viewer's policy reports, as plain values.
    int budget = 4096;
    for (const char *key : {"standard_identity", "legacy_managed", "legacy_base_update",
             "explicit_renegade", "estimated_branches", "role_estimates", "role_values",
             "strategy_flags", "strategy_marks", "strategy_flags_cleared"}) {
        lua_getfield(state, result, key);
        QJsonValue value;
        if (!lua_isnil(state, -1) && readLuaPolicyValue(state, -1, value, 0, budget))
            world.modePolicy.insert(QString::fromLatin1(key), value);
        lua_pop(state, 1);
    }
    lua_settop(state, top);
}

AiDecisionPhase::AiDecisionPhase(const AIRequest &request, const char *phase, int budgetScale)
    : m_label(QStringLiteral("decision=%1 player=%2 kind=%3 reason=%4 phase=%5 budget_scale=%6")
        .arg(request.decisionId).arg(request.viewerObjectName).arg(int(request.kind))
        .arg(request.choiceOptions.reason).arg(QString::fromLatin1(phase)).arg(budgetScale)),
      m_enabled(qEnvironmentVariableIntValue("QSAN_AI_DIAGNOSTICS") == 1)
{
    m_timer.start();
    if (m_enabled) qInfo().noquote() << "[AI_PHASE_BEGIN]" << m_label;
}

AiDecisionPhase::~AiDecisionPhase()
{
    const qint64 elapsed = m_timer.elapsed();
    if (m_enabled || elapsed >= 1000)
        qInfo().noquote() << "[AI_PHASE_END]" << m_label << "elapsed_ms=" << elapsed;
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
    : m_room(room), m_lua(LuaRuntime::Auxiliary), m_instructionBudget(8000000),
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
    const int softMiB = qMax(1, Config.value(QStringLiteral("AiLuaSoftMemoryMiB"),
                                             int(AiSoftMemoryLimit / 1024u / 1024u)).toInt());
    const int hardMiB = qMax(softMiB, Config.value(QStringLiteral("AiLuaHardMemoryMiB"),
                                                   int(AiHardMemoryLimit / 1024u / 1024u)).toInt());
    m_instructionBudget = qMax<qint64>(10000,
        Config.value(QStringLiteral("AiLuaInstructionBudget"), 8000000).toLongLong());
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
    loadConfiguredRoutes();
    m_routes.freeze();
    return true;
}

void AiLuaRuntime::shutdown()
{
    m_originalSDScopes.clear();
    m_originalSDSelections.clear();
    m_lua.shutdown();
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

qint64 AiLuaRuntime::scaledInstructionBudget() const
{
    // Planning and event views grow with the table, so the budget does too. Above 8
    // alive players decisions measured up to about 5M instructions per alive player,
    // so each gets half a base budget. Counting instructions keeps this deterministic.
    const int alive = m_room ? m_room->alivePlayerCount() : 0;
    if (alive > 8)
        return m_instructionBudget * alive / 2;
    return m_instructionBudget;
}

AIResult AiLuaRuntime::decideIsolated(const AIRequest &request, int budgetScale)
{
    AiDecisionPhase attemptPhase(request, "isolated_attempt", budgetScale);
    AIResult result;
    result.decisionId = request.decisionId;
    result.stateRevision = request.stateRevision;
    if (!m_lua.rawState())
        return result;

    bool rebuild = false;
    {
        LuaRuntime::Binding luaBinding(m_lua);
        ExecutionBinding executionBinding(*this);
        QScopedValueRollback<const AIRequest *> decisionScope(currentAiDecision, &request);
        // Interrupted Lua callbacks must not retain native planning handles.
        const auto clearSDScopes = qScopeGuard([&] { m_originalSDScopes.clear(); });
        QScopedValueRollback<int> scaleScope(currentAiBudgetScale, budgetScale);
        lua_State *state = m_lua.state();
        lua_getglobal(state, "ai_decide");
        if (!lua_isfunction(state, -1)) {
            lua_pop(state, 1);
            return result;
        }
        {
            AiDecisionPhase phase(request, "push_request", budgetScale);
            pushRequest(state, request);
        }
        const qint64 instructionBudget = scaledInstructionBudget() * qMax(1, budgetScale);
        m_instructionsRemaining = instructionBudget;
        m_instructionLimitExceeded = false;
        QScopedValueRollback<bool> profileScope(m_instructionProfiling,
            qEnvironmentVariableIntValue("QSAN_AI_DIAGNOSTICS") == 1);
        m_instructionProfileTicks = 0;
        m_instructionProfileSamples = 0;
        m_instructionProfile.clear();
        lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
        LuaRuntime::LuaInvocationScope invocation(m_lua);
        int status;
        {
            AiDecisionPhase phase(request, "lua_callback", budgetScale);
            status = lua_pcall(state, 1, 1, 0);
        }
        lua_sethook(state, nullptr, 0, 0);
        if (m_instructionProfiling) {
            const qint64 charged = instructionBudget - m_instructionsRemaining;
            qInfo().noquote() << "[AI_PROFILE] decision=" << request.decisionId
                << "instruction_budget=" << instructionBudget << "charged_instructions=" << charged
                << "samples=" << m_instructionProfileSamples << "interval=" << 10000
                << "symbols=" << m_instructionProfile.size() << "symbol_cap=" << 512 << "depth_cap=" << 32;
            // Print hot symbols only for substantial decisions. Inclusive hits
            // count a symbol once per stack sample, including recursive stacks.
            if (m_instructionLimitExceeded || charged >= instructionBudget / 2) {
                auto symbols = m_instructionProfile.keys();
                for (bool inclusive : {false, true}) {
                    std::sort(symbols.begin(), symbols.end(), [&](const QString &a, const QString &b) {
                        const auto av = m_instructionProfile.value(a), bv = m_instructionProfile.value(b);
                        const qint64 ac = inclusive ? av.second : av.first, bc = inclusive ? bv.second : bv.first;
                        return ac != bc ? ac > bc : a < b;
                    });
                    for (int index = 0; index < qMin(15, int(symbols.size())); ++index) {
                        const QString &symbol = symbols.at(index);
                        const auto hits = m_instructionProfile.value(symbol);
                        if ((inclusive ? hits.second : hits.first) == 0) break;
                        qInfo().noquote() << "[AI_PROFILE_HOT] decision=" << request.decisionId
                            << "order=" << (inclusive ? "inclusive" : "self")
                            << "self_samples=" << hits.first << "inclusive_samples=" << hits.second
                            << "symbol=" << symbol;
                    }
                }
            }
            m_instructionProfile.clear();
        }
        AiDecisionPhase resultPhase(request, "parse_result_gc", budgetScale);
        if (status != 0) {
            result.errorCode = m_instructionLimitExceeded
                ? QStringLiteral("AI_INSTRUCTION_LIMIT")
                : status == LUA_ERRMEM ? QStringLiteral("AI_MEMORY_LIMIT")
                                       : QStringLiteral("AI_RUNTIME_ERROR");
            if (!m_instructionLimitExceeded && status != LUA_ERRMEM)
                logLuaErrorOnce("AI_RUNTIME_ERROR", state);
            lua_pop(state, 1);
            rebuild = status == LUA_ERRMEM || m_instructionLimitExceeded;
        } else {
            if (Config.EnableHegemony) {
                // Coverage is not a runtime fault. Read only bounded primitive
                // diagnostics; never call Lua again after the budget is removed.
                const bool unanswered = lua_isnil(state, -1);
                const auto boundedString = [state](int index, size_t limit) {
                    size_t length = 0;
                    const char *value = lua_type(state, index) == LUA_TSTRING
                        ? lua_tolstring(state, index, &length) : nullptr;
                    return value ? QString::fromUtf8(value, int(qMin(length, limit))) : QString();
                };
                lua_getglobal(state, "ai_last_decision_outcome");
                const QString outcome = boundedString(-1, 64);
                lua_pop(state, 1);
                QString reason, key, traceback;
                lua_getglobal(state, "ai_last_decision_failure");
                if (lua_istable(state, -1)) {
                    lua_pushliteral(state, "reason");
                    lua_rawget(state, -2);
                    reason = boundedString(-1, 1024);
                    lua_pop(state, 1);
                    lua_pushliteral(state, "key");
                    lua_rawget(state, -2);
                    key = boundedString(-1, 256);
                    lua_pop(state, 1);
                    lua_pushliteral(state, "traceback");
                    lua_rawget(state, -2);
                    traceback = boundedString(-1, 4096);
                    lua_pop(state, 1);
                }
                lua_pop(state, 1);
                // A complete proposal may coexist with an unplanned candidate.
                // Keep that debt visible without marking the proposal accepted;
                // normal result parsing and authority validation still follow.
                if (unanswered || !reason.isEmpty()) {
                    qWarning().noquote() << (unanswered ? "[AI_HEGEMONY_NOT_COVERED] decision="
                                                       : "[AI_HEGEMONY_PARTIAL_COVERAGE] decision=")
                        << request.decisionId << "player=" << request.viewerObjectName << "outcome=" << outcome
                        << "reason=" << reason << "key=" << key;
                    // Correlate the rejected request with its issued incomplete tickets.
                    // This reads the snapshot only; no target rule is queried again.
                    if (unanswered && key == QStringLiteral("targets")
                        && qEnvironmentVariableIntValue("QSAN_AI_DIAGNOSTICS") == 1) {
                        for (const auto &candidate : request.cardCandidates) {
                            if (candidate.completeCoverage) continue;
                            qInfo().noquote() << "[AI_TARGET_REJECTED_CANDIDATE] decision=" << request.decisionId
                                << "card_id=" << candidate.cardId << "candidate_id=" << candidate.candidateId
                                << "available=" << candidate.available << "limited=" << candidate.limited
                                << "target_fixed=" << candidate.targetFixed << "legal_targets=" << candidate.legalTargets;
                        }
                    }
                    if (!traceback.isEmpty())
                        qWarning().noquote() << "[AI_HEGEMONY_UNSUPPORTED_TRACE] decision=" << request.decisionId << traceback;
                }
            }
            if (!lua_isnil(state, -1)) {
                if (!parseResult(state, result)
                    || (result.kind == AIResult::Answer && result.action.hasCardSpec
                        && request.kind != AIRequest::RespondCard)) {
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
        AiDecisionPhase rebuildPhase(request, "vm_rebuild", budgetScale);
        {
            AiDecisionPhase phase(request, "vm_shutdown", budgetScale);
            shutdown();
        }
        QString error;
        bool initialized;
        {
            AiDecisionPhase phase(request, "vm_initialize", budgetScale);
            initialized = initialize(&error);
        }
        if (!initialized)
            qWarning().noquote() << "Unable to rebuild AI Lua runtime:" << error;
    }
    return result;
}

bool AiLuaRuntime::processEvent(const AIWorldView &world, const AIEventView &event,
                                LuaRuntime &modeRuntime, QString *error)
{
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

    QList<AiIntentionDelta> deltas;
    QString failure;
    bool rebuild = false;
    {
        LuaRuntime::Binding binding(m_lua);
        ExecutionBinding executionBinding(*this);
        lua_State *state = m_lua.state();
        const int top = lua_gettop(state);
        lua_getglobal(state, "ai_event");
        if (!lua_isfunction(state, -1)) {
            lua_settop(state, top);
            return fail(QStringLiteral("AI_EVENT_UNHANDLED"));
        }
        pushAIWorldView(state, world);
        pushAIEvent(state, event);
        m_instructionsRemaining = scaledInstructionBudget();
        m_instructionLimitExceeded = false;
        lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
        const int status = LuaRuntime::protectedCall(state, 2, 1, 0);
        lua_sethook(state, nullptr, 0, 0);
        if (status != 0 || m_instructionLimitExceeded) {
            failure = m_instructionLimitExceeded ? QStringLiteral("AI_EVENT_INSTRUCTION_LIMIT")
                : status == LUA_ERRMEM ? QStringLiteral("AI_EVENT_MEMORY_LIMIT")
                                      : QStringLiteral("AI_EVENT_RUNTIME_ERROR");
            if (status != 0 && !m_instructionLimitExceeded && status != LUA_ERRMEM)
                logLuaErrorOnce("AI_EVENT_RUNTIME_ERROR", state);
            rebuild = status == LUA_ERRMEM || m_instructionLimitExceeded;
        } else if (!parseIntentionDeltas(state, world, deltas)) {
            failure = QStringLiteral("AI_EVENT_INVALID_RESULT");
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
    if (rebuild) {
        // Rebuild once for future work, never replay this event or commit its deltas.
        shutdown();
        QString rebuildError;
        if (!initialize(&rebuildError))
            failure += QStringLiteral(":REBUILD_FAILED");
    }
    if (!failure.isEmpty())
        return fail(failure);
    if (deltas.isEmpty())
        return true;

    LuaRuntime::Binding binding(modeRuntime);
    ExecutionBinding executionBinding(*this);
    lua_State *state = modeRuntime.state();
    const int top = lua_gettop(state);
    lua_getglobal(state, "sgs");
    if (!lua_istable(state, -1)) {
        lua_settop(state, top);
        return fail(QStringLiteral("AI_EVENT_MODE_UNAVAILABLE"));
    }
    // Keep the commit function below the preparation call. A policy callback
    // cannot replace the function the host will invoke after its budget stops.
    lua_getfield(state, -1, "commitModeAIIntentions");
    if (!lua_isfunction(state, -1)) {
        lua_settop(state, top);
        return fail(QStringLiteral("AI_EVENT_MODE_UNAVAILABLE"));
    }
    lua_getfield(state, -2, "prepareModeAIIntentions");
    lua_remove(state, top + 1); // sgs; leave commit, prepare on the stack.
    if (!lua_isfunction(state, -1)) {
        lua_settop(state, top);
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
        lua_rawseti(state, -2, index + 1);
    }
    // Only preparation shares the remaining event budget. It returns an opaque
    // Room-VM token without changing the mind, even if interrupted at its return.
    const lua_Hook previousHook = lua_gethook(state);
    const int previousMask = lua_gethookmask(state);
    const int previousCount = lua_gethookcount(state);
    lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
    const int status = LuaRuntime::protectedCall(state, 2, 1, 0);
    lua_sethook(state, nullptr, 0, 0);
    // A rejected preparation returns its reason string instead of raising.
    if ((status != 0 && !m_instructionLimitExceeded) || (status == 0 && !lua_istable(state, -1)))
        logLuaErrorOnce("AI_EVENT_MODE_ERROR", state);
    bool accepted = false;
    if (status == 0 && lua_istable(state, -1) && !m_instructionLimitExceeded) {
        if (m_room && m_room->roomRuntime()
            && m_room->roomRuntime()->stateRevision() != world.revision) {
            // A mode hook that entered native gameplay cannot commit an inference
            // derived from the board that existed before its mutation.
            failure = QStringLiteral("AI_EVENT_MODE_STALE");
        } else {
            // Stack is commit, prepared token. Commit contains no policy callbacks
            // or loops, and cannot time out after replacing a successfully staged mind.
            const int commitStatus = LuaRuntime::protectedCall(state, 1, 1, 0);
            if (commitStatus != 0 || !lua_isnil(state, -1)) logLuaErrorOnce("AI_EVENT_MODE_ERROR", state);
            accepted = commitStatus == 0 && lua_isnil(state, -1);
        }
    }
    lua_sethook(state, previousHook, previousMask, previousCount);
    if (!accepted && failure.isEmpty()) {
        failure = m_instructionLimitExceeded ? QStringLiteral("AI_EVENT_MODE_INSTRUCTION_LIMIT")
                                            : QStringLiteral("AI_EVENT_MODE_ERROR");
    }
    lua_settop(state, top);
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

void AiLuaRuntime::commitOriginalSDChoices(const AIRequest &request, const AIResult &result,
                                          const CardUseStruct &use)
{
    const QString turn = m_room->historyScopes().value("turn_id").toString();
    QStringList targets;
    for (const ServerPlayer *target : use.to) targets << target->objectName();
    const int physical = use.card && !use.card->isVirtualCard() ? use.card->getEffectiveId() : -1;
    const int conversion = result.action.hasCardSpec ? result.action.cardSpec.conversionId : -1;
    for (auto it = m_originalSDSelections.begin(); it != m_originalSDSelections.end();) {
        if (it->turn != turn) { it = m_originalSDSelections.erase(it); continue; }
        if (it->viewer == request.viewerObjectName && it->decision != request.decisionId && it->useEvent == 0) {
            it = m_originalSDSelections.erase(it);
            continue;
        }
        if (it->viewer != request.viewerObjectName || it->decision != request.decisionId) { ++it; continue; }
        if (!use.card || it->reason != use.card->objectName() || !targets.contains(it->target)
            || (conversion >= 0 ? it->conversionId != conversion : it->physicalId != physical)) {
            it = m_originalSDSelections.erase(it);
            continue;
        }
        it->acceptedSource = m_room->historyCardSnapshot(use.card);
        ++it;
    }
}

void AiLuaRuntime::observeOriginalSDUse(int triggerEvent, const CardUseStruct &use)
{
    if (!use.from || !use.card || (triggerEvent != CardUsed && triggerEvent != CardFinished)) return;
    const qint64 event = m_room->historyParent(m_room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
    if (event <= 0) return;
    const QVariantMap source = m_room->historyCardSnapshot(use.card);
    for (auto it = m_originalSDSelections.begin(); it != m_originalSDSelections.end();) {
        if (triggerEvent == CardFinished && it->useEvent == event) {
            it = m_originalSDSelections.erase(it);
            continue;
        }
        if (triggerEvent == CardUsed && it->useEvent == 0 && it->viewer == use.from->objectName()
            && it->acceptedSource == source) it->useEvent = event;
        ++it;
    }
}

int AiLuaRuntime::luaOriginalSDChoice(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    const AIRequest *request = currentAiDecision;
    const auto reply = [state](QJsonObject value) {
        value.insert("kind", "original_sd_choice");
        pushAIJsonValue(state, value);
        return 1;
    };
    const auto unknown = [&](const char *reason) {
        return reply(QJsonObject{{"known", false}, {"reason", QString::fromLatin1(reason)}});
    };
    if (!runtime || current() != runtime || !request || !Config.EnableHegemony || !isPlainTable(state, 1))
        return unknown("Original SD requires an active isolated Hegemony request");
    const auto stringField = [state](int index, const char *key) {
        lua_getfield(state, index, key);
        QString value;
        if (lua_type(state, -1) == LUA_TSTRING) readBoundedString(state, -1, value);
        lua_pop(state, 1);
        return value.size() <= 256 ? value : QString();
    };
    const auto intField = [state](int index, const char *key, int fallback) {
        lua_getfield(state, index, key);
        int value = fallback;
        if (lua_type(state, -1) != LUA_TNUMBER || !aiResultInteger(lua_tonumber(state, -1), value)) value = fallback;
        lua_pop(state, 1);
        return value;
    };
    const auto boolField = [state](int index, const char *key) {
        lua_getfield(state, index, key);
        const bool value = lua_type(state, -1) == LUA_TBOOLEAN && lua_toboolean(state, -1);
        lua_pop(state, 1);
        return value;
    };
    const auto token = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    const QString op = stringField(1, "op"), scopeKey = stringField(1, "scope");
    ServerPlayer *viewer = runtime->m_room->findPlayerByObjectName(request->viewerObjectName);
    if (!viewer) return unknown("Original SD viewer is unavailable");
    EngineRuntimeContextScope engineScope(*Sanguosha, runtime->m_room);
    LuaRuntime::Binding nativeBinding(runtime->m_room->roomRuntime()->lua(), false);
    const QString turn = runtime->m_room->historyScopes().value("turn_id").toString();
    const auto disclosed = [&](int id) {
        const auto contains = [id](const QList<AICardView> &cards) {
            for (const AICardView &card : cards) if (card.effectiveId == id) return true;
            return false;
        };
        if (contains(request->worldView.handCards) || contains(request->worldView.self.equips)
            || contains(request->worldView.self.judgingArea)) return true;
        for (const AIPlayerView &player : request->worldView.players)
            if (contains(player.knownCards) || contains(player.equips) || contains(player.judgingArea)) return true;
        return false;
    };
    if (op == "enter") {
        const QString reason = stringField(1, "reason");
        if ((reason != "snatch" && reason != "dismantlement") || runtime->m_originalSDScopes.size() >= 32)
            return unknown("Original SD source or scope limit is invalid");
        lua_getfield(state, 1, "source");
        if (!isPlainTable(state, -1)) { lua_pop(state, 1); return unknown("Original SD source is malformed"); }
        const int sourceIndex = lua_gettop(state);
        const int physical = intField(sourceIndex, "id", -1);
        const int conversion = intField(sourceIndex, "conversion_id", -1);
        const QString recipe = stringField(sourceIndex, "recipe_key");
        lua_pop(state, 1);
        const bool dummy = boolField(1, "dummy");
        bool admitted = dummy && !recipe.isEmpty();
        for (const AICardCandidateView &candidate : request->cardCandidates)
            if (candidate.cardId == physical && candidate.available) {
                const Card *card = Sanguosha->getCard(physical);
                admitted = card && card->objectName() == reason;
            }
        for (const AICardConversionView &candidate : request->cardConversions)
            if (candidate.conversionId == conversion && candidate.available
                && candidate.producedCard.objectName == reason) admitted = true;
        if (!admitted) return unknown("Original SD source was not issued to this request");
        OriginalSDScope scope;
        scope.decision = request->decisionId;
        scope.viewer = request->viewerObjectName;
        scope.reason = reason;
        scope.dummy = dummy;
        scope.physicalId = physical;
        scope.conversionId = conversion;
        const QString key = token();
        runtime->m_originalSDScopes.insert(key, scope);
        return reply(QJsonObject{{"known", true}, {"scope", key}});
    }
    if (op == "leave") {
        const auto it = runtime->m_originalSDScopes.constFind(scopeKey);
        if (it == runtime->m_originalSDScopes.cend() || it->decision != request->decisionId
            || it->viewer != request->viewerObjectName) return unknown("Original SD scope is stale");
        runtime->m_originalSDScopes.remove(scopeKey);
        return reply(QJsonObject{{"known", true}});
    }
    const QString targetName = stringField(1, "target");
    ServerPlayer *target = runtime->m_room->findPlayerByObjectName(targetName);
    if (op == "read") {
        const QString reason = stringField(1, "reason"), flags = stringField(1, "flags");
        if (!target || request->kind != AIRequest::CardChosen || request->choiceOptions.reason != reason
            || request->choiceOptions.context.value("who").toString() != targetName
            || request->choiceOptions.context.value("flags").toString() != flags)
            return unknown("Original SD read is outside its card-chosen request");
        const QVariantMap parent = runtime->m_room->historyParent(runtime->m_room->currentHistoryEventId(), "use_card", true);
        const QVariantMap data = parent.value("data").toMap();
        for (auto it = runtime->m_originalSDSelections.begin(); it != runtime->m_originalSDSelections.end();) {
            if (it->turn != turn) { it = runtime->m_originalSDSelections.erase(it); continue; }
            if (it->viewer != request->viewerObjectName || it->target != targetName || it->reason != reason
                || it->acceptedSource.isEmpty() || data.value("from").toString() != it->viewer
                || it->useEvent <= 0 || it->useEvent != parent.value("id").toLongLong()
                || data.value("card").toMap() != it->acceptedSource) { ++it; continue; }
            const int selected = it->selectedId;
            runtime->m_originalSDSelections.erase(it);
            for (auto domain = request->opaqueCardDomains.cbegin(); domain != request->opaqueCardDomains.cend(); ++domain) {
                const int index = domain.value().indexOf(selected);
                if (index >= 0) return reply(QJsonObject{{"known", true}, {"selected", true},
                    {"receipt", QJsonObject{{"token", domain.key()}, {"index", index + 1}}}});
            }
            break;
        }
        return reply(QJsonObject{{"known", true}, {"selected", false}});
    }
    auto frame = runtime->m_originalSDScopes.find(scopeKey);
    if (frame == runtime->m_originalSDScopes.end() || frame->decision != request->decisionId
        || frame->viewer != request->viewerObjectName) return unknown("Original SD planning scope is stale");
    if (op == "select") {
        const auto domain = frame->domains.constFind(stringField(1, "token"));
        const int ordinal = intField(1, "ordinal", -1);
        if (domain == frame->domains.cend() || ordinal < 0 || ordinal >= domain->second.size()
            || frame->handles.size() >= 128) return unknown("Original SD ordinal is outside the sealed domain");
        target = runtime->m_room->findPlayerByObjectName(domain->first);
        if (!target) return unknown("Original SD domain target is unavailable");
        const QString key = token();
        frame->handles.insert(key, qMakePair(domain->first, domain->second.at(ordinal)));
        return reply(QJsonObject{{"known", true}, {"handle", key}});
    }
    if (!target) return unknown("Original SD target is unavailable");
    if (op == "domain") {
        const QString flags = stringField(1, "flags");
        if (flags.isEmpty() || flags.size() > 3 || QString(flags).remove('h').remove('e').remove('j') != ""
            || frame->domains.size() >= 64) return unknown("Original SD domain is invalid");
        QList<int> ids;
        for (const Card *card : target->getCards(flags)) ids << card->getEffectiveId();
        const QString key = token();
        frame->domains.insert(key, qMakePair(targetName, ids));
        // Policy may identify public members by position; concealed identities
        // remain in native custody, including the exact selected hand card.
        QJsonObject visibleIndices;
        for (int index = 0; index < ids.size(); ++index)
            if (disclosed(ids.at(index))) visibleIndices.insert(QString::number(ids.at(index)), index + 1);
        return reply(QJsonObject{{"known", true}, {"token", key}, {"count", ids.size()},
            {"visible_card_indices", visibleIndices}});
    }
    if (op == "native_hand") {
        if (target->isKongcheng()) return reply(QJsonObject{{"known", true}, {"empty", true}});
        if (frame->handles.size() >= 128) return unknown("Original SD handle limit reached");
        const QString key = token();
        // Sample uniformly in native custody using the independent AI stream;
        // planning must not advance gameplay RNG or disclose a hand identity.
        const QList<int> hand = target->handCards();
        frame->handles.insert(key, qMakePair(targetName, hand.at(runtime->m_rng.bounded(int(hand.size())))));
        return reply(QJsonObject{{"known", true}, {"handle", key}});
    }
    if (op == "can_get") {
        lua_getfield(state, 1, "key");
        bool valid = false, value = false;
        int id = -1;
        if (lua_type(state, -1) == LUA_TNUMBER && aiResultInteger(lua_tonumber(state, -1), id) && disclosed(id)) {
            for (const Card *card : target->getCards("hej")) if (card->getEffectiveId() == id) valid = true;
            if (valid) value = viewer->canGet(target, id);
        } else if (lua_type(state, -1) == LUA_TSTRING) {
            QString flags;
            readBoundedString(state, -1, flags);
            valid = !flags.isEmpty() && flags.size() <= 3 && QString(flags).remove('h').remove('e').remove('j').isEmpty();
            if (valid) value = viewer->canGet(target, flags);
        }
        lua_pop(state, 1);
        if (!valid) return unknown("Original canGet key is not a disclosed card or finite zone");
        return reply(QJsonObject{{"known", true}, {"value", value}});
    }
    if (op == "write") {
        if (frame->dummy || turn.isEmpty() || turn == "0" || stringField(1, "reason") != frame->reason)
            return unknown("Original SD write has no actual source or turn");
        int selected = -1;
        lua_getfield(state, 1, "selection");
        if (lua_type(state, -1) == LUA_TSTRING) {
            QString key;
            readBoundedString(state, -1, key);
            const auto handle = frame->handles.constFind(key);
            if (handle != frame->handles.cend() && handle->first == targetName) selected = handle->second;
        } else if (lua_type(state, -1) == LUA_TNUMBER) {
            int id = -1;
            if (aiResultInteger(lua_tonumber(state, -1), id) && disclosed(id)) selected = id;
        }
        lua_pop(state, 1);
        bool inDomain = false;
        for (const Card *card : target->getCards("hej")) if (card->getEffectiveId() == selected) inDomain = true;
        if (!inDomain) return unknown("Original SD selected card left its target");
        auto &selections = runtime->m_originalSDSelections;
        for (auto it = selections.begin(); it != selections.end();) {
            if (it->turn != turn || (it->decision == frame->decision && it->viewer == frame->viewer
                && it->target == targetName && it->reason == frame->reason))
                it = selections.erase(it);
            else ++it;
        }
        if (selections.size() >= 128) return unknown("Original SD selection limit reached");
        OriginalSDSelection selection;
        selection.decision = frame->decision;
        selection.viewer = frame->viewer;
        selection.target = targetName;
        selection.reason = frame->reason;
        selection.turn = turn;
        selection.physicalId = frame->physicalId;
        selection.conversionId = frame->conversionId;
        selection.selectedId = selected;
        selections << selection;
        return reply(QJsonObject{{"known", true}});
    }
    return unknown("Original SD operation is not admitted");
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
    if (runtime->m_instructionProfiling && !runtime->m_instructionLimitExceeded
        && ++runtime->m_instructionProfileTicks == 10) {
        runtime->m_instructionProfileTicks = 0;
        ++runtime->m_instructionProfileSamples;
        QSet<QString> visited;
        lua_Debug frame;
        for (int level = 0; level < 32 && lua_getstack(state, level, &frame); ++level) {
            if (!lua_getinfo(state, "S", &frame) || frame.linedefined < 0) continue;
            const QString symbol = QString::fromUtf8(frame.short_src) + ':' + QString::number(frame.linedefined);
            if (visited.contains(symbol)) continue;
            visited.insert(symbol);
            if (!runtime->m_instructionProfile.contains(symbol) && runtime->m_instructionProfile.size() >= 512) continue;
            auto &hits = runtime->m_instructionProfile[symbol];
            if (level == 0) ++hits.first;
            ++hits.second;
        }
    } // Finish all C++ sample temporaries before luaL_error can unwind the hook.
    if (runtime->m_instructionsRemaining <= 0) {
        // Nested pcall handlers can catch the error; report the original stack once.
        if (!runtime->m_instructionLimitExceeded)
            logInstructionLimit(state);
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

    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaOriginalSDChoice, 1);
    lua_setglobal(state, "ai_original_sd_choice");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateCard, 1);
    lua_setglobal(state, "ai_evaluate_card");
    lua_pushcfunction(state, &AiLuaRuntime::luaMakeFacadeIndex);
    lua_setglobal(state, "ai_make_facade_index");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaWithOwnerRange, 1);
    lua_setglobal(state, "ai_with_owner_range");
    lua_pushlightuserdata(state, this);
    lua_pushcclosure(state, &AiLuaRuntime::luaEvaluateOwnerRangeCard, 1);
    lua_setglobal(state, "ai_evaluate_owner_range_card");
    lua_pushboolean(state, qEnvironmentVariableIsSet("QSAN_AI_UNSUPPORTED_TRACE"));
    lua_setglobal(state, "ai_unsupported_trace_enabled");

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
    // DamageStruct is a value struct without Q_ENUM. Export its real constants
    // explicitly so damage hooks never rely on duplicated Lua enum numbers.
    setIntegerField(state, "DamageStruct_Normal", DamageStruct::Normal);
    setIntegerField(state, "DamageStruct_Fire", DamageStruct::Fire);
    setIntegerField(state, "DamageStruct_Thunder", DamageStruct::Thunder);
    setIntegerField(state, "DamageStruct_Ice", DamageStruct::Ice);
    setIntegerField(state, "DamageStruct_Poison", DamageStruct::Poison);
    setIntegerField(state, "DamageStruct_God", DamageStruct::God);
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_UNKNOWN", CardUseStruct::CARD_USE_REASON_UNKNOWN);
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_PLAY", CardUseStruct::CARD_USE_REASON_PLAY);
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_RESPONSE", CardUseStruct::CARD_USE_REASON_RESPONSE);
    setIntegerField(state, "CardUseStruct_CARD_USE_REASON_RESPONSE_USE", CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    setIntegerField(state, "Room_GuanxingBothSides", Room::GuanxingBothSides);
    setIntegerField(state, "Room_GuanxingUpOnly", Room::GuanxingUpOnly);
    setIntegerField(state, "Room_GuanxingDownOnly", Room::GuanxingDownOnly);
    // The trigger_event values a world view's events carry.
    setIntegerField(state, "GameReady", GameReady);
    setIntegerField(state, "GameStart", GameStart);
    setIntegerField(state, "RoundStart", RoundStart);
    setIntegerField(state, "TurnStart", TurnStart);
    setIntegerField(state, "EventPhaseStart", EventPhaseStart);
    setIntegerField(state, "EventPhaseEnd", EventPhaseEnd);
    setIntegerField(state, "EventPhaseChanging", EventPhaseChanging);
    setIntegerField(state, "DrawNCards", DrawNCards);
    setIntegerField(state, "AfterDrawNCards", AfterDrawNCards);
    setIntegerField(state, "PreCardUsed", PreCardUsed);
    setIntegerField(state, "CardUsed", CardUsed);
    setIntegerField(state, "TargetSpecified", TargetSpecified);
    setIntegerField(state, "TargetConfirmed", TargetConfirmed);
    setIntegerField(state, "CardEffect", CardEffect);
    setIntegerField(state, "CardOffset", CardOffset);
    setIntegerField(state, "CardFinished", CardFinished);
    setIntegerField(state, "DamageForseen", DamageForseen);
    setIntegerField(state, "DamageCaused", DamageCaused);
    setIntegerField(state, "DamageInflicted", DamageInflicted);
    setIntegerField(state, "Damage", Damage);
    setIntegerField(state, "Damaged", Damaged);
    setIntegerField(state, "HpRecover", HpRecover);
    setIntegerField(state, "Dying", Dying);
    setIntegerField(state, "Death", Death);
    setIntegerField(state, "BeforeGameOverJudge", BeforeGameOverJudge);
    setIntegerField(state, "Revived", Revived);
    setIntegerField(state, "StartJudge", StartJudge);
    setIntegerField(state, "AskForRetrial", AskForRetrial);
    setIntegerField(state, "FinishJudge", FinishJudge);
    setIntegerField(state, "CardsMoveOneTime", CardsMoveOneTime);
    setIntegerField(state, "ShowCards", ShowCards);
    setIntegerField(state, "ChoiceMade", ChoiceMade);
    // The move reasons in card-move event details.
    const QList<QPair<const char *, int>> moveReasons{
        {"UNKNOWN", int(CardMoveReason::S_REASON_UNKNOWN)}, {"USE", int(CardMoveReason::S_REASON_USE)},
        {"RESPONSE", int(CardMoveReason::S_REASON_RESPONSE)}, {"DISCARD", int(CardMoveReason::S_REASON_DISCARD)},
        {"RECAST", int(CardMoveReason::S_REASON_RECAST)}, {"PINDIAN", int(CardMoveReason::S_REASON_PINDIAN)},
        {"DRAW", int(CardMoveReason::S_REASON_DRAW)}, {"GOTCARD", int(CardMoveReason::S_REASON_GOTCARD)},
        {"SHOW", int(CardMoveReason::S_REASON_SHOW)}, {"TRANSFER", int(CardMoveReason::S_REASON_TRANSFER)},
        {"PUT", int(CardMoveReason::S_REASON_PUT)}, {"LETUSE", int(CardMoveReason::S_REASON_LETUSE)},
        {"RETRIAL", int(CardMoveReason::S_REASON_RETRIAL)}, {"RULEDISCARD", int(CardMoveReason::S_REASON_RULEDISCARD)},
        {"THROW", int(CardMoveReason::S_REASON_THROW)}, {"DISMANTLE", int(CardMoveReason::S_REASON_DISMANTLE)},
        {"GIVE", int(CardMoveReason::S_REASON_GIVE)}, {"EXTRACTION", int(CardMoveReason::S_REASON_EXTRACTION)},
        {"GOTBACK", int(CardMoveReason::S_REASON_GOTBACK)}, {"RECYCLE", int(CardMoveReason::S_REASON_RECYCLE)},
        {"ROB", int(CardMoveReason::S_REASON_ROB)}, {"PREVIEWGIVE", int(CardMoveReason::S_REASON_PREVIEWGIVE)},
        {"EXCLUSIVE", int(CardMoveReason::S_REASON_EXCLUSIVE)}, {"TURNOVER", int(CardMoveReason::S_REASON_TURNOVER)},
        {"JUDGE", int(CardMoveReason::S_REASON_JUDGE)}, {"PREVIEW", int(CardMoveReason::S_REASON_PREVIEW)},
        {"DEMONSTRATE", int(CardMoveReason::S_REASON_DEMONSTRATE)}, {"SWAP", int(CardMoveReason::S_REASON_SWAP)},
        {"OVERRIDE", int(CardMoveReason::S_REASON_OVERRIDE)},
        {"EXCHANGE_FROM_PILE", int(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE)},
        {"NATURAL_ENTER", int(CardMoveReason::S_REASON_NATURAL_ENTER)},
        {"REMOVE_FROM_PILE", int(CardMoveReason::S_REASON_REMOVE_FROM_PILE)},
        {"JUDGEDONE", int(CardMoveReason::S_REASON_JUDGEDONE)},
        {"CHANGE_EQUIP", int(CardMoveReason::S_REASON_CHANGE_EQUIP)},
        {"SHUFFLE", int(CardMoveReason::S_REASON_SHUFFLE)}, {"PUT_END", int(CardMoveReason::S_REASON_PUT_END)}};
    for (const auto &reason : moveReasons)
        setIntegerField(state, QByteArray("CardMoveReason_S_REASON_").append(reason.first).constData(),
                        reason.second);
    setIntegerField(state, "CardMoveReason_S_MASK_BASIC_REASON", CardMoveReason::S_MASK_BASIC_REASON);
    lua_setglobal(state, "sgs");

    // Engine card kinds in engine order, one row per card name: static definitions
    // that let a policy classify a pattern. Suit, number and ids belong to single
    // cards, so they are not here.
    const QStringList banned = Sanguosha ? Sanguosha->getBanPackages() : QStringList();
    QHash<QString, int> copies;
    for (int id = 0; Sanguosha && id < Sanguosha->getCardCount(); ++id)
        if (const Card *card = Sanguosha->getEngineCard(id))
            ++copies[card->objectName()];
    QSet<QString> seen;
    lua_newtable(state);
    int row = 0;
    for (int id = 0; Sanguosha && id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (!card || seen.contains(card->objectName()))
            continue;
        seen.insert(card->objectName());
        lua_createtable(state, 0, 15);
        setStringField(state, "name", card->objectName());
        setIntegerField(state, "count", copies.value(card->objectName()));
        setStringField(state, "class_name", card->getClassName());
        setStringField(state, "subtype", card->getSubtype());
        setStringField(state, "package", card->getPackage());
        lua_pushboolean(state, banned.contains(card->getPackage()));
        lua_setfield(state, -2, "banned");
        setIntegerField(state, "type_id", int(card->getTypeId()));
        lua_createtable(state, int(card->getKindOfNames().size()), 0);
        for (int index = 0; index < card->getKindOfNames().size(); ++index) {
            pushQString(state, card->getKindOfNames().at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "kind_of");
        lua_pushboolean(state, card->targetFixed());
        lua_setfield(state, -2, "target_fixed");
        lua_pushboolean(state, card->isDamageCard());
        lua_setfield(state, -2, "damage_card");
        lua_pushboolean(state, card->isNDTrick());
        lua_setfield(state, -2, "nd_trick");
        lua_pushboolean(state, card->canRecast());
        lua_setfield(state, -2, "can_recast");
        lua_pushboolean(state, card->isTransferable());
        lua_setfield(state, -2, "transferable");
        lua_pushboolean(state, card->isSingleTargetCard());
        lua_setfield(state, -2, "single_target");
        if (const auto *equip = dynamic_cast<const EquipCard *>(card)) {
            setIntegerField(state, "equip_slot", int(equip->location()));
            const QList<int> occupied = equip->getOccupyLocations();
            lua_createtable(state, int(occupied.size()), 0);
            for (int index = 0; index < occupied.size(); ++index) {
                lua_pushinteger(state, occupied.at(index));
                lua_rawseti(state, -2, index + 1);
            }
            lua_setfield(state, -2, "occupy_locations");
        }
        if (const auto *weapon = dynamic_cast<const Weapon *>(card))
            setIntegerField(state, "weapon_range", weapon->getRange());
        lua_rawseti(state, -2, ++row);
    }
    lua_setglobal(state, "ai_card_catalog");
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
    // National war is independently isolated. Compatibility routes remain
    // available to other modes, never as a Hegemony policy dependency.
    if (Config.EnableHegemony) {
        for (const QString &callback : aiRoutableCallbackNames())
            routes.setCallbackRoute(callback, QString(), AiRouteIsolated);
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
    if (!Config.EnableHegemony) {
        addRoutes(QStringLiteral("AiLegacyDirectCallbacks"), AiRouteLegacyDirect);
        addRoutes(QStringLiteral("AiLegacyAdaptedCallbacks"), AiRouteLegacyAdapted);
    }
    addRoutes(QStringLiteral("AiIsolatedCallbacks"), AiRouteIsolated);
}

void AiLuaRuntime::loadConfiguredRoutes()
{
    loadConfiguredAiRoutes(m_routes);
}

QJsonObject AiLuaRuntime::publicCardCounts()
{
    const int size = Sanguosha->getCardCount();
    if (size == m_publicCardCount && !m_publicCardCounts.isEmpty())
        return m_publicCardCounts;
    QHash<QString, int> counts;
    bool missing = false;
    for (int id = 0; id < size; ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (!card) { missing = true; continue; }
        const QStringList names = card->getKindOfNames();
        const QSet<QString> unique(names.cbegin(), names.cend());
        for (const QString &name : unique) ++counts[name];
    }
    QJsonObject classes;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        classes.insert(it.key(), it.value());
    m_publicCardCount = size;
    m_publicCardCounts = QJsonObject{{"engine_size", size},
        {"missing_engine_card", missing}, {"engine_counts", classes}};
    return m_publicCardCounts;
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

    foreach (const QString &package, Sanguosha->getExtensions()) {
        const QString fileName =
            available.value(package.toLower() + QStringLiteral("-ai.lua"));
        if (fileName.isEmpty())
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
        foreach (const QString &configuredName, configuredScripts) {
            if (!loadIsolatedScript(configuredName.trimmed(), error))
                return false;
        }
        return true;
    }

    const QStringList coreScripts = declaredCoreScripts(error);
    if (coreScripts.isEmpty())
        return false;
    foreach (const QString &fileName, coreScripts) {
        if (!loadIsolatedScript(fileName, error))
            return false;
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

static void pushAICardCandidate(lua_State *state, const AICardCandidateView &candidate)
{
    lua_createtable(state, 0, 8);
    lua_pushinteger(state, candidate.candidateId);
    lua_setfield(state, -2, "candidate_id");
    lua_pushinteger(state, candidate.cardId);
    lua_setfield(state, -2, "card_id");
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
    if (candidate.extraTargetKnown) {
        lua_pushinteger(state, candidate.extraTargetCount);
        lua_setfield(state, -2, "extra_target_count");
        lua_createtable(state, 0, int(candidate.extraTargetByPlayer.size()));
        for (auto it = candidate.extraTargetByPlayer.constBegin();
             it != candidate.extraTargetByPlayer.constEnd(); ++it) {
            lua_pushinteger(state, it.value());
            lua_setfield(state, -2, it.key().toUtf8().constData());
        }
        lua_setfield(state, -2, "extra_target_by_player");
    }
    if (candidate.extraTargetKnown || !candidate.targetFilterEmpty.isEmpty()) {
        lua_createtable(state, 0, int(candidate.targetFilterEmpty.size()));
        for (auto it = candidate.targetFilterEmpty.constBegin();
             it != candidate.targetFilterEmpty.constEnd(); ++it) {
            lua_pushboolean(state, it.value());
            lua_setfield(state, -2, it.key().toUtf8().constData());
        }
        lua_setfield(state, -2, "target_filter_empty");
    }
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
    lua_pushboolean(state, action.equipmentActivation);
    lua_setfield(state, -2, "equipment_activation");
    if (action.physicalEquipSource.isValid()) {
        pushAIJsonValue(state, QJsonObject::fromVariantMap(action.physicalEquipSource.toVariantMap()));
        lua_setfield(state, -2, "physical_equipment");
    }
    if (action.hasEmptySelectionEvaluation) {
        // This action-local card face and target plan carry no submission token.
        lua_createtable(state, 0, 2);
        pushAICardView(state, action.emptySelectionCard);
        lua_setfield(state, -2, "card");
        pushAICardCandidate(state, action.emptySelectionCandidate);
        lua_setfield(state, -2, "candidate");
        lua_setfield(state, -2, "empty_selection");
    }
}
// context.acquired_card_evaluations[id] / printed_card_evaluations[name]: the card as
// the viewer would hold it, those owner rules again as card_rules, and its play
// candidate. Read-only evaluation.
static void pushChoiceCardEvaluations(lua_State *state, const QList<AIChoiceCardEvaluation> &rows,
                                      const QString &viewer, bool byName)
{
    lua_createtable(state, 0, int(rows.size()));
    for (const AIChoiceCardEvaluation &row : rows) {
        const AICardView &card = row.card;
        lua_createtable(state, 0, 4);
        lua_pushboolean(state, true);
        lua_setfield(state, -2, "evaluation_only");
        pushAICardView(state, card);
        lua_pushboolean(state, true);
        lua_setfield(state, -2, "evaluation_only");
        setStringField(state, "evaluation_owner", viewer);
        lua_setfield(state, -2, "card");
        lua_createtable(state, 0, 8);
        const auto pushMethods = [state](const QMap<int, bool> &values, const char *field) {
            lua_createtable(state, 0, int(values.size()));
            for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
                lua_pushboolean(state, it.value());
                lua_setfield(state, -2, QByteArray::number(it.key()).constData());
            }
            lua_setfield(state, -2, field);
        };
        pushMethods(card.limits, "limits");
        pushMethods(card.limitsHand, "limits_hand");
        lua_createtable(state, 0, int(card.prohibited.size()));
        for (auto it = card.prohibited.constBegin(); it != card.prohibited.constEnd(); ++it) {
            lua_pushboolean(state, it.value());
            lua_setfield(state, -2, it.key().toUtf8().constData());
        }
        lua_setfield(state, -2, "prohibited");
        lua_pushboolean(state, card.available);
        lua_setfield(state, -2, "available");
        lua_pushinteger(state, card.targetModResidue);
        lua_setfield(state, -2, "target_mod_residue");
        lua_pushinteger(state, card.targetModDistance);
        lua_setfield(state, -2, "target_mod_distance");
        lua_pushinteger(state, card.targetModExtra);
        lua_setfield(state, -2, "target_mod_extra");
        if (card.analepticStaticKnown) {
            lua_pushboolean(state, card.analepticStaticAvailable);
            lua_setfield(state, -2, "analeptic_static_available");
        }
        lua_setfield(state, -2, "card_rules");
        pushAICardCandidate(state, row.candidate);
        lua_setfield(state, -2, "candidate");
        lua_setfield(state, -2, byName ? card.objectName.toUtf8().constData()
                                       : QByteArray::number(card.effectiveId).constData());
    }
}
int AiLuaRuntime::luaMakeFacadeIndex(lua_State *state)
{
    luaL_checktype(state, 1, LUA_TTABLE);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    lua_settop(state, 2);
    lua_pushcclosure(state, &AiLuaRuntime::luaFacadeIndex, 2);
    return 1;
}

int AiLuaRuntime::luaFacadeIndex(lua_State *state)
{
    // Only table dispatch moves to native code: providers remain live, values
    // are never cached, and missing-value policy remains owned by Lua.
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_settop(state, 2);
    lua_pushvalue(state, 2);
    lua_rawget(state, lua_upvalueindex(1));
    if (!lua_isnil(state, -1)) return 1;
    lua_pop(state, 1);
    lua_pushliteral(state, "_scalar_methods");
    lua_rawget(state, 1);
    if (lua_istable(state, -1)) {
        lua_pushvalue(state, 2);
        lua_rawget(state, -2);
        if (!lua_isnil(state, -1)) return 1;
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    lua_pushvalue(state, lua_upvalueindex(2));
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_pushvalue(state, 1);
    lua_pushvalue(state, 2);
    lua_call(state, 3, 1);
    return 1;
}

bool AiLuaRuntime::consumeCardEvaluationQuery(const AIRequest &request)
{
    if (m_cardEvaluationDecision != request.decisionId) {
        m_cardEvaluationDecision = request.decisionId;
        m_cardEvaluationCalls = 0;
        m_cardEvaluationBudget = 16384;
    }
    return ++m_cardEvaluationCalls <= 64;
}

int AiLuaRuntime::luaWithOwnerRange(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    const AIRequest *request = currentAiDecision;
    if (!runtime || current() != runtime || !request || !Config.EnableHegemony
        || lua_gettop(state) != 3 || lua_type(state, 1) != LUA_TSTRING
        || lua_type(state, 2) != LUA_TSTRING || !lua_isfunction(state, 3)
        || lua_rawlen(state, 2) == 0 || lua_rawlen(state, 2) > 128
        || (currentOwnerRangeQuery && currentOwnerRangeQuery->depth >= 8)
        || !runtime->consumeCardEvaluationQuery(*request)) {
        lua_pushliteral(state, "owner range requires a bounded read-only isolated scope");
        return lua_error(state);
    }
    int status = 0;
    {
        const QString flag = QString::fromUtf8(lua_tostring(state, 1));
        ServerPlayer *owner = runtime->m_room->findPlayerByObjectName(request->viewerObjectName);
        if (!owner || !owner->isAlive() || runtime->m_room->roomRuntime()->stateRevision() != request->stateRevision
            || (flag != "InfinityAttackRange" && flag != "slashNoDistanceLimit")) {
            lua_pushliteral(state, "invalid owner range flag or stale decision");
            status = 1;
        } else {
            OwnerRangeQuery query{runtime, request, QString::fromUtf8(lua_tostring(state, 2)),
                currentOwnerRangeQuery ? currentOwnerRangeQuery->depth + 1 : 1};
            QScopedValueRollback<const OwnerRangeQuery *> queryGuard(currentOwnerRangeQuery, &query);
            // A read overlay bypasses memoized rules and leaves real flags,
            // signals, history and revision untouched, including callback errors.
            TargetModSkillQueryScope scope(owner, QMap<QString, bool>{{flag, true}});
            QJsonObject data{{"kind", "owner_range_hypothesis"}, {"viewer", request->viewerObjectName},
                {"flag", flag}, {"source_key", query.source},
                {"InfinityAttackRange", owner->hasFlag("InfinityAttackRange")},
                {"slashNoDistanceLimit", owner->hasFlag("slashNoDistanceLimit")},
                {"attack_range", owner->getAttackRange()}, {"attack_range_without_weapon", owner->getAttackRange(false)}};
            QJsonObject ranges;
            for (const ServerPlayer *target : runtime->m_room->getAlivePlayers())
                ranges.insert(target->objectName(), owner->inMyAttackRange(target));
            data.insert("in_attack_range", ranges);
            lua_pushvalue(state, 3);
            pushAIJsonValue(state, data);
            status = lua_pcall(state, 1, LUA_MULTRET, 0);
        }
    }
    // Never longjmp over native scope destructors.
    if (status != 0) return lua_error(state);
    return lua_gettop(state) - 3;
}

int AiLuaRuntime::luaEvaluateOwnerRangeCard(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    const AIRequest *request = currentAiDecision;
    const auto unknown = [state](const QString &reason) {
        pushAIJsonValue(state, QJsonObject{{"known", false}, {"error", reason}});
        return 1;
    };
    if (!runtime || current() != runtime || !request || !currentOwnerRangeQuery
        || currentOwnerRangeQuery->runtime != runtime || currentOwnerRangeQuery->request != request
        || !runtime->consumeCardEvaluationQuery(*request))
        return unknown(QStringLiteral("range card query is outside its bounded native scope"));
    QJsonValue value;
    int valueBudget = 256;
    if (lua_gettop(state) != 1 || !readLuaPolicyValue(state, 1, value, 0, valueBudget, true) || !value.isObject())
        return unknown(QStringLiteral("range card query requires a pure specification"));
    AIChoiceCardEvaluation evaluation;
    QJsonObject queries;
    QString error;
    if (!AiDecisionCoordinator::evaluateOwnerRangeCard(*runtime->m_room, *request, value.toObject(),
        evaluation, queries, runtime->m_cardEvaluationBudget, error)) return unknown(error);
    pushChoiceCardEvaluations(state, {evaluation}, request->viewerObjectName, true);
    lua_getfield(state, -1, evaluation.card.objectName.toUtf8().constData());
    lua_remove(state, -2);
    lua_getfield(state, -1, "card");
    lua_getfield(state, -2, "card_rules");
    lua_setfield(state, -2, "evaluation_rules");
    lua_pop(state, 1);
    for (auto it = queries.begin(); it != queries.end(); ++it) {
        pushAIJsonValue(state, it.value());
        lua_setfield(state, -2, it.key().toUtf8().constData());
    }
    return 1;
}

int AiLuaRuntime::luaEvaluateCard(lua_State *state)
{
    AiLuaRuntime *runtime = fromUpvalue(state);
    const AIRequest *request = currentAiDecision;
    const auto unknown = [state](const QString &reason) {
        pushAIJsonValue(state, QJsonObject{{"known", false}, {"error", reason}});
        return 1;
    };
    if (!runtime || current() != runtime || !request)
        return unknown(QStringLiteral("card evaluation requires an active isolated decision"));
    // This allowance is separate from initial request construction. Enumeration
    // shares its budget across queries; all other rule scans are bounded by the
    // query count and current player roster. Invalid calls consume a slot too.
    if (!runtime->consumeCardEvaluationQuery(*request))
        return unknown(QStringLiteral("card evaluation query limit exceeded"));
    QJsonValue value;
    int valueBudget = 256;
    if (lua_gettop(state) != 1 || !readLuaPolicyValue(state, 1, value, 0, valueBudget, true) || !value.isObject())
        return unknown(QStringLiteral("card evaluation requires a bounded pure specification"));
    QJsonObject spec = value.toObject();
    if (spec.value("operation").toString() == QStringLiteral("target_sequence")) {
        bool accepted = false;
        QString error;
        if (!AiDecisionCoordinator::evaluateTargetSequence(*runtime->m_room, *request, spec, accepted,
                                                           runtime->m_cardEvaluationBudget, error))
            return unknown(error);
        if (qEnvironmentVariableIntValue("QSAN_AI_DIAGNOSTICS") == 1)
            qInfo().noquote() << "[AI_TARGET_SEQUENCE] decision=" << request->decisionId
                << "candidate_id=" << spec.value("candidate_id").toInt()
                << "card_id=" << spec.value("card_id").toInt()
                << "targets=" << QJsonDocument(spec.value("targets").toArray()).toJson(QJsonDocument::Compact)
                << "accepted=" << accepted;
        spec.insert("known", true);
        spec.insert("accepted", accepted);
        pushAIJsonValue(state, spec);
        return 1;
    }
    if (!Config.EnableHegemony)
        return unknown(QStringLiteral("card hypothesis requires an isolated Hegemony decision"));
    AIChoiceCardEvaluation evaluation;
    QString error;
    if (!AiDecisionCoordinator::evaluateCard(*runtime->m_room, *request, spec, evaluation,
                                              runtime->m_cardEvaluationBudget, error))
        return unknown(error);
    if (qEnvironmentVariableIsSet("QSAN_AI_UNSUPPORTED_TRACE"))
        qInfo().noquote() << "[AI_CARD_EVALUATION] decision=" << request->decisionId
            << "viewer=" << request->viewerObjectName << "source=" << spec.value("source_key").toString()
            << "instance=" << spec.value("instance_id").toInt() << "card=" << evaluation.card.objectName
            << "complete=" << evaluation.candidate.completeCoverage
            << "combinations=" << evaluation.candidate.targetCombinations.size();
    // Use the existing choice-evaluation serializer and select its one row; no
    // second face/rule/target serializer or native Card object crosses this API.
    pushChoiceCardEvaluations(state, {evaluation}, request->viewerObjectName, true);
    lua_getfield(state, -1, evaluation.card.objectName.toUtf8().constData());
    lua_remove(state, -2);
    lua_pushboolean(state, true);
    lua_setfield(state, -2, "known");
    setStringField(state, "viewer", request->viewerObjectName);
    setStringField(state, "decision_id", request->getDecisionId());
    setStringField(state, "state_revision", request->getStateRevision());
    setStringField(state, "context_kind", QStringLiteral("activate"));
    setIntegerField(state, "handling_method", int(Card::MethodUse));
    pushAIJsonValue(state, spec);
    lua_setfield(state, -2, "spec");
    return 1;
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
    if (request.kind == AIRequest::Activate || request.kind == AIRequest::UseCard
        || request.kind == AIRequest::RespondCard) {
        lua_createtable(state, int(request.cardCandidates.size()), 0);
        for (int index = 0; index < request.cardCandidates.size(); ++index) {
            pushAICardCandidate(state, request.cardCandidates.at(index));
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "card_candidates");
        // Authorized conversions. The produced card's name and classification come from
        // the authority's own build, so an author reads them rather than asserting them.
        lua_createtable(state, int(request.cardConversions.size()), 0);
        for (int index = 0; index < request.cardConversions.size(); ++index) {
            const AICardConversionView &conversion = request.cardConversions.at(index);
            pushAICardView(state, conversion.producedCard);
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
            setStringField(state, "declared_name", conversion.declaredName);
            if (!conversion.producedCard.virtualCard) {
                lua_pushinteger(state, conversion.producedCard.effectiveId);
                lua_setfield(state, -2, "physical_product_id");
            }
            setStringField(state, "activation_owner", conversion.activationRef.ownerObjectName);
            setStringField(state, "activation_skill", conversion.activationRef.key.skillName);
            lua_pushinteger(state, conversion.activationRef.key.instanceID);
            lua_setfield(state, -2, "activation_instance");
            setStringField(state, "source_owner", conversion.sourceRef.ownerObjectName);
            setStringField(state, "source_skill", conversion.sourceRef.key.skillName);
            lua_pushinteger(state, conversion.sourceRef.key.instanceID);
            lua_setfield(state, -2, "source_instance");
            lua_pushboolean(state, conversion.equipmentActivation);
            lua_setfield(state, -2, "equipment_activation");
            if (conversion.physicalEquipSource.isValid()) {
                pushAIJsonValue(state, QJsonObject::fromVariantMap(conversion.physicalEquipSource.toVariantMap()));
                lua_setfield(state, -2, "physical_equipment");
            }
            lua_pushboolean(state, conversion.activationQuotaAvailable);
            lua_setfield(state, -2, "activation_quota_available");
            lua_pushboolean(state, conversion.sourceQuotaAvailable);
            lua_setfield(state, -2, "source_quota_available");
            lua_pushboolean(state, conversion.available);
            lua_setfield(state, -2, "available");
            if (request.kind != AIRequest::Activate) {
                lua_pushboolean(state, conversion.matchesResponsePattern);
                lua_setfield(state, -2, "matches_response_pattern");
            }
            lua_pushboolean(state, conversion.producedCard.locked);
            lua_setfield(state, -2, "locked");
            lua_pushboolean(state, conversion.jilei);
            lua_setfield(state, -2, "jilei");
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
            // Keep the conversion ABI alias without building the face list twice.
            lua_getfield(state, -1, "kind_of");
            lua_setfield(state, -2, "kind_of_names");
            if (conversion.extraTargetKnown) {
                lua_pushinteger(state, conversion.extraTargetCount);
                lua_setfield(state, -2, "extra_target_count");
                lua_createtable(state, 0, int(conversion.extraTargetByPlayer.size()));
                for (auto it = conversion.extraTargetByPlayer.constBegin();
                     it != conversion.extraTargetByPlayer.constEnd(); ++it) {
                    lua_pushinteger(state, it.value());
                    lua_setfield(state, -2, it.key().toUtf8().constData());
                }
                lua_setfield(state, -2, "extra_target_by_player");
                lua_createtable(state, 0, int(conversion.targetFilterEmpty.size()));
                for (auto it = conversion.targetFilterEmpty.constBegin();
                     it != conversion.targetFilterEmpty.constEnd(); ++it) {
                    lua_pushboolean(state, it.value());
                    lua_setfield(state, -2, it.key().toUtf8().constData());
                }
                lua_setfield(state, -2, "target_filter_empty");
            }
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
        if (request.kind == AIRequest::RespondCard || request.kind == AIRequest::UseCard) {
            // Only the physical class order comes from the authority. Lua derives the
            // per-class skill policy itself, so the policy is as complete as the
            // conversion list it chooses from. A use question without the class scan
            // reports its physical order incomplete.
            lua_createtable(state, 0, 3);
            lua_createtable(state, int(request.physicalClassOrder.size()), 0);
            for (int index = 0; index < request.physicalClassOrder.size(); ++index) {
                pushQString(state, request.physicalClassOrder.at(index));
                lua_rawseti(state, -2, index + 1);
            }
            lua_setfield(state, -2, "physical_class_order");
            lua_pushboolean(state, request.physicalClassOrderComplete);
            lua_setfield(state, -2, "physical_class_order_complete");
            if (request.catalogClassOrderComplete) {
                pushAIJsonValue(state, request.catalogClassOrder);
                lua_setfield(state, -2, "class_order");
                lua_pushboolean(state, true);
                lua_setfield(state, -2, "class_order_complete");
            }
            lua_pushboolean(state, request.conversionsEnumerated);
            lua_setfield(state, -2, "complete");
            lua_setfield(state, -2, "conversion_policy");
        }
    }
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
        if (!choiceOptions.acquiredCards.isEmpty()) {
            pushChoiceCardEvaluations(state, choiceOptions.acquiredCards, request.viewerObjectName, false);
            lua_setfield(state, -2, "acquired_card_evaluations");
        }
        if (!choiceOptions.printedCards.isEmpty()) {
            pushChoiceCardEvaluations(state, choiceOptions.printedCards, request.viewerObjectName, true);
            lua_setfield(state, -2, "printed_card_evaluations");
        }
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
        && readBoundedString(state, -1, spec.name) && !spec.name.isEmpty();
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
            || number < 0 || number > 13) {
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
        && aiResultInteger(lua_tonumber(state, -1), instanceId) && instanceId >= 0;
    lua_pop(state, 1);
    QString owner;
    lua_getfield(state, -1, "owner");
    const bool ownerOk = lua_isnil(state, -1)
        || (lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, owner));
    lua_pop(state, 1);
    lua_getfield(state, -1, "equipment");
    const bool equipmentOk = lua_isnil(state, -1) || lua_type(state, -1) == LUA_TBOOLEAN;
    const bool equipment = lua_type(state, -1) == LUA_TBOOLEAN && lua_toboolean(state, -1);
    lua_pop(state, 2);
    if (!skillOk || !instanceOk || !ownerOk || !equipmentOk)
        return false;
    if ((instanceId == 0) != equipment) return false;
    result.action.hasSkillActionContext = true;
    result.action.skillActionContext.equipmentActivation = equipment;
    result.action.skillActionContext.activationRef = SkillInstanceRef(owner,
        SkillInstanceKey(skillName, instanceId));
    return true;
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
    if (kind == QStringLiteral("answer")) {
        result.kind = AIResult::Answer;
        lua_getfield(state, -1, "random_card_choice");
        if (!lua_isnil(state, -1)) {
            if (!currentAiDecision || currentAiDecision->kind != AIRequest::CardChosen || !isPlainTable(state, -1)) {
                lua_pop(state, 1);
                return false;
            }
            lua_getfield(state, -1, "token");
            const bool tokenOk = lua_type(state, -1) == LUA_TSTRING && readBoundedString(state, -1, result.randomCardToken)
                && currentAiDecision->opaqueCardDomains.contains(result.randomCardToken);
            lua_pop(state, 1);
            lua_getfield(state, -1, "index");
            const bool indexOk = lua_type(state, -1) == LUA_TNUMBER
                && aiResultInteger(lua_tonumber(state, -1), result.randomCardIndex) && result.randomCardIndex > 0;
            lua_pop(state, 1);
            // A receipt selects a member; card-specific strategy never crosses
            // into the native authority. Reject extra receipt fields.
            bool fieldsOk = true;
            lua_pushnil(state);
            while (lua_next(state, -2) != 0) {
                QString key;
                if (lua_type(state, -2) != LUA_TSTRING || !readBoundedString(state, -2, key)
                    || (key != QLatin1String("token") && key != QLatin1String("index"))) fieldsOk = false;
                lua_pop(state, 1);
            }
            lua_pop(state, 1);
            if (!tokenOk || !indexOk || !fieldsOk) return false;
            for (const char *field : {"answer", "cards", "targets", "bottom_cards", "card_spec",
                                      "card_id", "card", "candidate_id", "skill_action"}) {
                lua_getfield(state, -1, field);
                const bool present = !lua_isnil(state, -1);
                lua_pop(state, 1);
                if (present) return false;
            }
            return true;
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
