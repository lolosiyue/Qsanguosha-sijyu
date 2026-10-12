#include "ai-strategy/owner.h"

// ---- owner ----
#include "ai-strategy/native-registry.h"
#include "ai-strategy/runtime.h"
#include "ai-strategy/prototypes.h"
#include "ai-strategy/shadows.h"

#include "ai.h"
#include "engine.h"
#include "card.h"
#include "general.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "serverplayer.h"
#include "skill.h"
#include "maneuvering.h"
#include "hegemony-mode.h"
#include "settings.h"
#include "scenario.h"
#include "couple-scenario.h"
#include "guandu-scenario.h"
#include "zombie-scenario.h"
#include "fancheng-scenario.h"
#include "boss-mode-scenario.h"
#include "challengedeveloper-scenario.h"
#include "miniscenarios.h"
#include "work-scenario.h"
#include "takeover-scenario.h"
#include <typeinfo>
#include "lua.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QSet>
#include <QScopeGuard>
#include <cmath>
#include <limits>

namespace QSanEngine { extern thread_local Player *Self; }

namespace {
// Resolve metadata without invoking Lua/C __index callbacks. The only C
// accessor accepted is the SWIG namespace accessor pinned before AI loading.
// Its audited .get/.fn algorithm is mirrored; native property getters remain
// unknown because observing their value would execute code.
bool pushStrategyMetadataField(lua_State *state, int table, const char *key, int depth = 0)
{
    if (depth > 16 || !lua_istable(state, table)) return false;
    table = lua_absindex(state, table);
    lua_pushstring(state, key); lua_rawget(state, table);
    if (!lua_isnil(state, -1)) return true;
    lua_pop(state, 1);
    if (!lua_getmetatable(state, table)) { lua_pushnil(state); return true; }
    const int meta = lua_absindex(state, -1);
    lua_pushliteral(state, "__index"); lua_rawget(state, meta);
    if (lua_isnil(state, -1)) return true;
    if (lua_istable(state, -1)) return pushStrategyMetadataField(state, -1, key, depth + 1);
    if (!lua_iscfunction(state, -1)) return false;
    lua_pushliteral(state, "qsan.native_sgs_namespace_index"); lua_rawget(state, LUA_REGISTRYINDEX);
    const bool trusted = lua_iscfunction(state, -1) && lua_rawequal(state, -1, -2);
    lua_pop(state, 2);
    if (!trusted) return false;
    lua_pushliteral(state, ".get"); lua_rawget(state, meta);
    if (!lua_istable(state, -1)) return false;
    lua_pushstring(state, key); lua_rawget(state, -2);
    if (lua_iscfunction(state, -1)) return false;
    lua_pop(state, 2);
    lua_pushliteral(state, ".fn"); lua_rawget(state, meta);
    if (!lua_istable(state, -1)) return false;
    lua_pushstring(state, key); lua_rawget(state, -2);
    if (!lua_isfunction(state, -1)) { lua_pop(state, 1); lua_pushnil(state); }
    return true;
}
// Public registry shape only. This never executes a donor callback, metamethod,
// or userdata conversion. Non-function data is limited to numeric literals and
// dense numeric arrays; the other scalar types need only their type tag.
QJsonObject discardCallbackMetadata(lua_State *state)
{
    QJsonObject entries;
    QJsonObject result{{"complete", false}, {"entries", entries}};
    if (!state) return result;
    const int top = lua_gettop(state);
    const auto restore = qScopeGuard([&] { lua_settop(state, top); });
    lua_pushglobaltable(state);
    lua_pushliteral(state, "sgs"); lua_rawget(state, -2);
    if (!lua_istable(state, -1)
        || !pushStrategyMetadataField(state, -1, "ai_skill_discard")
        || !lua_istable(state, -1)) return result;
    const int registry = lua_absindex(state, -1);
    if (lua_getmetatable(state, registry)) return result;
    bool complete = true;
    lua_pushnil(state);
    while (lua_next(state, registry)) {
        if (lua_type(state, -2) != LUA_TSTRING) {
            complete = false; lua_pop(state, 1); continue;
        }
        size_t size = 0;
        const char *raw = lua_tolstring(state, -2, &size);
        const QString key = QString::fromUtf8(raw, int(size));
        const int value = lua_absindex(state, -1);
        const int type = lua_type(state, value);
        QJsonObject row{{"known", true}, {"type", QString::fromLatin1(lua_typename(state, type))}};
        if (type == LUA_TNUMBER) {
            const double number = lua_tonumber(state, value);
            if (std::isfinite(number)) row.insert("value", number);
            else row.insert("known", false);
        } else if (type == LUA_TTABLE) {
            bool valid = true;
            const size_t length = lua_rawlen(state, value);
            if (length > 4096) valid = false;
            if (lua_getmetatable(state, value)) { valid = false; lua_pop(state, 1); }
            QJsonArray array;
            if (valid) {
                size_t keys = 0;
                lua_pushnil(state);
                while (lua_next(state, value)) {
                    const lua_Number index = lua_type(state, -2) == LUA_TNUMBER ? lua_tonumber(state, -2) : 0;
                    if (index < 1 || index > double(length) || std::floor(index) != index
                        || lua_type(state, -1) != LUA_TNUMBER || !std::isfinite(lua_tonumber(state, -1))) valid = false;
                    ++keys; lua_pop(state, 1);
                }
                if (keys != length) valid = false;
                if (valid) for (size_t index = 1; index <= length; ++index) {
                    lua_rawgeti(state, value, lua_Integer(index)); array.append(double(lua_tonumber(state, -1))); lua_pop(state, 1);
                }
            }
            row.insert("known", valid);
            if (valid) row.insert("value", array);
        } else if (type != LUA_TFUNCTION && type != LUA_TBOOLEAN && type != LUA_TSTRING) {
            row.insert("known", false);
        }
        entries.insert(key, row);
        lua_pop(state, 1);
    }
    return QJsonObject{{"complete", complete}, {"entries", entries}};
}
// Public fixed registry TYPE metadata only; no callback or metamethod execution.
// Missing keys are known absent only for an ordinary, completely walked table.
// Insert into ai-strategy/owner.cpp anonymous namespace; raw metadata only.
QJsonObject originalUseMethodMetadata(lua_State *state)
{
    QJsonObject result{{"complete",false},{"entries",QJsonObject{}}};
    if(!state)return result;
    const int top=lua_gettop(state);const auto restore=qScopeGuard([&]{lua_settop(state,top);});
    lua_pushglobaltable(state);lua_pushliteral(state,"SmartAI");lua_rawget(state,-2);
    if(!lua_istable(state,-1))return result;
    lua_pushliteral(state,"__instanceDict");lua_rawget(state,-2);
    if(!lua_istable(state,-1))return result;
    QJsonObject entries;QSet<const void*>seen;int count=0;
    for(int depth=0;depth<16;++depth){
        const int table=lua_absindex(state,-1);const void*identity=lua_topointer(state,table);
        if(seen.contains(identity))return result;seen.insert(identity);
        lua_pushnil(state);
        while(lua_next(state,table)){
            if(++count>4096||lua_type(state,-2)!=LUA_TSTRING)return result;
            size_t size=0;const char*key=lua_tolstring(state,-2,&size);
            if(size>4096)return result;
            const QString name=QString::fromUtf8(key,int(size));
            if(name.startsWith("useCard")&&!entries.contains(name)){
                const int type=lua_type(state,-1);QJsonObject entry{{"type",QString::fromLatin1(lua_typename(state,type))}};
                if(type==LUA_TBOOLEAN)entry.insert("value",bool(lua_toboolean(state,-1)));
                if(type==LUA_TFUNCTION){
                    lua_Debug ar{};lua_pushvalue(state,-1);
                    if(!lua_getinfo(state,">S",&ar))return result;
                    const QString source=QString::fromUtf8(ar.source?ar.source:"");
                    entry.insert("original",source.startsWith("@lua/ai/original-hegemony/"));
                    // Only a fixed public AI file path, never dynamically loaded source text.
                    if(source.startsWith("@lua/ai/")&&source.size()<=4096)entry.insert("source",source);
                    entry.insert("line",ar.linedefined);
                }
                entries.insert(name,entry);
            }
            lua_pop(state,1);
        }
        if(!lua_getmetatable(state,table)){result.insert("complete",true);result.insert("entries",entries);return result;}
        lua_pushliteral(state,"__index");lua_rawget(state,-2);
        if(lua_isnil(state,-1)){result.insert("complete",true);result.insert("entries",entries);return result;}
        if(!lua_istable(state,-1))return result;
    }
    return result;
}
QJsonObject originalHegemonyFacts(Room &room,const ServerPlayer *viewer)
{
    QJsonObject result;
    if(!viewer||!HegemonyMode::enabled())return result;
    QJsonObject piles,jade;
    for(const ServerPlayer *p:room.getAllPlayers(true)){
        QJsonObject open,contents;
        const QStringList names=p->getGeneralPileNames();
        // Marshal1841 broadcasts every pile key/count, but its content is masked
        // for nonowners unless generalPileOpen grants this exact observer.
        for(const QString&name:names){
            const bool visible=p==viewer||p->generalPileOpen(name,viewer->objectName());
            open.insert(name,visible);
            if(visible)contents.insert(name,QJsonArray::fromStringList(p->getGeneralPile(name)));
        }
        piles.insert(p->objectName(),QJsonObject{{"complete",true},{"names",QJsonArray::fromStringList(names)},
            {"open",open},{"piles",contents}});
    }
    // hasTreasure uses private virtual-equip and MethodEffect state. Owner-only
    // exact result; no raw foreign negative from printed treasure absence.
    jade.insert(viewer->objectName(),viewer->hasTreasure("JadeSeal"));
    result.insert("general_piles",piles);result.insert("jade_seal",jade);
    result.insert("kingdoms",QJsonArray::fromStringList(Sanguosha->getKingdoms()));
    result.insert("use_methods",originalUseMethodMetadata(room.roomRuntime()->lua().state()));
    return result;
}

QJsonObject originalAskCallbackTypes(lua_State *state)
{
    QJsonObject result;
    if (!state) return result;
    const int top = lua_gettop(state);
    const auto restore = qScopeGuard([&] { lua_settop(state, top); });
    for (const char *name : {"ai_skill_suit", "ai_skill_playerchosen", "ai_skill_askforyiji",
                             "ai_skill_cardask", "ai_nullification", "ai_skill_use"}) {
        lua_settop(state, top);
        QJsonObject entries;
        QJsonObject row{{"complete", false}, {"entries", entries}};
        lua_pushglobaltable(state);
        lua_pushliteral(state, "sgs"); lua_rawget(state, -2);
        if (!lua_istable(state, -1) || !pushStrategyMetadataField(state, -1, name)
            || !lua_istable(state, -1) || lua_getmetatable(state, -1)) {
            result.insert(QString::fromLatin1(name), row); continue;
        }
        const int registry = lua_absindex(state, -1);
        bool complete = true;
        int count = 0;
        lua_pushnil(state);
        while (lua_next(state, registry)) {
            if (++count > 4096 || lua_type(state, -2) != LUA_TSTRING) {
                complete = false; lua_pop(state, 1); break;
            }
            size_t size = 0;
            const char *key = lua_tolstring(state, -2, &size);
            if (size > 4096) { complete = false; lua_pop(state, 1); break; }
            entries.insert(QString::fromUtf8(key, int(size)), QString::fromLatin1(lua_typename(state, lua_type(state, -1))));
            lua_pop(state, 1);
        }
        row.insert("complete", complete);
        row.insert("entries", entries);
        result.insert(QString::fromLatin1(name), row);
    }
    return result;
}

QString strategyCardSortKey(const Card *card)
{
    QString key = card->toString();
    if (card->isKindOf("ActiveSkillCard")) {
        key += QLatin1Char('|') + card->getSkillName();
        if (card->getSkillInstanceId() > 0)
            key += QLatin1Char('#') + QString::number(card->getSkillInstanceId());
    }
    return key;
}
QJsonArray originalGeneralSkills(const QString &generalName)
{
    QJsonArray result;
    const General *general = Sanguosha->getGeneral(generalName);
    if (!general) return result;
    // Preserve the donor's General::getSkillList order, including hidden related
    // skill names. This helper is called exclusively for the owning viewer.
    for (const Skill *skill : general->getSkillList())
        if (skill) result.append(skill->objectName());
    return result;
}

QJsonObject publicSkillMetadata(const Skill *skill)
{
    QJsonObject row;
    row.insert(QStringLiteral("name"), skill->objectName());
    // Null target requests the registered public text, never a player's private
    // changeTranslation/property or per-instance description override.
    row.insert(QStringLiteral("description"), skill->getDescription());
    row.insert(QStringLiteral("frequency"), int(skill->getFrequency()));
    row.insert(QStringLiteral("limit_mark"), skill->getLimitMark());
    row.insert(QStringLiteral("lord_skill"), skill->isLordSkill());
    row.insert(QStringLiteral("attached_lord_skill"), skill->isAttachedLordSkill());
    row.insert(QStringLiteral("has_view_as_skill"), ViewAsSkill::parseViewAsSkill(skill) != nullptr);
    QJsonArray classes;
    for (const QMetaObject *meta = skill->metaObject(); meta; meta = meta->superClass())
        classes.append(QString::fromLatin1(meta->className()));
    row.insert(QStringLiteral("skill_classes"), classes);
    QJsonArray events;
    if (const auto *trigger = dynamic_cast<const TriggerSkill *>(skill))
        for (TriggerEvent event : trigger->getTriggerEvents()) events.append(int(event));
    row.insert(QStringLiteral("trigger_events"), events);
    return row;
}
QJsonObject ownerEvaluationCard(const Card *card, Room &room, ServerPlayer *viewer, int id)
{
    QJsonObject limits, prohibited;
    for (int method = int(Card::MethodNone); method <= int(Card::MethodGet); ++method)
        limits.insert(QString::number(method), viewer->isCardLimited(card, Card::HandlingMethod(method)));
    for (const auto *target : room.getAlivePlayers())
        prohibited.insert(target->objectName(), room.isProhibited(viewer, target, card) != nullptr);
    QJsonObject rules{{"limits", limits}, {"prohibited", prohibited}, {"available", card->isAvailable(viewer)},
        {"target_mod_residue", Sanguosha->correctCardTarget(TargetModSkill::Residue, viewer, card)}};
    if (card->isKindOf("Analeptic")) rules.insert("analeptic_static_available", Analeptic::IsAvailable(viewer, card));
    QJsonArray ownerFlags;
    for (const QString &flag : card->getFlags()) if (!flag.startsWith("visible_")) ownerFlags.append(flag);
    QJsonObject row{{"id", id}, {"effective_id", id},
        {"legacy_string_key", card->toString()}, {"legacy_sort_key", strategyCardSortKey(card)},
        {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
        {"engine_class_name", QString::fromLatin1(card->metaObject()->className())},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"suit", int(card->getSuit())}, {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
        {"skill_name", card->getSkillName(false)}, {"source_skill", card->getSourceSkillName()},
        {"skill_names", QJsonArray::fromStringList(card->getSkillNames())},
        {"red", card->isRed()}, {"black", card->isBlack()}, {"subcards", QJsonArray()},
        {"flags", ownerFlags}, {"flags_complete_scope", "owner_non_visibility"},
        {"virtual_card", card->isVirtualCard()}, {"target_fixed", card->targetFixed()},
        {"damage_card", card->isDamageCard()}, {"can_recast", card->canRecast()},
        {"handling_method", int(card->getHandlingMethod())}, {"evaluation_only", true},
        {"evaluation_owner", viewer->objectName()}, {"owner_state_known", true},
        {"using", false}, {"kill_off", false}, {"locked", viewer->isLocked(card)},
        {"jilei", viewer->isJilei(card)}, {"evaluation_rules", rules},
        {"target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, viewer, card)},
        {"target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, viewer, card)}};
    const QString identity = aiLegacyCardIdentity(room, viewer, card);
    if (!identity.isEmpty()) row.insert("legacy_identity_token", identity);
    return row;
}
} // namespace

QJsonObject consumeAIStrategyDeckCountShuffle(Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room)
        return QJsonObject{{"error", "invalid deck-count viewer"}};
    // PatternsCard(..., true) calls getRandomCards(false) even though its caller
    // only takes the length. Reproduce qsanShuffle's complete bound sequence,
    // including bounded(1), without allocating/permuting any real draw pile.
    // The request dispatcher must validate the active viewer/revision/budget;
    // projection itself must never call this branch-time RNG operation.
    const int count = Sanguosha->getDeckCardIds(false).size();
    for (int bound = count; bound > 0; --bound)
        room.roomRuntime()->rng().bounded(bound);
    return QJsonObject{{"kind", "deck_count_shuffle"}, {"count", count}};
}

QJsonObject sampleAIStrategyPatternLogName(Room &room, ServerPlayer *viewer, const QString &pattern, bool derivative)
{
    if (!viewer || viewer->getRoom() != &room)
        return QJsonObject{{"error", "invalid pattern-log viewer"}};
    // Huomo iterates the public patterns() Basic/Trick-name catalogue. Admit
    // that same finite metadata domain; never accept an arbitrary expression.
    // derivative=true is admitted only by the exact Weipo offered-choice caller.
    bool accepted = derivative || pattern == QStringLiteral("jink") || pattern == QStringLiteral("peach");
    if (!accepted && !pattern.startsWith(QLatin1Char('_'))) {
        const QStringList banned = Sanguosha->getBanPackages();
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *definition = Sanguosha->getEngineCard(id);
            if (definition && definition->objectName() == pattern
                && definition->getTypeId() < Card::TypeEquip
                && !banned.contains(definition->getPackage())) { accepted = true; break; }
        }
    }
    if (!accepted) return QJsonObject{{"error", "unsupported pattern-log pattern"}};
    // CardFinished.analeptic calls PatternsCard separately for jink and peach.
    // Preserve each complete shuffle and its first matching printed card; the
    // suit/number in getLogName is observable speech, not a class template.
    // This public configured deck is unrelated to the room's live draw pile.
    GameRng::Binding rng(room.roomRuntime()->rng());
    const QList<int> cards = Sanguosha->getRandomCards(derivative);
    for (int id : cards) {
        const Card *card = Sanguosha->getEngineCard(id);
        // PatternsCard uses objectName OR isKindOf, not ExpPattern: the
        // latter needs "%jink" for an object name; bare "jink" is not Jink.
        if (card && (card->objectName() == pattern || card->isKindOf(pattern.toLatin1().constData()))) {
            // A public printed definition, not the mutable live wrapper. It
            // carries no current location, owner, flags, tags or action right.
            QJsonObject printed{{"id", id}, {"effective_id", id}, {"evaluation_only", true},
                {"name", card->objectName()}, {"class_name", card->getClassName()},
                {"engine_class_name", QString::fromLatin1(card->metaObject()->className())},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"subtype", card->getSubtype()}, {"suit", int(card->getSuit())},
                {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
                {"red", card->isRed()}, {"black", card->isBlack()}, {"virtual_card", false},
                {"subcards", QJsonArray()}, {"legacy_string_key", QString::number(id)},
                {"legacy_sort_key", QString::number(id)}};
            if (derivative) {
                printed.insert("evaluation_owner", viewer->objectName());
                printed.insert("evaluation_rules", QJsonObject{{"available", card->isAvailable(viewer)}});
            }
            return QJsonObject{{"kind", "pattern_log_name"}, {"pattern", pattern}, {"derivative", derivative},
                {"found", true}, {"text", card->getLogName()}, {"printed_card", printed}};
        }
    }
    // Complete scan with no match is donor's known nil, never an invented name.
    return QJsonObject{{"kind", "pattern_log_name"}, {"pattern", pattern}, {"derivative", derivative}, {"found", false}};
}

void projectAIStrategyOwner(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    // Four original presence queries only. Each full-source lifetime ends
    // before its bool is consumed by the owner projection's conditional body.
    const auto hasOwnerSkill = [&](const QString &name) {
        std::unique_ptr<AIWorldSkillQueriesScope> skillSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            skillSource.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill(name);
    };
    const auto hasShownPublicSkill = [&](const ServerPlayer *subject, const QString &name) {
        std::unique_ptr<AIWorldSkillQueriesScope> skillSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            skillSource.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return subject->hasShownSkill(name);
    };
    world.strategyFacts.insert(QStringLiteral("clock_seconds"), double(QDateTime::currentSecsSinceEpoch()));
    world.strategyFacts.insert(QStringLiteral("has_current_active"), room.hasCurrent(true));
    // AI::relationTo (ai.cpp133): expose only the selected public rule family,
    // never evaluate a relation or inspect a player's role for this metadata.
    QString relationKind;
    if (room.getScenario()) {
        relationKind = QStringLiteral("scenario");
    } else {
        const QString relationMode = room.getMode();
        if (relationMode == QStringLiteral("06_3v3") || relationMode == QStringLiteral("06_XMode")
            || relationMode == QStringLiteral("08_defense") || relationMode == QStringLiteral("04_2v2"))
            relationKind = QStringLiteral("role_prefix");
        else if (Config.EnableHegemony)
            relationKind = QStringLiteral("public_hegemony");
        else
            relationKind = QStringLiteral("identity_balance");
    }
    world.strategyFacts.insert(QStringLiteral("legacy_relation_kind"), relationKind);
    if (const Scenario *scenario = room.getScenario()) {
        // Fixed scenario implementations: only these three override relationTo.
        // Exact C++ type avoids silently granting a future subclass the parent's
        // relation contract. This metadata evaluates no private relationship.
        const std::type_info &type = typeid(*scenario);
        QJsonObject scenarioRelation;
        if (type == typeid(GuanduScenario))
            scenarioRelation.insert(QStringLiteral("kind"), QStringLiteral("guandu"));
        else if (type == typeid(ZombieScenario))
            scenarioRelation.insert(QStringLiteral("kind"), QStringLiteral("zombie"));
        else if (type == typeid(CoupleScenario)) {
            scenarioRelation.insert(QStringLiteral("kind"), QStringLiteral("couple"));
            // Only a public #Marry/#Divorse observation that still agrees with
            // native state certifies this value; arbitrary private tags do not.
            const QJsonObject witness = readAIStrategyPublicSpouse(room, viewer);
            if (witness.value(QStringLiteral("known")).toBool())
                scenarioRelation.insert(QStringLiteral("spouse"), witness.value(QStringLiteral("spouse")));
        }
        else if (type == typeid(Scenario) || type == typeid(LuaScenario)
            || type == typeid(MiniScene) || type == typeid(CustomScenario)
            || type == typeid(LoadedScenario) || type == typeid(TestScenario)
            || type == typeid(QSanWorks::WorkScenario) || type == typeid(FanchengScenario)
            || type == typeid(ImpasseScenario) || type == typeid(ChallengeDeveloperScenario)
            || type == typeid(TakeoverScenario))
            scenarioRelation.insert(QStringLiteral("kind"), QStringLiteral("identity_balance"));
        world.strategyFacts.insert(QStringLiteral("legacy_scenario_relation"), scenarioRelation);
    }
    // Boss public suppression state: own tags are lawful exact observations;
    // foreign tags can precede public notifications, so use only public marks
    // and explicitly label that reconstruction as an estimate.
    QJsonObject boss = world.strategyFacts.value(QStringLiteral("boss")).toObject();
    QJsonObject bossPlayers;
    for (const ServerPlayer *subject : room.getAllPlayers(true)) {
        QJsonObject state;
        if (subject == viewer) {
            state.insert(QStringLiteral("god_speel_count"), subject->getTag(QStringLiteral("god_speelSkills")).toStringList().length());
            state.insert(QStringLiteral("wanghun_active"), !subject->getTag(QStringLiteral("yl_wanghunSkill")).toString().isEmpty());
            state.insert(QStringLiteral("status"), QStringLiteral("exact"));
        } else {
            int speelCount = 0;
            bool wanghun = false;
            for (const QString &mark : subject->getMarkNames()) {
                if (mark.startsWith(QStringLiteral("&god_speel+:+")) && subject->getMark(mark) > 0) ++speelCount;
                if (mark.startsWith(QStringLiteral("&yl_wanghun+:+")) && subject->getMark(mark) > 0) wanghun = true;
            }
            state.insert(QStringLiteral("god_speel_count"), speelCount);
            state.insert(QStringLiteral("wanghun_active"), wanghun);
            state.insert(QStringLiteral("status"), QStringLiteral("estimate"));
            state.insert(QStringLiteral("basis"), QStringLiteral("boss.foreign_suppression_public_marks_prior"));
        }
        bossPlayers.insert(subject->objectName(), state);
    }
    boss.insert(QStringLiteral("players"), bossPlayers);
    world.strategyFacts.insert(QStringLiteral("boss"), boss);
    // Controller_Name is the existing public-board control relation: its only
    // native setter publishes &controllby+#<name>+sys_ (room.cpp2686-2720).
    // Mirror getActualController's return value without its cycle/missing-edge
    // repair writes. Never call getAI() while producing an observation.
    const auto controlRoster = room.getAllPlayers(true);
    QHash<QString, ServerPlayer *> controlNames;
    for (ServerPlayer *subject : controlRoster) controlNames.insert(subject->objectName(), subject);
    const auto readOnlyController = [&](ServerPlayer *subject) {
        QSet<ServerPlayer *> visited;
        ServerPlayer *current = subject;
        while (current) {
            if (visited.contains(current)) return current;
            visited.insert(current);
            const QString nextName = current->getTag("Controller_Name").toString();
            if (nextName.isEmpty() || !controlNames.contains(nextName)) return current;
            current = controlNames.value(nextName);
        }
        return subject;
    };
    QJsonObject controlAvailability;
    for (ServerPlayer *subject : controlRoster) {
        ServerPlayer *controller = readOnlyController(subject);
        QJsonObject row{{"known", false}, {"basis", "foreign_nononline_private_onsole_state_unobserved"}};
        if (controller && controller != subject && controller->isOnline()) {
            row = {{"known", true}, {"has_ai", false}, {"basis", "public_online_actual_controller"}};
        } else if (subject->isOnline()) {
            row = {{"known", true}, {"has_ai", false}, {"basis", "public_online_player"}};
        } else if (subject == viewer || controller == viewer) {
            // Onsole relation is not broadcast as a general-purpose property.
            // Read it only for the viewer or the player they actually control.
            const ServerPlayer *onsole = subject->getOnsoleOwner();
            // ServerPlayer constructs a non-null BasicAI fallback (line142),
            // kept until destruction. No AI object or pointer leaves native.
            const bool hasAI = onsole && !onsole->isOnline();
            row = {{"known", true}, {"has_ai", hasAI},
                {"basis", subject == viewer ? "owner_control_state" : "authorized_controller_state"}};
        }
        controlAvailability.insert(subject->objectName(), row);
    }
    world.strategyFacts.insert("ai_control_availability", controlAvailability);
    // Fixed YJCM2022 owner-only selectors: count a tag already shown to this
    // request actor and inspect only their current hand's legacy acquisition marks.
    QJsonObject yjcm2022 = world.strategyFacts.value("yjcm2022").toObject();
    yjcm2022.insert("owner_koujing_shown_count", viewer->getTag("KoujingShowCards").toList().size());
    bool biejunReceived = false;
    for (int id : viewer->handCards())
        if (viewer->getMark("biejunGetCard_" + QString::number(id) + "-Clear") > 0) { biejunReceived = true; break; }
    yjcm2022.insert("owner_biejun_received_hand", biejunReceived);
    world.strategyFacts.insert("yjcm2022", yjcm2022);
    QJsonObject animic = world.strategyFacts.value("animic").toObject();
    animic.insert("owner_modao", viewer->getTag("modao").toInt());
    world.strategyFacts.insert("animic", animic);
    // ChangeSkill_<name>_State is written by Room::setChangeSkillState through
    // the public setPlayerProperty broadcast (player-state-service.cpp203).
    // Only names already disclosed in this viewer's skill roster may be queried
    // for another actor; never enumerate a concealed general's actual skills.
    QJsonObject publicCapabilities;
    for (const ServerPlayer *subject : room.getAllPlayers(true)) {
        QSet<QString> names;
        if (subject == viewer) {
            for (const Skill *skill : viewer->getSkillList(true))
                if (skill) names.insert(skill->objectName());
        } else {
            for (const auto &visible : world.players)
                if (visible.objectName == subject->objectName())
                    for (const auto &skill : visible.skills) names.insert(skill.skillName);
        }
        QJsonObject states;
        for (const QString &name : names) states.insert(name, subject->getChangeSkillState(name));
        // throwJudgeArea/obtainJudgeArea broadcast this board slot and log it.
        publicCapabilities.insert(subject->objectName(), QJsonObject{
            {"judge_area", subject->hasJudgeArea()}, {"change_skill_states", states}});
    }
    world.strategyFacts.insert("player_public_capabilities", publicCapabilities);



    // This is the public configured deck definition, never the room draw pile.
    // getDeckCardIds shares getRandomCards' filters without consuming RNG.
    const QList<int> definitionIds = Sanguosha->getDeckCardIds(false);
    static thread_local QList<int> cachedDefinitionIds;
    static thread_local QJsonObject cachedDefinitionCounts;
    static thread_local bool definitionCached = false;
    if (!definitionCached || cachedDefinitionIds != definitionIds) {
        QJsonObject counts;
        for (int id : definitionIds) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card) continue;
            QStringList names = card->getKindOfNames();
            names.append(QStringLiteral("QObject"));
            names.append(card->objectName());
            names.removeDuplicates();
            for (const QString &name : names) counts.insert(name, counts.value(name).toInt() + 1);
        }
        cachedDefinitionIds = definitionIds;
        cachedDefinitionCounts = counts;
        definitionCached = true;
    }
    world.strategyFacts.insert(QStringLiteral("deck_definition_counts"), cachedDefinitionCounts);
    world.strategyFacts.insert(QStringLiteral("deck_definition_counts_complete"), true);
    world.strategyFacts.insert(QStringLiteral("deck_definition_size"), definitionIds.size());
    // addFunction.lua patterns(): first accepted Basic/Trick definition name,
    // engine registration order, no deck shuffle or current draw-pile access.
    QJsonArray patternNames;
    QJsonObject patternClassNames;
    QSet<QString> acceptedPatternNames;
    const QStringList bannedPackages = Sanguosha->getBanPackages();
    world.strategyFacts.insert(QStringLiteral("scarletayuhuo"), QJsonObject{{"ban_packages", QJsonArray::fromStringList(bannedPackages)}});
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (!card || acceptedPatternNames.contains(card->objectName())) continue;
        // Source assigns this before banned/underscore filtering. Equipment
        // names are never added to cardNames, so repeated equipment rows still
        // participate in the source class-to-name overwrite order.
        patternClassNames.insert(card->getClassName(), card->objectName());
        if (card->objectName().startsWith(QLatin1Char('_')) || bannedPackages.contains(card->getPackage())) continue;
        if (card->getTypeId() < Card::TypeEquip) {
            acceptedPatternNames.insert(card->objectName());
            patternNames.append(card->objectName());
        }
    }
    world.strategyFacts.insert(QStringLiteral("pattern_names"), patternNames);
    world.strategyFacts.insert(QStringLiteral("pattern_class_names"), patternClassNames);

    // extra.lua:generateAllCardObjectNameTablePatterns: engine IDs 0..10000,
    // stop at the first missing ID, Basic/Trick names unique in first-seen order.
    QStringList newtypePatterns;
    for (int id = 0; id <= 10000; ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (!card) break;
        if ((card->isKindOf("BasicCard") || card->isKindOf("TrickCard"))
            && !newtypePatterns.contains(card->objectName())) newtypePatterns.append(card->objectName());
    }
    // The server's authoritative mode gate is Config via HegemonyMode::enabled,
    // not the client's ServerInfo flag. Outside this gate isFriendWith would
    // consult hidden identity roles and must never be used as a public oracle.
    if (HegemonyMode::enabled()) {
        QJsonObject shown, friends;
        const auto roster = room.getAllPlayers(true);
        for (const auto *player : roster) {
            shown.insert(player->objectName(), player->hasShownOneGeneral());
            QJsonObject row;
            for (const auto *other : roster)
                row.insert(other->objectName(), player->isFriendWith(other, false));
            friends.insert(player->objectName(), row);
        }
        world.strategyFacts.insert("public_revealed_relationships", QJsonObject{{"shown_one", shown}, {"friends", friends}});
    }
    QJsonObject hegemony{{"enabled", HegemonyMode::enabled()},
        {"first_show_reward", Config.value("RewardTheFirstShowingPlayer", true).toBool()}};
    // Only the successfully admitted original bundle overrides ordinary package AI.
    // These are immutable policy metadata, never callback execution or private AI state.
    hegemony.insert("original_overrides_loaded", QJsonValue::Null);
    hegemony.insert("owner_can_recover", viewer->canRecover());
    if (HegemonyMode::enabled()) {
        const General *head = viewer->getActualGeneral1();
        if (!head) head = Sanguosha->getGeneral("anjiang");
        hegemony.insert("owner_head_kingdom", head ? QJsonValue(head->getKingdom()) : QJsonValue::Null);
    }
    if (HegemonyMode::enabled()) {
        QJsonObject souls;
        QJsonObject definitions;
        for (const ServerPlayer *subject : room.getAllPlayers(true)) {
            const bool open = subject == viewer
                || subject->generalPileOpen(QStringLiteral("soul"), viewer->objectName());
            QJsonArray names;
            if (open) {
                for (const QString &name : subject->getGeneralPile(QStringLiteral("soul"))) {
                    names.append(name);
                    const General *general = Sanguosha->getGeneral(name);
                    QJsonObject row{{"exists", general != nullptr}};
                    if (general) {
                        row.insert("double_kingdoms", general->isDoubleKingdoms());
                        row.insert("kingdom", general->getKingdom());
                        // General::getKingdoms is QString; preserving it matters to
                        // the source's table.contains(string, kingdom) false branch.
                        row.insert("kingdoms", general->getKingdoms());
                    }
                    definitions.insert(name, row);
                }
            }
            souls.insert(subject->objectName(), QJsonObject{{"known", true},
                {"visible", open}, {"names", names}});
        }
        QJsonObject prospectiveFriends;
        for (const ServerPlayer *subject : room.getAllPlayers(true)) {
            // Unrevealed other players are an exact false in the fixed native
            // predicate; never consult their concealed general or role.
            prospectiveFriends.insert(subject->objectName(), subject == viewer
                || (subject->hasShownOneGeneral() && viewer->willBeFriendWith(subject)));
        }
        hegemony.insert("owner_prospective_friends", prospectiveFriends);
        hegemony.insert("soul_piles", souls);
        hegemony.insert("soul_general_definitions", definitions);
        const Player *lord = viewer->getLord();
        // Own concealed head plus publicly shown other identities only. The fixed
        // HegemonyMode::willBeFriendWith67 explicitly excludes concealed other lords.
        hegemony.insert("owner_will_be_friend_with_lord", lord
            ? QJsonValue(viewer->willBeFriendWith(lord)) : QJsonValue(false));
    }
    if (lua_State *state = room.roomRuntime()->lua().state()) {
        const int top = lua_gettop(state);
        const auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state); lua_pushliteral(state, "sgs"); lua_rawget(state, -2);
        const int sgsTable = lua_absindex(state, -1);
        // sp-ai.lua351: only the real red weapon selected as an owner Xueji
        // cost uses this formula. Publish its boolean result, not raw corrected
        // distance: positive offsets can otherwise reveal pre-clamp modifiers.
        QJsonObject xueji = world.strategyFacts.value("sp_xueji").toObject();
        QJsonObject reach{{"known", false}};
        const auto *weapon = viewer->getWeapon();
        const auto *xuejiSkill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("xueji"));
        if (room.getCurrent() == viewer && viewer->getPhase() == Player::Play
            && hasOwnerSkill("xueji") && weapon && weapon->getEffectiveId() >= 0
            && room.getCardOwner(weapon->getEffectiveId()) == viewer
            && room.getCardPlace(weapon->getEffectiveId()) == Player::PlaceEquip
            && weapon->isRed() && !viewer->isJilei(weapon) && xuejiSkill) {
            RoomState *runtimeState = room.roomState();
            Player *previousSelf = QSanEngine::Self;
            Player *previousPlayer = runtimeState->getCurrentPlayer();
            const auto previousReason = runtimeState->getCurrentCardUseReason();
            const QString previousPattern = runtimeState->getCurrentCardUsePattern();
            const auto restoreRequest = qScopeGuard([&] {
                runtimeState->setCurrentCardUsePattern(previousPattern);
                runtimeState->setCurrentCardUseReason(previousReason);
                runtimeState->setCurrentPlayer(previousPlayer); setEngineSelf(previousSelf);
            });
            setEngineSelf(viewer); runtimeState->setCurrentPlayer(viewer);
            runtimeState->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
            runtimeState->setCurrentCardUsePattern(QString());
            bool selectable = false;
            for (int instance : ([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->getValidSkillInstanceIds("xueji"); }())) {
                ActiveSkillRequest request;
                request.initiator = viewer; request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
                request.activationRef = SkillInstanceRef(viewer->objectName(), SkillInstanceKey("xueji", instance));
                request.sourceRef = room.resolveSkillInstanceRootRef(request.activationRef);
                if (!request.sourceRef.isValid() || !xuejiSkill->canActivateRequest(request)
                    || !xuejiSkill->canSelectCard(request, weapon)) continue;
                request.selectedCardIds << weapon->getEffectiveId();
                if (xuejiSkill->cardSelectionFeasible(request)) { selectable = true; break; }
            }
            lua_settop(state, sgsTable);
            if (selectable && pushStrategyMetadataField(state, sgsTable, "weapon_range") && lua_istable(state, -1)) {
                const QByteArray key = weapon->getClassName().toUtf8();
                if (pushStrategyMetadataField(state, -1, key.constData()) && lua_type(state, -1) == LUA_TNUMBER) {
                    const double printedRange = lua_tonumber(state, -1);
                    const double fix = printedRange - viewer->getAttackRange(false);
                    if (std::isfinite(fix) && fix == std::floor(fix)
                        && fix >= std::numeric_limits<int>::min() && fix <= std::numeric_limits<int>::max()) {
                        const int currentRange = viewer->getAttackRange();
                        QJsonObject targets;
                        for (const ServerPlayer *target : room.getAlivePlayers())
                            if (target != viewer) targets.insert(target->objectName(),
                                viewer->distanceTo(target, int(fix)) > currentRange);
                        reach = {{"known", true}, {"cost_id", weapon->getEffectiveId()},
                            {"exceeds_range", targets}};
                    }
                }
            }
        }
        xueji.insert("weapon_cost_distance", reach);
        world.strategyFacts.insert("sp_xueji", xueji);
        lua_settop(state, sgsTable);

        if (pushStrategyMetadataField(state, sgsTable, "original_hegemony_ai_loaded")) {
            if (lua_isnil(state, -1) || lua_isboolean(state, -1))
                hegemony.insert("original_overrides_loaded", lua_toboolean(state, -1) != 0);
        }
        for (const char *key : {"lose_equip_skill", "masochism_skill", "Active_cardneed_skill", "notActive_cardneed_skill", "use_lion_skill", "priority_skill", "throw_crossbow_skill", "exclusive_skill", "wizard_skill", "wizard_harm_skill", "force_slash_skill", "need_equip_skill", "drawpeach_skill"}) {
            lua_settop(state, sgsTable);
            if (pushStrategyMetadataField(state, sgsTable, key) && lua_type(state, -1) == LUA_TSTRING)
                hegemony.insert(QString::fromLatin1(key), QString::fromUtf8(lua_tostring(state, -1)));
        }
        lua_settop(state, sgsTable);
        if (pushStrategyMetadataField(state, sgsTable, "ai_damage_effect") && lua_istable(state, -1)) {
            const int callbacks = lua_absindex(state, -1);
            const bool hasMeta = lua_getmetatable(state, callbacks) != 0;
            if (hasMeta) lua_pop(state, 1);
            if (!hasMeta) {
                bool empty = true;
                lua_pushnil(state);
                while (lua_next(state, callbacks) != 0) {
                    if (lua_isfunction(state, -1)) empty = false;
                    lua_pop(state, 1);
                }
                hegemony.insert("damage_effect_empty", empty);
            }
        }
        for (const auto &registry : {QPair<const char *, const char *>("ai_cardsview", "cardsview_presence"),
                 QPair<const char *, const char *>("ai_view_as", "view_as_presence")}) {
            lua_settop(state, sgsTable);
            if (pushStrategyMetadataField(state, sgsTable, registry.first) && lua_istable(state, -1)) {
                const int callbacks = lua_absindex(state, -1);
                QSet<QString> names;
                for (const auto &skill : world.self.skills) names.insert(skill.skillName);
                for (const auto &player : world.players)
                    for (const auto &skill : player.skills) names.insert(skill.skillName);
                QJsonObject presence;
                for (const QString &name : names) {
                    lua_settop(state, callbacks);
                    const QByteArray key = name.toUtf8();
                    if (pushStrategyMetadataField(state, callbacks, key.constData())) presence.insert(name, lua_isfunction(state, -1));
                }
                hegemony.insert(QString::fromLatin1(registry.second), presence);
            }
        }
        lua_settop(state, sgsTable);
        if (pushStrategyMetadataField(state, sgsTable, "ai_trick_prohibit") && lua_istable(state, -1)) {
            const int callbacks = lua_absindex(state, -1);
            if (!lua_getmetatable(state, callbacks)) {
                QJsonArray names;
                bool complete = true;
                lua_pushnil(state);
                while (lua_next(state, callbacks) != 0) {
                    if (lua_isfunction(state, -1)) {
                        if (lua_type(state, -2) == LUA_TSTRING) names.append(QString::fromUtf8(lua_tostring(state, -2)));
                        else complete = false;
                    }
                    lua_pop(state, 1);
                }
                if (complete) hegemony.insert("trick_prohibit_functions", names);
            }
        }
        lua_settop(state, sgsTable);
        if (pushStrategyMetadataField(state, sgsTable, "ai_armor_value") && lua_istable(state, -1)) {
            const int callbacks = lua_absindex(state, -1);
            QSet<QString> names;
            for (const auto &skill : world.self.skills) names.insert(skill.skillName);
            for (const auto &player : world.players)
                for (const auto &skill : player.skills) names.insert(skill.skillName);
            for (int id = 0; id < Sanguosha->getCardCount(); ++id)
                if (const Card *card = Sanguosha->getEngineCard(id)) names.insert(card->objectName());
            QJsonObject presence;
            for (const QString &name : names) {
                lua_settop(state, callbacks);
                const QByteArray key = name.toUtf8();
                if (pushStrategyMetadataField(state, callbacks, key.constData()))
                    presence.insert(name, lua_isfunction(state, -1));
            }
            hegemony.insert("armor_value_presence", presence);
        }
        QJsonObject weapons;
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card || !card->isKindOf("Weapon") || weapons.contains(card->objectName())) continue;
            QJsonObject weapon;
            for (const auto &registry : {QPair<const char *, const char *>("ai_weapon_value", "value_callback"),
                     QPair<const char *, const char *>("ai_slash_weaponfilter", "filter_callback")}) {
                lua_settop(state, sgsTable);
                if (pushStrategyMetadataField(state, sgsTable, registry.first) && lua_istable(state, -1)) {
                    const QByteArray key = card->objectName().toUtf8();
                    if (pushStrategyMetadataField(state, -1, key.constData())) weapon.insert(QString::fromLatin1(registry.second), lua_isfunction(state, -1));
                }
            }
            lua_settop(state, sgsTable);
            if (pushStrategyMetadataField(state, sgsTable, "weapon_range") && lua_istable(state, -1)) {
                const QByteArray key = card->getClassName().toUtf8();
                if (pushStrategyMetadataField(state, -1, key.constData())) {
                    if (lua_isnil(state, -1)) weapon.insert("range", 0);
                    else if (lua_isnumber(state, -1)) weapon.insert("range", lua_tonumber(state, -1));
                }
            }
            weapons.insert(card->objectName(), weapon);
        }
        hegemony.insert("weapon_values", weapons);
        lua_settop(state, sgsTable);
        if (pushStrategyMetadataField(state, sgsTable, "ai_need_damaged") && lua_istable(state, -1)) {
            const int callbacks = lua_absindex(state, -1);
            QSet<QString> names;
            for (const auto &skill : world.self.skills) names.insert(skill.skillName);
            for (const auto &player : world.players)
                for (const auto &skill : player.skills) names.insert(skill.skillName);
            QJsonObject presence;
            for (const QString &name : names) {
                lua_settop(state, callbacks);
                const QByteArray key = name.toUtf8();
                if (pushStrategyMetadataField(state, callbacks, key.constData()))
                    presence.insert(name, lua_isfunction(state, -1));
            }
            hegemony.insert("need_damaged_presence", presence);
        }
    }
    {
        QJsonObject players;
        for (const ServerPlayer *player : room.getAllPlayers(true)) {
            QJsonObject shownSkills;
            QSet<QString> disclosedSkills;
            if (player == viewer) for (const auto &skill : world.self.skills) disclosedSkills.insert(skill.skillName);
            else for (const auto &row : world.players) if (row.objectName == player->objectName())
                for (const auto &skill : row.skills) disclosedSkills.insert(skill.skillName);
            for (const Skill *skill : player->getVisibleSkillList()) {
                if (!HegemonyMode::enabled() && (!skill || !disclosedSkills.contains(skill->objectName()))) continue;
                if (!skill || !hasShownPublicSkill(player,skill->objectName())) continue;
                const bool head = player->inHeadSkills(skill->objectName());
                const General *general = head ? player->getGeneral() : player->getGeneral2();
                shownSkills.insert(skill->objectName(), QJsonObject{{"head", head},
                    {"general", general ? general->objectName() : QString()},
                    {"general_is_lord", general && general->isLord()}});
            }
            QJsonObject siegeTargets;
            // inSiegeRelation(holder, victim) reads only public adjacency and
            // isFriendWith(..., false): unrevealed affiliations are rejected by
            // HegemonyMode before kingdom comparison; recruited roles are public.
            if (HegemonyMode::enabled() && hasShownPublicSkill(player,"heg_niaoxiang"))
                for (const ServerPlayer *target : room.getAllPlayers(true))
                    siegeTargets.insert(target->objectName(), player->inSiegeRelation(player, target));
            const General *knownHead = nullptr;
            if (HegemonyMode::enabled()) {
                if (player == viewer) knownHead = viewer->getActualGeneral1();
                else if (player->hasShownGeneral()) knownHead = player->getGeneral();
                else {
                    // Original common4776 consults this viewer's lawful
                    // KnownBoth memory, never the target's concealed general.
                    const QStringList remembered = viewer->getTag(QStringLiteral("KnownBoth_") + player->objectName()).toString().split(QLatin1Char('+'));
                    if (!remembered.isEmpty() && !remembered.first().isEmpty()) knownHead = Sanguosha->getGeneral(remembered.first());
                }
                if (!knownHead) knownHead = Sanguosha->getGeneral("anjiang");
            }
            players.insert(player->objectName(), QJsonObject{{"shown_one", player->hasShownOneGeneral()},
                {"siege_targets", siegeTargets},
                {"shown_all", aiShownAllGenerals(room, const_cast<ServerPlayer *>(viewer), const_cast<ServerPlayer *>(player))},
                {"public_careerist", HegemonyMode::enabled() && player->hasShownOneGeneral() && player->getRole() == QStringLiteral("careerist")},
                {"shown_head", player->hasShownGeneral()},
                {"shown_deputy", player->hasShownGeneral2()}, {"public_kingdom", player->getSeemingKingdom()},
                {"shown_skills", shownSkills},
                {"head_kingdom", knownHead ? QJsonValue(knownHead->getKingdom()) : QJsonValue::Null}});
        }
        QString ownKingdom = HegemonyMode::enabled() ? viewer->getSeemingKingdom() : QString();
        if (HegemonyMode::enabled() && ownKingdom.isEmpty()) {
            const QString role = viewer->getRole();
            ownKingdom = role == "careerist" || role.startsWith("careerist_")
                ? role : viewer->property("hegemony_kingdom").toString();
        }
        hegemony.insert("own_kingdom", ownKingdom.isEmpty() ? QStringLiteral("unknown") : ownKingdom);
        hegemony.insert("players", players);
    }
    world.strategyFacts.insert("hegemony", hegemony);
    QJsonObject publicProperties;
    QJsonObject publicDisplayCards;
    for (ServerPlayer *player : room.getAllPlayers(true)) {
        QJsonObject row;
        QJsonArray zhafu;
        // ZhafuCard::onEffect uses Room::setPlayerProperty, broadcasting this
        // source-name list to every client (tenyear2.cpp). It is public state.
        for (const QString &name : player->property("zhafu_from").toStringList()) zhafu.append(name);
        row.insert(QStringLiteral("zhafu_from"), zhafu);
        row.insert(QStringLiteral("has_display_property"), !player->property("display_cards").toString().isEmpty());
        // htms.lua's sole writer first broadcasts #s_newtype_choice.arg, then
        // sets the public 1-based position mark. No private hand data is used.
        const int newtypePosition = player->getMark("s_newtypepos");
        row.insert(QStringLiteral("newtype_pattern"), newtypePosition > 0 && newtypePosition <= newtypePatterns.length()
            ? newtypePatterns.at(newtypePosition - 1) : QString());
        QJsonArray areas;
        for (int index = 0; index < 5; ++index)
            if (player->hasEquipArea(index)) areas.append(index);
        row.insert(QStringLiteral("equip_areas"), areas);
        publicProperties.insert(player->objectName(), row);
        QJsonArray displayed;
        const QList<int> handIds = player->handCards();
        for (const QString &text : player->property("display_cards").toString().split(QChar('+'))) {
            bool valid = false;
            const int id = text.toInt(&valid);
            if (!valid || !handIds.contains(id)) continue;
            const Card *card = Sanguosha->getCard(id);
            if (!card) continue;
            // Explicitly public display identities. No private flags, source
            // skill, owner rule limits, or other concealed hand rows are read.
            QJsonObject visible{{"id", id}, {"effective_id", card->getEffectiveId()},
                {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
                {"engine_class_name", QString::fromLatin1(card->metaObject()->className())},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"suit", int(card->getSuit())}, {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
                {"red", card->isRed()}, {"black", card->isBlack()}, {"damage_card", card->isDamageCard()},
                {"virtual_card", card->isVirtualCard()}, {"target_fixed", card->targetFixed()},
                {"can_recast", card->canRecast()}, {"handling_method", int(card->getHandlingMethod())}};
            displayed.append(visible);
        }
        publicDisplayCards.insert(player->objectName(), displayed);
    }
    world.strategyFacts.insert(QStringLiteral("public_player_properties"), publicProperties);
    world.strategyFacts.insert(QStringLiteral("public_display_cards"), publicDisplayCards);
    if (!viewer || world.self.objectName != viewer->objectName()) return;

    {
        std::unique_ptr<AIOwnerPindianScope> pindianSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            pindianSource.reset(new AIOwnerPindianScope(room,viewer,gameLua));
        QJsonObject pindianTargets, pindianIncludingSelf;
        for (const auto *target : room.getAllPlayers(true)) {
            pindianTargets.insert(target->objectName(), viewer->canPindian(target, true));
            pindianIncludingSelf.insert(target->objectName(), viewer->canPindian(target, false));
        }
        world.strategyFacts.insert(QStringLiteral("owner_can_pindian"), QJsonObject{
            {"targets", pindianTargets}, {"targets_including_self", pindianIncludingSelf},
            {"any", viewer->canPindian(nullptr, true)}, {"any_including_self", viewer->canPindian(nullptr, false)}});
    }
    world.strategyFacts.insert(QStringLiteral("owner_shuangxiong_colors_turn"), viewer->property("shuangxiong_colors_turn").toString());

    QJsonObject ownerTags;
    ownerTags.insert(QStringLiteral("BaiyiUsed"), viewer->getTag("BaiyiUsed").toBool());
    QSet<QString> rosterNames;
    for (ServerPlayer *player : room.getAllPlayers(true)) rosterNames.insert(player->objectName());
    QJsonArray jinglveTargets;
    for (const QString &name : viewer->getTag("Jinglve_targets").toStringList())
        if (rosterNames.contains(name)) jinglveTargets.append(name);
    ownerTags.insert(QStringLiteral("Jinglve_targets"), jinglveTargets);
    world.strategyFacts.insert(QStringLiteral("owner_strategy_tags"), ownerTags);
    // Immutable printed faces for already disclosed owner cards only. A pile's
    // visible descriptor list is the authority boundary; never enumerate a
    // closed pile simply because its nominal owner is the viewer.
    QSet<int> printedIds;
    for (const auto &row : world.handCards) if (row.effectiveId >= 0) printedIds.insert(row.effectiveId);
    for (const auto &row : world.self.equips) if (row.effectiveId >= 0) printedIds.insert(row.effectiveId);
    for (const auto &pile : world.self.piles)
        for (const auto &row : pile.cards) if (row.effectiveId >= 0) printedIds.insert(row.effectiveId);
    QJsonObject printed;
    for (int id : printedIds) {
        if (room.getCardOwner(id) != viewer) continue;
        const Card *card = Sanguosha->getEngineCard(id);
        if (!card) continue;
        printed.insert(QString::number(id), QJsonObject{{"id", id}, {"effective_id", id},
            {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
            {"engine_class_name", QString::fromLatin1(card->metaObject()->className())},
            {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
            {"suit", int(card->getSuit())}, {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
            {"red", card->isRed()}, {"black", card->isBlack()}, {"virtual_card", false},
            {"subcards", QJsonArray()}, {"legacy_string_key", QString::number(id)}, {"legacy_sort_key", QString::number(id)}});
    }
    world.strategyFacts.insert("owner_printed_cards", printed);
    // Original common4432/4467 clones the physical material's CURRENT face,
    // even when isCard matched a different view-as class. Keep this separate
    // from Dismantlement+subcard probes. Only owner hand/equipment is inspected;
    // these detached descriptors can never authorize an action or reveal costs.
    if (HegemonyMode::enabled()) {
        QJsonObject phaseTrickProbes;
        for (const Card *material : viewer->getCards("he")) {
            Card *probe = Sanguosha->cloneCard(material->objectName(), material->getSuit(), material->getNumber());
            if (!probe) continue;
            const auto retire = qScopeGuard([probe] { probe->deleteLater(); });
            phaseTrickProbes.insert(QString::number(material->getEffectiveId()),
                ownerEvaluationCard(probe, room, viewer, -7000000 - material->getEffectiveId()));
        }
        world.strategyFacts.insert("owner_phase_trick_probes", phaseTrickProbes);
    }

    // Original Hegemony's Slash/Wenji policies read only their own state.
    world.strategyFacts.insert("owner_slash_count", viewer->getSlashCount());
    world.strategyFacts.insert("owner_current_card_use_reason", int(Sanguosha->getCurrentCardUseReason()));
    const Player *hegemonyLord = viewer->getLord();
    world.strategyFacts.insert("owner_hegemony_lord", hegemonyLord ? hegemonyLord->objectName() : QString());
    const int usedCards = room.countHistoryCards(viewer, QStringLiteral("turn"), QStringLiteral("."), false);
    const int respondedCards = room.countHistoryCards(viewer, QStringLiteral("turn"), QStringLiteral("."), true);
    world.strategyFacts.insert("owner_card_used_times", usedCards < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(usedCards));
    world.strategyFacts.insert("owner_card_responded_times", respondedCards < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(respondedCards));
    {
        std::unique_ptr<AIOwnerSlashScope> ownerSlashScope;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            ownerSlashScope.reset(new AIOwnerSlashScope(room,viewer,gameLua));
        world.strategyFacts.insert("owner_can_slash_without_crossbow", viewer->canSlashWithoutCrossbow());
    }

    world.strategyFacts.insert("owner_wenji_record", viewer->property("wenji_record").toString());
    world.strategyFacts.insert("owner_extra_slash_specific_assignee", viewer->property("extra_slash_specific_assignee").toString());
    QJsonObject noResponseById;
    for (int id : printedIds) {
        if (room.getCardOwner(id) != viewer) continue;
        const Card *card = Sanguosha->getCard(id);
        if (card) noResponseById.insert(QString::number(id), QJsonArray::fromStringList(card->getTag("NoResponse").toStringList()));
    }
    world.strategyFacts.insert("owner_card_no_response", noResponseById);
    QJsonObject usedHistory;
    const auto ownerHistory = viewer->getHistory();
    QJsonObject usedCounts;
    for (auto it = ownerHistory.cbegin(); it != ownerHistory.cend(); ++it) {
        usedHistory.insert(it.key(), viewer->hasUsed(it.key()));
        usedCounts.insert(it.key(), viewer->usedTimes(it.key()));
    }
    world.strategyFacts.insert("owner_used_history", QJsonObject{{"complete", true}, {"values", usedHistory}, {"counts", usedCounts}});
    // Fixed typemoon-ai free variable: observe only raw global presence/type.
    // Never evaluate __index or serialize a nonnil value (possibly userdata).
    if (lua_State *state = room.roomRuntime()->lua().state()) {
        const int top = lua_gettop(state);
        const auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state); lua_pushliteral(state, "slash"); lua_rawget(state, -2);
        const bool absent = lua_isnil(state, -1);
        const QString valueType = QString::fromLatin1(lua_typename(state, lua_type(state, -1)));
        bool lookupKnown = absent;
        if (absent && lua_getmetatable(state, -2)) {
            lua_pushliteral(state, "__index"); lua_rawget(state, -2);
            lookupKnown = lua_isnil(state, -1);
        }
        world.strategyFacts.insert("typemoon_global_slash", QJsonObject{{"known", lookupKnown},
            {"present", !absent}, {"type", valueType}});
    }
    QJsonObject skillProperties = world.strategyFacts.value("owner_skill_properties").toObject();
    skillProperties.insert("LuaYanjiu", viewer->property("LuaYanjiu").toString());
    if (hasOwnerSkill("chuming")) skillProperties.insert("chumingSubcard", viewer->property("chumingSubcard").toString());
    skillProperties.insert("nyarz_zhishi", viewer->property("nyarz_zhishi").toString());
    skillProperties.insert("nyarz_baguan", viewer->property("nyarz_baguan").toString());
    skillProperties.insert("jinqingleng_now_target", viewer->property("jinqingleng_now_target").toString());
    QJsonArray caozhaoNames;
    for (const auto &name : viewer->property("CaozhaoNames").toStringList()) caozhaoNames.append(name);
    skillProperties.insert("CaozhaoNames", caozhaoNames);
    skillProperties.insert("s2_mieshi", viewer->property("s2_mieshi").toString());
    skillProperties.insert("s3_fans_jibian_target", viewer->property("s3_fans_jibian_target").toString());
    world.strategyFacts.insert("owner_skill_properties", skillProperties);
    QJsonObject gaodaProperties;
    for (const auto &key : QStringList{"baizhan", "haidao", "luaqiangwucolor", "quanren"})
        gaodaProperties.insert(key, viewer->property(key.toUtf8().constData()).toString());
    gaodaProperties.insert("shewei", viewer->property("shewei").toInt());
    world.strategyFacts.insert("gaoda", QJsonObject{{"owner_properties", gaodaProperties},
        {"ban_packages", QJsonArray::fromStringList(bannedPackages)}});

    // Presence only: no legacy callback is called and no closure/upvalue is read.
    // Names are limited to this viewer's skills and disclosed Weidi lord skills.
    QSet<QString> hookNames;
    for (const auto &skill : world.self.skills) hookNames.insert(skill.skillName);
    if (hasOwnerSkill("weidi"))
        for (const auto &player : world.players)
            if (player.lord)
                for (const auto &skill : player.skills) if (skill.lordSkill) hookNames.insert(skill.skillName);
    QJsonObject hookPresence;
    bool hooksComplete = false;
    if (lua_State *state = room.roomRuntime()->lua().state()) {
        const int top = lua_gettop(state);
        const auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state);lua_pushliteral(state,"sgs");lua_rawget(state,-2);
        if (lua_istable(state,-1)) {
            lua_pushliteral(state,"ai_skill_cardask");lua_rawget(state,-2);
            if (lua_istable(state,-1)) {
                // A metatable may synthesize entries: never execute __index or
                // silently treat synthesized callback presence as absent.
                const bool hasMeta = lua_getmetatable(state,-1) != 0;
                if (hasMeta) lua_pop(state,1);
                if (!hasMeta) {
                    hooksComplete = true;
                    for (const QString &name : hookNames) {
                        const QByteArray key = name.toUtf8();
                        lua_pushlstring(state,key.constData(),size_t(key.size()));lua_rawget(state,-2);
                        hookPresence.insert(name,lua_isfunction(state,-1));lua_pop(state,1);
                    }
                }
            }
        }
    }
    world.strategyFacts.insert("owner_cardask_hooks",QJsonObject{{"complete",hooksComplete},{"by_skill",hookPresence}});
    projectAIBorrowedCallbacks(world, room, viewer);
    world.strategyFacts.insert("owner_discard_callbacks", discardCallbackMetadata(room.roomRuntime()->lua().state()));
    world.strategyFacts.insert("original_ask_callback_types", originalAskCallbackTypes(room.roomRuntime()->lua().state()));
    world.strategyFacts.insert("original37", originalHegemonyFacts(room,viewer));
    // Draw-count imitation consults exactly one package callback's presence.
    // Never invoke it in native or execute a metatable while observing metadata.
    bool jiangchiKnown = false, jiangchiPresent = false;
    if (lua_State *state = room.roomRuntime()->lua().state()) {
        const int top = lua_gettop(state);
        const auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state); lua_pushliteral(state, "sgs"); lua_rawget(state, -2);
        if (lua_istable(state, -1)) {
            lua_pushliteral(state, "ai_skill_choice"); lua_rawget(state, -2);
            if (lua_istable(state, -1)) {
                const bool hasMeta = lua_getmetatable(state, -1) != 0;
                if (hasMeta) lua_pop(state, 1);
                if (!hasMeta) {
                    lua_pushliteral(state, "jiangchi"); lua_rawget(state, -2);
                    jiangchiKnown = lua_isnil(state, -1) || lua_isfunction(state, -1);
                    jiangchiPresent = lua_isfunction(state, -1);
                }
            }
        }
    }
    world.strategyFacts.insert("jiangchi_choice_presence", QJsonObject{
        {"known", jiangchiKnown}, {"present", jiangchiPresent}});
    QJsonObject lolihimeTarget{{"known", false}, {"present", false}};
    if (lua_State *state = room.roomRuntime()->lua().state()) {
        const int top = lua_gettop(state);
        const auto restore = qScopeGuard([&] { lua_settop(state, top); });
        lua_pushglobaltable(state); lua_pushliteral(state, "sgs"); lua_rawget(state, -2);
        if (pushStrategyMetadataField(state, -1, "isGoodTarget")) {
            const bool present = lua_isfunction(state, -1);
            lolihimeTarget.insert("known", present || lua_isnil(state, -1));
            lolihimeTarget.insert("present", present);
            if (present) {
                lua_Debug info{};
                if (lua_getinfo(state, ">S", &info)) {
                    lolihimeTarget.insert("source", QString::fromUtf8(info.source ? info.source : ""));
                    lolihimeTarget.insert("line", info.linedefined);
                } else lolihimeTarget.insert("known", false);
            }
        }
    }
    world.strategyFacts.insert("lolihime_good_target", lolihimeTarget);
    QJsonArray arrangement;
    for (const QString &name : viewer->getTag("1v1Arrange").toStringList()) {
        const General *general = Sanguosha->getGeneral(name);
        QJsonObject skills;
        if (general)
            for (const QString &skill : {QStringLiteral("luoshen"), QStringLiteral("jizhi"),
                    QStringLiteral("xiaoji"), QStringLiteral("guhuo"), QStringLiteral("caizhaoji_hujia")})
                skills.insert(skill, general->hasSkill(skill));
        arrangement.append(QJsonObject{{"name", name}, {"exists", general != nullptr}, {"has_skills", skills}});
    }
    world.strategyFacts.insert("owner_1v1_arrange", arrangement);

    // Fixed standard-ai owner reads. These named fields are not a general tag
    // lookup and are never evaluated with another player as tag/property owner.
    QJsonObject standard = world.strategyFacts.value("standard").toObject();
    standard.insert("qingjian_move_skill", viewer->getTag("QingjianCurrentMoveSkill").toString());
    standard.insert("tishen_hp", viewer->property("tishen_hp").toInt());
    standard.insert("jilve_wansha", viewer->getTag("JilveWansha").toBool());
    const auto splitProperty = [viewer](const char *key) {
        const QString text = viewer->property(key).toString();
        return text.isEmpty() ? QJsonArray() : QJsonArray::fromStringList(text.split(QLatin1Char('+'), Qt::KeepEmptyParts));
    };
    standard.insert("extra_collateral", splitProperty("extra_collateral"));
    standard.insert("fenwei_targets", splitProperty("fenwei_targets"));
    // SP Songci's per-giver mark is public: both SongciCard::onEffect and
    // SongciViewAsSkill::effectOnTarget call addPlayerMark without a restricted
    // audience; Death clears the same key through setPlayerMark. Read only the
    // current viewer's giver prefix, never private per-instance recipients.
    QJsonObject songci = world.strategyFacts.value("sp_songci").toObject();
    songci.remove("given_marks");
    if (hasOwnerSkill("songci")) {
        QJsonObject given;
        const QString key = QStringLiteral("songci") + viewer->objectName();
        for (const ServerPlayer *target : room.getAllPlayers(true))
            given.insert(target->objectName(), target->getMark(key));
        songci.insert("given_marks", given);
    }
    world.strategyFacts.insert("sp_songci", songci);
    // SP jilei explicitly passes isHandcard=true for its empty-cost prototype.
    // Ordinary limits omit that argument and are not an equivalent query.
    {
        std::unique_ptr<AIOwnerJileiScope> jileiSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            jileiSource.reset(new AIOwnerJileiScope(room,viewer,gameLua));
        if (Card *exNihilo = Sanguosha->cloneCard("ex_nihilo", Card::NoSuit, 0)) {
            const auto retire = qScopeGuard([exNihilo] { exNihilo->deleteLater(); });
            QJsonObject jilei = world.strategyFacts.value("sp_jilei").toObject();
            jilei.insert("owner_ex_nihilo_hand_limited", viewer->isCardLimited(exNihilo, Card::MethodUse, true));
            world.strategyFacts.insert("sp_jilei", jilei);
        }
    }
    // AnimationCardpack.lua1517-1521: owner tag exists only around the discard
    // prompt; export a roster name, never a pointer or the damage object.
    const auto *roseTarget = viewer->getTag("hqiangwei").value<ServerPlayer *>();
    QJsonObject animation = world.strategyFacts.value("animation_cardpack").toObject();
    animation.insert("hqiangwei_target", roseTarget && rosterNames.contains(roseTarget->objectName())
        ? QJsonValue(roseTarget->objectName()) : QJsonValue(false));
    world.strategyFacts.insert("animation_cardpack", animation);
    const auto *victim = viewer->getTag("collateralVictim").value<ServerPlayer *>();
    standard.insert("collateral_victim", victim && rosterNames.contains(victim->objectName())
        ? QJsonValue(victim->objectName()) : QJsonValue(false));
    if (room.getMode() == "06_XMode") {
        // Publicly inferable cardinality: fixed XMode starts with three reserve
        // generals per team and each public mode replacement consumes one.
        // Never expose the reserved identities or the private selection order.
        QJsonObject backups;
        for (auto *player : room.getAllPlayers(true)) {
            const auto *leader = player->getTag("XModeLeader").value<ServerPlayer *>();
            const bool knownLeader = leader && rosterNames.contains(leader->objectName());
            QJsonObject row{{"has_leader", knownLeader}};
            if (knownLeader) row.insert("count", leader->getTag("XModeBackup").toStringList().size());
            backups.insert(player->objectName(), row);
        }
        standard.insert("xmode_backup_counts", backups);
    }
    world.strategyFacts.insert("standard", standard);


    // Exact owner-only static availability used by searchForAnaleptic.
    std::unique_ptr<AIOwnerAnalepticScope> ownerAnalepticScope;
    auto *analepticLua=room.roomRuntime()->lua().rawState();
    if(!inAIProjectionSourceScope(room,analepticLua))
        ownerAnalepticScope.reset(new AIOwnerAnalepticScope(room,viewer,analepticLua));
    const Analeptic analeptic(Card::NoSuit, 0);
    AIOwnerAnalepticBirth analepticBirth(room,viewer,&analeptic);
    recordAIProjectionOwnerSourceCard(room,viewer,&analeptic,QStringLiteral("analeptic"));
    world.strategyFacts.insert(QStringLiteral("owner_analeptic_available"), Analeptic::IsAvailable(viewer, &analeptic));
    ownerAnalepticScope.reset(); // End only the getter grant, not the original stack Card lifetime.

    int ignoreLimitCount = 0;
    {
        std::unique_ptr<AIOwnerCardStateScope> ignoreLimitSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            ignoreLimitSource.reset(new AIOwnerCardStateScope(room,viewer,gameLua));
        for (const Card *card : viewer->getHandcards())
            if (viewer->isCardLimited(card, Card::MethodIgnore)) ++ignoreLimitCount;
    }
    world.strategyFacts.insert(QStringLiteral("owner_ignore_limit_count"), ignoreLimitCount);

    QJsonArray disabledIds;
    for (const QVariant &value : viewer->getTag("cardChosenForAI").toList()) disabledIds.append(value.toInt());
    world.strategyFacts.insert(QStringLiteral("owner_card_chosen_disabled_ids"), disabledIds);

    QJsonArray duoruiSkills;
    for (const QString &name : viewer->property("duorui_skills").toStringList()) duoruiSkills.append(name);
    world.strategyFacts.insert(QStringLiteral("owner_duorui_skills"), duoruiSkills);

    QJsonObject originals;
    originals.insert(QStringLiteral("complete"), true);
    originals.insert(QStringLiteral("viewer"), viewer->objectName());
    originals.insert(QStringLiteral("general1"), QJsonArray());
    originals.insert(QStringLiteral("general2"), QJsonArray());
    if (viewer->inYinniState()) {
        originals.insert(QStringLiteral("general1"), originalGeneralSkills(viewer->property("yinni_general").toString()));
        originals.insert(QStringLiteral("general2"), originalGeneralSkills(viewer->property("yinni_general2").toString()));
    }
    world.strategyFacts.insert(QStringLiteral("owner_yinni"), originals);

    QJsonObject equipment;
    {
        std::unique_ptr<AIOwnerEquipmentQueriesScope> equipmentSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            equipmentSource.reset(new AIOwnerEquipmentQueriesScope(room,viewer,gameLua));
        for (const Skill *skill : viewer->getVisibleSkillList(true)) {
            if (!skill || !skill->inherits("ViewAsEquipSkill")) continue;
            QJsonArray names;
            if (const ViewAsEquipSkill *viewAs = Sanguosha->getViewAsEquipSkill(skill->objectName())) {
                // Owner-only: arbitrary extension viewAsEquip callbacks can depend
                // on hand cards or private marks. Never run this for other players.
                const QString declared = viewAs->viewAsEquip(viewer);
                for (const QString &name : declared.split(QChar(','), Qt::SkipEmptyParts)) names.append(name);
            }
            equipment.insert(skill->objectName(), names);
        }
    }
    world.strategyFacts.insert(QStringLiteral("owner_view_as_equips"), equipment);
    world.strategyFacts.insert(QStringLiteral("owner_view_as_equips_complete"), true);
}

