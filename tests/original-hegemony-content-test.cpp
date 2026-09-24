#include "engine-bootstrap.h"
#include "ai.h"
#include "ai-runtime.h"
#include "engine.h"
#include "general.h"
#include "gamerule.h"
#include "lua-runtime.h"
#include "lua.hpp"
#include "room.h"
#include "room-test-access.h"
#include "room-runtime.h"
#include "room-state.h"
#include "server-info.h"
#include "settings.h"
#include "skill.h"
#include "standard-cards.h"
#include "original-hegemony-compat.h"
#include "util.h"

#include <QCoreApplication>
#include <QDebug>
#include <QScopeGuard>
#include <memory>

namespace {

#define HEG_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Original HEG content contract failed" << __LINE__ << #condition; \
    return false; \
} } while (false)

const QMap<QString, int> generalCounts{
    {"heg_standard", 60}, {"heg_formation", 9}, {"heg_momentum", 9},
    {"heg_transformation", 10}, {"heg_power", 9}, {"heg_manoeuvre", 8},
    {"heg_newsgs", 8}, {"heg_mol", 7}, {"heg_overseas", 15}, {"heg_lord_ex", 24}};
const QMap<QString, int> deckCounts{
    {"heg_standard_cards", 108}, {"heg_strategic_advantage", 52},
    {"heg_formation_equip", 1}, {"heg_momentum_equip", 1},
    {"heg_transformation_equip", 1}, {"heg_power_equip", 1}, {"heg_lord_ex_card", 5}};

// These physical definitions are shared with identity mode. Their deck
// package selects the physical pool; the card meta-object stays native.
const QSet<QString> sharedNativeEquipment{
    "crossbow", "double_sword", "qinggang_sword", "spear", "axe", "kylin_bow",
    "eight_diagram", "ice_sword", "renwang_shield", "fan", "vine", "silver_lion",
    "wooden_ox", "jueying", "dilu", "zhuahuangfeidian", "chitu", "dayuan", "zixing",
    "jingfan"};
// These common trick effects keep their identity-mode native class. Their
// Hegemony package owns the deck entry, not a parallel H-prefixed card type.
const QSet<QString> sharedNativeTricks{
    "amazing_grace", "god_salvation", "savage_assault", "archery_attack", "duel",
    "ex_nihilo", "snatch", "dismantlement", "iron_chain", "fire_attack", "collateral",
    "nullification", "indulgence", "supply_shortage", "lightning"};
const QStringList sharedEquipmentSkills{
    "crossbow", "double_sword", "qinggang_sword", "spear", "axe", "kylin_bow",
    "eight_diagram", "ice_sword", "renwang_shield", "fan", "vine", "silver_lion",
    "wooden_ox"};

class ModeProhibitProbe : public ProhibitSkill
{
public:
    explicit ModeProhibitProbe(const QString &name) : ProhibitSkill(name) {}
    bool isProhibited(const Player *from, const Player *, const Card *, const QList<const Player *> &) const override
    {
        ++calls;
        nullSourceSeen = nullSourceSeen || !from;
        return false;
    }
    mutable int calls = 0;
    mutable bool nullSourceSeen = false;
};

class ModeTargetModProbe : public TargetModSkill
{
public:
    explicit ModeTargetModProbe(const QString &name) : TargetModSkill(name) {}
    int getResidueNum(const Player *, const Card *, const Player *) const override
    {
        ++calls;
        return 0;
    }
    mutable int calls = 0;
};

class ModeTriggerProbe : public TriggerSkill
{
public:
    explicit ModeTriggerProbe(const QString &name) : TriggerSkill(name)
    {
        global = true;
        events << ChoiceMade;
    }
    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        ++calls;
        return false;
    }
    mutable int calls = 0;
};

class ModeRuleProbe : public GameRule
{
public:
    explicit ModeRuleProbe(QObject *parent) : GameRule(parent)
    {
        setObjectName("test_mode_rule_subclass");
        events.clear();
        events << ChoiceMade;
    }
    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        ++calls;
        return false;
    }
    mutable int calls = 0;
};

