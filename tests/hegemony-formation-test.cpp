#include "engine-bootstrap.h"
#include "ai.h"
#include "engine.h"
#include "general.h"
#include "gamerule.h"
#include "lua-runtime.h"
#include "package.h"
#include "room.h"
#include "room-state.h"
#include "room-test-access.h"
#include "server-info.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill.h"
#include "standard-cards.h"
#include "h-formation.h"

#include <QDebug>
#include <QScopeGuard>
#include <memory>

namespace {
#define HEG_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Formation V2 contract failed" << __LINE__ << #condition; \
    return false; \
} } while (false)

class SummonProbeAI : public TrustAI
{
public:
    explicit SummonProbeAI(ServerPlayer *player) : TrustAI(player) {}

    bool askForSkillInvoke(const QString &name, const QVariant &data) override
    {
        if (name == "FormationSummon" || name == "SiegeSummon") {
            summons << name;
            return true;
        }
        return TrustAI::askForSkillInvoke(name, data);
    }

    QString askForChoice(const QString &name, const QString &choices, const QVariant &data) override
    {
        if (choices.split('+').contains(QStringLiteral("showhead")))
            return QStringLiteral("showhead");
        return TrustAI::askForChoice(name, choices, data);
    }

    QStringList summons;
};

bool formationMetadata()
{
    const QMap<QString, QStringList> formationShared{
        {"heg_dengai", {"tuntian", "jixi"}}, {"heg_jiangwei", {"tiaoxin"}},
        {"heg_hetaihou", {"zhendu", "qiluan"}}};
    for (auto it = formationShared.cbegin(); it != formationShared.cend(); ++it) {
        const General *general = Sanguosha->getGeneral(it.key());
        HEG_CHECK(general);
        for (const QString &name : it.value()) {
            HEG_CHECK(general->hasSkill(name) && Sanguosha->getSkill(name));
            HEG_CHECK(!Sanguosha->getSkill("heg_" + name));
        }
    }
    for (const QString &name : {"heg_ziliang", "heg_huyuan", "heg_heyi", "heg_tianfu",
                               "heg_shengxi", "heg_shoucheng", "heg_niaoxiang", "heg_yicheng",
                               "heg_qianhuan", "heg_zhangwu", "heg_shouyue", "heg_jizhao"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    HEG_CHECK(dynamic_cast<const DistanceSkillV2 *>(Sanguosha->getSkill("heg_feiying")));
    const auto *shangyi = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_shangyi"));
    HEG_CHECK(shangyi && shangyi->getLimitScope() == Skill::Limit_Phase);
    for (const QString &name : {"HZiliangCard", "HHuyuanCard", "HTiaoxinCard", "HShangyiCard",
                               "HQianhuanCard", "HHeyiSummon", "HTianfuSummon", "HNiaoxiangSummon"}) {
        std::unique_ptr<SkillCard> removed(Sanguosha->cloneSkillCard(name));
        HEG_CHECK(!removed);
    }
    return true;
}

class SharedHegemonyProbeAI : public TrustAI
{
public:
    explicit SharedHegemonyProbeAI(ServerPlayer *player) : TrustAI(player) {}

    bool askForSkillInvoke(const QString &name, const QVariant &data) override
    {
        if (name == "wangxi") return ++wangxiPrompts == 1;
        if (name == "xunxun") return true;
        if (name == "qianxi") return false;
        return TrustAI::askForSkillInvoke(name, data);
    }

    int askForAG(const QList<int> &ids, bool, const QString &) override { return ids.first(); }
    QString askForChoice(const QString &name, const QString &choices, const QVariant &data) override
    {
        return name == "benghuai" ? QStringLiteral("hp") : TrustAI::askForChoice(name, choices, data);
    }

    int wangxiPrompts = 0;
};

bool sharedNonstandardV2Contracts(bool hegemony)
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
    RoomTestAccess::attachThread(room);
    room.getRoomState()->reset();
    ServerPlayer *owner = RoomTestAccess::addPlayer(room, "shared-v2-owner", "robot");
    ServerPlayer *other = RoomTestAccess::addOrdinaryPlayer(room, "shared-v2-other");
    auto *ai = new SharedHegemonyProbeAI(owner);
    ai->setParent(owner);
    owner->setAI(ai);
    for (ServerPlayer *player : {owner, other}) {
        player->setAlive(true);
        player->setMaxHp(4);
        player->setHp(4);
        player->setPhase(Player::NotActive);
    }
    owner->setNext(other);
    other->setNext(owner);
    owner->setSeat(1);
    other->setSeat(2);
    RoomTestAccess::resetAlive(room);
    room.setCurrent(owner);

    // Registered shared definitions, helpers and earned skills must all use V2.
    for (const QString &name : {"tuntian", "xunxun", "wangxi", "qianxi", "#qianxi-clear",
                               "zhendu", "qiluan", "benghuai"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    for (const QString &name : {"jixi", "tiaoxin", "kanpo"})
        HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name)));
    HEG_CHECK(dynamic_cast<const DistanceSkillV2 *>(Sanguosha->getSkill("#tuntian-dist")));
    const auto *yingziMax = dynamic_cast<const MaxCardsSkillV2 *>(Sanguosha->getSkill("#heg_yingzi-sunce-maxcards"));
    HEG_CHECK(yingziMax);
    CorrectSkillContext correction;
    correction.holder = owner;
    correction.primary = owner;
    correction.currentAmount = yingziMax->getBaseAmount();
    correction.currentAmount = 9;
    HEG_CHECK(!yingziMax->getCorrection(correction).applies); // A fixed maximum never also adds cards.
    HEG_CHECK(yingziMax->getFixedValue(correction).value == owner->getMaxHp());

    // Old AI strings must rebuild as native proxies, preserving their history key.
    const auto *tiaoxin = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("tiaoxin"));
    const int tiaoxinInstance = owner->acquireSkill("tiaoxin");
    room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
    std::unique_ptr<const Card> oldTiaoxin(Card::Parse("@TiaoxinCard=."));
    HEG_CHECK(oldTiaoxin);
    const_cast<Card *>(oldTiaoxin.get())->setActivationSkill("tiaoxin", tiaoxinInstance);
    CardUseStruct oldUse(oldTiaoxin.get(), owner, other);
    HEG_CHECK(RoomTestAccess::resolveCardInstance(room, oldUse));
    HEG_CHECK(oldUse.card->isKindOf("ActiveSkillCard"));
    ActiveSkillRequest tiaoxinRequest;
    tiaoxinRequest.initiator = owner;
    tiaoxinRequest.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    HEG_CHECK(tiaoxin->historyKey(tiaoxinRequest) == "TiaoxinCard");
    HEG_CHECK(tiaoxin->canActivate(tiaoxinRequest));
    owner->addHistory("TiaoxinCard");
    HEG_CHECK(!tiaoxin->canActivate(tiaoxinRequest));

    const auto *xunxun = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("xunxun"));
    owner->acquireSkill("xunxun");
    QVariant phaseData;
    HEG_CHECK(xunxun->triggerable(EventPhaseStart, &room, owner, phaseData).isEmpty());
    owner->setPhase(Player::Draw);
    HEG_CHECK(!xunxun->triggerable(EventPhaseStart, &room, owner, phaseData).isEmpty());

    // Give the real movement pipeline a bounded, known deck for the draw effects.
    room.getDrawPile().clear();
    for (int id = 0; id < 12; ++id) {
        HEG_CHECK(Sanguosha->getCard(id));
        room.getDrawPile() << id;
        room.setCardMapping(id, nullptr, Player::DrawPile);
    }
    SkillContext xunxunContext;
    xunxunContext.owner = owner;
    xunxunContext.invoker = owner;
    HEG_CHECK(xunxun->cost(EventPhaseStart, &room, owner, xunxunContext));
    HEG_CHECK(xunxun->effect(EventPhaseStart, &room, owner, xunxunContext));
    HEG_CHECK(owner->getHandcardNum() == 2 && room.getDrawPile().size() == 10);

    const auto *wangxi = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("wangxi"));
    const int instance = owner->acquireSkill("wangxi");
    DamageStruct damage("shared-v2-test", owner, other, 3);
    QVariant damageData = QVariant::fromValue(damage);
    HEG_CHECK(!wangxi->triggerable(Damage, &room, owner, damageData).isEmpty());
    other->setFlags("Global_DebutFlag");
    HEG_CHECK(wangxi->triggerable(Damage, &room, owner, damageData).isEmpty());
    other->setFlags("-Global_DebutFlag");
    SkillContext wangxiContext;
    wangxiContext.owner = owner;
    wangxiContext.invoker = owner;
    wangxiContext.instanceID = instance;
    wangxiContext.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey("wangxi", instance));
    wangxiContext.sourceRef = wangxiContext.activationRef;
    wangxiContext.original_data = &damageData;
    HEG_CHECK(wangxi->cost(Damage, &room, owner, wangxiContext));
    HEG_CHECK(!wangxi->effectTarget(Damage, &room, owner, wangxiContext, other));
    // Accept point one, decline point two: no third prompt or third draw occurs.
    HEG_CHECK(ai->wangxiPrompts == 2);
    HEG_CHECK(owner->getHandcardNum() == 3 && other->getHandcardNum() == 1);

    const auto *jixi = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("jixi"));
    owner->addToPile("field", 11);
    ActiveSkillRequest jixiRequest;
    jixiRequest.initiator = owner;
    jixiRequest.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    jixiRequest.selectedCardIds = {owner->handCards().first()};
    HEG_CHECK(!jixi->cardSelectionFeasible(jixiRequest));
    jixiRequest.selectedCardIds = {11, 11};
    HEG_CHECK(!jixi->cardSelectionFeasible(jixiRequest));
    jixiRequest.selectedCardIds = {11};
    HEG_CHECK(jixi->cardSelectionFeasible(jixiRequest));
    std::unique_ptr<const Card> snatch(jixi->createCard(jixiRequest));
    HEG_CHECK(snatch && snatch->isKindOf("Snatch") && snatch->getSubcards() == QList<int>{11});
    HEG_CHECK(jixi->historyKey(jixiRequest) == "Snatch");

    const auto *benghuai = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("benghuai"));
    owner->acquireSkill("benghuai");
    owner->setPhase(Player::Finish);
    HEG_CHECK(benghuai->triggerable(EventPhaseStart, &room, owner, phaseData).isEmpty());
    other->setHp(3);
    HEG_CHECK(!benghuai->triggerable(EventPhaseStart, &room, owner, phaseData).isEmpty());
    owner->setMark("benghuai_nullification-Clear", 1);
    HEG_CHECK(benghuai->triggerable(EventPhaseStart, &room, owner, phaseData).isEmpty());
    owner->setMark("benghuai_nullification-Clear", 0);
    SkillContext benghuaiContext;
    benghuaiContext.owner = owner;
    benghuaiContext.invoker = owner;
    benghuaiContext.bypass_cost = true;
    HEG_CHECK(!benghuai->effect(EventPhaseStart, &room, owner, benghuaiContext));
    HEG_CHECK(owner->getHp() == 3 && owner->getMaxHp() == 4);

    const auto *qianxi = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("qianxi"));
    const auto *qianxiClear = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("#qianxi-clear"));
    const int qianxiInstance = owner->acquireSkill("qianxi");
    SkillContext qianxiContext;
    qianxiContext.owner = owner;
    qianxiContext.invoker = owner;
    const QList<int> beforeDecline = room.getDrawPile();
    HEG_CHECK(!qianxi->cost(EventPhaseStart, &room, owner, qianxiContext));
    HEG_CHECK(room.getDrawPile() == beforeDecline && !owner->getTag("qianxi").isValid());

    JudgeStruct judge;
    judge.reason = "qianxi";
    judge.who = owner;
    judge.card = Sanguosha->getCard(0);
    QVariant judgeData = QVariant::fromValue(&judge);
    HEG_CHECK(qianxi->triggerable(FinishJudge, &room, owner, judgeData).isEmpty());
    HEG_CHECK(qianxi->recordEvent(FinishJudge, &room, owner, judgeData));
    HEG_CHECK(judge.pattern == (judge.card->isRed() ? "red" : "black"));
    HEG_CHECK(owner->getTag("qianxi").toString() == judge.pattern);

    // Simulate two already-applied effects with the same instance number on
    // different owners. Cleanup must survive source loss and preserve the other.
    ServerPlayer *secondOwner = RoomTestAccess::addOrdinaryPlayer(room, "shared-v2-second-owner");
    const QString pattern = ".|red|.|hand$0";
    const auto applyPending = [&](ServerPlayer *source) {
        const QVariantMap pending{{"target", other->objectName()}, {"color", "red"}, {"instance", 1}};
        source->setTag("QianxiPendingEffects", QVariantList{pending});
        if (other->getMark("@qianxi_red") == 0)
            room.setPlayerCardLimitation(other, "use,response", pattern, false, "qianxi");
        room.addPlayerMark(other, "@qianxi_red");
        room.setPlayerFlag(other, "QianxiTarget");
    };
    applyPending(owner);
    applyPending(secondOwner);
    HEG_CHECK(owner->removeSkillInstance("qianxi", qianxiInstance));
    PhaseChangeStruct endTurn;
    endTurn.from = Player::Finish;
    endTurn.to = Player::NotActive;
    QVariant endData = QVariant::fromValue(endTurn);
    Slash redSlash(Card::Heart, 1);
    HEG_CHECK(other->isCardLimited(&redSlash, Card::MethodUse, true));
    // Expire the later source first: pattern-keyed reason storage cannot prove this.
    HEG_CHECK(qianxiClear->recordEvent(EventPhaseChanging, &room, secondOwner, endData));
    HEG_CHECK(other->isCardLimited(&redSlash, Card::MethodUse, true));
    HEG_CHECK(other->isCardLimited(&redSlash, Card::MethodResponse, true));
    HEG_CHECK(other->getCardLimitationReasons(Card::MethodUse).contains("qianxi"));
    HEG_CHECK(other->getMark("@qianxi_red") == 1 && other->hasFlag("QianxiTarget"));
    HEG_CHECK(!secondOwner->getTag("QianxiPendingEffects").isValid());
    // The remaining effect clears even after source removal and both players' deaths.
    owner->setAlive(false);
    other->setAlive(false);
    DeathStruct death;
    death.who = owner;
    QVariant deathData = QVariant::fromValue(death);
    HEG_CHECK(qianxiClear->recordEvent(Death, &room, owner, deathData));
    HEG_CHECK(!other->isCardLimited(&redSlash, Card::MethodUse, true));
    HEG_CHECK(!other->isCardLimited(&redSlash, Card::MethodResponse, true));
    HEG_CHECK(!other->getCardLimitationReasons(Card::MethodUse).contains("qianxi"));
    HEG_CHECK(other->getMark("@qianxi_red") == 0 && !other->hasFlag("QianxiTarget"));
    return true;
}

