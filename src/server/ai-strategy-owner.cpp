#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-owner.h"
#include "ai-strategy-borrowed-callbacks.h"
#include "ai-strategy-round28-common.h"
#include "ai-strategy-card-identity.h"
#include "ai-strategy-public-disclosure.h"

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
// Insert into ai-strategy-owner.cpp anonymous namespace; raw metadata only.
QJsonObject round37UseMethodMetadata(lua_State *state)
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
QJsonObject round37OriginalFacts(Room &room,const ServerPlayer *viewer)
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
    result.insert("use_methods",round37UseMethodMetadata(room.roomRuntime()->lua().state()));
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
        std::unique_ptr<AIRound56WorldSkillQueriesScope> skillSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            skillSource.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill(name);
    };
    const auto hasShownPublicSkill = [&](const ServerPlayer *subject, const QString &name) {
        std::unique_ptr<AIRound56WorldSkillQueriesScope> skillSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            skillSource.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
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
            for (int instance : ([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
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
                {"shown_all", aiRound28ShownAllGenerals(room, const_cast<ServerPlayer *>(viewer), const_cast<ServerPlayer *>(player))},
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
        std::unique_ptr<AIRound56OwnerPindianScope> pindianSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            pindianSource.reset(new AIRound56OwnerPindianScope(room,viewer,gameLua));
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
        std::unique_ptr<AIRound56OwnerSlashScope> ownerSlashScope;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            ownerSlashScope.reset(new AIRound56OwnerSlashScope(room,viewer,gameLua));
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
    projectAIRound39BorrowedCallbacks(world, room, viewer);
    world.strategyFacts.insert("owner_discard_callbacks", discardCallbackMetadata(room.roomRuntime()->lua().state()));
    world.strategyFacts.insert("original_ask_callback_types", originalAskCallbackTypes(room.roomRuntime()->lua().state()));
    world.strategyFacts.insert("original37", round37OriginalFacts(room,viewer));
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
        std::unique_ptr<AIRound56OwnerJileiScope> jileiSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            jileiSource.reset(new AIRound56OwnerJileiScope(room,viewer,gameLua));
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
    std::unique_ptr<AIRound56OwnerAnalepticScope> ownerAnalepticScope;
    auto *analepticLua=room.roomRuntime()->lua().rawState();
    if(!inAIRound56ProjectionScope(room,analepticLua))
        ownerAnalepticScope.reset(new AIRound56OwnerAnalepticScope(room,viewer,analepticLua));
    const Analeptic analeptic(Card::NoSuit, 0);
    AIRound56OwnerAnalepticBirth analepticBirth(room,viewer,&analeptic);
    recordAIRound56ProjectionOwnerSourceCard(room,viewer,&analeptic,QStringLiteral("analeptic"));
    world.strategyFacts.insert(QStringLiteral("owner_analeptic_available"), Analeptic::IsAvailable(viewer, &analeptic));
    ownerAnalepticScope.reset(); // End only the getter grant, not the original stack Card lifetime.

    int ignoreLimitCount = 0;
    {
        std::unique_ptr<AIRound56OwnerCardStateScope> ignoreLimitSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            ignoreLimitSource.reset(new AIRound56OwnerCardStateScope(room,viewer,gameLua));
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
        std::unique_ptr<AIRound56OwnerEquipmentQueriesScope> equipmentSource;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            equipmentSource.reset(new AIRound56OwnerEquipmentQueriesScope(room,viewer,gameLua));
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
    std::unique_ptr<AIRound56FixedSlashProbesScope> fixedSlashScope;
    auto *fixedSlashLua=room.roomRuntime()->lua().rawState();
    if(!inAIRound56ProjectionScope(room,fixedSlashLua))
        fixedSlashScope.reset(new AIRound56FixedSlashProbesScope(room,viewer,fixedSlashLua));
    // Standard Slash's Xiansi branch calls CanToCard on this exact no-cost
    // declaration. targetFilter is kept separate from prohibition/availability.
    if (Card *slash = Sanguosha->cloneCard("slash")) {
        recordAIRound56ProjectionOwnerSourceCard(room,viewer,slash,QStringLiteral("slash"));
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
                recordAIRound56ProjectionOwnerSourceCard(room,viewer,card,name);
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