bool skillModeAdmission(Room &room, bool hegemony, const Card *slash)
{
    for (const QString &name : sharedEquipmentSkills) {
        const Skill *skill = Sanguosha->getSkill(name);
        HEG_CHECK(skill && skill->isEquipSkill());
    }
    auto *identityProhibit = new ModeProhibitProbe("test_identity_prohibit");
    auto *originalProhibit = new ModeProhibitProbe("#heg_test_mode_prohibit");
    auto *identityTarget = new ModeTargetModProbe("test_identity_target");
    auto *originalTarget = new ModeTargetModProbe("heg_test_mode_target");
    auto *identityTrigger = new ModeTriggerProbe("test_identity_trigger");
    auto *originalTrigger = new ModeTriggerProbe("heg_test_mode_trigger");
    identityProhibit->setProperty("supportsSourceLessProhibition", true);
    originalProhibit->setProperty("supportsSourceLessProhibition", true);
    // Engine::addSkills adopts these probes into this room's definition root.
    Sanguosha->addSkills({identityProhibit, originalProhibit, identityTarget,
        originalTarget, identityTrigger, originalTrigger});
    const auto prohibits = Sanguosha->getProhibitSkills();
    const auto targetMods = Sanguosha->getTargetModSkills();
    const auto triggers = Sanguosha->getGlobalTriggerSkills();
    HEG_CHECK(prohibits.contains(identityProhibit));
    HEG_CHECK(prohibits.contains(originalProhibit));
    HEG_CHECK(targetMods.contains(identityTarget));
    HEG_CHECK(targetMods.contains(originalTarget));
    HEG_CHECK(triggers.contains(identityTrigger));
    HEG_CHECK(triggers.contains(originalTrigger));
    HEG_CHECK(prohibits.contains(qobject_cast<const ProhibitSkill *>(Sanguosha->getSkill("gameruleprohibit"))));
    HEG_CHECK(targetMods.contains(qobject_cast<const TargetModSkill *>(Sanguosha->getSkill("gameruleslashbuff"))));
    for (const QString &name : {"gameruleprohibit", "gamerulemaxcards", "gameruleattackrange",
             "gameruleslashbuff", "gameruledistancefrom", "gameruledistanceto", "gamerulestate"}) {
        const Skill *skill = Sanguosha->getSkill(name);
        HEG_CHECK(skill);
    }

    // Switching mode preserves both sets of general skills in the same registry.
    auto restoreMode = qScopeGuard([hegemony]() { Config.EnableHegemony = hegemony; });
    Config.EnableHegemony = !hegemony;
    HEG_CHECK(Sanguosha->getProhibitSkills().contains(identityProhibit));
    HEG_CHECK(Sanguosha->getProhibitSkills().contains(originalProhibit));
    HEG_CHECK(Sanguosha->getTargetModSkills().contains(identityTarget));
    HEG_CHECK(Sanguosha->getTargetModSkills().contains(originalTarget));
    Config.EnableHegemony = hegemony;

    RoomTestAccess::attachThread(room);
    ServerPlayer *player = RoomTestAccess::addOrdinaryPlayer(room, "mode-admission-probe", true);
    player->setNext(player);
    player->setGeneralName("caocao");
    player->setKingdom("wei");
    player->setRole("lord");
    player->setMaxHp(4);
    player->setHp(4);
    RoomTestAccess::resetAlive(room);
    room.setCurrent(player);
    // Deferred ImperialOrder supplies a null source. Identity mode retains
    // its existing contract and receives an ordinary, non-null source here.
    const ProhibitSkill *blocking = room.isProhibited(hegemony ? nullptr : player, player, slash);
    const int residue = Sanguosha->correctCardTarget(TargetModSkill::Residue, player, slash);
    // A real earlier prohibition or unlimited residue legitimately short-circuits
    // later callbacks. Membership above checks admission independently of that.
    if (!blocking) HEG_CHECK(identityProhibit->calls > 0 && originalProhibit->calls > 0);
    if (residue <= 500) HEG_CHECK(identityTarget->calls > 0 && originalTarget->calls > 0);
    if (hegemony && originalProhibit->calls > 0) HEG_CHECK(originalProhibit->nullSourceSeen);

    // General triggers and engine rules register in either mode.
    room.getThread()->addTriggerSkill(identityTrigger);
    room.getThread()->addTriggerSkill(originalTrigger);
    auto *rule = new ModeRuleProbe(room.getThread());
    rule->setParent(room.getThread());
    room.getThread()->addTriggerSkill(rule);
    QVariant data = QStringLiteral("mode-admission");
    room.getThread()->trigger(ChoiceMade, &room, player, data);
    HEG_CHECK(identityTrigger->calls == 1);
    HEG_CHECK(originalTrigger->calls == 1);
    HEG_CHECK(rule->calls == 1);
    if (!hegemony) {
        // The remaining Wu conversion is V2 and is available in an identity room.
        const QString name = QStringLiteral("heg_duoshi");
        player->addSkill(name, true);
        const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill(name));
        HEG_CHECK(skill);
        ActiveSkillRequest request;
        request.initiator = player;
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        HEG_CHECK(skill->canActivate(request));
        HEG_CHECK(skill->getMaxUsageLimit(SkillContext()) == 4);
        HEG_CHECK(player->hasShownSkill(name));
    }
    return true;
}

bool directViewAsContract(Room &room)
{
    // Exercise the native request boundary shared by UI and serialized AI replies.
    ServerPlayer player(&room);
    player.setMaxHp(4);
    player.setHp(4);
    ActiveSkillRequest request;
    request.initiator = &player;
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    const auto *boyan = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_boyan"));
    HEG_CHECK(boyan && boyan->canActivate(request) && boyan->cardSelectionFeasible(request));
    std::unique_ptr<const Card> action(boyan->createCard(request));
    HEG_CHECK(action && action->getClassName() == boyan->historyKey(request));
    request.selectedCardIds = {-1};
    HEG_CHECK(!boyan->cardSelectionFeasible(request) && !boyan->createCard(request));

    const auto *give = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_zhengbigive"));
    HEG_CHECK(give);
    request.selectedCardIds.clear();
    request.reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
    request.pattern = "@@heg_zhengbigive!";
    HEG_CHECK(give->canActivate(request));
    request.pattern = "@@heg_zhengbigive";
    HEG_CHECK(!give->canActivate(request));
    request.pattern = "@@heg_zhengbigive!";
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    HEG_CHECK(!give->canActivate(request));

    const auto *luanji = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_luanji"));
    HEG_CHECK(luanji && room.getDrawPile().size() >= 2);
    request.pattern.clear();
    const int first = room.getDrawPile().at(0), second = room.getDrawPile().at(1);
    request.selectedCardIds = {first, first};
    HEG_CHECK(!luanji->cardSelectionFeasible(request) && !luanji->createCard(request));
    request.selectedCardIds = {first, second};
    HEG_CHECK(luanji->cardSelectionFeasible(request));
    std::unique_ptr<const Card> arrows(luanji->createCard(request));
    HEG_CHECK(arrows && arrows->isKindOf("ArcheryAttack") && arrows->getSubcards() == request.selectedCardIds);
    // The existing variant declares its basic card via the server's AG choice.
    const auto *basic = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("tenyearrende"));
    HEG_CHECK(basic);
    player.setHp(3);
    request.selectedCardIds.clear();
    request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    request.pattern = "@@tenyearrende";
    HEG_CHECK(!basic->canActivate(request) && !basic->createCard(request));
    int peachId = -1;
    for (int id : room.getAvailableCardList(&player, "basic", "tenyearrende"))
        if (Sanguosha->getEngineCard(id)->isKindOf("Peach")) { peachId = id; break; }
    HEG_CHECK(peachId >= 0);
    // Room drops *Clear marks while no turn is current; set the fixture state directly.
    player.setMark("tenyearrende_id-PlayClear", peachId + 1);
    request.userString = "duel"; // A client declaration cannot replace the server's choice.
    std::unique_ptr<const Card> peach(basic->createCard(request));
    HEG_CHECK(peach && peach->isKindOf("Peach") && peach->getSkillName() == "tenyearrende");
    request.selectedCardIds = {first};
    HEG_CHECK(!basic->createCard(request));
    return true;
}

class LongdanChoiceAI : public TrustAI
{
public:
    explicit LongdanChoiceAI(ServerPlayer *player) : TrustAI(player) {}
    ServerPlayer *askForPlayerChosen(const QList<ServerPlayer *> &targets, const QString &) override
    {
        offered = targets;
        return targets.contains(chosen) ? chosen : nullptr;
    }
    QList<ServerPlayer *> offered;
    ServerPlayer *chosen = nullptr;
};