void projectAIStrategySkills(AIWorldView &world, Room &, ServerPlayer *viewer)
{
    // Public registered definitions plus lawful owner original names. Never
    // enumerate another player's concealed generals or acquired instances;
    // catalogue membership says nothing about who actually holds a skill.
    QSet<QString> names;
    for (const AISkillView &skill : world.self.skills) names.insert(skill.skillName);
    for (const AIPlayerView &player : world.players)
        for (const AISkillView &skill : player.skills) names.insert(skill.skillName);
    if (viewer && world.self.objectName == viewer->objectName()) {
        const QJsonObject originals = world.strategyFacts.value(QStringLiteral("owner_yinni")).toObject();
        if (originals.value(QStringLiteral("viewer")).toString() == viewer->objectName()) {
            for (const QString &key : {QStringLiteral("general1"), QStringLiteral("general2")})
                for (const QJsonValue &name : originals.value(key).toArray()) names.insert(name.toString());
        }
    }
    // Registered definitions (including related/acquired skills) are public
    // catalogue metadata, not a list of any player's concealed actual skills.
    for (const QString &name : Sanguosha->getSkillNames()) names.insert(name);
    QStringList ordered = names.values();
    ordered.sort();
    QJsonObject catalog;
    for (const QString &name : ordered)
        if (const Skill *skill = Sanguosha->getSkill(name)) catalog.insert(name, publicSkillMetadata(skill));
    world.strategyFacts.insert(QStringLiteral("skill_catalog"), catalog);
    world.strategyFacts.insert(QStringLiteral("skill_catalog_scope"), QStringLiteral("public_registered_skill_definitions"));
    QJsonArray generalSkills;
    const QStringList banned = Sanguosha->getBanPackages();
    for (const General *general : Sanguosha->getAllGenerals()) {
        if (banned.contains(general->getPackage()) || general->isHidden() || general->isTotallyHidden()) continue;
        for (const Skill *skill : general->getSkillList()) generalSkills.append(skill->objectName());
    }
    // Public selectable catalogue, preserving duplicate names and engine order;
    // this has no relationship to any player's concealed actual generals.
    world.strategyFacts.insert(QStringLiteral("ordered_available_general_skills"), generalSkills);
}

