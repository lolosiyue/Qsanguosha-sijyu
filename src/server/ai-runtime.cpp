#include "ai-runtime.h"

#include "ai-data-store.h"
#include "engine.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "room.h"
#include "room-runtime.h"
#include "settings.h"
#include "skill.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
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

AIResult AiLuaRuntime::decideIsolated(const AIRequest &request)
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
        shutdown();
        QString error;
        if (!initialize(&error))
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
        m_instructionsRemaining = m_instructionBudget;
        m_instructionLimitExceeded = false;
        lua_sethook(state, &AiLuaRuntime::luaInstructionHook, LUA_MASKCOUNT, 1000);
        const int status = LuaRuntime::protectedCall(state, 2, 1, 0);
        lua_sethook(state, nullptr, 0, 0);
        if (status != 0 || m_instructionLimitExceeded) {
            failure = m_instructionLimitExceeded ? QStringLiteral("AI_EVENT_INSTRUCTION_LIMIT")
                : status == LUA_ERRMEM ? QStringLiteral("AI_EVENT_MEMORY_LIMIT")
                                      : QStringLiteral("AI_EVENT_RUNTIME_ERROR");
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
    setIntegerField(state, "TargetSpecified", TargetSpecified);
    setIntegerField(state, "DamageInflicted", DamageInflicted);
    setIntegerField(state, "HpRecover", HpRecover);
    setIntegerField(state, "Death", Death);
    setIntegerField(state, "ChoiceMade", ChoiceMade);
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
    if (request.kind == AIRequest::Activate || request.kind == AIRequest::UseCard
        || request.kind == AIRequest::RespondCard) {
        lua_createtable(state, int(request.cardCandidates.size()), 0);
        for (int index = 0; index < request.cardCandidates.size(); ++index) {
            const AICardCandidateView &candidate = request.cardCandidates.at(index);
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
            lua_rawseti(state, -2, index + 1);
        }
        lua_setfield(state, -2, "card_candidates");
        // Authorized conversions. The produced card's name and classification come from
        // the authority's own build, so an author reads them rather than asserting them.
        lua_createtable(state, int(request.cardConversions.size()), 0);
        for (int index = 0; index < request.cardConversions.size(); ++index) {
            const AICardConversionView &conversion = request.cardConversions.at(index);
            lua_createtable(state, 0, 18);
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
            setStringField(state, "name", conversion.name);
            setStringField(state, "class_name", conversion.className);
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