bool longdanOffsetContract()
{
    const int previousDelay = Config.AIDelay;
    const auto restoreDelay = qScopeGuard([=]() { Config.AIDelay = previousDelay; });
    Config.AIDelay = 0;
    Room room(nullptr, QStringLiteral("04p"));
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(*room.luaRuntime());
    RoomTestAccess::attachThread(room);
    room.getThread()->addTriggerSkill(new GameRule(room.getThread()));
    auto add = [&](const QString &name, const QString &general) {
        ServerPlayer *player = RoomTestAccess::addOrdinaryPlayer(room, name, true);
        player->setState("robot");
        player->setGeneralName(general);
        player->setKingdom("shu");
        player->setRole("loyalist");
        player->setGeneralShowed(true);
        player->setMaxHp(4);
        player->setHp(3);
        auto *ai = new LongdanChoiceAI(player);
        ai->setParent(player);
        player->setAI(ai);
        return player;
    };
    ServerPlayer *attacker = add("longdan-attacker", "heg_zhaoyun");
    ServerPlayer *dodger = add("longdan-dodger", "heg_zhaoyun");
    ServerPlayer *other = add("longdan-other", "heg_liubei");
    ServerPlayer *lord = add("longdan-lord", "heg_lord_liubei");
    lord->setHp(4);
    lord->addSkill("heg_shouyue", true);
    attacker->addSkill("heg_longdan", true);
    dodger->addSkill("heg_longdan", true);
    const QList<ServerPlayer *> players = room.getPlayers();
    for (int i = 0; i < players.size(); ++i)
        players.at(i)->setNext(players.at((i + 1) % players.size()));
    RoomTestAccess::resetAlive(room);
    room.setCurrent(attacker);
    HEG_CHECK(attacker->getLord() == lord && lord->hasLordSkill("heg_shouyue"));
    const auto *longdan = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("heg_longdan"));
    HEG_CHECK(longdan);
    Slash slash(Card::Spade, 7);
    Jink jink(Card::Heart, 2);
    CardEffectStruct offset;
    offset.from = attacker;
    offset.to = dodger;
    offset.card = &slash;
    offset.offset_card = &jink;
    auto *attackAI = static_cast<LongdanChoiceAI *>(attacker->getAI());
    auto *dodgeAI = static_cast<LongdanChoiceAI *>(dodger->getAI());
    attackAI->chosen = dodgeAI->chosen = other;

    // Converted Slash can damage its user or a third player, but never the dodger.
    slash.setSkillName("heg_longdan");
    QVariant data = QVariant::fromValue(offset);
    HEG_CHECK(longdan->triggerable(CardOffset, &room, dodger, data).contains(attacker));
    SkillContext damage;
    damage.owner = attacker;
    damage.original_data = &data;
    HEG_CHECK(longdan->cost(CardOffset, &room, dodger, damage));
    HEG_CHECK(attackAI->offered.contains(attacker) && attackAI->offered.contains(other)
        && !attackAI->offered.contains(dodger));
    HEG_CHECK(damage.choice == "damage" && damage.targets == QList<ServerPlayer *>{other});
    const int handBefore = attacker->getHandcardNum();
    longdan->effect(CardOffset, &room, dodger, damage);
    HEG_CHECK(attacker->getHandcardNum() == handBefore); // Shouyue does not draw on offsets.
    longdan->effectTarget(CardOffset, &room, dodger, damage, other);
    HEG_CHECK(other->getHp() == 2 && dodger->getHp() == 3);

    // Converted Jink heals only a wounded third player, even when both participants are wounded.
    slash.setSkillName(QString());
    jink.setSkillName("heg_longdan");
    HEG_CHECK(longdan->triggerable(CardOffset, &room, dodger, data).contains(dodger));
    SkillContext recover;
    recover.owner = dodger;
    recover.original_data = &data;
    HEG_CHECK(longdan->cost(CardOffset, &room, dodger, recover));
    HEG_CHECK(dodgeAI->offered == QList<ServerPlayer *>{other});
    HEG_CHECK(recover.choice == "recover" && recover.targets == QList<ServerPlayer *>{other});
    const int dodgeHandBefore = dodger->getHandcardNum();
    longdan->effect(CardOffset, &room, dodger, recover);
    HEG_CHECK(dodger->getHandcardNum() == dodgeHandBefore);
    longdan->effectTarget(CardOffset, &room, dodger, recover, other);
    HEG_CHECK(other->getHp() == 3 && attacker->getHp() == 3 && dodger->getHp() == 3);
    return true;
}