void projectAIStrategySlashProbes(AIWorldView &world, Room &room, ServerPlayer *viewer)
{
    if (!viewer || world.self.objectName != viewer->objectName()) return;
    const QString extraSlash = room.getTag("AI_ExtraTargetSlash").toString();
    if (extraSlash.isEmpty()) {
        world.strategyFacts.insert(QStringLiteral("extra_target_slash"), false);
    } else {
        // This tag has no in-tree producer. Accept a no-cost printed Slash
        // declaration only; never publish serialized IDs or a private user string.
        const Card *parsed = Card::Parse(extraSlash);
        if (parsed && parsed->isKindOf("Slash") && parsed->isVirtualCard() && parsed->subcardsLength() == 0) {
            Card *copy = Sanguosha->cloneCard(parsed->objectName(), parsed->getSuit(), parsed->getNumber());
            if (copy) {
                const auto retire = qScopeGuard([copy] { copy->deleteLater(); });
                copy->setSkillName(parsed->getSkillName(false));
                world.strategyFacts.insert(QStringLiteral("extra_target_slash"), ownerEvaluationCard(copy, room, viewer, -3999999));
            }
        }
        // Missing field means the nonempty declaration could not be projected.
    }
    // Independent root for the entire fixed public clone/flag/skill-name loop.
    // The preceding arbitrary extra-target declaration is outside this scope.
    std::unique_ptr<AIFixedSlashProbesScope> fixedSlashScope;
    auto *fixedSlashLua=room.roomRuntime()->lua().rawState();
    if(!inAIProjectionSourceScope(room,fixedSlashLua))
        fixedSlashScope.reset(new AIFixedSlashProbesScope(room,viewer,fixedSlashLua));
    // Standard Slash's Xiansi branch calls CanToCard on this exact no-cost
    // declaration. targetFilter is kept separate from prohibition/availability.
    if (Card *slash = Sanguosha->cloneCard("slash")) {
        recordAIProjectionOwnerSourceCard(room,viewer,slash,QStringLiteral("slash"));
        const auto retire = qScopeGuard([slash] { slash->deleteLater(); });
        slash->setSkillName("_xiansi");
        QJsonObject targets;
        for (const auto *target : room.getAlivePlayers())
            targets.insert(target->objectName(), slash->targetFilter({}, target, viewer));
        world.strategyFacts.insert("xiansi_slash_probe", QJsonObject{
            {"card", ownerEvaluationCard(slash, room, viewer, -3999998)},
            {"candidate", QJsonObject{{"target_filter_empty", targets}}}});
    }
    QJsonObject reasons;
    int probeId = -3000000;
    for (const QString &reason : {QString(), QStringLiteral("heg_shunfu"), QStringLiteral("s4_zhaowu")}) {
        QJsonObject names;
        for (const QString &name : {QStringLiteral("slash"), QStringLiteral("thunder_slash"), QStringLiteral("fire_slash")}) {
            QJsonObject variants;
            for (int flagMask = 0; flagMask < 4; ++flagMask) {
                Card *card = Sanguosha->cloneCard(name);
                if (!card) continue;
                // This name comes from the fixed three-name loop, whose
                // entire factory family was proved before projection began.
                recordAIProjectionOwnerSourceCard(room,viewer,card,name);
                const auto retire = qScopeGuard([card] { card->deleteLater(); });
                if (!reason.isEmpty()) card->setSkillName(reason);
                if (flagMask & 1) card->setFlags("Qinggang");
                if (flagMask & 2) card->setFlags("SlashIgnoreArmor");
                QJsonObject row = ownerEvaluationCard(card, room, viewer, probeId);
                variants.insert(QString::number(flagMask), row);
                // Rule identity is owner-bound and intentionally has no ticket.
                // Other actors must use public facts/marked strategy estimates.
                // No native private query is run with another actor as source.
                // Each local clone is retired after its values have been copied.
                // Decrement independently of order used by Lua policy lookups.
                probeId--;
            }
            names.insert(name, variants);
        }
        reasons.insert(reason, names);
    }
    world.strategyFacts.insert(QStringLiteral("reason_slash_probes"), reasons);
}

// ---- projection ----
#include "ai-strategy/source-slots.h"
#include "ai-strategy/diy.h"
#include "ai-strategy/context.h"
#include "ai-strategy/anime.h"
#include "card-lifetime-manager.h"
#include "ai-strategy/official.h"
#include "ai-card-query-scope.h"
#include "wrapped-card.h"
#include <memory>
namespace QSanEngine { extern thread_local Player *Self; }
#include "standard-cards.h"