bool arrayProxyContracts()
{
    // Formation, Momentum, and XXY must expose the same V2 no-target proxy.
    for (const QString &name : {"heg_heyi", "heg_tianfu", "heg_niaoxiang", "heg_fengshi",
                                "heg_fengyang", "heg_fangyuan"})
        HEG_CHECK(dynamic_cast<const HArraySummon *>(Sanguosha->getViewAsSkill(name)));

    // Assert the production array definitions directly, without a donor adapter.
    for (const QString &name : {"heg_fengyang", "heg_fangyuan"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));

    const bool previousHegemony = Config.EnableHegemony;
    const bool previousSecond = Config.Enable2ndGeneral;
    const ServerInfoStruct previousServerInfo = ServerInfo;
    const QVariantMap previousOverrides = Config.valueOverrides();
    const auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = previousHegemony;
        Config.Enable2ndGeneral = previousSecond;
        ServerInfo = previousServerInfo;
        Config.setValueOverrides(previousOverrides);
    });
    Config.EnableHegemony = true;
    Config.Enable2ndGeneral = true;
    ServerInfo.EnableHegemony = true;
    ServerInfo.Enable2ndGeneral = true;
    QVariantMap overrides = previousOverrides;
    overrides.insert("EnableHegemony", true);
    Config.setValueOverrides(overrides);

    auto runProductionUse = [&](const QString &skillName, const QString &head,
                                const QString &deputy, int expectedBind) {
        Room room(nullptr, QStringLiteral("04p"));
        EngineRuntimeContextScope engineScope(*Sanguosha, &room);
        LuaRuntime::Binding luaBinding(*room.luaRuntime());
        RoomTestAccess::attachThread(room);
        room.getRoomState()->reset();
        // Definitions belong to this room's overlay, not the next test suite.
        auto *package = new Package(QStringLiteral("test_hegemony_array_contract"));
        auto *fengyang = new General(package, "test_array_fengyang", "wei", 4);
        fengyang->addSkill("heg_fengyang");
        auto *fangyuan = new General(package, "test_array_fangyuan", "wei", 4);
        fangyuan->addSkill("heg_fangyuan");
        new General(package, "test_array_empty", "wei", 4);
        new General(package, "test_array_enemy", "shu", 4);
        Sanguosha->addPackage(package);
        auto add = [&](const QString &name, const QString &actualHead,
                       const QString &actualDeputy, const QString &kingdom) {
            ServerPlayer *player = RoomTestAccess::addPlayer(room, name, "robot");
            auto *ai = new SummonProbeAI(player);
            ai->setParent(player);
            player->setAI(ai);
            player->setActualGeneral1Name(actualHead);
            player->setActualGeneral2Name(actualDeputy);
            player->setGeneralName("anjiang");
            player->setGeneral2Name("anjiang");
            player->setGeneralShowed(false);
            player->setGeneral2Showed(false);
            player->setKingdom("god");
            player->setRole(HegemonyRule::getMappedRole(kingdom));
            player->setShownRole(false);
            player->setPhase(Player::NotActive);
            player->setSeat(room.getPlayers().size());
            player->setMaxHp(4);
            player->setHp(4);
            room.setTag(name, QStringList{actualHead, actualDeputy});
            player->setProperty("hegemony_generals", actualHead + '+' + actualDeputy);
            return player;
        };
        ServerPlayer *owner = add("array-owner", head, deputy, "wei");
        const bool siege = skillName == "heg_fangyuan";
        ServerPlayer *rightEnemy = siege
            ? add("array-right", "test_array_enemy", "test_array_enemy", "shu") : nullptr;
        ServerPlayer *target = add("array-target", "test_array_empty", "test_array_empty", "wei");
        if (!siege) rightEnemy = add("array-opposite", "test_array_enemy", "test_array_enemy", "shu");
        ServerPlayer *leftEnemy = add("array-left", "test_array_enemy", "test_array_enemy", "shu");
        const QList<ServerPlayer *> players = room.getPlayers();
        for (int i = 0; i < players.size(); ++i)
            players.at(i)->setNext(players.at((i + 1) % players.size()));
        RoomTestAccess::resetAlive(room);
        room.setCurrent(owner);
        room.preparePlayers();
        room.showGeneral(rightEnemy, "h");
        room.showGeneral(leftEnemy, "h");
        HEG_CHECK(rightEnemy->hasShownOneGeneral() && leftEnemy->hasShownOneGeneral());
        owner->setPhase(Player::Play);

        const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(skillName));
        HEG_CHECK(skill);
        const QList<int> ids = owner->getSkillInstanceIds(skillName);
        HEG_CHECK(ids.size() == 1 && owner->findSkillInstance(skillName, ids.first())->bindHead == expectedBind);
        ActiveSkillRequest request;
        request.initiator = owner;
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        request.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(skillName, ids.first()));
        room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        // A blocked exact source cannot be admitted or reveal either general.
        owner->setDisableShow(expectedBind == 1 ? "h" : "d", "array_contract");
        // An unbound second copy must not authorize the blocked innate source.
        const int otherInstance = owner->acquireSkill(skillName);
        HEG_CHECK(otherInstance > 0 && otherInstance != ids.first());
        HEG_CHECK(!skill->canActivate(request));
        HEG_CHECK(!RoomTestAccess::resolveActiveRequest(room, owner, skill, request));
        HEG_CHECK(!owner->hasShownOneGeneral() && !target->hasShownOneGeneral());
        HEG_CHECK(owner->removeSkillInstance(skillName, otherInstance));
        owner->removeDisableShow("array_contract");
        HEG_CHECK(skill->canActivate(request));
        const Card *resolved = RoomTestAccess::resolveActiveRequest(room, owner, skill, request);
        HEG_CHECK(resolved && resolved->isKindOf("ActiveSkillCard"));
        CardUseStruct use(resolved, owner);
        use.setOwnedCard(const_cast<Card *>(resolved));
        use.activationRef = request.activationRef;
        use.sourceRef = request.activationRef;
        use.m_validateTargets = true;
        HEG_CHECK(room.useCard(use) && use.cardFinished);
        const QStringList expectedSummons{siege ? "SiegeSummon" : "FormationSummon"};
        HEG_CHECK(static_cast<SummonProbeAI *>(target->getAI())->summons == expectedSummons);
        HEG_CHECK(target->hasShownOneGeneral() && target->isFriendWith(owner));
        if (expectedBind == 1)
            HEG_CHECK(owner->hasShownGeneral() && !owner->hasShownGeneral2());
        else
            HEG_CHECK(!owner->hasShownGeneral() && owner->hasShownGeneral2());
        return true;
    };

    HEG_CHECK(runProductionUse("heg_fengyang", "test_array_fengyang", "test_array_empty", 1));
    HEG_CHECK(runProductionUse("heg_fangyuan", "test_array_empty", "test_array_fangyuan", 2));

    Room room(nullptr, QStringLiteral("04p"));
    ServerPlayer owner(&room);
    ActiveSkillRequest request;
    request.initiator = &owner;
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    // An unowned source cannot summon.
    const auto *array = dynamic_cast<const HArraySummon *>(Sanguosha->getViewAsSkill("heg_niaoxiang"));
    HEG_CHECK(array && !array->canActivate(request));

    for (const QString &name : {"HFengyangSummon", "HFangyuanSummon"}) {
        std::unique_ptr<SkillCard> retired(Sanguosha->cloneSkillCard(name));
        HEG_CHECK(!retired);
    }
    return true;
}