bool luaContract(Room &room, bool hegemony)
{
    lua_State *state = room.getLuaState();
    HEG_CHECK(state);
    const int top = lua_gettop(state);
    auto restore = qScopeGuard([=]() { lua_settop(state, top); });
    if (hegemony) {
        // Reproduce the low-HP rescue branch with a real visible Peach. The
        // observer's Room AI supplies relationships; no donor recorder exists.
        room.getCurrent()->setHp(1);
        // Hegemony uses the migrated V2 skill name; the native donor name is
        // not the callback's visible skill identity in this room.
        room.getCurrent()->addSkill("mobilefangzhu", true);
        room.getCurrent()->setGeneralShowed(true);
        ServerPlayer *observer = RoomTestAccess::addOrdinaryPlayer(room, "rescue-observer", true);
        observer->setGeneralName("heg_caocao");
        observer->setKingdom("wei");
        observer->setRole("loyalist");
        observer->setMaxHp(4);
        observer->setHp(4);
        observer->setNext(room.getCurrent());
        room.getCurrent()->setNext(observer);
        RoomTestAccess::resetAlive(room);
        int peachId = -1;
        for (int id : room.getDrawPile()) {
            if (Sanguosha->getCard(id)->objectName() == "peach") {
                peachId = id;
                break;
            }
        }
        HEG_CHECK(peachId >= 0);
        room.getDrawPile().removeOne(peachId);
        // The AI visibility contract is viewer-scoped; mapping a card to a
        // player's hand alone does not make it publicly visible.
        room.setCardFlag(peachId, "visible");
        room.setCardMapping(peachId, observer, Player::PlaceHand);
        observer->addCard(peachId, Player::PlaceHand);
    }
    // Exercise the actual regenerated ABI, not a duplicate native-only struct.
    const QByteArray script = QStringLiteral(R"lua(
        assert((sgs.original_hegemony_ai_loaded == true) == %1)
        if %1 then
            -- Validate the actual callable ABI used by the imported scripts,
            -- before a full game can silently fall back after a missing helper.
            local missing = {}
            for _, name in ipairs({
                "Analeptic_IsAvailable", "CardList", "Card_Parse", "GetConfig", "LoadPackageScript",
                "PlayerList", "PlayerList2SPlayerList", "QList2Table", "SPlayerList", "ai_get_cardType",
                "cardIsVisible", "cloneCard", "findIntersectionSkills", "findPlayerByShownSkillName", "findUnionSkills",
                "getDefense", "getDefenseSlash", "getReward", "getValue", "hasNullSkill",
                "isAnjiang", "isGoodHp", "isGoodTarget", "list", "originalHegemonyGameProcess",
                "originalHegemonyHasShownSkills", "originalHegemonyPublicKingdom", "originalHegemonyShownCount", "originalHegemonySkillCount", "qlist",
                "reverse", "updateIntention", "updateIntentions"
            }) do
                local value = sgs[name]
                local meta = (type(value) == "table" or type(value) == "userdata") and getmetatable(value)
                -- SWIG constructors are callable class tables, not Lua functions.
                if type(value) ~= "function" and not (type(meta) == "table" and meta.__call) then
                    table.insert(missing, name)
                end
            end
            assert(#missing == 0, "missing imported AI callables: " .. table.concat(missing, ", "))
            local slash = sgs.cloneCard("slash")
            assert(slash and slash:getClassName() == "Slash" and slash:getEffectiveId() < 0)
            assert(table.concat(sgs.KingdomsTable, ",") == "wei,shu,wu,qun")
            assert(sgs.Slash_Natures[slash:getClassName()] == sgs.DamageStruct_Normal)
            local fire = sgs.cloneCard("fire_slash")
            local thunder = sgs.cloneCard("thunder_slash")
            assert(sgs.Slash_Natures[fire:getClassName()] == sgs.DamageStruct_Fire)
            assert(sgs.Slash_Natures[thunder:getClassName()] == sgs.DamageStruct_Thunder)
            local room = sgs.Sanguosha:currentRoom()
            local target = room:findPlayerByObjectName("mode-admission-probe")
            local observer = room:findPlayerByObjectName("rescue-observer")
            local previousAi = sgs.ais[observer:objectName()]
            local previousRoom = global_room
            global_room = room
            local friends, calls = {}, 0
            sgs.ais[observer:objectName()] = {
                player = observer, room = room,
                getFriends = function(self, player)
                    assert(self.player:objectName() == observer:objectName())
                    assert(player:objectName() == target:objectName())
                    calls = calls + 1
                    return friends
                end
            }
            assert(sgs.recorder == nil)
            assert(sgs.isGoodHp(target, observer) == false)
            friends = {observer}
            assert(sgs.isGoodHp(target, observer) == true)
            assert(calls == 2)
            local ai = sgs.ais[observer:objectName()]
            ai.hasEightDiagramEffect = function() return false end
            local function event(kind, player, value)
                local hook = sgs.ai_event_callback[kind].original_hegemony
                assert(type(hook) == "function", "missing HEG event hook")
                hook(ai, player, value)
            end
            local function useData(card)
                local use = sgs.CardUseStruct()
                use.card, use.from = card, observer
                use.to:append(target)
                local value = sgs.QVariant()
                value:setValue(use)
                return value
            end
            -- Same-name virtual cards have different native identities. Repeated
            -- callbacks must not duplicate frames or erase an outer AOE context.
            local outer = sgs.cloneCard("archery_attack")
            local inner = sgs.cloneCard("archery_attack")
            assert(outer ~= inner and outer:getEffectiveId() < 0 and inner:getEffectiveId() < 0)
            local outerData, innerData = useData(outer), useData(inner)
            event(sgs.PreCardUsed, observer, outerData)
            event(sgs.PreCardUsed, observer, outerData)
            event(sgs.TargetConfirmed, observer, outerData)
            event(sgs.TargetConfirmed, observer, outerData)
            assert(sgs.ai_AOE_data.card == outer and sgs.ai_AOE_data.targets[target:objectName()])
            event(sgs.PreCardUsed, observer, innerData)
            event(sgs.TargetConfirmed, observer, innerData)
            assert(sgs.ai_AOE_data.card == inner)
            event(sgs.CardFinished, observer, innerData)
            event(sgs.CardFinished, observer, innerData)
            assert(sgs.ai_AOE_data.card == outer)
            event(sgs.CardFinished, observer, outerData)
            assert(sgs.ai_AOE_data == nil)
            -- A nested use can retain the same Card identity. Production emits
            -- one CardFinished per use, so each finish must pop just one frame.
            event(sgs.PreCardUsed, observer, outerData)
            event(sgs.TargetConfirmed, observer, outerData)
            event(sgs.PreCardUsed, observer, outerData)
            event(sgs.TargetConfirmed, observer, outerData)
            event(sgs.CardFinished, observer, outerData)
            assert(sgs.ai_AOE_data.card == outer)
            event(sgs.CardFinished, observer, outerData)
            assert(sgs.ai_AOE_data == nil)
            local name = target:objectName()
            sgs.card_lack[name] = {Slash=0, Jink=0, Peach=0}
            event(sgs.ChoiceMade, target, sgs.QVariant("cardResponded:jink:test:"))
            assert(sgs.card_lack[name].Jink == 1)
            event(sgs.ChoiceMade, target, sgs.QVariant("cardResponded:jink:test:jink[club:2]=0"))
            assert(sgs.card_lack[name].Jink == 0)
            local move = sgs.CardsMoveOneTimeStruct()
            move.to = target
            move.to_place = sgs.Player_PlaceHand
            move.card_ids:append(observer:handCards():first())
            move.from_places:append(sgs.Player_DrawPile)
            move.open:append(false)
            -- Reset the rescue fixture's public flag before simulating a hidden gain.
            room:setCardFlag(observer:handCards():first(), "-visible")
            local moved = sgs.QVariant()
            moved:setValue(move)
            sgs.card_lack[name] = {Slash=1, Jink=1, Peach=1}
            event(sgs.CardsMoveOneTime, target, moved)
            assert(sgs.card_lack[name].Slash == 0 and sgs.card_lack[name].Jink == 0 and sgs.card_lack[name].Peach == 0)
            -- A publicly obtained Peach only disproves the corresponding lack.
            move.open:clear()
            move.open:append(true)
            moved:setValue(move)
            sgs.card_lack[name] = {Slash=1, Jink=1, Peach=1}
            event(sgs.CardsMoveOneTime, target, moved)
            assert(sgs.card_lack[name].Slash == 1 and sgs.card_lack[name].Jink == 1 and sgs.card_lack[name].Peach == 0)
            sgs.ais[observer:objectName()] = previousAi
            global_room = previousRoom
        end
        local number = sgs.PlayerNumStruct()
        number.m_num = 3
        number.m_toCalculate = "wei"
        number.m_reason = "AI"
        local value = sgs.QVariant()
        value:setValue(number)
        local copy = value:toPlayerNum()
        assert(copy.m_num == 3 and copy.m_toCalculate == "wei" and copy.m_reason == "AI")
        assert(type(sgs.GeneralShown) == "number" and sgs.GeneralHidden == sgs.GeneralShown + 1)
        assert(sgs.GeneralRemoved == sgs.GeneralHidden + 1)
        assert(sgs.ConfirmPlayerNum == sgs.GeneralRemoved + 1)
        assert(sgs.Player_DrawPileBottom == sgs.Player_PlaceWuGu + 1)
    )lua").arg(hegemony ? QStringLiteral("true") : QStringLiteral("false")).toUtf8();
    if (luaL_dostring(state, script.constData()) != LUA_OK) {
        qCritical() << "Original HEG Lua content ABI:" << lua_tostring(state, -1);
        return false;
    }
    return true;
}

bool contentMode(bool hegemony)
{
    Config.EnableHegemony = hegemony;
    Config.Enable2ndGeneral = hegemony;
    ServerInfo.EnableHegemony = hegemony;
    ServerInfo.Enable2ndGeneral = hegemony;
    QVariantMap values = Config.valueOverrides();
    values.insert("EnableHegemony", hegemony);
    Config.setValueOverrides(values);
    Room room(nullptr, QStringLiteral("04p"));
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding luaBinding(*room.luaRuntime());
    room.getRoomState()->reset();
    HEG_CHECK(isNormalGameMode(room.getMode(), hegemony) == !hegemony);
    HEG_CHECK(Sanguosha->getSkill("jianxiong") && Sanguosha->getSkill("nosjianxiong"));
    HEG_CHECK(Sanguosha->getGeneral("heg_caocao")->hasSkill("nosjianxiong"));
    HEG_CHECK(!Sanguosha->getSkill("heg_jianxiong"));
    {
        const bool duringGame = ServerInfo.DuringGame;
        const QString mode = ServerInfo.GameMode;
        auto restore = qScopeGuard([duringGame, mode]() {
            ServerInfo.DuringGame = duringGame;
            ServerInfo.GameMode = mode;
        });
        ServerInfo.DuringGame = true;
        ServerInfo.GameMode = room.getMode();
        ModeTriggerProbe description("heg_test_description");
        Sanguosha->addTranslationEntry(":heg_test_description", "[NoAutoRep]Hegemony body");
        Sanguosha->addTranslationEntry(":heg_test_description_p", "[NoAutoRep]Identity body");
        HEG_CHECK(description.getDescription().contains(hegemony ? "Hegemony body" : "Identity body"));
        HEG_CHECK(description.objectName() == "heg_test_description");
    }
    if (hegemony) {
        const AiRouteRegistry &routes = room.roomRuntime()->ai().routes();
        HEG_CHECK(routes.routeFor(AIRequest::UseCard, "activate") == AiRouteLegacyAdapted);
        HEG_CHECK(routes.routeFor(AIRequest::UseCard, "askForGuanxing") == AiRouteLegacyAdapted);
        const QVariantMap configured = Config.valueOverrides();
        auto restoreRoutes = qScopeGuard([configured]() { Config.setValueOverrides(configured); });
        QVariantMap defaults = configured;
        defaults.insert("AiLegacyDirectCallbacks", QStringList());
        defaults.insert("AiLegacyAdaptedCallbacks", QStringList());
        defaults.insert("AiIsolatedCallbacks", QStringList());
        Config.setValueOverrides(defaults);
        HEG_CHECK(AiLuaRuntime::requiresLegacyRuntime());
        // Isolating one callback must not suppress the other donor callbacks'
        // legacy runtime. This queries routing without constructing another room.
        defaults.insert("AiIsolatedCallbacks", QStringList{"activate"});
        Config.setValueOverrides(defaults);
        HEG_CHECK(AiLuaRuntime::requiresLegacyRuntime());
    }

    QMap<QString, int> actualGenerals;
    for (const General *general : Sanguosha->getAllGenerals()) {
        if (generalCounts.contains(general->getPackage()))
            ++actualGenerals[general->getPackage()];
    }
    // Count hidden sovereign definitions too; the selectable pool excludes them.
    HEG_CHECK(actualGenerals == generalCounts);
    const QStringList pool = Sanguosha->getLimitedGeneralNames();
    HEG_CHECK(!pool.isEmpty());
    QSet<QString> poolPackages;
    for (const QString &name : pool) {
        const General *general = Sanguosha->getGeneral(name);
        HEG_CHECK(general);
        poolPackages.insert(general->getPackage());
    }
    HEG_CHECK(poolPackages.contains(hegemony ? "heg_standard" : "standard"));
    HEG_CHECK(pool.contains("caocao") == !hegemony);
    HEG_CHECK(pool.contains("heg_caocao") == hegemony);
    {
        const QStringList banned = ServerInfo.BanPackages;
        const bool dedup = Config.GeneralVersionDedup;
        auto restoreSelection = qScopeGuard([banned, dedup]() {
            ServerInfo.BanPackages = banned;
            Config.GeneralVersionDedup = dedup;
        });
        Config.GeneralVersionDedup = true;
        const QStringList ranked = Sanguosha->getLimitedGeneralNames();
        HEG_CHECK(ranked.contains("heg_caocao") == hegemony);
        // Version ranking may remove variants, never a different full identity.
        QSet<QString> poolCharacters, rankedCharacters;
        for (const QString &name : pool) poolCharacters.insert(name.section('_', -1));
        for (const QString &name : ranked) rankedCharacters.insert(name.section('_', -1));
        HEG_CHECK(poolCharacters == rankedCharacters);

        // Only the selected package is enabled: the other mode's sole version
        // remains selectable instead of being suppressed by a loaded definition.
        Config.GeneralVersionDedup = false;
        const General *onlyGeneral = Sanguosha->getGeneral(hegemony ? "caocao" : "heg_caocao");
        HEG_CHECK(onlyGeneral);
        const QString onlyPackage = onlyGeneral->getPackage();
        for (const General *general : Sanguosha->getAllGenerals()) {
            if (general->getPackage() != onlyPackage)
                ServerInfo.BanPackages << general->getPackage();
        }
        ServerInfo.BanPackages.removeDuplicates();
        const QStringList singleVersion = Sanguosha->getLimitedGeneralNames();
        HEG_CHECK(singleVersion.contains("caocao") == hegemony);
        HEG_CHECK(singleVersion.contains("heg_caocao") == !hegemony);
    }

    const QList<int> deck = Sanguosha->getRandomCards(true);
    HEG_CHECK(!deck.isEmpty());
    if (hegemony) {
        ServerPlayer equipped(&room);
        for (int id : deck) {
            const Card *card = room.getCard(id);
            if (card->objectName() == "DragonPhoenix" || card->objectName() == "eight_diagram"
                || card->objectName() == "JadeSeal")
                equipped.setEquip(card);
        }
        const Skill *weaponDefinition = Sanguosha->getSkill("heg_DragonPhoenix");
        const Skill *armorDefinition = Sanguosha->getSkill("eight_diagram");
        auto *weapon = dynamic_cast<const WeaponSkillV2 *>(weaponDefinition);
        auto *armor = dynamic_cast<const ArmorSkillV2 *>(armorDefinition);
        const Skill *treasureDefinition = Sanguosha->getSkill("heg_JadeSeal");
        auto *treasure = dynamic_cast<const TreasureSkillV2 *>(treasureDefinition);
        HEG_CHECK(weapon && armor && treasure
            && weapon->isEquipSkill() && armor->isEquipSkill() && treasure->isEquipSkill());
        HEG_CHECK(weapon->triggerable(&equipped) && armor->triggerable(&equipped)
            && treasure->triggerable(&equipped));
        // CardLimitation is the equipment authority; scoped rules belong to
        // the equipment holder and must not leak to other sources.
        ServerPlayer restrictedSource(&room), otherSource(&room);
        restrictedSource.setObjectName("equipment_restricted_source");
        otherSource.setObjectName("equipment_other_source");
        const QString scopedArmor = "Armor|.|.|.|target:" + restrictedSource.objectName();
        room.setPlayerEquipsNullified(&equipped, scopedArmor, "equipment_v2_test", false);
        HEG_CHECK(!equipped.hasArmorEffect("eight_diagram", &restrictedSource));
        HEG_CHECK(equipped.hasArmorEffect("eight_diagram", &otherSource));
        room.removePlayerEquipsNullified(&equipped, scopedArmor, "equipment_v2_test");
        room.setPlayerEquipsNullified(&equipped, "EquipCard", "equipment_v2_test", false);
        HEG_CHECK(!weapon->triggerable(&equipped) && !armor->triggerable(&equipped)
            && !treasure->triggerable(&equipped));
        room.removePlayerEquipsNullified(&equipped, "EquipCard", "equipment_v2_test");
    }
    QMap<QString, int> actualDeck;
    QSet<int> physicalIds;
    for (int id : deck) {
        const Card *card = Sanguosha->getEngineCard(id);
        HEG_CHECK(card && !physicalIds.contains(id));
        physicalIds.insert(id);
        HEG_CHECK(deckCounts.contains(card->getPackage()) == hegemony);
        if (!hegemony) continue;
        ++actualDeck[card->getPackage()];
        HEG_CHECK(room.getCard(id) && room.getCard(id)->isTransferable() == card->isTransferable());
        const QString nativeClass = QString::fromLatin1(card->metaObject()->className());
        const bool sharedNativeClass = sharedNativeEquipment.contains(card->objectName())
            || sharedNativeTricks.contains(card->objectName());
        if (card->getTypeId() == Card::TypeBasic)
            HEG_CHECK(!nativeClass.startsWith("H") && nativeClass == card->getClassName());
        else if (sharedNativeClass)
            HEG_CHECK(!nativeClass.startsWith("H") && nativeClass == card->getClassName());
        else
            HEG_CHECK(isHegemonyCardClassName(nativeClass) || nativeClass == "HegNullification");
        // HegNullification is a native Nullification subtype, not H + egNullification.
        if (nativeClass == "HegNullification") HEG_CHECK(card->isKindOf("Nullification"));
        const QByteArray canonicalClass = (isHegemonyCardClassName(nativeClass)
            ? nativeClass.mid(1) : nativeClass).toLatin1();
        HEG_CHECK(card->isKindOf(canonicalClass.constData()));
        std::unique_ptr<Card> clone(Sanguosha->cloneCard(card));
        if (!clone || clone->metaObject() != card->metaObject())
            qCritical() << "Physical clone factory mismatch" << id << card->objectName()
                << card->getPackage() << nativeClass
                << (clone ? clone->metaObject()->className() : "null");
        HEG_CHECK(clone && clone->metaObject() == card->metaObject());
        HEG_CHECK(clone->objectName() == card->objectName());
        HEG_CHECK(clone->getSuit() == card->getSuit() && clone->getNumber() == card->getNumber());
        HEG_CHECK(clone->getEffectiveId() == id && clone->isTransferable() == card->isTransferable());
        if (card->getPackage() == "heg_formation_equip") HEG_CHECK(card->isKindOf("DragonPhoenix"));
        if (card->getPackage() == "heg_momentum_equip") HEG_CHECK(card->isKindOf("PeaceSpell"));
    }
    // This is the admitted physical pool before session initialization reserves
    // the four Imperial Edict faction tricks outside the ordinary draw pile.
    if (hegemony) HEG_CHECK(actualDeck == deckCounts && deck.size() == 169);

    std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash", Card::Spade, 7));
    HEG_CHECK(slash && slash->isKindOf("Slash"));
    HEG_CHECK(QString::fromLatin1(slash->metaObject()->className()) == "Slash");
    // Physical copying must retain the original factory even in an identity room.
    const Card *donorSlash = nullptr;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card && card->getPackage() == "heg_standard_cards" && card->objectName() == "slash") {
            donorSlash = card;
            break;
        }
    }
    HEG_CHECK(donorSlash);
    std::unique_ptr<Card> physicalCopy(Sanguosha->cloneCard(donorSlash));
    HEG_CHECK(physicalCopy && physicalCopy->metaObject() == donorSlash->metaObject());

    bool nativeHorse = false;
    for (const DistanceSkill *skill : Sanguosha->getDistanceSkills()) {
        nativeHorse = nativeHorse || skill->objectName() == "horse";
    }
    HEG_CHECK(nativeHorse);
    // Package origin does not remove card skills from either mode's registry.
    const auto targetMods = Sanguosha->getTargetModSkills();
    for (const QString &name : {"crossbow", "halberd", "heg_halberd-target"}) {
        const auto *skill = qobject_cast<const TargetModSkill *>(Sanguosha->getSkill(name));
        HEG_CHECK(skill && targetMods.contains(skill));
    }
    return (!hegemony || longdanOffsetContract()) && directViewAsContract(room) && skillModeAdmission(room, hegemony, slash.get())
        && luaContract(room, hegemony);
}