namespace {
QString projectionStrategyCardSortKey(const Card *card)
{
    QString key = card->toString();
    if (card->isKindOf("ActiveSkillCard")) {
        key += QLatin1Char('|') + card->getSkillName();
        if (card->getSkillInstanceId() > 0)
            key += QLatin1Char('#') + QString::number(card->getSkillInstanceId());
    }
    return key;
}
QObject *notificationLedger(Room &room, bool create)
{
    const auto name = QStringLiteral("isolated-ai-notification-ledger");
    auto *ledger = room.findChild<QObject *>(name, Qt::FindDirectChildrenOnly);
    if (!ledger && create) { ledger = new QObject(&room); ledger->setObjectName(name); }
    return ledger;
}
}
void recordAIStrategyNotifiedPile(Room &room, ServerPlayer *viewer, const QString &pile,
                                const QJsonObject &locations, bool active,
                                const QList<int> &orderedIds, bool orderKnown)
{
    if (!viewer) return;
    auto *ledger = notificationLedger(room, true);
    auto all = ledger->property("disclosures").toJsonObject();
    auto byViewer = all.value(viewer->objectName()).toObject();
    if (active) {
        QJsonObject cards;
        QJsonArray currentCards;
        for (auto it = locations.begin(); it != locations.end(); ++it) {
            const int id = it.key().toInt();
            // Fake-pile moves disclose IDs and locations, not a private wrapped
            // card update. Only this viewer's own card may use its live wrapper.
            // For every other source retain the immutable printed identity,
            // regardless of whether its hidden wrapper has changed.
            const bool owned = room.getCardOwner(id) == viewer;
            const Card *card = owned ? Sanguosha->getCard(id) : Sanguosha->getEngineCard(id);
            if (!card) continue;
            if (owned) currentCards.append(id);
            QJsonObject printed{{"id", id}, {"effective_id", id}, {"name", card->objectName()},
                {"legacy_string_key", QString::number(id)}, {"legacy_sort_key", QString::number(id)},
                {"class_name", card->getClassName()}, {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())}, {"number", card->getNumber()},
                {"red", card->isRed()}, {"black", card->isBlack()}, {"subtype", card->getSubtype()},
                {"skill_name", card->getSkillName(false)}, {"single_target", card->isSingleTargetCard()}, {"damage_card", card->isDamageCard()},
                {"virtual_card", false}, {"subcards", QJsonArray()}, {"will_throw", card->willThrow()}, {"can_recast", card->canRecast()},
                {"target_fixed", card->targetFixed()}, {"handling_method", int(card->getHandlingMethod())}};
            if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
                printed.insert("equip_slot", int(equip->location()));
                QJsonArray occupiedSlots;
                for (int slot : equip->getOccupyLocations()) occupiedSlots.append(slot);
                printed.insert("equip_occupy_locations", occupiedSlots);
            }
            if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard())) printed.insert("weapon_range", weapon->getRange());
            cards.insert(it.key(), printed);
        }
        QJsonObject notification{{"locations", locations}, {"cards", cards},
            {"current_card_ids", currentCards}, {"revision", QString::number(room.roomRuntime()->stateRevision())}};
        // Actual recipient notification order, including duplicates and a
        // genuinely empty list. Absence means this producer did not supply order.
        if (orderKnown) {
            QJsonArray order; for (int id : orderedIds) order.append(id);
            notification.insert("ordered_ids", order);
        }
        byViewer.insert(pile, notification);
    }
    else byViewer.remove(pile);
    all.insert(viewer->objectName(), byViewer);
    ledger->setProperty("disclosures", all);
}
QJsonObject readAIStrategyNotifiedPile(Room &room, ServerPlayer *viewer, const QString &pile)
{
    auto *ledger = notificationLedger(room, false);
    if (!ledger || !viewer) return {};
    auto row = ledger->property("disclosures").toJsonObject()
        .value(viewer->objectName()).toObject().value(pile).toObject();
    // A subsequent hidden move can invalidate a remembered location. Never
    // consult its actual hidden position to decide whether it is still known.
    if (row.value("revision").toString() != QString::number(room.roomRuntime()->stateRevision())) {
        row.insert("locations", QJsonObject());
        row.insert("current_card_ids", QJsonArray());
        row.remove("ordered_ids"); // No resurrection from later card membership.
    }
    // A fake-pile ID is not a current face. An independently witnessed real
    // public move may refresh only keys still in this active notification.
    auto cards = row.value("cards").toObject();
    auto locations = row.value("locations").toObject();
    auto current = row.value("current_card_ids").toArray();
    for (auto it = cards.begin(); it != cards.end(); ++it) {
        bool ok = false; const int id = it.key().toInt(&ok);
        if (!ok || id < 0) continue;
        const auto disclosed = readAIStrategyPublicDisclosure(room, viewer, id);
        if (!disclosed.value("known").toBool()) continue;
        it.value() = disclosed.value("card");
        locations.insert(it.key(), QJsonObject{{"owner", disclosed.value("owner")}, {"place", disclosed.value("place")}});
        if (!current.contains(id)) current.append(id);
    }
    if (!row.isEmpty()) {
        row.insert("cards", cards); row.insert("locations", locations); row.insert("current_card_ids", current);
    }
    return row;
}

QMap<QString, bool> projectAICanUseTargets(Room &room, ServerPlayer *owner, const Card *card)
{
    QMap<QString, bool> result;
    if (!owner || !card) return result;
    const bool limited = owner->isCardLimited(card, Card::MethodUse);
    AIMarkQueryScope temporary(owner, QStringLiteral("&xiyan1-Clear"), owner->getMark("&xiyan1-Clear") + 1);
    for (const auto *target : room.getAlivePlayers()) {
        int votes = 0;
        result.insert(target->objectName(), !limited && (card->targetFilter({}, target, owner, votes) || votes > 0));
    }
    return result;
}

namespace {
QJsonObject candidateJson(const AICardCandidateView &c)
{
    QJsonArray combinations;
    for (const auto &targets : c.targetCombinations) combinations.append(QJsonArray::fromStringList(targets));
    QJsonObject filters, votes, canUse, extra;
    for (auto it = c.extraTargetByPlayer.cbegin(); it != c.extraTargetByPlayer.cend(); ++it) extra.insert(it.key(), it.value());
    for (auto it = c.canUseTargets.cbegin(); it != c.canUseTargets.cend(); ++it) canUse.insert(it.key(), it.value());
    for (auto it = c.targetFilterEmpty.cbegin(); it != c.targetFilterEmpty.cend(); ++it) filters.insert(it.key(), it.value());
    for (auto it = c.maxVotes.cbegin(); it != c.maxVotes.cend(); ++it) votes.insert(it.key(), it.value());
    QJsonObject result{{"candidate_id", -1}, {"card_id", c.cardId}, {"available", c.available},
        {"limited", c.limited}, {"jilei", c.jilei}, {"target_fixed", c.targetFixed},
        {"complete_coverage", c.completeCoverage}, {"feasible_with_no_target", c.feasibleWithNoTarget},
        {"legal_targets", QJsonArray::fromStringList(c.legalTargets)}, {"target_combinations", combinations},
        {"target_filter_empty", filters}, {"extra_target_by_player", extra}, {"can_use_targets", canUse}, {"max_votes", votes}, {"history_available", c.historyAvailable}};
    if (c.extraTargetKnown) result.insert("extra_target_count", c.extraTargetCount);
    if (c.affectedTargetsKnown) result.insert("affected_targets", QJsonArray::fromStringList(c.affectedTargets));
    return result;
}
QJsonObject evaluationCard(const Card *card, ServerPlayer *owner, bool localClone = false)
{
    QJsonArray subcards, flags, locations;
    for (int id : card->getSubcards()) subcards.append(id);
    const bool ownsCard = card->getEffectiveId() >= 0
        && owner->getRoom()->getCardOwner(card->getEffectiveId()) == owner;
    if (ownsCard || localClone)
        for (const auto &flag : card->getFlags()) if (!flag.startsWith("visible_")) flags.append(flag);
    QJsonObject result{{"id", card->getId()}, {"effective_id", card->getEffectiveId()},
        {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"suit", int(card->getSuit())}, {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
        {"skill_name", card->getSkillName(false)},
        {"skill_names", QJsonArray::fromStringList(card->getSkillNames())},
        {"red", card->isRed()}, {"black", card->isBlack()}, {"subcards", subcards}, {"flags", flags},
        {"virtual_card", card->isVirtualCard()}, {"target_fixed", card->targetFixed()},
        {"single_target", card->isSingleTargetCard()}, {"damage_card", card->isDamageCard()}, {"will_throw", card->willThrow()}, {"can_recast", card->canRecast()},
        {"handling_method", int(card->getHandlingMethod())}, {"evaluation_only", true}, {"evaluation_owner", owner->objectName()},
        {"owner_state_known", true}, {"locked", owner->isLocked(card)}, {"jilei", owner->isJilei(card)},
        {"using", card->hasFlag("using")}, {"kill_off", card->hasFlag("AIGlobal_KillOff")}};
    result.insert("skill_names_complete", ownsCard || localClone);
    if (ownsCard || localClone) {
        result.insert("flags_complete_scope", "owner_non_visibility");
        result.insert("flags_complete", true);
        QJsonObject knownFlags;
        for (const auto &flag : flags) knownFlags.insert(flag.toString(), true);
        result.insert("known_flags", knownFlags);
    }
    if (!ownsCard && !localClone) {
        result.remove("skill_names");
        result.remove("flags");
        result.insert("known_flags", readAIStrategyPublicCardFlags(*owner->getRoom(), owner, card));
    }
    const QString identity = aiLegacyCardIdentity(*owner->getRoom(), owner, card);
    if (!identity.isEmpty()) result.insert("legacy_identity_token", identity);
    if (ownsCard || localClone || !card->isVirtualCard()) {
        result.insert("legacy_string_key", card->toString());
        result.insert("legacy_sort_key", projectionStrategyCardSortKey(card));
    }
    if (ownsCard || card->getSourceSkillName() == card->getSkillName(false))
        result.insert("source_skill", card->getSourceSkillName());
    if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
        result.insert("equip_slot", int(equip->location()));
        for (int slot : equip->getOccupyLocations()) locations.append(slot);
        result.insert("equip_occupy_locations", locations);
    }
    if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard())) result.insert("weapon_range", weapon->getRange());
    QJsonObject residue;
    for (const auto *target : owner->getRoom()->getAlivePlayers())
        residue.insert(target->objectName(), Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, card, target));
    result.insert("target_residue", residue);
    return result;
}
QJsonObject cardRules(Room &room, ServerPlayer *owner, const Card *card)
{
    QJsonObject limits, prohibited, canUse;
    const auto values = projectAICanUseTargets(room, owner, card);
    for (auto it = values.cbegin(); it != values.cend(); ++it) canUse.insert(it.key(), it.value());
    for (int method = int(Card::MethodNone); method <= int(Card::MethodGet); ++method)
        limits.insert(QString::number(method), owner->isCardLimited(card, Card::HandlingMethod(method)));
    for (const auto *target : room.getAlivePlayers()) prohibited.insert(target->objectName(), room.isProhibited(owner, target, card) != nullptr);
    QJsonObject result{{"limits", limits}, {"prohibited", prohibited}, {"can_use", canUse}, {"available", card->isAvailable(owner)},
        {"target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, owner, card)},
        {"target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, owner, card)},
        {"target_mod_residue", Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, card)}};
    if (card->isKindOf("Analeptic")) result.insert("analeptic_static_available", Analeptic::IsAvailable(owner, card));
    return result;
}
void historyAvailability(AICardCandidateView &candidate, const Card *card, ServerPlayer *owner)
{
    if (!card || card->getTypeId() != Card::TypeBasic) return;
    const QString key = card->isKindOf("Slash") ? QStringLiteral("Slash") : card->getClassName();
    // The common planner tries at most four pending Slashes, while Analeptic's
    // count follows the actual owner card pool. No live history is modified.
    const int maximum = qMin(256, owner->getCards("he").size() + owner->getHandPile().size() + 8);
    for (int offset = 0; offset <= maximum; ++offset) {
        AIHistoryQueryScope scope(owner, key, offset);
        candidate.historyAvailable.insert(QString::number(offset), card->isAvailable(owner));
    }
}
void projectSlash(AIWorldView &world, Room &room, ServerPlayer *viewer, const Card *card, const QString &key)
{
    if (!card || !card->isKindOf("Slash")) return;
    QJsonObject byActor = world.strategyFacts.value("slash_checks").toObject();
    QJsonObject targets = byActor.value(viewer->objectName()).toObject();
    const auto *weapon = dynamic_cast<const Weapon *>(viewer->getWeapon());
    const int printedRange = weapon ? weapon->getRange() : 1;
    const QSet<int> offsets{0, 1, printedRange - 2, printedRange - 3,
        viewer->getAttackRange() - 2, viewer->getAttackRange() - 3};
    for (const auto *target : room.getAlivePlayers()) {
        QJsonObject cards = targets.value(target->objectName()).toObject(), modes;
        for (bool distance : {true, false}) {
            QJsonObject values;
            for (int offset : offsets) values.insert(QString::number(offset), viewer->canSlash(target, card, distance, offset));
            modes.insert(distance ? "true" : "false", values);
        }
        cards.insert(key, modes); targets.insert(target->objectName(), cards);
    }
    byActor.insert(viewer->objectName(), targets); world.strategyFacts.insert("slash_checks", byActor);
}
}

namespace {
void projectCardFlags(AICardView &row, const QString &owner, ServerPlayer *viewer)
{
    row.legacyIdentityToken = aiLegacyCardIdentity(*viewer->getRoom(), viewer, row.nativeIdentity);
    row.skillNamesComplete = false;
    row.skillNames.clear();
    if (row.effectiveId < 0) return;
    const Card *card = Sanguosha->getCard(row.effectiveId);
    if (!card) return;
    row.willThrow = card->willThrow();
    const auto observation = readAIResponseCardObservation(*viewer->getRoom(), viewer, card);
    if (observation.contains("dwqingshidamage")) row.knownFlags.insert("dwqingshidamage", observation.value("dwqingshidamage"));
    row.knownFlags.insert("visible", card->hasFlag("visible"));
    if (!owner.isEmpty()) {
        const QString flag = "visible_" + viewer->objectName() + "_" + owner;
        row.knownFlags.insert(flag, card->hasFlag(flag));
    }
    // Card identity disclosure does not disclose arbitrary private card flags.
    if (owner == viewer->objectName()) {
        row.skillNames = card->getSkillNames();
        row.skillNamesComplete = true;
        row.flagsComplete = true;
        for (const QString &flag : card->getFlags())
            if (!flag.startsWith("visible_")) row.knownFlags.insert(flag, true);
    }
}
void projectPlayerCardFlags(AIPlayerView &player, ServerPlayer *viewer)
{
    for (auto *cards : {&player.equips, &player.judgingArea, &player.knownCards})
        for (auto &row : *cards) projectCardFlags(row, player.objectName, viewer);
    for (auto &pile : player.piles)
        for (auto &row : pile.cards) projectCardFlags(row, player.objectName, viewer);
}
}

void projectAIStrategyWorld(AIWorldView &world, Room &room, ServerPlayer *viewer, bool eventOnly)
{
    if (!viewer || world.self.objectName != viewer->objectName()) return;
    // Player::getSiblings follows QObject child creation order, independently
    // of the turn-rotated Room roster. Only public player names are projected.
    QJsonObject siblingOrders;
    QSet<QString> knownPlayers{world.self.objectName};
    for (const auto &row : world.players) knownPlayers.insert(row.objectName);
    for (const auto *player : room.getAllPlayers(true)) {
        QJsonArray names; bool complete = true;
        for (const auto *sibling : player->getSiblings(true)) {
            if (!sibling || !knownPlayers.contains(sibling->objectName())) { complete = false; break; }
            names.append(sibling->objectName());
        }
        if (complete) siblingOrders.insert(player->objectName(), names);
    }
    world.strategyFacts.insert("player_sibling_order", siblingOrders);
    projectAIStrategyAliases(world, room);
    world.strategyFacts.insert("role_predictable_config", Config.value("RolePredictable", false).toBool());
    projectAIStrategyKnownCards(world, room, viewer);
    projectPlayerCardFlags(world.self, viewer);
    for (auto &player : world.players) projectPlayerCardFlags(player, viewer);
    for (auto &row : world.handCards) projectCardFlags(row, viewer->objectName(), viewer);
    for (auto &row : world.discardPile) projectCardFlags(row, {}, viewer);
    QJsonObject equipSkills;
    for (const auto *equip : Sanguosha->findChildren<const EquipCard *>())
        if (Sanguosha->getSkill(equip)) equipSkills.insert(equip->objectName(), true);
    world.strategyFacts.insert("public_equip_skill_names", equipSkills);
    QJsonArray dying;
    for (const auto *player : room.getAlivePlayers())
        if (player->hasFlag("Global_Dying")) dying.append(player->objectName());
    world.strategyFacts.insert("dying_players", dying);
    projectAIStrategyFlagLifecycle(world, room);
    projectAIStrategyOwner(world, room, viewer);
    projectAIStrategyAnimecardCatalog(world, room, viewer);
    projectAIStrategySkills(world, room, viewer);
    projectAIPingjian(world, room, viewer);
    projectAIStrategyDamage(world, room, viewer);
    QJsonObject filtered, filteredHand;
    QSet<int> filterIds;
    for (const Card *card : viewer->getCards("he")) filterIds.insert(card->getEffectiveId());
    for (int id : viewer->getHandPile()) filterIds.insert(id);
    for (const AICardView &card : world.self.judgingArea) filterIds.insert(card.effectiveId);
    for (const AICardPileView &pile : world.self.piles)
        if (pile.open)
            for (const AICardView &card : pile.cards) filterIds.insert(card.effectiveId);
    for (int id : filterIds) {
        filtered.insert(QString::number(id), projectAIOwnerFilteredCard(room, viewer, id));
        filteredHand.insert(QString::number(id), projectAIOwnerFilteredCard(room, viewer, id, nullptr, int(Player::PlaceHand)));
    }
    world.strategyFacts.insert("card_filter_hand", QJsonObject{{viewer->objectName(), filteredHand}});
    world.strategyFacts.insert("card_filter", QJsonObject{{viewer->objectName(), filtered}});
    // Qiaobian evaluates public delayed tricks with the viewer as actor. Only
    // public board descriptors enter this owner rule map; hidden hands do not.
    const auto projectJudgingRules = [&](const AIPlayerView &player) {
        for (const auto &row : player.judgingArea) {
            const Card *card = Sanguosha->getCard(row.effectiveId);
            if (!card) continue;
            const QString key = "id:" + QString::number(row.effectiveId);
            QJsonObject rule = world.self.cardRules.value(key).toObject(), prohibited;
            for (const auto *target : room.getAlivePlayers())
                prohibited.insert(target->objectName(), viewer->isProhibited(target, card));
            rule.insert("prohibited", prohibited);
            world.self.cardRules.insert(key, rule);
        }
    };
    projectJudgingRules(world.self);
    for (const auto &player : world.players) projectJudgingRules(player);
    if (eventOnly) return;
    projectAIStrategySlashProbes(world, room, viewer);
    // Separate finite owner evaluation/material consumer, after the fixed
    // Slash-probe lifetime ended; never extends over the earlier world work.
    std::unique_ptr<AIOwnerEvaluationsScope> ownerEvaluationsScope;
    auto *evaluationLua=room.roomRuntime()->lua().rawState();
    if(!inAIProjectionSourceScope(room,evaluationLua))
        ownerEvaluationsScope.reset(new AIOwnerEvaluationsScope(room,viewer,evaluationLua));
    int budget = 8192;
    QJsonObject evaluations;
    for (auto it = world.evaluationCards.begin(); it != world.evaluationCards.end(); ++it) {
        if(ownerEvaluationsScope)ownerEvaluationsScope->prepareFactory(it.key());
        Card *card = Sanguosha->cloneCard(it.key());
        if (!card) continue;
        // This fresh world's keys come only from the coordinator's fixed
        // six-name initializer, before later choice-specific additions.
        recordAIProjectionOwnerSourceCard(room,viewer,card,it.key());
        auto retire = qScopeGuard([card] { card->deleteLater(); });
        auto candidate = projectAIStrategyCandidate(room, viewer, card, Card::MethodUse, AIRequest::Activate, {}, budget);
        historyAvailability(candidate, card, viewer);
        evaluations.insert(it.key(), candidateJson(candidate));
        // These are fresh owner-side prototypes, so their empty skill chain,
        // flags and exact native scalar metadata are known rather than omitted.
        auto descriptor = evaluationCard(card, viewer, true);
        descriptor.insert("evaluation_rules", cardRules(room, viewer, card));
        if (it.key() == "analeptic")
            descriptor.insert("source_analeptic_available", aiSourceAnalepticAvailable(viewer));
        it.value() = descriptor;
        projectSlash(world, room, viewer, card, "name:" + it.key());
    }
    world.strategyFacts.insert("evaluation_candidates", evaluations);
    QJsonObject dismantlementProbes;
    for (const Card *cost : viewer->getCards("he")) {
        Card *probe = Sanguosha->cloneCard("dismantlement");
        if (!probe) break;
        // The complete projection preflight proved this literal factory before
        // owner materials were selected. Record its actual native birth before
        // addSubcard or any policy query; cost comes from the same disclosed
        // owner getCards("he") source retained by that live readonly scope.
        recordAIProjectionOwnerSourceCard(room,viewer,probe,QStringLiteral("dismantlement"));
        const auto retire = qScopeGuard([probe] { probe->deleteLater(); });
        probe->addSubcard(cost);
        QJsonObject row = evaluationCard(probe, viewer, true);
        row.insert("id", -2000000 - cost->getEffectiveId());
        row.insert("effective_id", -2000000 - cost->getEffectiveId());
        row.insert("evaluation_rules", cardRules(room, viewer, probe));
        dismantlementProbes.insert(QString::number(cost->getEffectiveId()), row);
    }
    world.strategyFacts.insert("dismantlement_probes_by_cost", dismantlementProbes);
    for (const auto &row : world.handCards) {
        const Card *card = Sanguosha->getCard(row.effectiveId);
        projectSlash(world, room, viewer, card, "id:" + QString::number(row.effectiveId));
    }
}