bool formationMode(bool hegemony)
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
    {
        // getNextAlive() follows the Room roster as well as the seat ring.
        ServerPlayer &owner = *RoomTestAccess::addOrdinaryPlayer(room, "formation-owner");
        ServerPlayer &right = *RoomTestAccess::addOrdinaryPlayer(room, "formation-right");
        ServerPlayer &opposite = *RoomTestAccess::addOrdinaryPlayer(room, "formation-opposite");
        ServerPlayer &left = *RoomTestAccess::addOrdinaryPlayer(room, "formation-left");
        RoomTestAccess::resetAlive(room);
        owner.setNext(&right);
        right.setNext(&opposite);
        opposite.setNext(&left);
        left.setNext(&owner);
        const auto *heyi = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_heyi"));
        const auto *shangyi = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_shangyi"));
        const auto *niaoxiang = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("heg_niaoxiang"));
        HEG_CHECK(heyi && shangyi && niaoxiang);
        ActiveSkillRequest request;
        request.initiator = &owner;
        request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
        request.pattern = "@@heg_heyi";
        HEG_CHECK(heyi->canActivate(request) == !hegemony);
        HEG_CHECK(heyi->targetMode() == (hegemony ? ViewAsSkillV2::NoTarget : ViewAsSkillV2::SelectTargets));
        HEG_CHECK(heyi->targetsFeasible(request, {}) == hegemony);
        if (!hegemony) {
            // Both clockwise and wraparound groups are legal; gaps and omission of self are not.
            HEG_CHECK(heyi->targetsFeasible(request, {&owner, &right}));
            HEG_CHECK(heyi->targetsFeasible(request, {&left, &owner, &right}));
            HEG_CHECK(heyi->targetsFeasible(request, {&owner, &right, &opposite, &left}));
            HEG_CHECK(!heyi->targetsFeasible(request, {&owner}));
            HEG_CHECK(!heyi->targetsFeasible(request, {&right, &opposite}));
            HEG_CHECK(!heyi->targetsFeasible(request, {&owner, &opposite}));
            HEG_CHECK(Sanguosha->getSkill("feiying") && Sanguosha->getSkill("kanpo"));
        }
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        request.pattern.clear();
        HEG_CHECK(shangyi->canActivate(request) == !hegemony); // Empty hand remains legal in identity mode.
        HEG_CHECK(shangyi->canSelectTarget(request, {}, &right));
        HEG_CHECK(!shangyi->canSelectTarget(request, {}, &owner));
        HEG_CHECK(niaoxiang->getFrequency(&owner) == (hegemony ? Skill::Compulsory : Skill::NotFrequent));
        if (!hegemony) {
            for (const QString &name : {"heg_heyi", "heg_tianfu", "heg_niaoxiang"})
                HEG_CHECK(!dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name))->canActivate(request));
        }
    }
    {
        ServerPlayer equipped(&room);
        const auto *array = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("heg_niaoxiang"));
        HEG_CHECK(array);
        ActiveSkillRequest summonRequest;
        summonRequest.initiator = &equipped;
        summonRequest.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        HEG_CHECK(array->targetMode() == ViewAsSkillV2::NoTarget);
        HEG_CHECK(!array->canActivate(summonRequest)); // An unowned array cannot summon.
        std::unique_ptr<const Card> summon(array->createCard(summonRequest));
        HEG_CHECK(summon && summon->isKindOf("ActiveSkillCard"));
        HEG_CHECK(summon->getSkillName() == "heg_niaoxiang");

        // Response selectors reject unrelated cards and duplicate submissions.
        for (const QString &name : {"heg_ziliang", "heg_huyuan", "heg_qianhuan"}) {
            const auto *selection = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name));
            HEG_CHECK(selection && !selection->willThrowSelectedCards());
            ActiveSkillRequest request;
            request.initiator = &equipped;
            request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
            request.pattern = "@@" + name;
            HEG_CHECK(selection->canActivate(request));
            HEG_CHECK(!selection->cardSelectionFeasible(request));
            request.selectedCardIds = {0, 0};
            HEG_CHECK(!selection->cardSelectionFeasible(request) && !selection->createCard(request));
            request.selectedCardIds.clear();
            request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
            HEG_CHECK(!selection->canActivate(request));
        }
    }
    return true;
}
}

int runHegemonyFormationTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Formation bootstrap failed:" << error;
        return 1;
    }
    const bool hegemony = Config.EnableHegemony;
    const bool second = Config.Enable2ndGeneral;
    const bool ai = Config.EnableAI;
    const int delay = Config.AIDelay;
    const ServerInfoStruct serverInfo = ServerInfo;
    const QVariantMap previousOverrides = Config.valueOverrides();
    const auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = hegemony;
        Config.Enable2ndGeneral = second;
        Config.EnableAI = ai;
        Config.AIDelay = delay;
        Config.setValueOverrides(previousOverrides);
        ServerInfo = serverInfo;
    });
    // Selection contracts do not require an AI runtime or a running game.
    Config.EnableAI = false;
    Config.AIDelay = 0;
    if (!formationMetadata()) return 2;
    if (!arrayProxyContracts()) return 3;
    if (!formationMode(true)) return 4;
    if (!formationMode(false)) return 5;
    if (!sharedNonstandardV2Contracts(true)) return 6;
    if (!sharedNonstandardV2Contracts(false)) return 7;
    qInfo() << "HEGEMONY_FORMATION_TEST_RESULT status=PASS";
    return 0;
}