bool originalMetadata()
{
    // Qun binds the canonical identity definitions, never a second HEG implementation.
    const QMap<QString, QString> sharedQunSkills{
        {"heg_huatuo", "chuli"}, {"heg_pangde", "tenyearjianchu"}, {"heg_panfeng", "kuangfu"}, {"heg_jiling", "shuangren"}, {"heg_tianfeng", "sijian"}, {"heg_mateng", "xiongyi"}, {"heg_kongrong", "lirang"},
        {"heg_zhangjiao", "nosleiji"}, {"heg_yanliangwenchou", "shuangxiong"}};
    for (auto it = sharedQunSkills.cbegin(); it != sharedQunSkills.cend(); ++it) {
        const General *general = Sanguosha->getGeneral(it.key());
        const Skill *skill = Sanguosha->getSkill(it.value());
        HEG_CHECK(general && skill && general->hasSkill(it.value()));
        HEG_CHECK(general->getVisibleSkillList().contains(skill));
    }
    HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("chuli")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("tenyearjianchu")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("shuangren")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("sijian")));
    HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("xiongyi")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("lirang")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("nosleiji")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("shuangxiong")));
    HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("shuangxiong")));
    for (const QString &retired : {"heg_chuli", "heg_jianchu", "heg_kuangfu", "#heg_kuangfu-effect", "heg_shuangren", "#heg_shuangren-slash-ndl", "heg_sijian", "heg_xiongyi", "heg_lirang", "heg_leiji", "heg_shuangxiong", "#heg_shuangxiong"})
        HEG_CHECK(!Sanguosha->getSkill(retired));
    // Equal rules share definitions; donor differences bind the upgraded HEG IDs.
    const QMap<QString, QStringList> weiSkills{
        {"heg_caocao", {"nosjianxiong"}}, {"heg_simayi", {"nosfankui", "heg_guicai"}},
        {"heg_xiahoudun", {"heg_ganglie"}}, {"heg_zhangliao", {"tenyeartuxi"}},
        {"heg_xuchu", {"heg_luoyi"}}, {"heg_guojia", {"tiandu", "heg_yiji"}},
        {"heg_zhenji", {"qingguo", "heg_luoshen"}}, {"heg_xiahouyuan", {"heg_shensu"}},
        {"heg_zhanghe", {"qiaobian"}}, {"heg_xuhuang", {"heg_duanliang"}},
        {"heg_caoren", {"heg_jushou"}}, {"heg_dianwei", {"heg_qiangxi"}},
        {"heg_xunyu", {"quhu", "heg_jieming"}}, {"heg_caopi", {"xingshang", "mobilefangzhu"}},
        {"heg_yuejin", {"xiaoguo"}}};
    for (auto it = weiSkills.cbegin(); it != weiSkills.cend(); ++it) {
        const General *general = Sanguosha->getGeneral(it.key());
        HEG_CHECK(general);
        QStringList actual;
        for (const Skill *skill : general->getVisibleSkillList()) {
            HEG_CHECK(skill == Sanguosha->getSkill(skill->objectName()));
            actual << skill->objectName();
        }
        QStringList expected = it.value();
        actual.sort();
        expected.sort();
        if (actual != expected) {
            qCritical() << "Original HEG metadata skill mismatch" << it.key()
                        << "actual=" << actual << "expected=" << expected;
            return false;
        }
    }
    for (const QString &name : {"tenyeartuxi", "mobilefangzhu", "nosjianxiong", "nosfankui", "tiandu", "qiaobian", "xingshang",
                               "heg_luoshen", "#heg_luoshen-move", "xiaoguo"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    for (const QString &name : {"qingguo", "quhu", "qiaobian"})
        HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name)));
    // Removed duplicates must not survive as a second registered implementation.
    HEG_CHECK(!Sanguosha->getSkill("heg_tuxi") && !Sanguosha->getSkill("heg_fangzhu"));
    HEG_CHECK(!Sanguosha->getSkill("heg_qiaobian") && !Sanguosha->getSkill("heg_xiaoguo"));
    for (const QString &name : {"HTuxiCard", "HShensuCard", "HQiaobianCard", "HQiangxiCard", "HQuhuCard"}) {
        std::unique_ptr<SkillCard> removed(Sanguosha->cloneSkillCard(name));
        HEG_CHECK(!removed);
    }
    // Single-letter H must not consume native class names such as Halberd/Horse.
    Halberd nativeHalberd(Card::Spade, 12);
    HEG_CHECK(nativeHalberd.getClassName() == "Halberd");
    HEG_CHECK(nativeHalberd.getKindOfNames().contains("Halberd"));
    HEG_CHECK(!nativeHalberd.getKindOfNames().contains("alberd"));
    HEG_CHECK(!isHegemonyCardClassName("Horse") && !isHegemonyCardClassName("HuashenCard"));
    HEG_CHECK(isHegemonyCardClassName("HHalberd") && isHegemonyCardClassName("HRendeCard"));
    HEG_CHECK(!Sanguosha->getPackage("hegemony") && !Sanguosha->getPackage("h_formation")
        && !Sanguosha->getPackage("h_momentum") && !Sanguosha->getPackage("oh_standard"));
    for (const QString &shared : {"xiaoguo", "shushen", "shenzhi", "duanbing", "fenxun", "kuangfu", "zhendu", "qiluan"})
        HEG_CHECK(Sanguosha->getSkill(shared));
    // Registration must survive deletion of the old StandardPackage shared-skill hook.
    for (const QString &name : {"xiaoguo", "shushen", "shenzhi", "kuangfu", "zhendu", "qiluan"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    HEG_CHECK(dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill("duanbing")));
    const auto *fenxun = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill("fenxun"));
    HEG_CHECK(fenxun && fenxun->getN() == 1);
    HEG_CHECK(fenxun->historyKey(ActiveSkillRequest()) == "FenxunCard");
    std::unique_ptr<SkillCard> retiredFenxun(Sanguosha->cloneSkillCard("FenxunCard"));
    HEG_CHECK(!retiredFenxun);


    HEG_CHECK(Sanguosha->getGeneral("heg_machao")->hasSkill("mashu"));
    HEG_CHECK(Sanguosha->getSkill("mashu"));
    const General *dengai = Sanguosha->getGeneral("heg_dengai");
    const General *jiangwei = Sanguosha->getGeneral("heg_jiangwei");
    const General *sunce = Sanguosha->getGeneral("heg_sunce");
    HEG_CHECK(dengai && jiangwei && sunce);
    HEG_CHECK(dengai->getDoubleMaxHp() == 4 && dengai->getMaxHpHead() == 3 && dengai->getMaxHpDeputy() == 4);
    HEG_CHECK(jiangwei->getDoubleMaxHp() == 4 && jiangwei->getMaxHpHead() == 4 && jiangwei->getMaxHpDeputy() == 3);
    HEG_CHECK(sunce->getDoubleMaxHp() == 4 && sunce->getMaxHpHead() == 4 && sunce->getMaxHpDeputy() == 3);
    const Skill *tianfu = Sanguosha->getSkill("heg_tianfu");
    HEG_CHECK(tianfu && tianfu->relateToPlace(true) && !tianfu->relateToPlace(false));
    // Shu differences retain their V2 definitions; identical rules share originals.
    for (const QString &name : {"niepan", "xiangle", "fangquan", "lieren"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    for (const QString &name : {"lianhuan", "huoji", "kanpo", "fangquan"})
        HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name)));
    HEG_CHECK(!Sanguosha->getSkill("heg_xiangle"));
    HEG_CHECK(!Sanguosha->getSkill("#wusheng-target"));
    HEG_CHECK(dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill("#tenyearwushengmod")));
    HEG_CHECK(!Sanguosha->getSkill("heg_kuanggu"));
    // Shared originals own these effects; retired aliases must not be registered.
    HEG_CHECK(dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill("nosqicai")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("wansha")));
    HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("#bazhen")));
    HEG_CHECK(dynamic_cast<const ViewAsEquipSkill *>(Sanguosha->getSkill("bazhen")));
    for (const QString &name : {"heg_qicai", "heg_bazhen", "heg_liuli", "heg_wansha"})
        HEG_CHECK(!Sanguosha->getSkill(name));
    std::unique_ptr<SkillCard> retiredLiuli(Sanguosha->cloneSkillCard("HLiuliCard"));
    HEG_CHECK(!retiredLiuli);
    HEG_CHECK(Sanguosha->getGeneral("liushan")->hasSkill("xiangle"));
    // The server's nullification preflight still uses this legacy probe.
    HEG_CHECK(Sanguosha->getViewAsSkill("kanpo")->isEnabledAtResponse(nullptr, "nullification"));
    HEG_CHECK(!Sanguosha->getViewAsSkill("kanpo")->isEnabledAtPlay(nullptr));
    for (const QString &name : {"rende", "tenyearrende", "guanxing", "heg_yizhi", "heg_kongcheng",
                               "heg_longdan", "heg_tieqi", "heg_jizhi", "heg_liegong",
                               "tenyearkuanggu", "heg_huoshou", "heg_juxiang", "shushen", "shenzhi"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    for (const QString &name : {"rende", "tenyearrende", "tenyearwusheng", "heg_longdan"})
        HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name)));
    for (const QString &name : {"heg_rende", "heg_wusheng", "heg_guanxing", "heg_shushen", "heg_shenzhi"})
        HEG_CHECK(!Sanguosha->getSkill(name));
    // XXY Longdan must retain its offset event, not only its card conversion.
    HEG_CHECK(Sanguosha->getTriggerSkill("heg_longdan")->getTriggerEvents().contains(CardOffset));
    const QMap<QString, QStringList> expectedBindings{
        {"heg_liubei", {"tenyearrende"}}, {"heg_guanyu", {"tenyearwusheng"}},
        {"heg_zhugeliang", {"guanxing"}}, {"heg_ganfuren", {"shushen", "shenzhi"}},
        {"heg_huangyueying", {"nosqicai"}},
        {"heg_pangtong", {"lianhuan", "niepan"}}, {"heg_wolong", {"huoji", "kanpo", "bazhen"}},
        {"heg_liushan", {"xiangle", "fangquan"}}, {"heg_menghuo", {"heg_zaiqi"}},
        {"heg_simayi", {"heg_guicai"}}, {"heg_daqiao", {"guose", "liuli"}},
        {"heg_jiaxu", {"wansha"}}, {"heg_dengai", {"tuntian", "jixi"}},
        {"heg_weiyan", {"tenyearkuanggu"}}, {"heg_zhurong", {"lieren"}}};
    for (auto it = expectedBindings.cbegin(); it != expectedBindings.cend(); ++it) {
        const General *general = Sanguosha->getGeneral(it.key());
        HEG_CHECK(general);
        for (const QString &name : it.value())
            HEG_CHECK(general->hasSkill(name) && Sanguosha->getSkill(name));
    }
    std::unique_ptr<SkillCard> removedRende(Sanguosha->cloneSkillCard("HRendeCard"));
    std::unique_ptr<SkillCard> retiredNativeRende(Sanguosha->cloneSkillCard("RendeCard"));
    std::unique_ptr<SkillCard> removedFangquan(Sanguosha->cloneSkillCard("HFangquanCard"));
    HEG_CHECK(!removedRende && !retiredNativeRende && !removedFangquan);
    const auto *rende = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("rende"));
    HEG_CHECK(rende && rende->historyKey(ActiveSkillRequest()) == "RendeCard");
    return true;
}

}

int runOriginalHegemonyContentTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Original HEG bootstrap failed:" << error;
        return 1;
    }
    const bool hegemony = Config.EnableHegemony;
    const bool second = Config.Enable2ndGeneral;
    const bool ai = Config.EnableAI;
    const QStringList bannedPackages = Config.BanPackages;
    const bool dedup = Config.GeneralVersionDedup;
    const GameModeStruct gameMode = Config.GameMode;
    const ServerInfoStruct serverInfo = ServerInfo;
    const QVariantMap previousOverrides = Config.valueOverrides();
    auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = hegemony;
        Config.Enable2ndGeneral = second;
        Config.EnableAI = ai;
        Config.BanPackages = bannedPackages;
        Config.GeneralVersionDedup = dedup;
        Config.GameMode = gameMode;
        Config.setValueOverrides(previousOverrides);
        ServerInfo = serverInfo;
    });
    Config.EnableAI = true;
    // Both mode catalogs are tested independently of the saved package selection.
    Config.BanPackages.clear();
    Config.GeneralVersionDedup = false;
    ServerInfo.BanPackages.clear();
    ServerInfo.GameMode = "04p";
    Config.GameMode.mode_id = "04p";
    QVariantMap overrides = previousOverrides;
    for (const QString &key : {"Banlist/Cards", "Banlist/Roles", "Banlist/Basara", "Banlist/Hegemony"})
        overrides.insert(key, QStringList());
    // Explicitly request a legacy route so both rooms exercise the declared AI loader.
    overrides.insert("AiLegacyDirectCallbacks", QStringList{"askForUseCard:@@lianying"});
    overrides.insert("AiLegacyAdaptedCallbacks", QStringList());
    overrides.insert("AiIsolatedCallbacks", QStringList());
    Config.setValueOverrides(overrides);
    if (QCoreApplication::arguments().contains(QStringLiteral("--metadata-only"))) {
        Config.EnableHegemony = ServerInfo.EnableHegemony = false;
        return originalMetadata() ? 0 : 4;
    }
    if (!contentMode(true)) return 2;
    if (!contentMode(false)) return 3;
    if (!originalMetadata()) return 4;
    qInfo() << "ORIGINAL_HEGEMONY_CONTENT_TEST_RESULT status=PASS";
    return 0;
}