void projectAIStrategyFanProbe(AIWorldView &world, Room &room, ServerPlayer *viewer, const Card *source)
{
    if (!viewer || world.self.objectName != viewer->objectName() || !source) return;
    // Called only for the owning Fan invoke's declared CardUse. Match donor
    // dummyCard("fire_slash") + addSubcard(use.card), including virtual costs.
    // This hypothetical result is not an activation/conversion capability.
    Card *probe = Sanguosha->cloneCard("fire_slash", Card::NoSuit, 0);
    if (!probe) return;
    const auto retire = qScopeGuard([probe] { probe->deleteLater(); });
    probe->setSkillName("fan");
    probe->addSubcard(source);
    QJsonObject row = evaluationCard(probe, viewer, true);
    row.insert("id", -4100000); row.insert("effective_id", -4100000);
    row.insert("evaluation_rules", cardRules(room, viewer, probe));
    row.insert("target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, viewer, probe));
    row.insert("target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, viewer, probe));
    QJsonObject sourceIdentity{{"name", source->objectName()}, {"class_name", source->getClassName()},
        {"suit", int(source->getSuit())}, {"number", source->getNumber()}};
    world.strategyFacts.insert("fan_conversion_probe", QJsonObject{{"source", sourceIdentity}, {"card", row}});
}

void projectAIStrategyDisclosedFilters(AIWorldView &world, Room &room, ServerPlayer *viewer,
                                       const QList<AICardView> &disclosed)
{
    if (!viewer || world.self.objectName != viewer->objectName()) return;
    // Both AG's hypothetical acquisition (Hand) and Guanxing/retrial's
    // hypothetical judgment (Judge) use explicitly disclosed request cards.
    // Never turn choiceOptions.cardIds alone into identities: those may name
    // another player's concealed candidates.
    QJsonObject filterActors = world.strategyFacts.value("card_filter_hand").toObject();
    QJsonObject acquiredFilters = filterActors.value(viewer->objectName()).toObject();
    QJsonObject judgeActors = world.strategyFacts.value("card_filter").toObject();
    QJsonObject judgeFilters = judgeActors.value(viewer->objectName()).toObject();
    for (const auto &offered : disclosed) {
        const QString key = QString::number(offered.effectiveId);
        acquiredFilters.insert(key,
            projectAIOwnerFilteredCard(room, viewer, offered.effectiveId, nullptr, int(Player::PlaceHand), &offered));
        judgeFilters.insert(key,
            projectAIOwnerFilteredCard(room, viewer, offered.effectiveId, nullptr, int(Player::PlaceJudge), &offered));
    }
    filterActors.insert(viewer->objectName(), acquiredFilters);
    judgeActors.insert(viewer->objectName(), judgeFilters);
    world.strategyFacts.insert("card_filter_hand", filterActors);
    world.strategyFacts.insert("card_filter", judgeActors);
}

void projectAIStrategyRequest(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->objectName() != request.viewerObjectName) return;
    projectAIUseCardDispatch(request, room, viewer);
    if (request.kind == AIRequest::UseCard
        && request.pattern == "@@_shuibojian"
        && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const qint64 executionId = viewer->getTag("ShuibojianExecution").toLongLong();
        const SkillContext parent = room.getSkillExecutionContext(executionId);
        if (parent.owner == viewer && parent.skill_name == "_shuibojian" && parent.original_data) {
            const CardUseStruct use = parent.original_data->value<CardUseStruct>();
            if (use.card && use.from == viewer) {
                QJsonArray targets;
                for (const auto *target : use.to) if (target) targets.append(target->objectName());
                request.choiceOptions.context.insert("shuibojian_use", QJsonObject{
                    {"from", viewer->objectName()}, {"to", targets}, {"card", evaluationCard(use.card, viewer)}});
                QJsonObject candidates;
                const QVariantMap values = viewer->property("shuibojian_candidates").toMap();
                for (auto it = values.cbegin(); it != values.cend(); ++it) {
                    if (!room.findPlayerByObjectName(it.key(), true)) continue;
                    QJsonArray victims;
                    for (const QString &name : it.value().toStringList())
                        if (room.findPlayerByObjectName(name, true)) victims.append(name);
                    candidates.insert(it.key(), victims);
                }
                request.choiceOptions.context.insert("shuibojian_candidates", candidates);
                request.choiceOptions.context.insert("shuibojian_maximum", viewer->property("shuibojian_maximum").toInt());
            }
        }
    }
    for (auto &row : request.choiceOptions.cards) {
        const Player *owner = room.getCardOwner(row.effectiveId);
        projectCardFlags(row, owner ? owner->objectName() : QString(), viewer);
    }
    if (request.kind == AIRequest::PlayerChosen && request.choiceOptions.reason == "qiaobian") {
        const auto *target = room.getTag("QiaobianTarget").value<ServerPlayer *>();
        request.choiceOptions.context.insert("qiaobian_target", target && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(target))
            ? QJsonValue(target->objectName()) : QJsonValue(false));
    }
    // These transient ForAI flags identify publicly declared skill targets.
    // Mingce logs #CollateralSlash before its choice; Dimeng recipients are
    // public declared targets before the hand exchange triggers Luoying.
    const auto auditedLuoyingAlias = [&] {
        const QString reason = request.choiceOptions.reason;
        if (reason != "shenxian" && reason != "yinli") return false;
        const auto aliases = request.worldView.strategyFacts.value("audited_aliases").toObject();
        const auto row = aliases.value("entries").toObject()
            .value("ai_skill_invoke." + reason).toObject();
        return aliases.value("captured").toBool() && row.value("known").toBool()
            && row.value("present").toBool() && row.value("enabled").toBool()
            && row.value("matched").toBool() && row.value("source_matches").toBool()
            && row.value("canonical_registry").toString() == "ai_skill_invoke"
            && row.value("canonical_path").toArray() == QJsonArray{QStringLiteral("luoying")};
    };
    if ((request.kind == AIRequest::Choice && request.choiceOptions.reason == "mingce")
        || (request.kind == AIRequest::SkillInvoke
            && (request.choiceOptions.reason == "luoying" || auditedLuoyingAlias()))) {
        const bool mingce = request.choiceOptions.reason == "mingce";
        QJsonArray targets;
        for (const ServerPlayer *player : room.getAllPlayers(true))
            if (player && player->hasFlag(mingce ? "MingceTarget" : "DimengTarget")) targets.append(player->objectName());
        request.choiceOptions.context.insert(mingce ? "mingce_targets" : "dimeng_targets", targets);
    }
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "guzheng")
        request.choiceOptions.context.insert("guzheng_to_get_count", viewer->getTag("GuzhengToGet").toList().size());
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "nosguhuo") {
        // The fixed skill broadcasts #Guhuo with this declaration before
        // questioning any player. The concealed material is never projected.
        QJsonObject standard = request.worldView.strategyFacts.value("standard").toObject();
        standard.insert("nosguhuo_type", room.getTag("NosGuhuoType").toString());
        request.worldView.strategyFacts.insert("standard", standard);
    }
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "huashen") {
        QJsonObject skills;
        for (const QString &name : request.choiceOptions.choices) {
            const Skill *skill = Sanguosha->getSkill(name);
            const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(name);
            const auto *trigger = dynamic_cast<const TriggerSkill *>(skill);
            QJsonObject row{{"exists", skill != nullptr}, {"view_as", viewAs != nullptr},
                {"trigger", trigger != nullptr}, {"has_cards_move", trigger && trigger->getTriggerEvents().contains(CardsMoveOneTime)}};
            if (skill) row.insert("description", skill->getDescription());
            if (viewAs) row.insert("enabled_at_play", viewAs->isEnabledAtPlay(viewer));
            skills.insert(name, row);
        }
        request.choiceOptions.context.insert("huashen_skills", skills);
    }
    const QJsonValue extraSlash = request.worldView.strategyFacts.value("extra_target_slash");
    if (!extraSlash.isUndefined()) request.choiceOptions.context.insert("extra_target_slash", extraSlash);
    if (request.choiceOptions.reason == "gongxin") {
        const auto *target = viewer->getTag("gongxin").value<ServerPlayer *>();
        if (target && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(target)))
            request.choiceOptions.context.insert("gongxin_target", target->objectName());
        else request.choiceOptions.context.insert("gongxin_target", false);
        QJsonObject printed;
        for (const auto &offered : request.choiceOptions.cards) {
            const Card *card = Sanguosha->getEngineCard(offered.effectiveId);
            if (!card) continue;
            // Only immutable faces of already disclosed offered IDs. No live
            // wrapper flags, skill identities, private tags, or hidden cards.
            printed.insert(QString::number(offered.effectiveId), QJsonObject{
                {"id", offered.effectiveId}, {"effective_id", offered.effectiveId},
                {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"suit", int(card->getSuit())}, {"number", card->getNumber()},
                {"type_id", int(card->getTypeId())}, {"red", card->isRed()}, {"black", card->isBlack()},
                {"legacy_string_key", QString::number(offered.effectiveId)}, {"legacy_sort_key", QString::number(offered.effectiveId)},
                {"virtual_card", false}, {"subcards", QJsonArray()}});
        }
        request.choiceOptions.context.insert("gongxin_engine_cards", printed);
    }
    projectAIStrategyYCZH2016(request, room, viewer);
    projectAIStrategyRandomRequest(request, room, viewer);
    projectAIStrategyRandomPlanningRequest(request, room, viewer);
    for (auto &candidate : request.cardCandidates)
        historyAvailability(candidate, Sanguosha->getCard(candidate.cardId), viewer);
    projectAIStrategyDisclosedFilters(request.worldView, room, viewer, request.choiceOptions.cards);
    QList<AICardView> acquiredCards;
    if (request.kind == AIRequest::AmazingGrace || request.kind == AIRequest::Guanxing)
        acquiredCards = request.choiceOptions.cards;
    else if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "taoxi") {
        // The owner is deciding whether to borrow from this declared-use target.
        // Evaluate only faces already present in this viewer's lawful snapshot;
        // a tag or a guessed physical ID does not reveal another hand.
        const auto use = request.worldView.strategyFacts.value("yjcm2015").toObject().value("taoxi_use").toObject();
        const auto targets = use.value("to").toArray();
        if (use.value("from").toString() == viewer->objectName() && !targets.isEmpty()) {
            for (const auto &other : request.worldView.players)
                if (other.objectName == targets.first().toString()) acquiredCards = other.knownCards;
            projectAIStrategyDisclosedFilters(request.worldView, room, viewer, acquiredCards);
        }
    }
    else if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "eastfast") {
        const auto context = request.choiceOptions.context.value("eastfast").toObject();
        const auto row = context.value("card").toObject();
        if (context.value("known").toBool() && row.value("effective_id").isDouble()
            && row.value("effective_id").toInt(-1) >= 0 && row.value("name").isString()
            && row.value("class_name").isString() && row.value("suit").isDouble()
            && row.value("number").isDouble() && row.value("virtual_card").isBool()
            && !row.value("virtual_card").toBool()) {
            AICardView card;
            card.cardId = card.effectiveId = row.value("effective_id").toInt();
            card.objectName = row.value("name").toString(); card.className = row.value("class_name").toString();
            card.suit = row.value("suit").toInt(); card.number = row.value("number").toInt();
            card.skillName = row.value("skill_name").toString(); card.knownFlags = row.value("known_flags").toObject();
            acquiredCards.append(card);
            projectAIStrategyDisclosedFilters(request.worldView, room, viewer, acquiredCards);
        }
    }
    if (acquiredCards.isEmpty()) return;
    int budget = 8192; QJsonObject rows;
    for (const auto &offered : acquiredCards) {
        // Hypothetical acquisition evaluates the offered face, not another
        // location's private wrapped flags or skill identity.
        std::unique_ptr<Card> disclosedCard;
        const Card *card = nullptr;
        if (request.kind == AIRequest::AmazingGrace && request.choiceOptions.reason == "shouxi"
            && request.choiceOptions.cardIds.contains(offered.effectiveId)) {
            // Fixed Shouxi asks getEngineCard for each already offered ID.
            // Its printed definition is independent of any live owner filter.
            card = Sanguosha->getEngineCard(offered.effectiveId);
        } else if (room.getCardOwner(offered.effectiveId) == viewer) card = Sanguosha->getCard(offered.effectiveId);
        else if (offered.effectiveId >= 0 && !offered.virtualCard) {
            QStringList flags;
            for (auto it = offered.knownFlags.constBegin(); it != offered.knownFlags.constEnd(); ++it)
                if (it.value().toBool()) flags.append(it.key());
            disclosedCard.reset(Sanguosha->cloneCard(offered.objectName, Card::Suit(offered.suit), offered.number, flags));
            if (!disclosedCard || disclosedCard->getClassName() != offered.className) continue;
            disclosedCard->setId(offered.effectiveId);
            disclosedCard->setSkillName(offered.skillName);
            card = disclosedCard.get();
        }
        if (!card) continue;
        auto candidate = projectAIStrategyCandidate(room, viewer, card, Card::MethodUse, AIRequest::Activate, {}, budget);
        historyAvailability(candidate, card, viewer);
        QJsonObject acquired{{"evaluation_only", true},
            {"card", evaluationCard(card, viewer)}, {"candidate", candidateJson(candidate)},
            {"card_rules", cardRules(room, viewer, card)}};
        // These two source AG bodies plan getCard(id), not our hypothetical
        // acquisition clone. Keep that clone/profile identity unchanged. Only
        // a separate current offered-object witness can save the source Card.
        if (request.kind == AIRequest::AmazingGrace
            && (request.choiceOptions.reason == "kezumingjie" || request.choiceOptions.reason == "kezulilun")
            && request.choiceOptions.cardIds.contains(offered.effectiveId)) {
            const Card *actual = Sanguosha->getCard(offered.effectiveId);
            const auto token = offered.nativeIdentity;
            if (actual && dynamic_cast<const WrappedCard *>(actual) && token
                && globalCardLifetimeManager().isLive(token) && token->address == actual
                && offered.cardId == actual->getId() && offered.effectiveId == actual->getEffectiveId()
                && !offered.virtualCard && !actual->isVirtualCard()
                && offered.objectName == actual->objectName() && offered.className == actual->getClassName()
                && offered.suit == int(actual->getSuit()) && offered.number == actual->getNumber()
                && offered.skillName == actual->getSkillName(false) && offered.subcardIds == actual->getSubcards()
                && offered.canRecast == actual->canRecast()) {
                const QString identity = aiLegacyCardIdentity(room, viewer, token);
                if (!identity.isEmpty() && identity == offered.legacyIdentityToken)
                    acquired.insert("observed_source", QJsonObject{{"physical_id", offered.effectiveId},
                        {"legacy_identity_token", identity}});
            }
        }
        rows.insert(QString::number(offered.effectiveId), acquired);
        projectSlash(request.worldView, room, viewer, card, "id:" + QString::number(offered.effectiveId));
    }
    request.choiceOptions.context.insert("acquired_card_evaluations", rows);
}

QJsonObject projectAIOwnerFilteredCard(Room &room, ServerPlayer *viewer, int id, const JudgeStruct *judge, int hypotheticalPlace, const AICardView *disclosed)
{
    if (!viewer) return {};
    const bool owns = room.getCardOwner(id) == viewer;
    if (!owns && (!disclosed || disclosed->effectiveId != id || id < 0)) return {};
    std::unique_ptr<Card> publicInput;
    const Card *original = nullptr;
    if (owns) original = Sanguosha->getCard(id);
    else {
        // Hypothetical acquisition starts from the exact disclosed descriptor,
        // not arbitrary private flags/source tags of the current native wrapper.
        QStringList flags;
        for (auto it = disclosed->knownFlags.constBegin(); it != disclosed->knownFlags.constEnd(); ++it)
            if (it.value().toBool()) flags.append(it.key());
        publicInput.reset(Sanguosha->cloneCard(disclosed->objectName, Card::Suit(disclosed->suit), disclosed->number, flags));
        if (!publicInput || publicInput->getClassName() != disclosed->className) return {};
        publicInput->setId(id); publicInput->setSkillName(disclosed->skillName);
        original = publicInput.get();
    }
    if (!original) return {};
    QList<const FilterSkill *> filters;
    for (const Skill *skill : viewer->getSkillList(true)) {
        if (skill->isLordSkill() ? !([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasLordSkill(skill); }()) : !([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill(skill); }())) continue;
        if (const auto *filter = dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(skill->objectName()))) filters.append(filter);
    }
    // sgs.getPlayerSkillList appends the lord's visible lord skills for Weidi,
    // without deduplication or rechecking them as the viewer's owned instances.
    const ServerPlayer *lord = room.getLord();
    if (([&] { std::unique_ptr<AIWorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIProjectionSourceScope(room,gameLua))
            source.reset(new AIWorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("weidi"); }()) && lord && lord != viewer) {
        for (const Skill *skill : lord->getSkillList(true))
            if (skill->isLordSkill())
                if (const auto *filter = dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(skill->objectName()))) filters.append(filter);
    }
    // These callback bodies were audited at the pinned engine/extensions SHAs:
    // they only construct cards or mutate their selected WrappedCard. Unknown
    // providers must not be granted a live Room mutation channel by a name API.
    static const QSet<QString> audited{
        QStringLiteral("#MeowHongyan"),
        QStringLiteral("#ZhuohunBf"),
        QStringLiteral("#htms_godotfilter"),
        QStringLiteral("#huanmeng-filter"),
        QStringLiteral("#japenknifeslashe2"),
        QStringLiteral("#kejiexianjuaoex"),
        QStringLiteral("#kejiguaner"),
        QStringLiteral("#kejinxguner"),
        QStringLiteral("#linguangfilter"),
        QStringLiteral("#mankongzhibf"),
        QStringLiteral("#meibu-filter"),
        QStringLiteral("#olmeibu-filter"), QStringLiteral("#olzhixi-filter"),
        QStringLiteral("#mouhongyan_vsc"),
        QStringLiteral("#myjl-filter"),
        QStringLiteral("#ny_tenth_jvewu_change"),
        QStringLiteral("#ofhongyuanbf"),
        QStringLiteral("#qhwindwushenFilter"),
        QStringLiteral("#se_mingqie_filter"),
        QStringLiteral("#sfofl_shuangrenFilter"),
        QStringLiteral("#tiequanf"),
        QStringLiteral("#xuanguangfilter"),
        QStringLiteral("#yuanjue1"),
        QStringLiteral("#zhizhe"),
        QStringLiteral("LuaKuangshi"),
        QStringLiteral("SevenBeiJie"),
        QStringLiteral("SixGuicaiFilter"),
        QStringLiteral("TH_PerfectMath"),
        QStringLiteral("TH_ScarletDestiny"),
        QStringLiteral("bloodfight"),
        QStringLiteral("chihun"),
        QStringLiteral("duqu"),
        QStringLiteral("eatforlike"),
        QStringLiteral("fcj_longnu_yang"),
        QStringLiteral("fcj_longnu_yin"),
        QStringLiteral("fczhizhe_vs"),
        QStringLiteral("fuhei"),
        QStringLiteral("fuzou_filter"),
        QStringLiteral("guandu_quanjiu"),
        // zombie-scenario.cpp:296; equip-outside-slot -> local IronChain.
        QStringLiteral("ganran"),
        QStringLiteral("heixing"),
        QStringLiteral("hongyan"),
        QStringLiteral("japenknifeslashe"),
        QStringLiteral("jghupo"),
        QStringLiteral("jinjiu"),
        QStringLiteral("jjjuanjia"),
        QStringLiteral("kechengduxingex"),
        QStringLiteral("keolweilin_black"),
        QStringLiteral("keolweilin_red"),
        QStringLiteral("kezhuanninghanbuff"),
        QStringLiteral("krskitgzhanyue"),
        QStringLiteral("leij"),
        QStringLiteral("luajuesi"),
        QStringLiteral("luaqilin"),
        QStringLiteral("luayudun"),
        QStringLiteral("lxtx_jinjiu"),
        QStringLiteral("meizlkuijiu"),
        QStringLiteral("melee_peach_filter"),
        QStringLiteral("mingchuan"),
        QStringLiteral("mobilejinjiu"),
        QStringLiteral("mobilexinxianghai"),
        QStringLiteral("ny_10th_jiaoxia_filter"),
        QStringLiteral("olhongyan"),
        QStringLiteral("olwushen"),
        QStringLiteral("qishi"),
        QStringLiteral("ruyi"),
        QStringLiteral("s2_benxi"),
        QStringLiteral("s2_feiyan"),
        QStringLiteral("s2_haozhan"),
        QStringLiteral("s2_hengchong"),
        QStringLiteral("s2_qiangji"),
        QStringLiteral("s2_shanxianOther"),
        QStringLiteral("s4_pinglu"),
        QStringLiteral("s4_s_liaoshang"),
        QStringLiteral("s_w_tuyong"),
        QStringLiteral("secondtenyearjinjiu"),
        QStringLiteral("secondtenyearshizhi"),
        QStringLiteral("sfofl_xuezhan"),
        QStringLiteral("sgkgodwushen"),
        QStringLiteral("sgskanxueSlash"),
        QStringLiteral("shimaZHCMT"),
        QStringLiteral("shizhi"),
        QStringLiteral("sxwuyan"),
        QStringLiteral("tc_wumou"),
        QStringLiteral("wushen"),
        QStringLiteral("xdbaohe"),
        QStringLiteral("xunshi"),
        QStringLiteral("ys_xionchijink"),
        QStringLiteral("zb_ganran")
    };
    std::unique_ptr<Card> result(Sanguosha->cloneCard(original));
    if (!result) return {};
    bool transformed = false;
    QString repair;
    for (const auto *filter : filters) {
        if (room.getCardPlace(id) == Player::PlaceEquip) break;
        if (!owns) {
            const QString name = filter->objectName();
            const QMap<QString, QString> flagReads{{"#ny_tenth_jvewu_change", "ny_tenth_jvewu"},
                {"keolweilin_red", "keolweilin"}, {"keolweilin_black", "keolweilin"},
                };
            const bool readsFlag = flagReads.contains(name)
                && (name != "keolweilin_red" || original->isRed())
                && (name != "keolweilin_black" || !original->isRed());
            if (name == "#zhizhe" || (name == "fczhizhe_vs"
                && (original->objectName() == "_fczhizhe_basic" || original->objectName() == "_fczhizhe_trick"))
                || (name == "SixGuicaiFilter" && hypotheticalPlace == int(Player::PlaceJudge))
                || (readsFlag && !disclosed->flagsComplete
                    && !disclosed->knownFlags.contains(flagReads.value(name))))
                return {{"complete", false}, {"unsupported_filter", "undisclosed_card_state:" + name}};
        }
        if (filter->objectName() == QStringLiteral("luaqilin") && !room.getCurrent())
            return {{"complete", false}, {"unsupported_filter", "luaqilin_no_current"}};
        const bool repairDouba = filter->objectName() == QStringLiteral("s2_douba");
        if (repairDouba && !room.getCurrent()) return {{"complete", false}, {"unsupported_filter", "s2_douba_no_current"}};
        if (!repairDouba && !audited.contains(filter->objectName()))
            return {{"complete", false}, {"unsupported_filter", filter->objectName()}};
        Card *input = Sanguosha->cloneCard(original);
        if (!input) return {};
        input->setId(id);
        WrappedCard shadow(input);
        AICardQueryScope mapping(&room, room.roomState(), id, &shadow, viewer, hypotheticalPlace);
        Player *oldSelf = QSanEngine::Self;
        Player *oldPlayer = room.roomState()->getCurrentPlayer();
        setEngineSelf(viewer); room.roomState()->setCurrentPlayer(viewer);
        auto restore = qScopeGuard([&] { setEngineSelf(oldSelf); room.roomState()->setCurrentPlayer(oldPlayer); });
        if (!filter->viewFilter(&shadow)) continue;
        if (filter->objectName() == QStringLiteral("#zhizhe")
            && room.getTag("ZhizheFilter_" + shadow.toString()).toString().split('+').size() < 3)
            return {{"complete", false}, {"unsupported_filter", "zhizhe_malformed_native_descriptor"}};
        const Card *converted = nullptr;
        if (repairDouba) {
            // Donor assigns a card to its global skill variable. Preserve the
            // red/black result with a local clone; never corrupt rules globals.
            if (!shadow.isRed() && !shadow.isBlack())
                return {{"complete", false}, {"unsupported_filter", "s2_douba_colorless_uninitialized_card"}};
            Card *local = Sanguosha->cloneCard(shadow.isRed() ? "slash" : "duel", shadow.getSuit(), shadow.getNumber());
            if (!local) return {};
            local->setSkillName(filter->objectName()); converted = local;
            repair = "s2_douba_global_card_assignment_replaced_with_local";
        } else converted = filter->viewAs(&shadow);
        if (!converted) continue;
        Card *copy = Sanguosha->cloneCard(converted);
        if (converted != &shadow && converted->isVirtualCard()) const_cast<Card *>(converted)->deleteLater();
        if (!copy) return {};
        copy->setId(-1); result.reset(copy); transformed = true;
    }
    const Card *card = result.get();
    QJsonArray disclosedFlags;
    for (const QString &flag : card->getFlags()) if (!flag.startsWith("visible_")) disclosedFlags.append(flag);
    QJsonObject dto{{"id", transformed ? -1 : id}, {"effective_id", transformed ? -1 : id},
        {"legacy_string_key", card->toString()}, {"legacy_sort_key", projectionStrategyCardSortKey(card)},
        {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"suit", int(card->getSuit())}, {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
        {"skill_name", card->getSkillName(false)}, {"red", card->isRed()}, {"black", card->isBlack()},
        {"virtual_card", transformed}, {"subcards", QJsonArray()}, {"flags", disclosedFlags}, {"flags_complete_scope", "owner_non_visibility"},
        {"skill_names_complete", false},
        {"single_target", card->isSingleTargetCard()}, {"damage_card", card->isDamageCard()}, {"target_fixed", card->targetFixed()},
        {"handling_method", int(card->getHandlingMethod())}, {"will_throw", card->willThrow()}, {"can_recast", card->canRecast()}};
    QJsonObject projected{{"complete", true}, {"card", dto}};
    if (judge) projected.insert("judge_outcome", judge->isGood(card));
    if (!repair.isEmpty()) projected.insert("donor_error_repair", repair);
    return projected;
}

// ---- preview-authority ----

namespace {
bool previewAuthorityFail(QString *error, const char *message)
{ if (error) *error = QString::fromLatin1(message); return false; }
bool base(const AIRequest &request, Room &room, ServerPlayer *viewer,
    QJsonObject &preview, QString *error)
{
    const QString skill = aiStrategyPreviewSkill(request);
    if (skill.isEmpty() || !viewer || viewer->getRoom() != &room
        || request.decisionId == 0 || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return previewAuthorityFail(error, "preview requires a current exact owner question");
    const auto snapshot = request.choiceOptions.context.value("retrial_preview").toObject();
    preview = readAIStrategyRetrialPreview(room, viewer, skill);
    // Equality is a native-issued snapshot comparison, not a caller-supplied
    // face check. A new notification is a new serial even for identical IDs.
    if (!snapshot.value("known").toBool() || !preview.value("known").toBool()
        || !snapshot.value("serial").isString() || snapshot.value("serial").toString().isEmpty()
        || snapshot != preview)
        return previewAuthorityFail(error, "preview witness changed or was not issued to this request");
    return true;
}
bool identity(const AIRequest &request, Room &room, ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source, QString *error)
{
    const QString skill = aiStrategyPreviewSkill(request);
    if (!activation.isValid() || !source.isValid()
        || activation.ownerObjectName != viewer->objectName() || activation.key.skillName != skill
        || !viewer->hasSkillInstance(skill, activation.key.instanceID)
        || viewer->isSkillInvalid(skill, activation.key.instanceID))
        return previewAuthorityFail(error, "preview activation instance is not live for this owner");
    auto root = room.resolveSkillInstanceRootRef(activation);
    if (!root.isValid()) root = activation;
    if (root != source) return previewAuthorityFail(error, "preview activation root changed");
    bool issued = request.hasSkillActionContext && request.skillActionContext.activationRef == activation
        && request.skillActionContext.sourceRef == source;
    for (const auto &action : request.skillActions)
        if (action.activationRef == activation && action.sourceRef == source) issued = true;
    if (!issued) return previewAuthorityFail(error, "preview activation was not issued to this request");
    return true;
}
QJsonObject faceFor(const QJsonObject &preview, int id)
{
    if (!preview.value("ids").toArray().contains(id)) return {};
    for (const auto &value : preview.value("cards").toArray()) {
        const auto face = value.toObject();
        if (face.value("effective_id").toInt(-1) == id) return face;
    }
    return {};
}
}

QString aiStrategyPreviewSkill(const AIRequest &request)
{
    if (request.kind != AIRequest::UseCard
        || (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)) return {};
    if (request.pattern == "@PlusYiji") return "PlusYiji";
    if (request.pattern == "@fatewangzhe") return "fatewangzhe";
    if (request.pattern == "@@caoduo" && request.prompt == "@xinji-use") return "caoduo";
    return {};
}

bool authorizeAIStrategyPreviewEvaluation(const AIRequest &request, Room &room, ServerPlayer *viewer,
    int cardId, QJsonObject *disclosedFace, QString *error)
{
    QJsonObject preview;
    if (!base(request, room, viewer, preview, error)) return false;
    const auto face = faceFor(preview, cardId);
    if (cardId < 0 || face.isEmpty()) return previewAuthorityFail(error, "card is outside the issued preview");
    // Caoduo source evaluates EVERY disclosed card before checking the flag.
    // Therefore this readonly admission deliberately does not test eligibility.
    if (disclosedFace) *disclosedFace = face;
    return true;
}

bool authorizeAIStrategyPreviewPayment(const AIRequest &request, Room &room, ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    const QList<int> &costs, QJsonArray *disclosedFaces, QString *error)
{
    QJsonObject preview;
    if (!base(request, room, viewer, preview, error)
        || !identity(request, room, viewer, activation, source, error)) return false;
    const QString skill = aiStrategyPreviewSkill(request);
    if (costs.isEmpty() || costs.size() > 2 || (skill != "PlusYiji" && costs.size() != 1))
        return previewAuthorityFail(error, "preview payment count differs from the fixed native skill");
    QSet<int> seen; QJsonArray faces;
    for (int id : costs) {
        const auto face = faceFor(preview, id);
        if (id < 0 || seen.contains(id) || face.isEmpty())
            return previewAuthorityFail(error, "preview payment is hidden or duplicate");
        const Card *card = room.getCard(id);
        if (!card || card->isVirtualCard()
            || card->getEffectiveId() != id || !card->hasFlag(skill))
            return previewAuthorityFail(error, "preview payment is hidden, duplicate, or no longer flagged");
        seen.insert(id); faces.append(face);
    }
    if (disclosedFaces) *disclosedFaces = faces;
    return true;
}

bool authorizeAIStrategyPreviewPhysicalProduct(const AIRequest &request, Room &room, ServerPlayer *viewer,
    const SkillInstanceRef &activation, const SkillInstanceRef &source,
    int id, QJsonObject *disclosedFace, QString *error)
{
    if (aiStrategyPreviewSkill(request) != "caoduo")
        return previewAuthorityFail(error, "only the exact Caoduo source submits a borrowed physical product");
    QJsonArray faces;
    if (!authorizeAIStrategyPreviewPayment(request, room, viewer, activation, source, {id}, &faces, error)) return false;
    const auto *owner = room.getCardOwner(id);
    if (!owner || owner == viewer || owner->getRoom() != &room || room.getCardPlace(id) != Player::PlaceHand)
        return previewAuthorityFail(error, "Caoduo material is no longer in the disclosed foreign hand");
    if (disclosedFace) *disclosedFace = faces.first().toObject();
    return true;
}

// ---- readonly-card-store ----
#include "structs.h"
#include <QHash>
#include <QPointer>
#include <QUuid>
#include <vector>
#include <functional>

namespace {
// Canonical manager retention exposes tokens for direct Card payloads, but its
// self-leasing struct exemption does not expose nested tokens. Reject those
// opaque shapes at issuance instead of assuming leases defeat direct delete.
bool observableTagShape(const QVariant &value, int depth, int &budget)
{
    if (depth > 32 || --budget < 0) return false;
    if (!value.isValid()) return true;
    const int type = value.userType();
    if (type == QMetaType::QVariantList) {
        for (const auto &item : value.toList())
            if (!observableTagShape(item, depth + 1, budget)) return false;
        return true;
    }
    if (type == QMetaType::QVariantMap) {
        const auto map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            if (!observableTagShape(it.value(), depth + 1, budget)) return false;
        return true;
    }
    if (type == qMetaTypeId<CardTagOwner>() || type == qMetaTypeId<CardEffectStruct>()) return true;
    const char *name = QMetaType::typeName(type);
    if (name && (qstrcmp(name, "Card*") == 0 || qstrcmp(name, "const Card*") == 0)) return true;
    switch (type) {
    case QMetaType::Bool: case QMetaType::Int: case QMetaType::UInt:
    case QMetaType::LongLong: case QMetaType::ULongLong: case QMetaType::Double:
    case QMetaType::Float: case QMetaType::QChar: case QMetaType::QString:
    case QMetaType::QStringList: case QMetaType::QByteArray: return true;
    default: return false;
    }
}
}

struct AIOwnerReadonlyCardStore::State {
    struct Entry {
        Card *card = nullptr;
        QString sourceKey;
        bool ownsCard = false;
        std::vector<CardLifetimeLease> leases;
        std::vector<std::shared_ptr<const CardLifetimeToken>> tokens;
        std::vector<QPointer<Card>> objects;
        ~Entry() {
            // Keep dependencies pinned until deletion is requested; all release
            // and eventual destruction follows the existing lifetime manager.
            auto &manager = globalCardLifetimeManager();
            for (size_t i = 0; i < tokens.size(); ++i)
                if (objects[i].isNull() && tokens[i]->live)
                    manager.invalidateIfObserved(tokens[i]);
            if (ownsCard && card && !tokens.empty() && tokens.front()->live && !objects.front().isNull())
                manager.requestNativeDelete(tokens.front());
            globalCardLifetimeManager().releaseVariantTags(this);
        }
    };
    static std::shared_ptr<Entry> retainCard(const Card *);
    static const Card *liveCard(const std::shared_ptr<Entry> &entry) {
        if (!entry) return nullptr;
        for (size_t i = 0; i < entry->tokens.size(); ++i) {
            const auto &token = entry->tokens[i];
            if (entry->objects[i].isNull()) {
                if (token && token->live) globalCardLifetimeManager().invalidateIfObserved(token);
                return nullptr;
            }
            if (!token || !token->live) return nullptr;
        }
        return entry->card;
    }
    Room *room;
    const AIRequest *request;
    QString viewer;
    quint64 decision, revision;
    const void *ledger;
    QString prefix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    quint64 next = 0;
    QHash<QString, std::shared_ptr<Entry>> entries;
    bool matches(ServerPlayer *player, const AIRequest &r) const {
        return player && player->getRoom()==room && &r==request
            && player->objectName()==viewer && r.viewerObjectName==viewer
            && r.decisionId==decision && r.stateRevision==revision
            && r.conversionLedger.get()==ledger && aiRangeRequestCurrent(*room,r,player);
    }
};

AIOwnerReadonlyCardStore::AIOwnerReadonlyCardStore(Room &room, const AIRequest &r)
    : m_state(new State{&room,&r,r.viewerObjectName,r.decisionId,r.stateRevision,r.conversionLedger.get()}) {}
AIOwnerReadonlyCardStore::~AIOwnerReadonlyCardStore() = default;
void AIOwnerReadonlyCardStore::clear() { m_state->entries.clear(); }

std::shared_ptr<AIOwnerReadonlyCardStore::State::Entry>
AIOwnerReadonlyCardStore::State::retainCard(const Card *card)
{
    auto entry=std::make_shared<Entry>();
    auto &manager=globalCardLifetimeManager();
    // Traverse only this producer's already-authorized native object graph.
    // No registry lookup, arbitrary ID lookup or foreign wrapper is performed.
    QSet<const Card *> visiting, retained;
    std::function<bool(const Card *)> retain=[&](const Card *value) {
        if (!value || visiting.contains(value)) return false;
        if (retained.contains(value)) return true;
        if (retained.size()>=128) return false;
        // addChangeEdge can create a non-physical observeLive token. Always
        // upgrade the known live producer object so QObject destruction and
        // native reclamation are tracked, even if that token already exists.
        auto token=manager.observeCard(const_cast<Card *>(value));
        if (!token || !token->live) return false;
        CardLifetimeLease lease(manager,token);
        if (!lease.isValid()) return false;
        entry->leases.push_back(std::move(lease));
        entry->tokens.push_back(token);
        entry->objects.emplace_back(const_cast<Card *>(value));
        visiting.insert(value);retained.insert(value);
        // QVariant values remain entirely native; nested Card payloads need
        // their normal lifecycle pins, not serialization of private tags.
        int tagBudget = 4096;
        if (!observableTagShape(value->tag, 0, tagBudget)
            || !manager.retainVariantTag(entry.get(),QByteArray::number(reinterpret_cast<quintptr>(value)),value->tag)) return false;
        for (const Card *changed:value->getChangeCards()) if (!retain(changed)) return false;
        visiting.remove(value);return true;
    };
    if (!retain(card)) return {};
    // Walk tag edges only through the manager's canonical, already retained
    // tokens. Re-read after each frontier because a referenced Card may itself
    // carry tags; never extract raw pointers from an opaque QVariant here.
    for (;;) {
        bool added = false;
        for (const auto &token : manager.variantTagTokens(entry.get())) {
            if (!token || !token->live) return {};
            const auto *dependency = static_cast<const Card *>(token->address);
            if (!retained.contains(dependency)) {
                if (!retain(dependency)) return {};
                added = true;
            }
        }
        if (!added) break;
    }
    entry->card=const_cast<Card *>(card);
    return entry;
}

QString AIOwnerReadonlyCardStore::issue(ServerPlayer *player, const AIRequest &request,
                                      std::unique_ptr<Card> card, const QString &sourceKey)
{
    if (!m_state->matches(player,request) || !card || card->parent()
        || !card->isVirtualCard()
        || sourceKey.isEmpty() || sourceKey.size()>128 || m_state->entries.size()>=128) return {};
    auto entry=State::retainCard(card.get());
    if (!entry) return {};
    entry->sourceKey=sourceKey;
    entry->card=card.release();
    entry->ownsCard=true;
    const auto key=QStringLiteral("readonly:")+m_state->prefix+QLatin1Char(':')+QString::number(++m_state->next);
    m_state->entries.insert(key,entry);
    return key;
}

QString AIOwnerReadonlyCardStore::issueBorrowed(ServerPlayer *player, const AIRequest &request,
                                               const Card *card, const QString &sourceKey)
{
    if (!m_state->matches(player,request) || !card || sourceKey.isEmpty()
        || sourceKey.size()>128 || m_state->entries.size()>=128) return {};
    auto entry=State::retainCard(card);
    if (!entry) return {};
    entry->sourceKey=sourceKey;
    // The current gameplay owner retains deletion authority. The observation
    // only pins this exact graph through the canonical lifetime manager.
    const auto key=QStringLiteral("readonly:")+m_state->prefix+QLatin1Char(':')+QString::number(++m_state->next);
    m_state->entries.insert(key,entry);
    return key;
}

const Card *AIOwnerReadonlyCardStore::lookup(ServerPlayer *player, const AIRequest &request,
                                          const QString &key) const
{
    if (!m_state->matches(player,request)) return nullptr;
    const auto it=m_state->entries.constFind(key);
    if (it==m_state->entries.cend()) return nullptr;
    return State::liveCard(it.value());
}

QString AIOwnerReadonlyCardStore::sourceKey(ServerPlayer *player, const AIRequest &request,
                                            const QString &key) const
{
    if (!lookup(player, request, key)) return {};
    return m_state->entries.value(key)->sourceKey;
}

AIReadonlyCardLease AIOwnerReadonlyCardStore::retain(ServerPlayer *player, const AIRequest &request,
                                                   const QString &key) const
{
    if (!lookup(player, request, key)) return {};
    // Keep precisely the issued producer's native custody, not a serialized
    // reconstruction. Captured QPointers/generations are checked on every read.
    const auto entry = m_state->entries.value(key);
    return [entry]() { return State::liveCard(entry); };
}

AIReadonlyCardLease AIOwnerReadonlyCardStore::observe(const Card *card)
{
    if (!card || card->isVirtualCard()) return {};
    const auto entry=State::retainCard(card);
    if (!entry) return {};
    return [entry]() { return State::liveCard(entry); };
}

AIReadonlyCardLease AIOwnerReadonlyCardStore::own(std::unique_ptr<Card> card)
{
    if (!card || card->parent() || !card->isVirtualCard()) return {};
    const auto entry=State::retainCard(card.get());
    if (!entry) return {};
    entry->card=card.release(); entry->ownsCard=true;
    return [entry]() { return State::liveCard(entry); };
}

// ---- public-deck ----
#include "game-rng.h"
#include <QThread>
#include "room-definition-registry.h"
#include "lua-runtime.h"
namespace {
QJsonObject denied(const QString &reason) { return {{"error",reason},{"unsupported",true},{"key","rng.public_deck_order"}}; }
// This is a reached source-effect ledger, not an action receipt. It retains
// only weak pointers to issued catalogue objects; Room owns its destruction.
struct PrintedTraversal : QObject {
    explicit PrintedTraversal(Room &room):QObject(&room){}
    quint64 serial=0,decision=0,revision=0;
    QString viewer,sequence;
    QWeakPointer<AIConversionLedger> requestLedger;
    QList<QPointer<Card>> cards;
    QJsonArray faces;
    int next=0;
    bool awaitingAvailability=false,availabilityInFlight=false;
};
PrintedTraversal *traversal(Room &room,bool create)
{
    const char *key="_ai_round40_printed_traversal";
    auto *value=static_cast<PrintedTraversal *>(room.property(key).value<QObject *>());
    if(!value&&create){value=new PrintedTraversal(room);room.setProperty(key,QVariant::fromValue(static_cast<QObject *>(value)));}
    return value;
}
QJsonObject advancePrinted(Room &room,ServerPlayer *viewer,const AIRequest &request,const QJsonObject &query)
{
    const QString key=query.value("key").toString();
    const bool setting=key=="sfofl_zhengan_basic_set";
    if(query.size()!=4||query.value("kind")!="response_catalogue"||!query.value("sequence").isString()
       ||!query.value("index").isDouble())return denied("invalid printed source-effect shape");
    auto *state=traversal(room,false);
    const double index=query.value("index").toDouble();
    if(!state||state->decision!=request.decisionId||state->revision!=request.stateRevision
       ||state->viewer!=viewer->objectName()||state->requestLedger.toStrongRef()!=request.conversionLedger
       ||state->sequence!=query.value("sequence").toString()||index!=state->next+1
       ||state->next>=state->cards.size()||state->availabilityInFlight||setting==state->awaitingAvailability)
        return denied("printed source-effect sequence is stale or out of order");
    Card *card=state->cards.at(state->next);
    const auto face=state->faces.at(state->next).toObject();
    if(!card||card!=Sanguosha->getEngineCard(face.value("printed_id").toInt())
       ||card->objectName()!=face.value("name").toString()||int(card->getSuit())!=face.value("suit").toInt()
       ||card->getNumber()!=face.value("number").toInt())return denied("issued printed source identity changed");
    // Compiled BasicCards keep their actual Engine pointer. The common native
    // field accessor stores attribution in the current Room, so old aliases and
    // all same-Room native readers see it without writing shared QObject state.
    const bool shared = room.roomRuntime()->definitions().isEngineCard(card);
    if ((shared && card->getTypeId() != Card::TypeBasic)
        || (!shared && (room.roomRuntime()->engineCard(card->getId()) != card
                       || card->thread() != QThread::currentThread())))
        return denied("printed source object has no canonical Room attribution storage");
    // The catalogue query already charged one traversal. Each issued ordinal
    // permits exactly set then availability once; no unbounded query loop or
    // budget increase is introduced for a source list longer than 128 cards.
    EngineRuntimeContextScope engine(*Sanguosha,&room);
    LuaRuntime::Binding binding(room.roomRuntime()->lua(),false);
    if(setting){
        card->setSkillName(QStringLiteral("sfofl_zhengan_basic"));
        state->awaitingAvailability=true;
        return {{"known",true},{"key",key},{"sequence",state->sequence},{"index",state->next+1}};
    }
    // Exact actual printed object, after the preceding persistent setter. No
    // hypothetical clone, payment candidate, or generic arbitrary-ID query.
    if(card->getSkillName(false)!="sfofl_zhengan_basic")return denied("printed source setter prefix changed");
    const QString capturedSequence=state->sequence;
    const int capturedNext=state->next;
    const auto capturedLedger=state->requestLedger;
    QPointer<PrintedTraversal> liveState(state);
    state->availabilityInFlight=true;
    const auto unchanged=[&]{return liveState&&liveState->sequence==capturedSequence
        &&liveState->next==capturedNext&&liveState->requestLedger==capturedLedger;};
    const auto restore=qScopeGuard([&]{if(unchanged())liveState->availabilityInFlight=false;});
    const bool available=card->isAvailable(viewer);
    if(!unchanged()||state->decision!=request.decisionId||state->revision!=request.stateRevision
       ||room.roomRuntime()->stateRevision()!=request.stateRevision||state->cards.at(capturedNext)!=card
       ||card->getSkillName(false)!="sfofl_zhengan_basic")
        return denied("printed availability changed its issued traversal");
    state->awaitingAvailability=false;state->availabilityInFlight=false;++state->next;
    return {{"known",true},{"key",key},{"sequence",state->sequence},{"index",state->next},{"available",available}};
}
QJsonObject printed(const Card *card) {
    // Source Engine::getEngineCard: no Room::getCard, owner, place, live flags,
    // tags, skill chains or payment identities. These values are catalogue data.
    QJsonArray kinds;
    for (const char *kind : {"BasicCard","TrickCard","DelayedTrick","EquipCard","Slash"})
        if(card->isKindOf(kind)) kinds.append(kind);
    if(!kinds.contains(card->getClassName())) kinds.append(card->getClassName());
    return {{"printed_id",card->getId()}, {"id",-1}, {"effective_id",-1},
        {"name",card->objectName()}, {"class_name",card->getClassName()},
        {"suit",int(card->getSuit())}, {"number",card->getNumber()},
        {"type_id",int(card->getTypeId())}, {"kind_of",kinds},
        {"basic",card->isKindOf("BasicCard")}, {"nd_trick",card->isNDTrick()},
        {"damage_card",card->isDamageCard()}, {"red",card->isRed()}, {"black",card->isBlack()},
        {"evaluation_only",true}, {"subcards",QJsonArray()}};
}
}
QJsonObject resolveAIPublicDeck(Room &room,ServerPlayer *viewer,
                                      const AIRequest &request,const QJsonObject &query) {
    if(!viewer || viewer->getRoom()!=&room || request.viewerObjectName!=viewer->objectName()
       || request.worldView.self.objectName!=viewer->objectName()
       || request.stateRevision!=room.roomRuntime()->stateRevision())
        return denied("public printed deck request is stale or belongs to another viewer");
    const QString operation=query.value("key").toString();
    if(operation=="sfofl_zhengan_basic_set"||operation=="sfofl_zhengan_basic_available") {
        if(request.kind!=AIRequest::UseCard||request.pattern!="@@sfofl_zhengan"
           ||!viewer->hasSkill("sfofl_zhengan_basic",true))return denied("printed source effect outside actual question");
        return advancePrinted(room,viewer,request,query);
    }
    if(query.size()!=2 || query.value("kind")!="response_catalogue" || !query.value("key").isString())
        return denied("public printed deck query accepts only its finite source key");
    const QString key=query.value("key").toString();
    const bool basic=key==QStringLiteral("sfofl_zhengan_basic_printed");
    const bool jvewu=key==QStringLiteral("ny_tenth_jvewu_printed");
    const bool basicScope=basic && request.kind==AIRequest::UseCard
        && request.pattern==QStringLiteral("@@sfofl_zhengan")
        && viewer->hasSkill(QStringLiteral("sfofl_zhengan_basic"),true);
    const bool jvewuScope=jvewu && request.kind==AIRequest::Activate
        && request.reason==CardUseStruct::CARD_USE_REASON_PLAY
        && viewer->hasSkill(QStringLiteral("ny_tenth_jvewu"));
    if(!basicScope && !jvewuScope)return denied("public printed deck query is outside its fixed source question");
    if(jvewuScope) {
        bool rankTwo=false;
        for(const Card *card:viewer->getCards("he"))if(card->getNumber()==2){rankTwo=true;break;}
        if(!rankTwo)return denied("Jvewu source shuffle has not reached its owner material prefix");
    }
    if(!request.conversionLedger || request.conversionLedger->queryCount>=128)
        return denied("public printed deck query budget exhausted");
    ++request.conversionLedger->queryCount;
    // Both fixed callers use derivative=false. qsanShuffle consumes bounds
    // N,N-1,...,1 including bounded(1), with native rejection sampling. Calling
    // the real helper preserves filters and RNG poststate, not merely a count.
    EngineRuntimeContextScope engine(*Sanguosha,&room);
    GameRng::Binding gameplay(room.roomRuntime()->rng());
    const QList<int> ids=Sanguosha->getRandomCards(false);
    QJsonArray cards;QList<QPointer<Card>> issued;
    for(int id:ids) {
        const Card *card=Sanguosha->getEngineCard(id);
        if(!card)return denied("configured printed deck contains an absent definition after shuffle");
        // addFunction755 PatternsCard("BasicCard",true): retain every match,
        // duplicate object name and original physical face in shuffled order.
        if(!basic || card->objectName()==QStringLiteral("BasicCard") || card->isKindOf("BasicCard"))
        {
            cards.append(printed(card));
            if(basic)issued.append(const_cast<Card *>(card));
        }
    }
    QJsonObject result{{"known",true},{"complete",true},{"key",key},{"cards",cards}};
    if(basic){
        auto *state=traversal(room,true);++state->serial;
        state->decision=request.decisionId;state->revision=request.stateRevision;
        state->viewer=viewer->objectName();state->requestLedger=request.conversionLedger;
        state->sequence=QString::number(state->serial);state->cards=issued;state->faces=cards;
        state->next=0;state->awaitingAvailability=false;state->availabilityInFlight=false;result.insert("sequence",state->sequence);
    }
    return result;
}

// ---- public-disclosure ----
#include "roomthread.h"

#include <QObject>

namespace {
QObject *publicLedger(Room &room, bool create)
{
    auto *ledger = room.findChild<QObject *>(QStringLiteral("isolated-ai-public-move-ledger"),
                                          Qt::FindDirectChildrenOnly);
    if (!ledger && create) {
        ledger = new QObject(&room);
        ledger->setObjectName(QStringLiteral("isolated-ai-public-move-ledger"));
    }
    return ledger;
}

bool publicDestination(Player::Place place)
{
    return place == Player::PlaceTable || place == Player::DiscardPile
        || place == Player::PlaceEquip || place == Player::PlaceJudge;
}
QJsonObject publicFace(const Card *card, int id)
{
    return QJsonObject{{"id", id}, {"effective_id", id},
                {"name", card->objectName()}, {"class_name", card->getClassName()},
                {"engine_class_name", QString::fromLatin1(card->metaObject()->className())},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"subtype", card->getSubtype()}, {"suit", int(card->getSuit())},
                {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
                {"red", card->isRed()}, {"black", card->isBlack()},
                {"virtual_card", false}, {"subcards", QJsonArray()},
                {"legacy_string_key", QString::number(id)}, {"legacy_sort_key", QString::number(id)}};
}

}

void recordAIStrategyPublicCoupleLog(Room &room, const LogMessage &log)
{
    if (!dynamic_cast<const CoupleScenario *>(room.getScenario()) || !log.from
        || log.from->getRoom() != &room || log.to.size() != 1 || !log.to.first()
        || log.to.first()->getRoom() != &room || log.from == log.to.first()
        || (log.type != "#Marry" && log.type != "#Divorse")) return;
    auto *ledger = publicLedger(room, true);
    auto spouses = ledger->property("public_spouses").toJsonObject();
    const QString from = log.from->objectName(), to = log.to.first()->objectName();
    spouses.insert(from, log.type == "#Marry" ? QJsonValue(to) : QJsonValue(false));
    spouses.insert(to, log.type == "#Marry" ? QJsonValue(from) : QJsonValue(false));
    ledger->setProperty("public_spouses", spouses);
}

void recordAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player, const QString &operation)
{
    if (!player || player->getRoom() != &room || (operation != "KaiyuanShengshi"
        && operation != "-KaiyuanShengshi" && operation != ".")) return;
    auto *ledger = publicLedger(room, true);
    auto flags = ledger->property("public_kaiyuan_flags").toJsonObject();
    const bool observed = operation == "KaiyuanShengshi";
    if (player->hasFlag("KaiyuanShengshi") == observed) flags.insert(player->objectName(), observed);
    else flags.insert(player->objectName(), QJsonValue());
    ledger->setProperty("public_kaiyuan_flags", flags);
}

QJsonValue readAIStrategyPublicKaiyuanFlag(Room &room, ServerPlayer *player)
{
    if (!player || player->getRoom() != &room) return QJsonValue();
    const auto *ledger = publicLedger(room, false);
    const auto flags = ledger ? ledger->property("public_kaiyuan_flags").toJsonObject() : QJsonObject();
    const auto value = flags.contains(player->objectName()) ? flags.value(player->objectName()) : QJsonValue(false);
    if (!value.isBool() || player->hasFlag("KaiyuanShengshi") != value.toBool()) return QJsonValue();
    return value;
}

QJsonObject readAIStrategyPublicSpouse(Room &room, ServerPlayer *viewer)
{
    const QJsonObject unknown{{"known", false}};
    const auto *scenario = dynamic_cast<const CoupleScenario *>(room.getScenario());
    if (!scenario || !viewer || viewer->getRoom() != &room) return unknown;
    const auto *ledger = publicLedger(room, false);
    const auto spouses = ledger ? ledger->property("public_spouses").toJsonObject() : QJsonObject();
    // Scenario construction starts without marriages. An unwitnessed positive
    // tag never establishes a relationship, including during nested callbacks.
    const QJsonValue observed = spouses.value(viewer->objectName()).isUndefined()
        ? QJsonValue(false) : spouses.value(viewer->objectName());
    const auto *current = scenario->getSpouse(viewer);
    if (current ? !observed.isString() || observed.toString() != current->objectName()
                : observed != QJsonValue(false)) return unknown;
    return {{"known", true}, {"spouse", observed}};
}

void invalidateAIStrategyPublicDisclosure(Room &room, int cardId)
{
    auto *ledger = publicLedger(room, false);
    if (!ledger) return;
    auto rows = ledger->property("cards").toJsonObject();
    rows.remove(QString::number(cardId));
    ledger->setProperty("cards", rows);
}

void recordAIStrategyPublicMoves(Room &room, const QList<CardsMoveStruct> &moves,
                                bool publiclyVisible)
{
    // Room::notifyMoveCards sets recipient.open = visible || isRelevant(player).
    // move.open is therefore NOT an all-viewer disclosure proof. The argument
    // here must be the real committed movement's explicit visible argument.
    if (!publiclyVisible) return;
    auto *ledger = publicLedger(room, true);
    auto rows = ledger->property("cards").toJsonObject();
    const QString revision = QString::number(room.roomRuntime()->stateRevision());
    QJsonArray audience;
    for (const auto *player : room.getAllPlayers(true)) audience.append(player->objectName());
    for (const auto &move : moves) {
        if (!publicDestination(move.to_place)) continue;
        for (int id : move.card_ids) {
            // Confirm the completed destination after nested install callbacks.
            // In particular, an unknown mapping defaults to Table in the native
            // index; only a genuine completed move may reach this code.
            if (id < 0 || room.getCardPlace(id) != move.to_place
                || room.getCardOwner(id) != move.to) continue;
            const Card *card = room.getCard(id);
            if (!card || card->isVirtualCard() || card->getEffectiveId() != id) continue;
            // Public face primitives only. No flags, tags, skill/source IDs,
            // userString, availability, private limitations or action ticket.
            const QJsonObject face = publicFace(card, id);
            auto watched = ledger->property("watched").toJsonObject();
            const QString key = QString::number(id);
            if (!watched.contains(key)) {
                if (const auto *wrapped = qobject_cast<const WrappedCard *>(card)) {
                    QObject::connect(wrapped, &WrappedCard::identityChanged, ledger,
                        [&room, id]() { invalidateAIStrategyPublicDisclosure(room, id); },
                        Qt::DirectConnection);
                    watched.insert(key, true);
                    ledger->setProperty("watched", watched);
                }
            }
            rows.insert(QString::number(id), QJsonObject{{"known", true}, {"card", face},
                {"place", int(move.to_place)}, {"owner", move.to ? move.to->objectName() : QString()},
                {"revision", revision}, {"provenance", "completed_public_card_move"},
                {"audience", audience}});
        }
    }
    ledger->setProperty("cards", rows);
}

QJsonObject readAIStrategyPublicDisclosure(Room &room, ServerPlayer *viewer, int cardId)
{
    const QJsonObject unknown{{"known", false}};
    if (!viewer || viewer->getRoom() != &room || cardId < 0) return unknown;
    auto *ledger = publicLedger(room, false);
    if (!ledger) return unknown;
    auto row = ledger->property("cards").toJsonObject().value(QString::number(cardId)).toObject();
    if (!row.value("known").toBool()
        || !row.value("audience").toArray().contains(viewer->objectName())) return unknown;
    // A real witness must precede all location/face reads. Mapping writes
    // invalidate it synchronously, so private moves cannot expose their new
    // destination or wrapper. Unrelated tag/revision updates do not lose an
    // already public face (notifyMoveToPile itself updates a tag).
    const auto stale = [&]() { invalidateAIStrategyPublicDisclosure(room, cardId); return unknown; };
    const auto place = Player::Place(row.value("place").toInt(-1));
    if (!publicDestination(place) || room.getCardPlace(cardId) != place) return stale();
    const auto *owner = room.getCardOwner(cardId);
    if ((owner ? owner->objectName() : QString()) != row.value("owner").toString()) return stale();
    const Card *card = room.getCard(cardId);
    if (!card || card->isVirtualCard() || card->getEffectiveId() != cardId
        || publicFace(card, cardId) != row.value("card").toObject()) return stale();
    row.remove("audience");
    return row;
}

// ---- public-action-flags ----
namespace {
const QStringList &auditedNames()
{
    static const QStringList names{"Qinggang", "SlashIgnoreArmor", "nosjiefan-slash"};
    return names;
}

struct FlagWitness {
    bool value = false;
    QSet<QString> audience;
};

class PublicActionFlagsLedger final : public QObject {
public:
    explicit PublicActionFlagsLedger(Room &room) : QObject(&room) {}
    QHash<const Card *, QHash<QString, FlagWitness>> cards;
    QSet<const Card *> watched;
};

PublicActionFlagsLedger *ledgerFor(Room &room, bool create)
{
    const QString key = QStringLiteral("isolated-ai-public-action-flag-ledger");
    auto *object = room.findChild<QObject *>(key, Qt::FindDirectChildrenOnly);
    if (!object && create) {
        object = new PublicActionFlagsLedger(room);
        object->setObjectName(key);
    }
    return static_cast<PublicActionFlagsLedger *>(object);
}

bool isActualPhysicalCard(Room &room, const Card *card)
{
    return card && !card->isVirtualCard() && card->getId() >= 0
        && card->getEffectiveId() == card->getId()
        && room.getCard(card->getId()) == card;
}
}

void recordAIStrategyPublicCardFlag(Room &room, const Card *card,
                                   const QString &operation)
{
    if (!isActualPhysicalCard(room, card)) return;
    const bool clearAll = operation == QStringLiteral(".");
    const bool remove = operation.startsWith('-');
    const QString name = remove ? operation.mid(1) : operation;
    if (!clearAll && !auditedNames().contains(name)) return;
    auto *ledger = ledgerFor(room, true);
    if (!ledger->watched.contains(card)) {
        ledger->watched.insert(card);
        QObject::connect(card, &QObject::destroyed, ledger, [ledger, card]() {
            ledger->cards.remove(card);
            ledger->watched.remove(card);
        }, Qt::DirectConnection);
        if (const auto *wrapped = qobject_cast<const WrappedCard *>(card)) {
            QObject::connect(wrapped, &WrappedCard::identityChanged, ledger,
                [ledger, card]() { ledger->cards.remove(card); }, Qt::DirectConnection);
        }
    }
    QSet<QString> audience;
    for (const ServerPlayer *player : room.getAllPlayers(true))
        audience.insert(player->objectName());
    auto &witnesses = ledger->cards[card];
    const QStringList names = clearAll ? auditedNames() : QStringList{name};
    for (const QString &flag : names) {
        const bool value = !clearAll && !remove;
        // A callback during notification could have changed the flag. Such a
        // mismatch never reveals its new value and cannot establish a witness.
        if (card->hasFlag(flag) != value) { witnesses.remove(flag); continue; }
        witnesses.insert(flag, FlagWitness{value, audience});
    }
}

QJsonObject readAIStrategyPublicCardFlags(Room &room, ServerPlayer *viewer,
                                        const Card *card)
{
    QJsonObject result;
    if (!viewer || viewer->getRoom() != &room || !card) return result;
    auto *ledger = ledgerFor(room, false);
    if (!ledger) return result;
    auto found = ledger->cards.find(card);
    if (found == ledger->cards.end()) return result;
    // There must be a prior public witness before checking the live wrapper.
    // No ID lookup, flag enumeration, face or private value leaves this method.
    if (!isActualPhysicalCard(room, card)) { ledger->cards.erase(found); return result; }
    auto &witnesses = found.value();
    for (auto it = witnesses.begin(); it != witnesses.end();) {
        if (!it.value().audience.contains(viewer->objectName())) { ++it; continue; }
        if (card->hasFlag(it.key()) != it.value().value) {
            it = witnesses.erase(it);
            continue;
        }
        result.insert(it.key(), it.value().value);
        ++it;
    }
    return result;
}

// ---- flag-lifecycle ----
#include <QJsonObject>

namespace {
// Room ownership avoids process-global state, address reuse and viewer lifetime
// dependencies. No Lua is entered by the mutation hook.
class FlagClearLedger final : public QObject {
public:
    explicit FlagClearLedger(Room &room) : QObject(&room) {}
    qint64 sequence = 0;
    QHash<QString, qint64> latest;
    QHash<QString, qint64> playLatest;
    QHash<QString, qint64> turnLatest;
    QHash<QString, qint64> allMarkLatest;
    QHash<QString, qint64> lolWEmptyLatest;
};
FlagClearLedger *ledger(Room &room, bool create)
{
    for (QObject *child : room.children())
        if (auto *value = dynamic_cast<FlagClearLedger *>(child)) return value;
    return create ? new FlagClearLedger(room) : nullptr;
}
bool belongsToRoom(const Room &room, const ServerPlayer *player)
{
    if (!player || player->getRoom() != &room) return false;
    return room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(player));
}
}

void recordAIStrategyFlagsCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    // QJson's number and both Lua builds represent these integers exactly.
    // Exhaustion is impossible in a normal Room; never silently wrap an epoch.
    constexpr qint64 maxExactInteger = 9007199254740991LL;
    if (value->sequence == maxExactInteger)
        qFatal("isolated strategy flag-clear epoch exhausted");
    value->latest.insert(player->objectName(), ++value->sequence);
    // Player::clearFlags() emits no property change when the real set is empty.
    // That must still invalidate a decision made before the shadow-state clear.
    if (room.roomRuntime())
        room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyPlayMarksCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated play-mark epoch exhausted");
    value->playLatest.insert(player->objectName(), ++value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyTurnMarksCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated turn-mark epoch exhausted");
    value->turnLatest.insert(player->objectName(), ++value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyAllMarksCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated all-mark epoch exhausted");
    value->allMarkLatest.insert(player->objectName(), ++value->sequence);
    value->playLatest.insert(player->objectName(), value->sequence);
    value->turnLatest.insert(player->objectName(), value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void recordAIStrategyLolWEmptyCleared(Room &room, const ServerPlayer *player)
{
    if (!belongsToRoom(room, player)) return;
    auto *value = ledger(room, true);
    if (value->sequence == 9007199254740991LL) qFatal("isolated named-flag epoch exhausted");
    value->lolWEmptyLatest.insert(player->objectName(), ++value->sequence);
    if (room.roomRuntime()) room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
}

void projectAIStrategyFlagLifecycle(AIWorldView &world, Room &room)
{
    QJsonObject epochs;
    const auto *value = ledger(room, false);
    // Only the answering viewer's clear history is disclosed, never another
    // player's private flag value or clear history.
    world.strategyFacts.insert("lol2015", QJsonObject{{"w_empty_clear_epoch",
        value ? value->lolWEmptyLatest.value(world.self.objectName, 0) : qint64(0)}});
    for (const auto *player : room.getAllPlayers(true))
        epochs.insert(player->objectName(), value ? value->latest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_flag_clear_epochs"), epochs);
    QJsonObject playEpochs;
    for (const auto *player : room.getAllPlayers(true))
        playEpochs.insert(player->objectName(), value ? value->playLatest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_play_clear_epochs"), playEpochs);
    QJsonObject turnEpochs;
    for (const auto *player : room.getAllPlayers(true))
        turnEpochs.insert(player->objectName(), value ? value->turnLatest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_turn_clear_epochs"), turnEpochs);
    QJsonObject allEpochs;
    for (const auto *player : room.getAllPlayers(true))
        allEpochs.insert(player->objectName(), value ? value->allMarkLatest.value(player->objectName(), 0) : qint64(0));
    world.strategyFacts.insert(QStringLiteral("strategy_all_mark_clear_epochs"), allEpochs);
}

// ---- random ----
#include "ai-runtime.h"

#include "standard.h"


namespace {

class RandomStrategyState final : public QObject {
public:
    explicit RandomStrategyState(Room &room) : QObject(&room)
    { setObjectName(QStringLiteral("isolated_ai_random_strategy_state")); }
    quint64 generation = 0;
    QMap<QString, QSet<int>> lastCardChosenDisabled;
};

RandomStrategyState &randomState(Room &room)
{
    auto *state = dynamic_cast<RandomStrategyState *>(room.findChild<QObject *>(
        QStringLiteral("isolated_ai_random_strategy_state"), Qt::FindDirectChildrenOnly));
    if (!state) state = new RandomStrategyState(room);
    const quint64 generation = room.roomRuntime()->ai().lua().generation();
    if (state->generation != generation) {
        state->generation = generation;
        state->lastCardChosenDisabled.clear();
    }
    return *state;
}

bool requestTarget(Room &room, ServerPlayer *viewer, const AIRequest &request,
                   ServerPlayer *&target, QString &flags)
{
    if (!viewer || viewer->getRoom() != &room || request.kind != AIRequest::CardChosen
        || request.viewerObjectName != viewer->objectName()
        || request.choiceOptions.playerNames.size() != 1
        || request.choiceOptions.choices.size() != 1
        || request.stateRevision != room.roomRuntime()->stateRevision()) return false;
    target = room.findPlayerByObjectName(request.choiceOptions.playerNames.first(), true);
    flags = request.choiceOptions.choices.first();
    if (!target || flags.isEmpty()) return false;
    for (QChar zone : flags) if (zone != QLatin1Char('h') && zone != QLatin1Char('e')
        && zone != QLatin1Char('j')) return false;
    const QJsonObject &context = request.choiceOptions.context;
    if (context.value("who").toString() != target->objectName()
        || context.value("flags").toString() != flags
        || !context.value("method").isDouble()
        || context.value("method").toDouble() != int(request.handlingMethod)) return false;
    return true;
}

QList<int> eligibleCards(ServerPlayer *viewer, ServerPlayer *target,
                         const QString &flags, Card::HandlingMethod method)
{
    QSet<int> disabled;
    for (const QVariant &value : viewer->getTag("cardChosenForAI").toList())
        disabled.insert(value.toInt());
    QList<int> ids;
    // Match getCardRandomly's exact native getCards order. In particular, do
    // not sort hidden IDs or move public cards ahead of hidden hand members.
    for (const Card *card : target->getCards(flags)) {
        if (!card) continue;
        const int id = card->getEffectiveId();
        if (id < 0 || disabled.contains(id)) continue;
        if (method == Card::MethodDiscard && !viewer->canDiscard(target, id)) continue;
        ids << id;
    }
    return ids;
}

bool originalRandomQuestion(const AIRequest &request)
{
    const auto hegemony=request.worldView.strategyFacts.value("hegemony").toObject();
    return request.kind==AIRequest::CardChosen
        && request.choiceOptions.question==QStringLiteral("askForCardChosen")
        && hegemony.value("enabled")==QJsonValue(true)
        && hegemony.value("original_overrides_loaded")==QJsonValue(true);
}
QList<int> originalFullCards(ServerPlayer *target,const QString &flags)
{
    QList<int> ids;
    for(const Card *card:target->getCards(flags)) {
        if(!card || card->getEffectiveId()<0)return {};
        ids << card->getEffectiveId();
    }
    return ids;
}

bool isSubset(const QString &zones, const QString &flags)
{
    if (zones.isEmpty()) return false;
    for (QChar zone : zones) if (!flags.contains(zone)) return false;
    return true;
}

bool planningKind(AIRequest::DecisionKind kind)
{
    return kind == AIRequest::Activate || kind == AIRequest::UseCard
        || kind == AIRequest::AmazingGrace || kind == AIRequest::RespondCard;
}

bool selectedHandSample(const AIRequest &request)
{
    return request.kind == AIRequest::CardChosen
        && request.choiceOptions.question == QStringLiteral("askForCardChosen")
        && request.choiceOptions.reason == QStringLiteral("yj_tuixinzhifu")
        && request.choiceOptions.choices.size() == 1
        && request.choiceOptions.choices.first().contains(QLatin1Char('h'));
}

bool sourceFirstXuanfeng(const AIRequest &request, const QString &flags)
{
    return request.kind == AIRequest::CardChosen
        && request.choiceOptions.question == QStringLiteral("askForCardChosen")
        && request.choiceOptions.reason == QStringLiteral("xuanfeng")
        && flags == QStringLiteral("he")
        && request.handlingMethod == Card::MethodDiscard;
}

bool sourceFirstLiwei(const AIRequest &request, const QString &flags,
                      const ServerPlayer *target)
{
    // kexianbao.lua:2506 omits the optional method: the native default is
    // MethodNone, not MethodDiscard. Only the source's face-free small-hand
    // branch is covered; three or more cards would inspect a private flag.
    return request.kind == AIRequest::CardChosen
        && request.choiceOptions.question == QStringLiteral("askForCardChosen")
        && request.choiceOptions.reason == QStringLiteral("kejiexianliwei")
        && flags == QStringLiteral("he")
        && request.handlingMethod == Card::MethodNone
        && target && target->getEquips().isEmpty()
        && target->getHandcardNum() >= 1 && target->getHandcardNum() <= 2;
}

QList<int> fullHand(ServerPlayer *target)
{
    QList<int> ids;
    for (const Card *card : target->getHandcards()) {
        if (!card || card->getEffectiveId() < 0) return {};
        ids << card->getEffectiveId();
    }
    return ids;
}

} // namespace

void projectAIStrategyTuixinzhifuThirdPassRequest(AIRequest &request, Room &room,
                                                ServerPlayer *viewer)
{
    request.choiceOptions.context.remove("yongjian_third_pass");
    ServerPlayer *target = nullptr;
    QString flags;
    if (!requestTarget(room, viewer, request, target, flags)
        || request.choiceOptions.question != QStringLiteral("askForCardChosen")
        || request.choiceOptions.reason != QStringLiteral("yj_tuixinzhifu")
        || flags != QStringLiteral("hej") || target == viewer) return;
    QJsonObject domain{{"known", false}, {"target", target->objectName()}};
    request.choiceOptions.context.insert("yongjian_third_pass", domain);
    // Do not infer disclosure from a live flag or from a native card's face.
    // Only identities whose descriptors were already authorized in this request
    // may participate. A partial hand must not produce a partial ordered list.
    const AIPlayerView *view = nullptr;
    for (const AIPlayerView &candidate : request.worldView.players)
        if (candidate.objectName == target->objectName()) { view = &candidate; break; }
    if (!view) return;
    QSet<int> disclosed;
    for (const auto *rows : {&view->knownCards, &view->equips, &view->judgingArea})
        for (const AICardView &card : *rows)
            if (card.effectiveId >= 0) disclosed.insert(card.effectiveId);
    QJsonArray ordered;
    // Exact ServerPlayer::getCards sequence h, e, j. Unlike the offered domain,
    // source getCardRandomly(no_dis=nil) does not filter canDiscard. Disabled
    // IDs are filtered lazily by Lua against its original remembered cache.
    for (const Card *card : target->getCards(QStringLiteral("hej"))) {
        if (!card || card->getEffectiveId() < 0 || !disclosed.contains(card->getEffectiveId())) return;
        ordered.append(card->getEffectiveId());
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) return;
    domain.insert("known", true);
    domain.insert("ordered_card_ids", ordered);
    request.choiceOptions.context.insert("yongjian_third_pass", domain);
}

void projectAIStrategyRandomRequest(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (request.kind != AIRequest::CardChosen) return;
    request.randomCardChoices.clear();
    request.choiceOptions.context.remove("random_card_choices");
    projectAIStrategyTuixinzhifuThirdPassRequest(request, room, viewer);
    ServerPlayer *target = nullptr;
    QString flags;
    if (!requestTarget(room, viewer, request, target, flags) || target == viewer) return;
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    QJsonObject choices;
    // The donor asks for "h" inside hand-priority branches and the original
    // flags in its final fallback. Other subsets are never authorized here.
    QStringList zones{flags};
    if (flags.contains(QLatin1Char('h')) && flags != QStringLiteral("h")) zones << QStringLiteral("h");
    for (const QString &zone : zones) {
        const QList<int> ids = eligibleCards(viewer, target, zone, request.handlingMethod);
        // UUID uses no Room decision RNG, preserving the donor's lazy draw.
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.randomCardChoices.insert(token, ids);
        choices.insert(zone, QJsonObject{{"token", token}, {"count", ids.size()}});
    }
    // Fixed Original heg-common1814-1829 samples the FULL native order,
    // including disabled cards. No identity or face crosses this boundary.
    if(originalRandomQuestion(request))for(const QString &zone:zones) {
        const auto ids=originalFullCards(target,zone);
        const QString token=QStringLiteral("isolated-original-random:")
            +QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.randomCardChoices.insert(token,ids);
        choices.insert(QStringLiteral("original:")+zone,QJsonObject{
            {"token",token},{"count",ids.size()},{"original_full_domain",true}});
    }
    // Xuanfeng's source callback selects hand[1], not getCardRandomly. Retain
    // the original full hand order, including a disabled/illegal first card;
    // the final CardChosen authority gate must reject that choice if needed.
    // The domain discloses no card IDs, faces or private first-card flags.
    if (sourceFirstXuanfeng(request, flags)) {
        const QList<int> ids = fullHand(target);
        if (ids.size() == target->getHandcardNum()) {
            const QString token = QStringLiteral("isolated-xuanfeng-first:")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.randomCardChoices.insert(token, ids);
            choices.insert(QStringLiteral("xuanfeng_first_hand"), QJsonObject{
                {"token", token}, {"count", ids.size()}, {"source_first", true}});
        }
    }
    if (sourceFirstLiwei(request, flags, target)) {
        const QList<int> ids = fullHand(target);
        if (ids.size() == target->getHandcardNum()) {
            const QString token = QStringLiteral("isolated-kejiexianliwei-first:")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
            request.randomCardChoices.insert(token, ids);
            choices.insert(QStringLiteral("kejiexianliwei_first_hand"), QJsonObject{
                {"token", token}, {"count", ids.size()}, {"source_first", true}});
        }
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) {
        request.randomCardChoices.clear();
        return;
    }
    request.choiceOptions.context.insert("random_card_choices", choices);
    if (selectedHandSample(request)) projectAIStrategyRandomPlanningRequest(request, room, viewer);
}

bool resolveAIStrategyRandomChoice(Room &room, ServerPlayer *viewer,
                                   const AIRequest &request, AIResult &result)
{
    const QJsonObject choice = result.action.randomCardChoice;
    if (choice.isEmpty()) return true;
    ServerPlayer *target = nullptr;
    QString flags;
    if (!requestTarget(room, viewer, request, target, flags) || target == viewer
        || !result.handled || !result.errorCode.isEmpty() || result.kind != AIResult::Answer
        || result.decisionId != request.decisionId || result.stateRevision != request.stateRevision
        || result.action.randomCardChoiceResolved
        || !result.action.selectedCardIds.isEmpty() || !result.action.bottomCardIds.isEmpty()
        || !result.action.selectedTargetNames.isEmpty() || !result.action.userString.isEmpty()
        || !result.action.legacyCardString.isEmpty() || result.action.hasCardSpec
        || result.action.hasSkillActionContext || result.action.useCardId != -1
        || result.action.candidateId != -1 || choice.size() != 3) return false;
    const QJsonValue tokenValue = choice.value("token");
    const QJsonValue indexValue = choice.value("index");
    const QJsonValue avoidValue = choice.value("avoid_wounded_enemy_silver_lion");
    if (!tokenValue.isString() || !indexValue.isDouble() || !avoidValue.isBool()) return false;
    const QString token = tokenValue.toString();
    if(token.startsWith(QStringLiteral("isolated-original-sd-selected:")))return resolveAIOriginalSD(room,viewer,request,result);
    if (token.startsWith(QStringLiteral("isolated-hand-selected:"))) {
        if (!selectedHandSample(request) || indexValue.toDouble() != 1 || avoidValue.toBool()
            || !request.conversionLedger) return false;
        auto it = request.conversionLedger->handSampleReceipts.find(token);
        if (it == request.conversionLedger->handSampleReceipts.end()) return false;
        auto &receipt = it.value();
        if (receipt.consumed || receipt.decisionId != request.decisionId
            || receipt.revision != request.stateRevision || receipt.viewer != viewer->objectName()
            || receipt.target != target->objectName() || fullHand(target) != receipt.domain
            || !receipt.domain.contains(receipt.selectedId)) return false;
        for (const QVariant &value : viewer->getTag("cardChosenForAI").toList())
            if (value.toInt() == receipt.selectedId) return false;
        if (request.handlingMethod == Card::MethodDiscard && !viewer->canDiscard(target, receipt.selectedId)) return false;
        receipt.consumed = true;
        result.action.selectedCardIds << receipt.selectedId;
        result.action.randomCardChoiceResolved = true;
        return true;
    }
    const auto authorized = request.randomCardChoices.constFind(token);
    if (token.isEmpty() || authorized == request.randomCardChoices.cend()) return false;
    const QList<int> &ids = authorized.value();
    const double ordinal = indexValue.toDouble();
    if (!std::isfinite(ordinal) || ordinal != std::floor(ordinal)
        || ordinal < 1 || ordinal > ids.size()) return false;
    const QJsonObject projected = request.choiceOptions.context.value("random_card_choices").toObject();
    QString zones;
    bool sourceFirst = false;
    bool originalFull = false;
    for (auto it = projected.constBegin(); it != projected.constEnd(); ++it) {
        const QJsonObject row = it.value().toObject();
        if (row.value("token").toString() == token) {
            if (!zones.isEmpty() || row.value("count").toDouble(-1) != ids.size()) return false;
            zones = it.key();
            originalFull=zones.startsWith(QStringLiteral("original:"));
            if(originalFull) {
                if(!originalRandomQuestion(request)||row.size()!=3
                    ||row.value("original_full_domain")!=QJsonValue(true)
                    ||!token.startsWith(QStringLiteral("isolated-original-random:")))return false;
                zones=zones.mid(9);
            }
            sourceFirst = zones == QStringLiteral("xuanfeng_first_hand")
                || zones == QStringLiteral("kejiexianliwei_first_hand");
            if (sourceFirst && (row.size() != 3 || row.value("source_first") != QJsonValue(true))) return false;
        }
    }
    if (sourceFirst) {
        const bool admitted = zones == QStringLiteral("xuanfeng_first_hand")
            ? sourceFirstXuanfeng(request, flags)
                && token.startsWith(QStringLiteral("isolated-xuanfeng-first:"))
            : sourceFirstLiwei(request, flags, target)
                && token.startsWith(QStringLiteral("isolated-kejiexianliwei-first:"));
        if (!admitted || ordinal != 1 || avoidValue.toBool()) return false;
    } else if (!isSubset(zones, flags)) return false;
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    // Some lock/tag changes do not advance the revision. Recheck the entire
    // ordered domain, not merely the selected ID, before using its ordinal.
    if (sourceFirst) {
        if (fullHand(target) != ids || ids.size() != target->getHandcardNum()) return false;
    } else if(originalFull) {
        if(originalFullCards(target,zones)!=ids)return false;
    } else if (eligibleCards(viewer, target, zones, request.handlingMethod) != ids) return false;
    int index = int(ordinal) - 1;
    const Card *selected = Sanguosha->getCard(ids.at(index));
    if (!selected) return false;
    // Enemy is the viewer's strategy judgment carried as a boolean, not another
    // player's hidden true role. Wound/armor prerequisites are rechecked live.
    if (avoidValue.toBool()) {
        if (!target->isWounded() || !target->hasArmorEffect(QStringLiteral("SilverLion"))) return false;
        if (originalFull ? selected==target->getArmor() : selected->isKindOf("SilverLion")) {
            if (index + 1 < ids.size()) ++index;
            else if (index > 0) --index;
        }
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) return false;
    result.action.selectedCardIds << ids.at(index);
    result.action.randomCardChoiceResolved = true;
    // Keep the already-validated opaque payload for diagnostics; the native-only
    // resolved bit is the caller's final gate, never payload presence alone.
    // the hidden ID is never appended to a serialized request candidate list.
    return true;
}

void projectAIStrategyRandomPlanningRequest(AIRequest &request, Room &room,
                                           ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || !(planningKind(request.kind) || selectedHandSample(request))
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return;
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    // Remove only this purpose's old capabilities when projection is refreshed.
    // CardChosen's independently validated token domain is not interchangeable.
    const auto previous = request.worldView.strategyFacts.value("random_hand_discard").toObject();
    for (auto it = previous.constBegin(); it != previous.constEnd(); ++it)
        request.randomCardChoices.remove(it.value().toObject().value("token").toString());
    QJsonObject projected;
    for (ServerPlayer *target : room.getAlivePlayers()) {
        if (target == viewer || (selectedHandSample(request)
            && request.choiceOptions.playerNames != QStringList{target->objectName()})) continue;
        const QList<int> ids = fullHand(target);
        if (ids.size() != target->getHandcardNum()) continue;
        const QString token = QStringLiteral("isolated-hand-discard:")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        request.randomCardChoices.insert(token, ids);
        projected.insert(target->objectName(), QJsonObject{{"token", token}, {"count", ids.size()}});
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision()) {
        for (auto it = projected.constBegin(); it != projected.constEnd(); ++it)
            request.randomCardChoices.remove(it.value().toObject().value("token").toString());
        request.worldView.strategyFacts.remove("random_hand_discard");
        return;
    }
    request.worldView.strategyFacts.insert("random_hand_discard", projected);
}

QJsonObject resolveAIStrategyRandomPlanningQuery(Room &room, ServerPlayer *viewer,
                                                const AIRequest &request,
                                                const QJsonObject &query)
{
    const auto reject = [](const char *message) {
        return QJsonObject{{"error", QString::fromLatin1(message)}, {"key", "random_hand_discard"}};
    };
    if (!viewer || viewer->getRoom() != &room || !(planningKind(request.kind) || selectedHandSample(request))
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("stale or unauthorized hand-sample request");
    if (!request.conversionLedger || ++request.conversionLedger->queryCount > 128)
        return reject("hand-sample query budget exceeded");
    if (query.size() != 3 || query.value("kind").toString() != QStringLiteral("random_hand_discard")
        || !query.value("token").isString() || !query.value("target").isString())
        return reject("invalid hand-sample query");
    const QString token = query.value("token").toString();
    const QString name = query.value("target").toString();
    const auto row = request.worldView.strategyFacts.value("random_hand_discard").toObject().value(name).toObject();
    const auto authorized = request.randomCardChoices.constFind(token);
    if (!token.startsWith(QStringLiteral("isolated-hand-discard:"))
        || row.value("token").toString() != token || authorized == request.randomCardChoices.cend()
        || !row.value("count").isDouble() || row.value("count").toDouble(-1) != authorized.value().size())
        return reject("unknown hand-sample capability");
    ServerPlayer *target = room.findPlayerByObjectName(name);
    if (!target || target == viewer || !target->isAlive()) return reject("invalid hand-sample target");
    EngineRuntimeContextScope contextScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(room.roomRuntime()->lua(), false);
    const QList<int> ids = fullHand(target);
    if (ids.size() != target->getHandcardNum() || ids != authorized.value())
        return reject("hand-sample domain changed");
    QJsonObject sample{{"kind", "hand_discard_sample"}, {"token", token},
                       {"target", name}, {"empty", ids.isEmpty()}};
    if (ids.isEmpty()) return sample;
    // Player::getRandomHandCard draws before canDiscard/disabled/poison tests,
    // including singleton and rejected-target branches. Do not prefilter IDs.
    const int id = ids.at(room.roomRuntime()->rng().bounded(ids.size()));
    sample.insert("can_discard", viewer->canDiscard(target, id));
    sample.insert("disabled", randomState(room).lastCardChosenDisabled.value(viewer->objectName()).contains(id));
    // Only identities already disclosed in this exact request can be returned.
    // Sampling never promotes an unknown hand card into the viewer's knowledge.
    for (const AIPlayerView &player : request.worldView.players) {
        if (player.objectName != name) continue;
        for (const AICardView &card : player.knownCards)
            if (card.effectiveId == id) { sample.insert("known_card_id", id); break; }
        break;
    }
    if (selectedHandSample(request)) {
        ServerPlayer *authorizedTarget = nullptr;
        QString flags;
        if (!requestTarget(room, viewer, request, authorizedTarget, flags) || authorizedTarget != target)
            return reject("selected hand-sample target is not the current question");
        const QString receiptToken = QStringLiteral("isolated-hand-selected:")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        AIHandSampleReceipt receipt;
        receipt.decisionId = request.decisionId; receipt.revision = request.stateRevision;
        receipt.viewer = viewer->objectName(); receipt.target = name;
        receipt.domain = ids; receipt.selectedId = id;
        request.conversionLedger->handSampleReceipts.insert(receiptToken, receipt);
        sample.insert("selected_choice", QJsonObject{{"token", receiptToken}, {"index", 1},
            {"avoid_wounded_enemy_silver_lion", false}});
    }
    if (request.stateRevision != room.roomRuntime()->stateRevision())
        return reject("hand-sample request changed during query");
    return sample;
}

QJsonObject refreshAIStrategyDiscardPreferences(Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room)
        return QJsonObject{{"error", "invalid discard-preference viewer"}};
    QSet<int> disabled;
    QJsonArray values;
    for (const QVariant &value : viewer->getTag("cardChosenForAI").toList()) {
        const int id = value.toInt();
        disabled.insert(id);
        values.append(id);
    }
    randomState(room).lastCardChosenDisabled.insert(viewer->objectName(), disabled);
    return QJsonObject{{"kind", "discard_preferences"}, {"disabled_ids", values}};
}

// ---- range-query-distance ----
thread_local AIOwnerDistanceScope *AIOwnerDistanceScope::s_current=nullptr;
AIOwnerDistanceScope::AIOwnerDistanceScope(const ServerPlayer *owner,int delta)
    :m_owner(owner),m_mark(owner,QStringLiteral("ExtraBfDistanceFrom-Clear"),
        owner->getMark("ExtraBfDistanceFrom-Clear")+delta),m_previous(s_current){s_current=this;}
AIOwnerDistanceScope::~AIOwnerDistanceScope(){s_current=m_previous;}
bool AIOwnerDistanceScope::active(const ServerPlayer *owner)
{for(auto *s=s_current;s;s=s->m_previous)if(!owner||owner==s->m_owner)return true;return false;}
bool validateAIOwnerDistance(Room &room,ServerPlayer *viewer,const AIRequest &r,const QString &source,int delta)
{
    if(!viewer||viewer->getRoom()!=&room||r.viewerObjectName!=viewer->objectName()
        ||r.stateRevision!=room.roomRuntime()->stateRevision()||r.kind!=AIRequest::Activate
        ||r.reason!=CardUseStruct::CARD_USE_REASON_PLAY||source!="olqimou"||!viewer->hasSkill("olqimou")
        ||delta>=0||-qint64(delta)>viewer->getHp()||AIOwnerDistanceScope::active())return false;
    const qint64 value=qint64(viewer->getMark("ExtraBfDistanceFrom-Clear"))+delta;
    return value>=std::numeric_limits<int>::min()&&value<=std::numeric_limits<int>::max();
}
QJsonObject projectAIOwnerDistance(Room &room,ServerPlayer *viewer,const QString &source,int delta)
{
    auto row=projectAIStrategyOwnerRange(room,viewer,QStringLiteral("olqimou_distance"),source);
    QJsonObject distances,fixOne;
    for(auto *p:room.getAllPlayers(true)){
        distances.insert(p->objectName(),viewer->distanceTo(p));
        fixOne.insert(p->objectName(),viewer->distanceTo(p,1));
    }
    row.insert("distance_delta",delta);row.insert("distance_to",distances);row.insert("distance_fix_one",fixOne);
    return row;
}

// ---- range-query-range ----
#include "ai-owner-range-query-scope.h"
#include <QMap>
namespace {
bool owner(const AIRequest&r,Room&room,const ServerPlayer*p){return p&&p->getRoom()==&room&&!room.roomRuntime()->isClosing()&&r.decisionId!=0&&r.viewerObjectName==p->objectName()&&r.worldView.self.objectName==p->objectName()&&aiRangeRequestCurrent(room,r,p);}
const Card* hand(const AIRequest&r,Room&room,const ServerPlayer*p,int id){
 if(id<0||room.getCardOwner(id)!=p||room.getCardPlace(id)!=Player::PlaceHand||!p->handCards().contains(id))return nullptr;
 const Card*c=Sanguosha->getCard(id);if(!c)return nullptr;
 for(const auto&v:r.worldView.handCards)if(v.effectiveId==id&&v.objectName==c->objectName()&&v.className==c->getClassName()&&v.suit==int(c->getSuit())&&v.number==c->getNumber()&&!v.legacyIdentityToken.isEmpty()&&v.legacyIdentityToken==aiLegacyCardIdentity(room,const_cast<ServerPlayer*>(p),c))return c;
 return nullptr;
}
struct Tuple {QString skill;int suit;int number;int costs;};
const QMap<QString,Tuple>&tuples(){static const QMap<QString,Tuple>v{
 {"sfofl_sitong",{"sfofl_sitong",int(Card::NoSuit),0,0}},
 {"sfofl_jiaozheng",{"sfofl_jiaozheng",int(Card::NoSuit),0,0}},
 {"sfofl_qingjin",{"sfofl_qingjin",int(Card::NoSuit),0,0}},
 {"BearingDownBorder",{"BearingDownBorder",int(Card::SuitToBeDecided),-1,0}},
 {"@sfofl_bearing_down_border",{"BearingDownBorder",int(Card::SuitToBeDecided),-1,1}},
 {"sfofl_jubing",{"_sfofl_jubing",int(Card::SuitToBeDecided),-1,0}},
 {"@@sfofl_qifeng",{"sfofl_qifeng",int(Card::NoSuit),0,0}},
 {"sfofl_juedou",{"sfofl_juedou",int(Card::NoSuit),0,0}}};return v;}
}
int aiRangeExtra(const QString&source){return source=="mouwusheng"||source=="sfofl_sitong"||source=="sfofl_qingjin"||source=="BearingDownBorder"?99:source=="#sfofl_sixi"?1:0;}
bool aiRangeConstruction(const AIRequest&r,Room&room,const ServerPlayer*p,const QJsonObject&s){
 if(!owner(r,room,p))return false;
 const QString source=AIOwnerRangeQueryScope::sourceKey(p);const auto it=tuples().constFind(source);
 if(it==tuples().cend())return false;
 static const QSet<QString>fields{"source_key","name","skill","suit","number","subcards"};
 if(s.size()!=fields.size())return false;for(auto j=s.begin();j!=s.end();++j)if(!fields.contains(j.key()))return false;
 const auto&t=it.value();
 if(s.value("source_key")!=QJsonValue("owner_range:"+source)||s.value("name")!="slash"||s.value("skill")!=t.skill||s.value("suit")!=t.suit||s.value("number")!=t.number||!s.value("subcards").isArray())return false;
 const auto ids=s.value("subcards").toArray();if(ids.size()!=t.costs)return false;
 for(const auto&v:ids){const double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max()||!hand(r,room,p,int(n)))return false;}
 return true;
}
QJsonObject writeAIRangeQinggang(const AIRequest&r,Room&room,ServerPlayer*p,const QJsonObject&s){
 const auto fail=[](const char*e){return QJsonObject{{"error",e},{"key","owner_range.Qinggang"}};};
 if(!owner(r,room,p)||r.kind!=AIRequest::UseCard||r.pattern!="@@f_yonghunSlash"||AIOwnerRangeQueryScope::sourceKey(p)!="@@f_yonghunSlash")return fail("Qinggang requires its actual owner source scope");
 if(s.size()!=3||s.value("card_flag")!="Qinggang"||!s.value("enabled").isBool()||!s.value("id").isDouble())return fail("Qinggang source fields differ");
 const double n=s.value("id").toDouble(-1);if(n<0||n!=std::floor(n)||n>std::numeric_limits<int>::max())return fail("Qinggang source ID invalid");
 const Card*c=hand(r,room,p,int(n));if(!c||!c->isKindOf("Slash")||!c->hasFlag("f_yonghun"))return fail("Qinggang source requires its current marked owner Slash");
 // Fixed Genius468-472 changes this very physical Card, not a cloned Slash.
 // Card::setFlags has no room-property signal/revision/payment side effect.
 const auto before=room.roomRuntime()->stateRevision();
 c->setFlags(s.value("enabled").toBool()?"Qinggang":"-Qinggang");
 if(room.roomRuntime()->stateRevision()!=before||!aiRangeRequestCurrent(room,r,p))return fail("Qinggang setter unexpectedly changed native revision");
 return{{"kind","owner_range_card_flag"},{"viewer",p->objectName()},{"decision_id",r.getDecisionId()},{"state_revision",r.getStateRevision()},{"id",int(n)},{"enabled",s.value("enabled")}};
}

// ---- range-query-routes ----
#include "ai-strategy/ol-ai.h"
#include "ai-strategy/overseas.h"
namespace {
struct Origin {QString file;int line;};
const QMap<QString,Origin>&origins(){static const QMap<QString,Origin> values{
        {"kejieshengzhuihun",{"@lua/ai/isolated/keshengbao-ai.lua",423}},
        {"shenghua",{"@lua/ai/isolated/legends-ai.lua",1230}},
        {"#sfofl_shuangren",{"@lua/ai/isolated/sfoflCard-ai.lua",2610}},
        {"sfofl_sitong",{"@lua/ai/isolated/sfoflCard-ai.lua",3476}},
        {"sfofl_jiaozheng",{"@lua/ai/isolated/sfoflCard-ai.lua",3568}},
        {"sfofl_qingjin",{"@lua/ai/isolated/sfoflCard-ai.lua",3610}},
        {"#sfofl_zainei",{"@lua/ai/isolated/sfoflCard-ai.lua",4111}},
        {"#sfofl_sixi",{"@lua/ai/isolated/sfoflCard-ai.lua",5786}},
        {"BearingDownBorder",{"@lua/ai/isolated/sfoflCard-ai.lua",6581}},
        {"@sfofl_bearing_down_border",{"@lua/ai/isolated/sfoflCard-ai.lua",6605}},
        {"sfofl_jubing",{"@lua/ai/isolated/sfoflCard-ai.lua",6623}},
        {"@@sfofl_qifeng",{"@lua/ai/isolated/sfoflCard-ai.lua",6720}},
        {"sfofl_juedou",{"@lua/ai/isolated/sfoflCard-ai.lua",7054}},
        {"mouwusheng",{"@lua/ai/isolated/newgenerals-ai.lua",4871}},
        {"@mouxianchou-slash",{"@lua/ai/isolated/newgenerals-ai.lua",5312}},
        {"shijun",{"@lua/ai/isolated/meizl-ai.lua",5839}},
        {"@@luazhuixi",{"@lua/ai/isolated/leo-ai.lua",97}},
        {"@@luayonglue",{"@lua/ai/isolated/leo-ai.lua",97}},
        {"@@f_yonghunSlash",{"@lua/ai/isolated/GeniusPackageGod-ai.lua",564}},
        {"@PlusZhongyong",{"@lua/ai/isolated/biaofeng-ai.lua",2329}},
        {"SixQiaoxie",{"@lua/ai/isolated/biaofeng-ai.lua",5246}},
        {"GodShijun",{"@lua/ai/isolated/goddess-ai.lua",142}},
    };return values;}
bool owned(const ServerPlayer *p,const QString &skill) {
    if(!p->hasSkill(skill))return false;
    for(const auto &instance:p->getSkillInstances())if(instance.skillName==skill)return true;
    return false;
}
}
bool aiRangeRoute(Room &room,const AIRequest &r,ServerPlayer *p,
 const QString &flag,const QString &source,lua_State *L,int closure) {
    if(!p||!p->isAlive()||!aiRangeRequestCurrent(room,r,p)
        ||!aiRangeSource(flag,source)||!L||!lua_isfunction(L,closure))return false;
    const auto it=origins().constFind(source);if(it==origins().cend())return false;
    lua_pushvalue(L,closure);lua_Debug info{};
    if(!lua_getinfo(L,">S",&info)||!info.what||QByteArray(info.what)!="Lua"
        ||!info.source||QString::fromUtf8(info.source)!=it->file||info.linedefined!=it->line)return false;
    // Bind the package-created closure to the exact serialized active request,
    // not a stale closure retained in isolated VM state from a prior question.
    bool sameRequest=false;closure=lua_absindex(L,closure);
    for(int index=1;;++index) {
        const char *name=lua_getupvalue(L,closure,index);if(!name)break;
        if(QByteArray(name)=="self"&&lua_istable(L,-1)) {
            lua_pushliteral(L,"request");lua_rawget(L,-2);
            sameRequest=lua_istable(L,-1)&&aiRangeLuaRequestCurrent(room,r,lua_topointer(L,-1));
            lua_pop(L,1);
        }
        lua_pop(L,1);
        if(sameRequest)break;
    }
    if(!sameRequest)return false;
    // The executed adapter must be the trusted facade prototype and must close
    // over the very same package closure, not an arbitrary callback paired with
    // an otherwise authentic fourth argument. Sandbox loaders/debug mutation
    // are removed before any configured script is loaded.
    if(!lua_isfunction(L,3))return false;
    lua_pushvalue(L,3);lua_Debug adapter{};
    if(!lua_getinfo(L,">S",&adapter)||!adapter.source
        ||QByteArray(adapter.source)!="@lua/ai/isolated-facades.lua"
        ||adapter.linedefined!=5451)return false;
    bool sameClosure=false,sameSelf=false;
    for(int index=1;;++index){
        const char *name=lua_getupvalue(L,3,index);if(!name)break;
        if(QByteArray(name)=="closure")sameClosure=lua_rawequal(L,-1,closure);
        if(QByteArray(name)=="self"&&lua_istable(L,-1)){
            lua_pushliteral(L,"request");lua_rawget(L,-2);
            sameSelf=lua_istable(L,-1)&&aiRangeLuaRequestCurrent(room,r,lua_topointer(L,-1));
            lua_pop(L,1);
        }
        lua_pop(L,1);
    }
    if(!sameClosure||!sameSelf)return false;
    return aiRangeQuestion(r,p,source);
}
bool aiRangeQuestion(const AIRequest &r,const ServerPlayer *p,const QString &source) {
    if(!p||!p->isAlive()||!p->getRoom())return false;
    Room &room=*p->getRoom();
    const bool nested=r.worldView.strategyFacts.contains("readonly_play_context")
        &&(aiOlClanPlanningQuestion(r,room,p)||aiXiafengChoicePlanningQuestion(r,room,p)
            ||aiNiwoShelieChoicePlanningQuestion(r,room,p));
    const bool play=(r.kind==AIRequest::Activate&&r.reason==CardUseStruct::CARD_USE_REASON_PLAY)||nested;
    static const QMap<QString,QString> playSkills{{"kejieshengzhuihun","kejieshengzhuihun"},
        {"shenghua","shenghua"},{"#sfofl_shuangren","sfofl_shuangren"},{"#sfofl_zainei","sfofl_zainei"},
        {"#sfofl_sixi","sfofl_sixi"},{"shijun","shijun"},{"GodShijun","GodShijun"}};
    if(playSkills.contains(source))return play&&owned(p,playSkills.value(source));
    // A printed-card planner is admitted by its actual immutable body, not by
    // inventing a skill named BearingDownBorder or granting a payment ticket.
    if(source=="BearingDownBorder")return play;
    static const QSet<QString> invoke{"sfofl_sitong","sfofl_qingjin","sfofl_jubing","sfofl_juedou"};
    if(invoke.contains(source))return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==source&&owned(p,source);
    // SixQiaoxie asks the current turn player, NOT the skill owner (biaofeng12865).
    if(source=="SixQiaoxie")return r.kind==AIRequest::SkillInvoke&&r.choiceOptions.reason==source;
    if(source=="mouwusheng")return r.kind==AIRequest::PlayerChosen&&r.choiceOptions.reason==source&&owned(p,source);
    // Native recipient inquiries deliberately require no recipient skill.
    if(source=="sfofl_jiaozheng")return r.kind==AIRequest::PlayersChosen&&r.choiceOptions.reason==source;
    if(source=="@mouxianchou-slash")return r.kind==AIRequest::RespondCard&&r.pattern==".."&&r.prompt.split(':').first()==source;
    if(source=="@sfofl_bearing_down_border")return r.kind==AIRequest::RespondCard&&r.pattern=="."&&r.prompt.split(':').first()==source;
    if(source=="@@f_yonghunSlash")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"f_yonghun");
    if(source=="@@sfofl_qifeng")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"sfofl_qifeng");
    if(source=="@@luazhuixi")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"luazhuixi");
    if(source=="@@luayonglue")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"luayonglue");
    if(source=="@PlusZhongyong")return r.kind==AIRequest::UseCard&&r.pattern==source&&owned(p,"PlusZhongyong");
    return false;
}

// ---- range-query ----
bool validateAIStrategyOwnerRange(Room &room, ServerPlayer *viewer, const AIRequest &request,
                                 const QString &flag, const QString &sourceKey,
                                 AIOwnerRangeQueryScope::Flag &kind)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || !aiRangeRequestCurrent(room,request,viewer)) return false;
    if (sourceKey == "ov_xiafeng") {
        if (flag != "InfinityAttackRange" || !aiXiafengChoicePlanningQuestion(request, room, viewer)) return false;
        kind = AIOwnerRangeQueryScope::InfinityAttackRange;
        return true;
    }
    // Fixed Original heg-standard-wei-ai.lua762-764, called by
    // useCardThreatenEmperor(1174). Owner-only
    // readonly Slash planning; no receipt, native skill or final-use authority.
    if (sourceKey == "original:shensu3") {
        if (flag != "slashNoDistanceLimit" || !request.worldView.enableHegemony
            || !request.worldView.strategyFacts.value("hegemony").toObject()
                    .value("original_overrides_loaded").toBool()
            || !viewer->hasSkill("heg_shensu")) return false;
        const bool play = request.kind == AIRequest::Activate
            && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
        if (!play) return false;
        kind = AIOwnerRangeQueryScope::SlashNoDistanceLimit;
        return true;
    }
    // Fixed strategic_advantage-ai.lua638-642. The readonly scope changes only
    // the current owner query; native action/cost/target acceptance is separate.
    if (sourceKey == "original:useCardLureTiger") {
        if (flag != "slashNoDistanceLimit" || !request.worldView.enableHegemony
            || request.kind != AIRequest::Activate
            || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        bool source = viewer->hasSkill("heg_diaogui");
        auto disclosedLure = [&](const AICardView &card) {
            return card.effectiveId >= 0 && card.objectName == "lure_tiger"
                && room.getCardOwner(card.effectiveId) == viewer;
        };
        for (const AICardView &card : request.worldView.handCards) source |= disclosedLure(card);
        for (const AICardPileView &pile : request.worldView.self.piles)
            for (const AICardView &card : pile.cards) source |= disclosedLure(card);
        if (!source) return false;
        kind = AIOwnerRangeQueryScope::SlashNoDistanceLimit;
        return true;
    }
    // Fixed b6025 source callback identities. These permit owner-only planning,
    // not activation, materials or foreign native queries. No wildcard aliases.
    if (sourceKey == "bu_s2_benxi" && (request.kind != AIRequest::Activate
        || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !viewer->hasSkill(sourceKey))) return false;
    static const QSet<QString> infinite{
        "bu_s2_benxi",
        "GodShijun", "kejieshengzhuihun", "mouwusheng", "@mouxianchou-slash",
        "@@f_yonghunSlash", "BearingDownBorder", "@sfofl_bearing_down_border",
        "sfofl_jubing", "@@sfofl_qifeng"};
    static const QSet<QString> noDistance{
        "shenghua", "shijun", "@@luazhuixi", "@@luayonglue", "@PlusZhongyong",
        "SixQiaoxie", "#sfofl_shuangren", "sfofl_sitong", "sfofl_jiaozheng",
        "sfofl_qingjin", "#sfofl_zainei", "#sfofl_sixi", "sfofl_juedou"};
    if (flag == QLatin1String("InfinityAttackRange") && infinite.contains(sourceKey)) {
        kind = AIOwnerRangeQueryScope::InfinityAttackRange;
        return true;
    }
    if (flag == QLatin1String("slashNoDistanceLimit") && noDistance.contains(sourceKey)) {
        kind = AIOwnerRangeQueryScope::SlashNoDistanceLimit;
        return true;
    }
    return false;
}

QJsonObject projectAIStrategyOwnerRange(Room &room, ServerPlayer *viewer,
                                      const QString &flag, const QString &sourceKey)
{
    QJsonObject reach;
    for (ServerPlayer *target : room.getAllPlayers(true))
        reach.insert(target->objectName(), viewer->inMyAttackRange(target));
    return {{"kind", "owner_range_hypothesis"}, {"flag", flag}, {"source_key", sourceKey},
        {"viewer", viewer->objectName()},
        {"InfinityAttackRange", viewer->hasFlag("InfinityAttackRange")},
        {"slashNoDistanceLimit", viewer->hasFlag("slashNoDistanceLimit")},
        {"attack_range", viewer->getAttackRange()},
        {"attack_range_without_weapon", viewer->getAttackRange(false)},
        {"in_attack_range", reach}};
}

// ---- card-identity ----
#include "ai-strategy/inovation-ai.h"
namespace {
struct Entry { std::weak_ptr<const CardLifetimeToken> token; QString label; };
struct ViewerIdentity {
    QHash<const CardLifetimeToken *, Entry> entries;
    QString prefix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    quint64 next = 0;
};
class IdentityStore final : public QObject {
public:
    explicit IdentityStore(Room &room) : QObject(&room) {}
    QHash<QString, ViewerIdentity> viewers;
};
thread_local Room *activeRoom = nullptr;
thread_local ServerPlayer *activeViewer = nullptr;
}
QString aiLegacyCardIdentity(Room &room, ServerPlayer *viewer,
                             const std::shared_ptr<const CardLifetimeToken> &token)
{
    if (!viewer || viewer->getRoom() != &room || !token || !token->live) return {};
    IdentityStore *store = nullptr;
    for (QObject *child : room.children()) if ((store = dynamic_cast<IdentityStore *>(child))) break;
    if (!store) store = new IdentityStore(room);
    auto &space = store->viewers[viewer->objectName()];
    auto &entries = space.entries;
    auto found = entries.find(token.get());
    if (found != entries.end() && found->token.lock() == token) return found->label;
    // Weak references neither keep cards alive nor confuse recycled addresses.
    for (auto i = entries.begin(); i != entries.end();) {
        if (i->token.expired()) i = entries.erase(i); else ++i;
    }
    const QString label = QStringLiteral("native:") + space.prefix + QLatin1Char(':') + QString::number(++space.next);
    entries.insert(token.get(), Entry{token, label}); return label;
}
QString aiLegacyCardIdentity(Room &room, ServerPlayer *viewer, const Card *card)
{
    if (!card || !viewer || viewer->getRoom() != &room) return {};
    auto &lifetime = globalCardLifetimeManager();
    auto token = lifetime.liveToken(card);
    // A lawful fresh native clone may not have crossed SWIG yet. Observe the
    // actual live object exactly as SWIG does; do not adopt it or hold a lease.
    if (!token) token = lifetime.observeCard(const_cast<Card *>(card));
    return aiLegacyCardIdentity(room, viewer, token);
}
QString aiScopedLegacyCardIdentity(const Card *card)
{
    return activeRoom ? aiLegacyCardIdentity(*activeRoom, activeViewer, card) : QString();
}
AIIdentityProjectionScope::AIIdentityProjectionScope(Room &room, ServerPlayer *viewer)
    : previousRoom(activeRoom), previousViewer(activeViewer)
{ activeRoom = &room; activeViewer = viewer; }
AIIdentityProjectionScope::~AIIdentityProjectionScope()
{ activeRoom = previousRoom; activeViewer = previousViewer; }

QJsonObject readAIStrategyScopedPublicCardFlags(const Card *card)
{
    if (!activeRoom || !activeViewer) return {};
    auto flags = readAIStrategyPublicCardFlags(*activeRoom, activeViewer, card);
    const auto fresh = readAIXiedouFlags(*activeRoom, activeViewer, card);
    if (fresh.contains("nosjiefan-slash")) flags.insert("nosjiefan-slash", fresh.value("nosjiefan-slash"));
    const auto observation = readAIResponseCardObservation(*activeRoom, activeViewer, card);
    if (observation.contains("dwqingshidamage")) flags.insert("dwqingshidamage", observation.value("dwqingshidamage"));
    return flags;
}
