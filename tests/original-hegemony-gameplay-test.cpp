#include "ai.h"
#include "aux-skills.h"
#include "h-strategic-advantage.h"
#include "engine-bootstrap.h"
#include "engine.h"
#include "gamerule.h"
#include "general.h"
#include "lua-runtime.h"
#include "h-momentum.h"
#include "standard.h"
#include "standard-generals.h"
#include "thicket.h"
#include "h-standard-shu-generals.h"
#include "room-test-access.h"
#include "room-state.h"
#include "server-info.h"
#include "settings.h"
#include "skill-instance-utils.h"

#include <QCoreApplication>
#include <QDebug>
#include <QScopeGuard>

namespace {

#define HEG_CHECK(condition) do { if (!(condition)) { \
    qCritical() << "Original hegemony gameplay contract failed at line" << __LINE__ << #condition; \
    return false; \
} } while (false)

// Only chooses a supplied physical response. The engine still validates, pays,
// counter-nullifies and resolves the real imported card.
class PhysicalNullificationAI : public TrustAI
{
public:
    explicit PhysicalNullificationAI(ServerPlayer *player) : TrustAI(player) {}

    const Card *askForNullification(const Card *, ServerPlayer *, ServerPlayer *, bool positive) override
    {
        ++nullificationQueries;
        if (!response || positive != respondToPositive) return nullptr;
        const Card *answer = response;
        response = nullptr;
        return answer;
    }

    bool askForSkillInvoke(const QString &, const QVariant &) override { return invokeSkills; }

    int askForAG(const QList<int> &ids, bool refusable, const QString &reason) override
    {
        if (reason == "tenyearrende" && !ids.isEmpty()) return ids.first();
        return TrustAI::askForAG(ids, refusable, reason);
    }

    QString askForUseCard(const QString &pattern, const QString &prompt, const Card::HandlingMethod method) override
    {
        if (pattern == QLatin1String("@@tenyearrende")) {
            ++rendePromptQueries;
            return QStringLiteral(".");
        }
        return TrustAI::askForUseCard(pattern, prompt, method);
    }

    QString askForTriggerOrder(const QString &reason, QMap<ServerPlayer *, QStringList> &skills,
                              bool optional, const QVariant &data) override
    {
        // Ordered equipment contracts must not randomly skip the first target.
        if (chooseFirstEquipmentTarget && !skills.isEmpty() && !skills.first().isEmpty())
            return skills.first().first();
        return TrustAI::askForTriggerOrder(reason, skills, optional, data);
    }

    const Card *response = nullptr;
    bool respondToPositive = true;
    bool chooseFirstEquipmentTarget = false;
    bool invokeSkills = false;
    int rendePromptQueries = 0;
    int nullificationQueries = 0;
};

class OriginalGameplayFixture
{
public:
    OriginalGameplayFixture()
        : room(nullptr, "04p"), engineScope(*Sanguosha, &room), luaBinding(*room.luaRuntime())
    {
        RoomTestAccess::attachThread(room);
        room.getRoomState()->reset();
        // reset() creates card wrappers; a production start also establishes
        // their server locations before any obtain/draw operation.
        for (int id : room.getDrawPile()) room.setCardMapping(id, nullptr, Player::DrawPile);
    }

    ServerPlayer *add(const QString &head, const QString &deputy)
    {
        if (!Sanguosha->getGeneral(head) || !Sanguosha->getGeneral(deputy)) return nullptr;
        ServerPlayer *player = RoomTestAccess::addPlayer(room,
            QStringLiteral("h-contract-%1").arg(room.getPlayers().size() + 1), "robot");
        player->setActualGeneral1Name(head);
        player->setActualGeneral2Name(deputy);
        player->setGeneralName("anjiang");
        player->setGeneral2Name("anjiang");
        player->setGeneralShowed(false);
        player->setGeneral2Showed(false);
        player->setKingdom("god");
        player->setRole(HegemonyRule::getMappedRole(player->getActualGeneral1()->getKingdom()));
        player->setShownRole(false);
        player->setSeat(room.getPlayers().size());
        player->setPhase(Player::NotActive);
        player->setMaxHp(player->getGeneralMaxHp());
        player->setHp(player->getGeneralStartHp());
        player->setProperty("hegemony_generals", head + '+' + deputy);
        room.setTag(player->objectName(), QStringList{head, deputy});
        auto *ai = new PhysicalNullificationAI(player);
        ai->setParent(player);
        player->setAI(ai);
        ais.insert(player, ai);
        return player;
    }

    bool mixedPlayers()
    {
        return add("heg_zhouyu", "heg_luxun") && add("heg_liubei", "heg_zhangfei")
            && add("heg_caocao", "heg_xiahoudun") && add("heg_lvbu", "heg_diaochan");
    }

    void prepare()
    {
        const QList<ServerPlayer *> players = room.getPlayers();
        for (int i = 0; i < players.size(); ++i)
            players.at(i)->setNext(players.at((i + 1) % players.size()));
        RoomTestAccess::resetAlive(room);
        room.setCurrent(players.first());
        room.preparePlayers();
        // These direct card uses bypass the play request that sets RoomState.
        room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        auto *rule = new HegemonyRule(room.getThread());
        rule->setParent(room.getThread());
        room.getThread()->addTriggerSkill(rule);
        for (ServerPlayer *player : players) room.getThread()->addPlayerSkills(player);
    }

    int cardInDeck(const QString &name)
    {
        for (int id : room.getDrawPile()) {
            const Card *card = room.getCard(id);
            // The room's selected deck already determines the mode, not a class prefix.
            if (card && card->objectName() == name)
                return id;
        }
        return -1;
    }

    QList<int> giveBasicCards(ServerPlayer *player, int count)
    {
        QList<int> ids;
        for (int id : room.getDrawPile()) {
            if (room.getCard(id)->getTypeId() == Card::TypeBasic) ids << id;
            if (ids.size() == count) break;
        }
        for (int id : ids) {
            room.obtainCard(player, id, false);
            if (room.getDrawPile().contains(id) || room.getCardOwner(id) != player
                || room.getCardPlace(id) != Player::PlaceHand) {
                qCritical() << "Original hegemony fixture failed to obtain draw-pile card" << id;
                return QList<int>();
            }
        }
        return ids;
    }

    Room room;
    EngineRuntimeContextScope engineScope;
    LuaRuntime::Binding luaBinding;
    QMap<ServerPlayer *, PhysicalNullificationAI *> ais;
};

bool weiXiaoguoPaymentAndTargets()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *owner = fixture.add("heg_yuejin", "heg_caocao");
    ServerPlayer *otherOwner = fixture.add("heg_yuejin", "heg_simayi");
    ServerPlayer *target = fixture.add("heg_caoren", "heg_xuchu");
    HEG_CHECK(owner && otherOwner && target);
    fixture.prepare();
    owner->showGeneral(true, false, false);
    otherOwner->showGeneral(true, false, false);
    target->showGeneral(true, false, false);
    fixture.room.setCurrent(target);
    target->setPhase(Player::Finish);
    const QList<int> first = fixture.giveBasicCards(owner, 1);
    const QList<int> second = fixture.giveBasicCards(otherOwner, 1);
    HEG_CHECK(first.size() == 1 && second.size() == 1);
    const auto *skill = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill("xiaoguo"));
    HEG_CHECK(skill);
    QVariant data;
    const TriggerList candidates = skill->triggerable(EventPhaseStart, &fixture.room, target, data);
    HEG_CHECK(candidates.contains(owner) && candidates.contains(otherOwner) && !candidates.contains(target));

    SkillContext ctx;
    ctx.owner = owner;
    ctx.invoker = target;
    fixture.room.registerTestOverride(owner, "card", "xiaoguo", first.first());
    HEG_CHECK(skill->cost(EventPhaseStart, &fixture.room, owner, ctx));
    // A cancelled/intercepted invocation has selected a card but paid nothing.
    HEG_CHECK(fixture.room.getCardOwner(first.first()) == owner);
    HEG_CHECK(fixture.room.getCardPlace(first.first()) == Player::PlaceHand);
    HEG_CHECK(ctx.targets == QList<ServerPlayer *>{target});
    HEG_CHECK(skill->pay(EventPhaseStart, &fixture.room, owner, ctx));
    HEG_CHECK(fixture.room.getCardPlace(first.first()) == Player::DiscardPile);
    HEG_CHECK(!skill->pay(EventPhaseStart, &fixture.room, owner, ctx));

    SkillContext stale;
    stale.owner = otherOwner;
    stale.invoker = target;
    fixture.room.registerTestOverride(otherOwner, "card", "xiaoguo", second.first());
    HEG_CHECK(skill->cost(EventPhaseStart, &fixture.room, otherOwner, stale));
    fixture.room.obtainCard(target, second.first(), false);
    HEG_CHECK(!skill->pay(EventPhaseStart, &fixture.room, otherOwner, stale));
    HEG_CHECK(fixture.room.getCardOwner(second.first()) == target);

    const int equipId = fixture.cardInDeck("crossbow");
    HEG_CHECK(equipId >= 0);
    fixture.room.obtainCard(target, equipId, false);
    fixture.room.registerTestOverride(target, "card", ".Equip", equipId);
    const int before = owner->getHandcardNum(), hp = target->getHp();
    skill->effectTarget(EventPhaseStart, &fixture.room, owner, ctx, target);
    // The pinned xxyheaven donor awards one card after successful equipment payment.
    HEG_CHECK(owner->getHandcardNum() == before + 1 && target->getHp() == hp);
    HEG_CHECK(fixture.room.getCardPlace(equipId) == Player::DiscardPile);
    fixture.room.registerTestOverride(target, "card", ".Equip", -1);
    skill->effectTarget(EventPhaseStart, &fixture.room, owner, ctx, target);
    HEG_CHECK(target->getHp() == hp - 1);
    return true;
}

class ThrowingCostRevealProbe : public TriggerSkill
{
public:
    ThrowingCostRevealProbe(ServerPlayer *source, const QList<int> &cards, const SkillInstanceRef &ref,
                            int sourceMutation)
        : TriggerSkill("heg_test_throwing_cost_reveal"), source(source), cards(cards), ref(ref),
          sourceMutation(sourceMutation)
    {
        global = true;
        events << PreCardUsed << GeneralShown << CardUsed << CardFinished;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player != source) return false;
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getSkillName() != "heg_duoshi") return false;
            ++usesSeen;
            acceptedSource = use.activationRef == ref;
            for (int id : cards)
                heldBeforePayment = heldBeforePayment && room->getCardOwner(id) == source
                    && room->getCardPlace(id) == Player::PlaceHand;
        } else if (event == GeneralShown) {
            ++revealsSeen;
            exactDeputyShown = !data.toBool() && !source->hasShownGeneral()
                && source->hasShownGeneral2()
                && source->hasSkillInstance(ref.key.skillName, ref.key.instanceID);
            // GeneralShown callbacks must observe the conversion's paid material,
            // before the ordinary trick resolves.
            for (int id : cards)
                paidBeforeReveal = paidBeforeReveal && room->getCardOwner(id) != source
                    && room->getCardPlace(id) == Player::PlaceTable;
            // Exercise cleanup when the exact source disappears during reveal.
            if (sourceMutation == 1) {
                room->detachSkillFromPlayer(source,
                    SkillInstanceUtils::formatName(ref.key.skillName, ref.key.instanceID),
                    false, false, false);
            } else if (sourceMutation == 2) {
                source->setAlive(false);
            }
        } else if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || use.card->getSkillName() != "heg_duoshi") return false;
            ++cardsUsedSeen;
            usedTargets = use.to;
        } else {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || use.card->getSkillName() != "heg_duoshi") return false;
            ++finishesSeen;
            finishedMarked = use.cardFinished;
        }
        return false;
    }

    ServerPlayer *source;
    QList<int> cards;
    SkillInstanceRef ref;
    int sourceMutation;
    mutable int usesSeen = 0, revealsSeen = 0;
    mutable int cardsUsedSeen = 0, finishesSeen = 0;
    mutable QList<ServerPlayer *> usedTargets;
    mutable bool finishedMarked = false;
    mutable bool acceptedSource = false, exactDeputyShown = false;
    mutable bool heldBeforePayment = true, paidBeforeReveal = true;
};

class FinishInterruptionProbe : public TriggerSkill
{
public:
    FinishInterruptionProbe(const QString &cardClass, TriggerEvent interruption)
        : TriggerSkill("heg_test_finish_interruption"), cardClass(cardClass), interruption(interruption)
    {
        global = true;
        events << CardFinished;
    }

    int getPriority(TriggerEvent) const override { return 100; }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getClassName() != cardClass) return false;
        ++finishesSeen;
        markedBeforeDispatch = markedBeforeDispatch && use.cardFinished;
        throw interruption;
    }

    QString cardClass;
    TriggerEvent interruption;
    mutable int finishesSeen = 0;
    mutable bool markedBeforeDispatch = true;
};

class BurningCampsProbe : public TriggerSkill
{
public:
    explicit BurningCampsProbe(ServerPlayer *source)
        : TriggerSkill("heg_test_burning_camps"), source(source)
    {
        global = true;
        events << PreCardUsed << DamageComplete << CardFinished;
    }

    bool trigger(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == DamageComplete) {
            const DamageStruct damage = data.value<DamageStruct>();
            damagedTargets << damage.to;
            validDamage = validDamage && damage.card && damage.card->objectName() == "burning_camps"
                && damage.from == source && damage.damage == 1
                && damage.nature == DamageStruct::Fire && !damage.prevented;
            return false;
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->objectName() != "burning_camps") return false;
        if (event == PreCardUsed) {
            ++usesSeen;
            selectedTargets = use.to;
        } else {
            ++finishesSeen;
            finishedTargets = use.to;
            markedFinished = use.cardFinished;
        }
        return false;
    }

    ServerPlayer *source;
    mutable QList<ServerPlayer *> selectedTargets, damagedTargets, finishedTargets;
    mutable int usesSeen = 0, finishesSeen = 0;
    mutable bool validDamage = true, markedFinished = false;
};

bool burningCampsHitsOnlyNextFormation()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *source = fixture.add("heg_liubei", "heg_zhangfei");
    ServerPlayer *first = fixture.add("heg_caocao", "heg_xiahoudun");
    ServerPlayer *ally = fixture.add("heg_xuhuang", "heg_zhangliao");
    ServerPlayer *outsider = fixture.add("heg_zhouyu", "heg_luxun");
    HEG_CHECK(source && first && ally && outsider);
    fixture.prepare();
    for (ServerPlayer *player : fixture.room.getPlayers()) player->showGeneral(true, false, false);
    source->setPhase(Player::Play);
    HEG_CHECK(source->getNextAlive() == first && first->isFriendWith(ally));
    HEG_CHECK(!first->isFriendWith(source) && !first->isFriendWith(outsider));
    const QList<const Player *> formation = {first, ally};
    HEG_CHECK(first->getFormation() == formation);
    const QList<ServerPlayer *> expectedTargets = {first, ally};
    QList<int> hp;
    for (ServerPlayer *player : fixture.room.getPlayers()) hp << player->getHp();

    const int id = fixture.cardInDeck("burning_camps");
    HEG_CHECK(id >= 0);
    fixture.room.obtainCard(source, id, false);
    HEG_CHECK(fixture.room.getCardOwner(id) == source
        && fixture.room.getCardPlace(id) == Player::PlaceHand);
    auto *probe = new BurningCampsProbe(source);
    probe->setParent(fixture.room.getThread());
    fixture.room.getThread()->addTriggerSkill(probe);

    // Object IDs deliberately differ from general names: the imported AOE
    // must resolve formation members through the room's object-name lookup.
    CardUseStruct use(fixture.room.getCard(id), source);
    use.m_validateTargets = true;
    HEG_CHECK(fixture.room.useCard(use));
    HEG_CHECK(probe->usesSeen == 1 && probe->selectedTargets == expectedTargets);
    HEG_CHECK(probe->validDamage && probe->damagedTargets == expectedTargets);
    HEG_CHECK(source->getHp() == hp.at(0) && first->getHp() == hp.at(1) - 1
        && ally->getHp() == hp.at(2) - 1 && outsider->getHp() == hp.at(3));
    HEG_CHECK(probe->finishesSeen == 1 && probe->markedFinished
        && probe->finishedTargets == expectedTargets && use.cardFinished);
    HEG_CHECK(fixture.room.getCardPlace(id) == Player::DiscardPile
        && fixture.room.getDiscardPile().contains(id));
    return true;
}

bool throwingCostPrecedesPreciseReveal()
{
    // Reuse the complete setup for the ordinary path and both reveal-time
    // source invalidation paths; each iteration gets a fresh room/roster.
    for (int sourceMutation = 0; sourceMutation < 3; ++sourceMutation) {
        OriginalGameplayFixture fixture;
        ServerPlayer *source = fixture.add("heg_zhoutai", "heg_luxun");
        HEG_CHECK(source && fixture.add("heg_liubei", "heg_zhangfei")
            && fixture.add("heg_caocao", "heg_xiahoudun") && fixture.add("heg_lvbu", "heg_diaochan")
            && fixture.add("heg_sunquan", "heg_zhouyu"));
        fixture.prepare();
        source->setPhase(Player::Play);
        ServerPlayer *wuAlly = fixture.room.getPlayers().last();
        wuAlly->showGeneral(true, false, false);
        int materialId = -1;
        for (int id : fixture.room.getDrawPile()) {
            if (fixture.room.getCard(id)->isRed()) { materialId = id; break; }
        }
        HEG_CHECK(materialId >= 0);
        fixture.room.obtainCard(source, materialId, false);
        const QList<int> cards{materialId};
        const QList<int> instances = source->getSkillInstanceIds("heg_duoshi");
        HEG_CHECK(instances.size() == 1);
        const SkillInstanceRef ref(source->objectName(), SkillInstanceKey("heg_duoshi", instances.first()));
        auto *probe = new ThrowingCostRevealProbe(source, cards, ref, sourceMutation);
        probe->setParent(fixture.room.getThread());
        fixture.room.getThread()->addTriggerSkill(probe);

        const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill("heg_duoshi"));
        HEG_CHECK(skill);
        ActiveSkillRequest request;
        request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
        request.initiator = source;
        request.activationRef = ref;
        request.selectedCardIds = cards;
        auto *converted = const_cast<Card *>(skill->createCard(request));
        HEG_CHECK(converted);
        CardUseStruct use(converted, source);
        // Acquire the native event lease before requesting managed deletion.
        converted->deleteLater();
        converted->setSkillInstanceId(instances.first());
        use.activationRef = ref;
        use.m_validateTargets = true;
        HEG_CHECK(fixture.room.useCard(use, true));
        HEG_CHECK(probe->usesSeen == 1 && probe->revealsSeen == 1);
        HEG_CHECK(probe->acceptedSource && probe->exactDeputyShown);
        HEG_CHECK(probe->heldBeforePayment && probe->paidBeforeReveal);
        HEG_CHECK(probe->finishesSeen == 1 && probe->finishedMarked);
        if (sourceMutation == 0) {
            HEG_CHECK(probe->cardsUsedSeen == 1 && probe->usedTargets.contains(wuAlly));
        } else {
            // Removing/killing the exact source during GeneralShown must reject
            // CardUsed while still completing one guarded finish and payment.
            HEG_CHECK(probe->cardsUsedSeen == 0);
            HEG_CHECK(sourceMutation == 1
                ? !source->hasSkillInstance(ref.key.skillName, ref.key.instanceID)
                : !source->isAlive());
        }
        HEG_CHECK(use.cardFinished && source->usedTimes("DuoshiAE") == 1);
        for (int id : cards) HEG_CHECK(fixture.room.getCardPlace(id) == Player::DiscardPile);
        if (sourceMutation == 2)
            RoomTestAccess::resetAlive(fixture.room);
    }
    return true;
}

bool momentumRemovalKeepsSurvivingInstances()
{
    const QMap<QString, QStringList> shared{
        {"heg_lidian", {"xunxun", "wangxi"}}, {"heg_madai", {"mashu", "qianxi"}}, {"heg_sunce", {"heg_jiang"}}};
    for (auto it = shared.cbegin(); it != shared.cend(); ++it) {
        const General *general = Sanguosha->getGeneral(it.key());
        HEG_CHECK(general);
        for (const QString &name : it.value()) HEG_CHECK(general->hasSkill(name) && Sanguosha->getSkill(name));
    }
    for (const QString &name : {"heg_hengjiang", "heg_guixiu", "heg_yongjue", "heg_yingyang", "heg_hunshang",
                               "heg_fenming", "heg_hengzheng", "heg_baoling", "heg_chuanxin", "heg_fengshi", "heg_wuxin", "heg_hongfa"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    for (const QString &name : {"heg_cunsi", "heg_duanxie", "heg_wendao", "heg_hongfa_slash"})
        HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill(name)));
    const bool hegemony = Config.EnableHegemony;
    const bool serverHegemony = ServerInfo.EnableHegemony;
    const auto restore = qScopeGuard([&]() {
        Config.EnableHegemony = hegemony;
        ServerInfo.EnableHegemony = serverHegemony;
    });
    for (bool identity : {false, true}) {
        Config.EnableHegemony = ServerInfo.EnableHegemony = true;
        OriginalGameplayFixture fixture;
        ServerPlayer *source = fixture.add("heg_mifuren", "heg_guanyu");
        HEG_CHECK(source && fixture.add("heg_liubei", "heg_zhangfei")
            && fixture.add("heg_caocao", "heg_xiahoudun") && fixture.add("heg_zhouyu", "heg_luxun"));
        fixture.prepare();
        source->showGeneral(true);
        const int retired = source->getSkillInstanceIds("heg_guixiu").first();
        const int survivor = fixture.room.acquireSkill(source, "heg_guixiu");
        HEG_CHECK(survivor > 0 && survivor != retired);
        source->setHp(1);
        fixture.ais.value(source)->invokeSkills = true;
        fixture.ais.value(source)->chooseFirstEquipmentTarget = true;
        Config.EnableHegemony = ServerInfo.EnableHegemony = !identity;
        if (identity) {
            // An event naming a still-owned instance is not a retirement capability.
            QVariant forged = SkillChangeStruct("heg_guixiu", survivor).toVariant();
            fixture.room.getThread()->trigger(EventLoseSkill, &fixture.room, source, forged);
            HEG_CHECK(source->getHp() == 1);
            fixture.room.detachSkillFromPlayer(source, SkillInstanceUtils::formatName("heg_guixiu", retired));
        } else source->removeGeneral(true);
        HEG_CHECK(source->getHp() == 2);
        HEG_CHECK(!source->hasSkillInstance("heg_guixiu", retired));
        HEG_CHECK(source->hasSkillInstance("heg_guixiu", survivor));
    }
    return true;
}

bool customCardFinishIsNotRepeated()
{
    for (TriggerEvent interruption : {TurnBroken, StageChange}) {
        for (int variant = 0; variant < 2; ++variant) {
            OriginalGameplayFixture fixture;
            ServerPlayer *source = fixture.add("heg_diaochan", "heg_jiaxu");
            HEG_CHECK(source && fixture.add("heg_liubei", "heg_zhangfei")
                && fixture.add("heg_caocao", "heg_xiahoudun") && fixture.add("heg_zhouyu", "heg_luxun"));
            fixture.prepare();
            source->setPhase(Player::Play);
            SkillCard *card = variant == 0 ? static_cast<SkillCard *>(new LijianCard)
                : static_cast<SkillCard *>(new LuanwuCard);
            CardUseStruct use(card, source);
            card->deleteLater();
            const QString skill = variant == 0 ? "lijian" : "luanwu";
            const QList<int> instances = source->getSkillInstanceIds(skill);
            HEG_CHECK(instances.size() == 1);
            card->setSkillName(skill);
            card->setSkillInstanceId(instances.first());
            if (variant == 0) {
                const QList<int> cost = fixture.giveBasicCards(source, 1);
                HEG_CHECK(cost.size() == 1);
                card->addSubcard(cost.first());
            } else if (variant == 1) {
                fixture.room.setPlayerMark(source, "@chaos", 1);
            }
            auto *probe = new FinishInterruptionProbe(card->getClassName(), interruption);
            probe->setParent(fixture.room.getThread());
            fixture.room.getThread()->addTriggerSkill(probe);
            use.activationRef = SkillInstanceRef(source->objectName(), SkillInstanceKey(skill, instances.first()));
            if (variant == 0) use.to << fixture.room.getPlayers().at(1) << fixture.room.getPlayers().at(2);
            // Exercise the real custom lifecycle and payment while isolating
            // the finish exception from Duel/forced-Slash gameplay effects.
            use.skipSkillEffect = true;
            bool interrupted = false;
            try {
                fixture.room.useCard(use);
            } catch (TriggerEvent event) {
                HEG_CHECK(event == interruption);
                interrupted = true;
            }
            HEG_CHECK(interrupted && probe->finishesSeen == 1);
            HEG_CHECK(probe->markedBeforeDispatch && use.cardFinished);
            HEG_CHECK(!source->hasFlag("Global_ProcessBroken"));
        }
    }
    return true;
}

bool zhihengV2MaximumHpAndPaymentContract()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *source = fixture.add("heg_sunquan", "heg_zhoutai");
    HEG_CHECK(source && fixture.add("heg_liubei", "heg_zhangfei")
        && fixture.add("heg_caocao", "heg_xiahoudun") && fixture.add("heg_lvbu", "heg_diaochan"));
    fixture.prepare();
    source->setPhase(Player::Play);
    source->setMaxHp(2);
    source->setHp(1);
    const QList<int> cards = fixture.giveBasicCards(source, 3);
    HEG_CHECK(cards.size() == 3);
    const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill("heg_zhiheng"));
    HEG_CHECK(skill && !source->hasSkill("zhiheng"));
    HEG_CHECK(skill->getLimitScope() == Skill::Limit_Phase
        && skill->getMaxUsageLimit(SkillContext()) == 1);
    const QList<int> instances = source->getSkillInstanceIds("heg_zhiheng");
    HEG_CHECK(instances.size() == 1);
    ActiveSkillRequest request;
    request.initiator = source;
    request.activationRef = SkillInstanceRef(source->objectName(),
        SkillInstanceKey("heg_zhiheng", instances.first()));
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    HEG_CHECK(skill->canActivate(request) && !skill->createCard(request));
    request.selectedCardIds = cards.mid(0, 2);
    // Two cards are legal at one HP: maximum HP controls the selection bound.
    HEG_CHECK(skill->cardSelectionFeasible(request));
    HEG_CHECK(!skill->canSelectCard(request, fixture.room.getCard(cards.last())));
    request.selectedCardIds = cards;
    HEG_CHECK(!skill->cardSelectionFeasible(request) && !skill->createCard(request));
    request.selectedCardIds = {cards.first(), cards.first()};
    HEG_CHECK(!skill->cardSelectionFeasible(request));
    request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    HEG_CHECK(!skill->canActivate(request));
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    request.selectedCardIds = cards.mid(0, 2);
    fixture.room.obtainCard(fixture.room.getPlayers().at(1), cards.last(), false);

    auto *proxy = const_cast<Card *>(skill->createCard(request));
    HEG_CHECK(proxy && proxy->isKindOf("ActiveSkillCard") && proxy->targetFixed());
    CardUseStruct use(proxy, source);
    proxy->deleteLater();
    proxy->setSkillInstanceId(instances.first());
    use.activationRef = request.activationRef;
    use.m_validateTargets = true;
    HEG_CHECK(fixture.room.useCard(use, true) && use.cardFinished);
    // All two hand cards were paid once, and exactly two were drawn (no all-hand bonus).
    HEG_CHECK(source->getHandcardNum() == 2 && source->usedTimes("HZhihengCard") == 1);
    for (int id : request.selectedCardIds)
        HEG_CHECK(fixture.room.getCardPlace(id) == Player::DiscardPile);
    SkillContext context;
    context.owner = context.initiator = context.invoker = source;
    context.sourceRef = context.activationRef = request.activationRef;
    context.skill_name = "heg_zhiheng";
    context.instanceID = instances.first();
    HEG_CHECK(!skill->isUsable(context));
    return true;
}

bool sharedDingfengV2Contract()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *source = fixture.add("heg_dingfeng", "heg_luxun");
    ServerPlayer *near = fixture.add("heg_liubei", "heg_zhangfei");
    ServerPlayer *far = fixture.add("heg_caocao", "heg_xiahoudun");
    HEG_CHECK(source && near && far && fixture.add("heg_lvbu", "heg_diaochan"));
    fixture.prepare();
    source->setPhase(Player::Play);
    source->showGeneral(true);
    HEG_CHECK(source->distanceTo(near) == 1 && source->distanceTo(far) == 2);
    Slash slash(Card::NoSuit, 0);
    // The extra slot is candidate-specific, rather than an unrestricted second target.
    HEG_CHECK(slash.targetFilter(QList<const Player *>{far}, near, source));
    HEG_CHECK(!slash.targetFilter(QList<const Player *>{near}, far, source));
    HEG_CHECK(!slash.targetFilter(QList<const Player *>{far, near}, fixture.room.getPlayers().last(), source));

    const auto *fenxun = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill("fenxun"));
    HEG_CHECK(fenxun && !source->getSkillInstanceIds("fenxun").isEmpty());
    const int instance = source->getSkillInstanceIds("fenxun").first();
    const int material = fixture.room.getDrawPile().first();
    fixture.room.obtainCard(source, material, false);
    ActiveSkillRequest request;
    request.initiator = source;
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    request.activationRef = SkillInstanceRef(source->objectName(), SkillInstanceKey("fenxun", instance));
    request.selectedCardIds = {material};
    Card *proxy = const_cast<Card *>(fenxun->createCard(request));
    HEG_CHECK(proxy && proxy->isKindOf("ActiveSkillCard"));
    CardUseStruct use(proxy, source, far);
    // Acquire the event lease before requesting managed retirement.
    proxy->deleteLater();
    proxy->setSkillInstanceId(instance);
    use.activationRef = request.activationRef;
    use.m_validateTargets = true;
    const bool fenxunAccepted = fixture.room.useCard(use, true);
    if (!fenxunAccepted || !use.cardFinished) {
        qCritical() << "Fenxun use rejected"
                    << "accepted=" << fenxunAccepted
                    << "finished=" << use.cardFinished
                    << "activation=" << use.activationRef.ownerObjectName
                    << use.activationRef.key.skillName << use.activationRef.key.instanceID
                    << "source=" << use.sourceRef.ownerObjectName
                    << use.sourceRef.key.skillName << use.sourceRef.key.instanceID
                    << "headShown=" << source->hasShownGeneral()
                    << "deputyShown=" << source->hasShownGeneral2()
                    << "instanceIds=" << source->getSkillInstanceIds("fenxun")
                    << "history=" << source->usedTimes("FenxunCard")
                    << "materialPlace=" << fixture.room.getCardPlace(material);
    }
    HEG_CHECK(fenxunAccepted && use.cardFinished);
    HEG_CHECK(fixture.room.getCardPlace(material) == Player::DiscardPile);
    if (source->usedTimes("FenxunCard") != 1 || source->distanceTo(far) != 1) {
        qCritical() << "Fenxun result mismatch"
                    << "usedTimes=" << source->usedTimes("FenxunCard")
                    << "distance=" << source->distanceTo(far)
                    << "addHistory=" << use.m_addHistory
                    << "targets=" << source->getTag("FenxunTargets").toStringList()
                    << "history=" << source->getHistory();
    }
    HEG_CHECK(source->usedTimes("FenxunCard") == 1 && source->distanceTo(far) == 1);

    // A paid duration survives detachment, then expires even without an owning instance.
    fixture.room.detachSkillFromPlayer(source, SkillInstanceUtils::formatName("fenxun", instance));
    HEG_CHECK(!source->hasSkillInstance("fenxun", instance) && source->distanceTo(far) == 1);
    PhaseChangeStruct change;
    change.from = Player::Finish;
    change.to = Player::NotActive;
    QVariant data = QVariant::fromValue(change);
    fixture.room.getThread()->trigger(EventPhaseChanging, &fixture.room, source, data);
    HEG_CHECK(source->distanceTo(far) == 2 && source->getTag("FenxunTargets").toStringList().isEmpty());
    return true;
}

bool wuSharedSkillsAndDuoshiV2Contract()
{
    HEG_CHECK(sharedDingfengV2Contract());
    HEG_CHECK(zhihengV2MaximumHpAndPaymentContract());
    // Existing skills resolve to the shared definitions, including related helpers.
    const QMap<QString, QStringList> shared = {
        {"heg_ganning", {"qixi"}},
        {"heg_lvmeng", {"keji"}}, {"heg_huanggai", {"heg_kurou"}},
        {"heg_zhouyu", {"yingzi", "fanjian"}}, {"heg_daqiao", {"guose", "liuli"}},
        {"heg_luxun", {"heg_qianxun"}}, {"heg_sunshangxiang", {"jieyin", "xiaoji"}},
        {"heg_sunjian", {"yinghun"}}, {"heg_xiaoqiao", {"tenyeartianxiang", "hongyan"}},
        {"heg_taishici", {"tianyi"}}, {"heg_zhoutai", {"buqu", "mobilefenji"}},
        {"heg_lusu", {"haoshi", "dimeng"}}, {"heg_erzhang", {"zhijian", "guzheng"}},
        {"heg_dingfeng", {"duanbing", "fenxun"}}
    };
    for (auto it = shared.cbegin(); it != shared.cend(); ++it) {
        const General *general = Sanguosha->getGeneral(it.key());
        HEG_CHECK(general);
        for (const QString &name : it.value()) {
            const Skill *skill = Sanguosha->getSkill(name);
            HEG_CHECK(skill && general->hasSkill(name) && general->getSkillList().contains(skill));
            for (const Skill *helper : Sanguosha->getRelatedSkills(name))
                HEG_CHECK(general->getSkillList().contains(helper));
        }
    }

    // H generals must resolve the upgraded canonical definitions, not V1 copies.
    for (const QString &name : {"keji", "yingzi", "heg_qianxun", "xiaoji",
                               "yinghun", "buqu", "haoshi", "guzheng", "mobilefenji"})
        HEG_CHECK(dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getSkill(name)));
    for (const QString &name : {"qixi", "kurou", "fanjian", "guose", "liuli", "jieyin",
                               "tenyeartianxiang", "tianyi", "haoshi", "dimeng", "zhijian", "fenxun"})
        HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name)));
    HEG_CHECK(dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill("duanbing")));
    // Filtering has no V2 counterpart; keep the single canonical Hongyan filter.
    HEG_CHECK(dynamic_cast<const FilterSkill *>(Sanguosha->getSkill("hongyan")));

    OriginalGameplayFixture fixture;
    HEG_CHECK(fixture.mixedPlayers());
    fixture.prepare();
    ServerPlayer *luxun = fixture.room.getPlayers().first();
    const QString name = QStringLiteral("heg_duoshi");
    const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill(name));
    HEG_CHECK(skill && skill->getLimitScope() == Skill::Limit_Phase);
    HEG_CHECK(skill->getMaxUsageLimit(SkillContext()) == 4);
    const QList<int> instances = luxun->getSkillInstanceIds(name);
    HEG_CHECK(instances.size() == 1);
    // mixedPlayers binds Lu Xun to the deputy slot beside Zhou Yu.
    HEG_CHECK(luxun->findSkillInstance(name, instances.first())->bindHead == 2);
    ActiveSkillRequest request;
    request.initiator = luxun;
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    HEG_CHECK(skill->canActivate(request) && !skill->cardSelectionFeasible(request));
    HEG_CHECK(!skill->createCard(request));
    request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE;
    HEG_CHECK(!skill->canActivate(request));
    request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    request.pattern = "await_exhausted";
    HEG_CHECK(!skill->canActivate(request));
    return true;
}

bool drawEventsPreservePayloadAndPhaseScope()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *jade = fixture.add("heg_zhaoyun", "heg_menghuo");
    ServerPlayer *xuchu = fixture.add("heg_xuchu", "heg_xiahoudun");
    ServerPlayer *lusu = fixture.add("heg_lusu", "heg_luxun");
    HEG_CHECK(jade && xuchu && lusu && fixture.add("heg_caocao", "heg_xuhuang"));
    fixture.prepare();
    // The production RoomThread registers equipment trigger skills during
    // startup; this fixture attaches the JadeSeal skill explicitly because it
    // uses a Hegemony-prefixed trigger behind a native equipment object name.
    fixture.room.getThread()->addTriggerSkill(
        qobject_cast<const TriggerSkill *>(Sanguosha->getSkill("heg_JadeSeal")));
    jade->showGeneral(true, false, false);
    lusu->showGeneral(true, false, false);
    const int jadeId = fixture.cardInDeck("JadeSeal");
    HEG_CHECK(jadeId >= 0);
    fixture.room.moveCardTo(Sanguosha->getCard(jadeId), jade, Player::PlaceEquip, true);
    HEG_CHECK(jade->hasTreasure("JadeSeal"));

    // Exercise the production draw service: kill rewards keep their count and
    // typed recipient; only the draw-phase request gains JadeSeal's extra card.
    jade->setPhase(Player::Play);
    jade->drawCards(1, "kill");
    HEG_CHECK(jade->getHandcardNum() == 1);
    jade->setPhase(Player::Draw);
    jade->drawCards(2, "draw_phase");
    HEG_CHECK(jade->getHandcardNum() == 4);

    // Removing the real equipment must also remove the phase-only modifier;
    // a later ordinary draw request must receive exactly its requested count.
    const Card *jadeSeal = fixture.room.getCard(jadeId);
    fixture.room.moveCardTo(jadeSeal, nullptr, Player::DiscardPile, true);
    HEG_CHECK(!jade->hasTreasure("JadeSeal"));
    jade->setPhase(Player::Draw);
    jade->drawCards(2, "draw_phase");
    HEG_CHECK(jade->getHandcardNum() == 6);

    jade->setPhase(Player::NotActive);
    fixture.room.setCurrent(xuchu);
    HEG_CHECK(xuchu->hasSkill("heg_luoyi") && !xuchu->hasSkill("nosluoyi"));
    xuchu->setPhase(Player::Play);
    xuchu->drawCards(1, "kill");
    HEG_CHECK(xuchu->getHandcardNum() == 1 && !xuchu->hasFlag("nosluoyi"));
    xuchu->setPhase(Player::Draw);
    xuchu->drawCards(2, "draw_phase");
    // HLuoyi activates at Draw phase end / DamageCaused, not DrawNCards.
    HEG_CHECK(xuchu->getHandcardNum() == 3 && !xuchu->hasFlag("nosluoyi"));

    xuchu->setPhase(Player::NotActive);
    fixture.room.setCurrent(lusu);
    fixture.room.registerTestOverride(lusu, "skill_invoke", "haoshi", true);
    lusu->setPhase(Player::Play);
    lusu->drawCards(1, "kill");
    HEG_CHECK(lusu->getHandcardNum() == 1 && !lusu->hasFlag("haoshi"));
    fixture.room.registerTestOverride(lusu, "skill_invoke", "haoshi", true);
    lusu->setPhase(Player::Draw);
    lusu->drawCards(2, "draw_phase");
    HEG_CHECK(lusu->getHandcardNum() == 5 && !lusu->hasFlag("haoshi"));
    lusu->setFlags("haoshi");
    lusu->setPhase(Player::Play);
    lusu->drawCards(1, "kill");
    HEG_CHECK(lusu->getHandcardNum() == 6 && lusu->hasFlag("haoshi"));
    return true;
}

bool peaceSpellUninstallLosesHpAndDraws()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *player = fixture.add("heg_zhaoyun", "heg_menghuo");
    HEG_CHECK(player && fixture.add("heg_caocao", "heg_xiahoudun")
        && fixture.add("heg_liubei", "heg_zhangfei")
        && fixture.add("heg_zhouyu", "heg_luxun"));
    fixture.prepare();
    player->showGeneral(true, false, false);
    const int peaceSpellId = fixture.cardInDeck("PeaceSpell");
    HEG_CHECK(peaceSpellId >= 0);
    fixture.room.moveCardTo(Sanguosha->getCard(peaceSpellId), player, Player::PlaceEquip, true);
    HEG_CHECK(player->hasArmorEffect("PeaceSpell"));

    player->setHp(player->getMaxHp() - 1);
    const int hp = player->getHp();
    const int hand = player->getHandcardNum();
    fixture.room.moveCardTo(fixture.room.getCard(peaceSpellId), nullptr, Player::DiscardPile, true);

    // HPeaceSpell::onUninstall marks the movement; the registered V2 skill
    // observes the real CardsMoveOneTime dispatch and resolves its penalty.
    HEG_CHECK(!player->hasArmorEffect("PeaceSpell"));
    HEG_CHECK(player->getHp() == hp - 1 && player->getHandcardNum() == hand + 2);
    return true;
}

bool silverLionUninstallRecoversOnce()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *player = fixture.add("heg_zhaoyun", "heg_menghuo");
    HEG_CHECK(player && fixture.add("heg_caocao", "heg_xiahoudun")
        && fixture.add("heg_liubei", "heg_zhangfei")
        && fixture.add("heg_zhouyu", "heg_luxun"));
    fixture.prepare();
    player->showGeneral(true, false, false);
    const int silverLionId = fixture.cardInDeck("silver_lion");
    HEG_CHECK(silverLionId >= 0);
    fixture.room.moveCardTo(Sanguosha->getCard(silverLionId), player, Player::PlaceEquip, true);
    HEG_CHECK(player->hasArmorEffect("silver_lion"));
    player->setHp(player->getMaxHp() - 2);
    const int hp = player->getHp();
    fixture.room.moveCardTo(fixture.room.getCard(silverLionId), nullptr, Player::DiscardPile, true);
    HEG_CHECK(!player->hasArmorEffect("silver_lion") && player->getHp() == hp + 1);
    HEG_CHECK(!player->hasFlag("SilverLionRecover"));
    return true;
}

bool orderedEquipmentKeepsOwnerAndTargetIdentity()
{
    for (const QString &weaponName : {QStringLiteral("double_sword"), QStringLiteral("DragonPhoenix")}) {
        OriginalGameplayFixture fixture;
        ServerPlayer *owner = fixture.add("heg_zhaoyun", "heg_menghuo");
        ServerPlayer *first = fixture.add("heg_diaochan", "heg_daqiao");
        ServerPlayer *second = fixture.add("heg_xiaoqiao", "heg_sunshangxiang");
        HEG_CHECK(owner && first && second && fixture.add("heg_caocao", "heg_xiahoudun"));
        fixture.prepare();
        owner->showGeneral(true, false, false);
        first->showGeneral(true, false, false);
        second->showGeneral(true, false, false);
        const int weaponId = fixture.cardInDeck(weaponName);
        const int slashId = fixture.cardInDeck("slash");
        HEG_CHECK(weaponId >= 0 && slashId >= 0);
        fixture.room.moveCardTo(fixture.room.getCard(weaponId), owner, Player::PlaceEquip, true);
        fixture.ais.value(owner)->chooseFirstEquipmentTarget = true;
        const QString skillName = weaponName == "DragonPhoenix"
            ? QStringLiteral("heg_DragonPhoenix") : QStringLiteral("double_sword");
        fixture.room.registerTestOverride(owner, "skill_invoke", skillName, true);
        if (weaponName == "DragonPhoenix") {
            HEG_CHECK(fixture.giveBasicCards(first, 1).size() == 1);
            HEG_CHECK(fixture.giveBasicCards(second, 1).size() == 1);
        }
        const int hand = owner->getHandcardNum();
        CardUseStruct use(fixture.room.getCard(slashId), owner);
        use.to << first << second;
        QVariant data = QVariant::fromValue(use);
        fixture.room.getThread()->trigger(TargetSpecified, &fixture.room, owner, data);
        if (weaponName == "double_sword") {
            // Victims' AI declines invokes: only the weapon owner is authorized.
            HEG_CHECK(owner->getHandcardNum() == hand + 2);
        } else {
            // The first target ceases to be discardable; the second still resolves.
            HEG_CHECK(first->isKongcheng() && second->isKongcheng());
        }
    }
    return true;
}

bool woodenOxTransferMovesPile()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *source = fixture.add("heg_zhaoyun", "heg_menghuo");
    ServerPlayer *target = fixture.add("heg_caocao", "heg_xiahoudun");
    HEG_CHECK(source && target && fixture.add("heg_liubei", "heg_zhangfei")
        && fixture.add("heg_zhouyu", "heg_luxun"));
    fixture.prepare();
    source->showGeneral(true, false, false);
    target->showGeneral(true, false, false);
    const int woodenOxId = fixture.cardInDeck("wooden_ox");
    HEG_CHECK(woodenOxId >= 0);
    fixture.room.moveCardTo(Sanguosha->getCard(woodenOxId), source, Player::PlaceEquip, true);
    HEG_CHECK(source->hasTreasure("wooden_ox"));

    const QList<int> pileCards = fixture.giveBasicCards(source, 1);
    HEG_CHECK(pileCards.size() == 1);
    source->addToPile("wooden_ox", pileCards, false);
    HEG_CHECK(source->getPile("wooden_ox") == pileCards);

    fixture.room.moveCardTo(fixture.room.getCard(woodenOxId), source, target,
        Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
            source->objectName(), target->objectName(), "wooden_ox", QString()));
    HEG_CHECK(!source->hasTreasure("wooden_ox") && target->hasTreasure("wooden_ox"));
    HEG_CHECK(source->getPile("wooden_ox").isEmpty()
        && target->getPile("wooden_ox") == pileCards);
    return true;
}

bool rendeDonorThresholdAndBasicPrompt()
{
    OriginalGameplayFixture fixture;
    HEG_CHECK(fixture.mixedPlayers());
    fixture.prepare();
    ServerPlayer *liubei = fixture.room.getPlayers().at(1);
    ServerPlayer *target = fixture.room.getPlayers().at(2);
    ServerPlayer *secondTarget = fixture.room.getPlayers().at(3);
    fixture.room.setCurrent(liubei);
    liubei->setPhase(Player::Play);
    liubei->setHp(liubei->getMaxHp() - 2);
    const int hp = liubei->getHp();
    const QList<int> ids = fixture.giveBasicCards(liubei, 4);
    HEG_CHECK(ids.size() == 4);
    const QList<int> instances = liubei->getSkillInstanceIds("tenyearrende");
    HEG_CHECK(instances.size() == 1);

    // Submit the V2 proxy through the actual selection/payment/reveal pipeline.
    const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill("tenyearrende"));
    HEG_CHECK(skill);
    ActiveSkillRequest request;
    request.initiator = liubei;
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    request.activationRef = SkillInstanceRef(liubei->objectName(), SkillInstanceKey("tenyearrende", instances.first()));
    HEG_CHECK(!skill->createCard(request));
    request.selectedCardIds = {ids.first(), ids.first()};
    HEG_CHECK(!skill->cardSelectionFeasible(request));
    const SkillInstanceRef ref(liubei->objectName(), SkillInstanceKey("tenyearrende", instances.first()));
    auto useRende = [&](const QList<int> &gift, ServerPlayer *recipient) {
        // Each subsequent play request follows the response prompt's reason.
        fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
        ActiveSkillRequest current = request;
        current.selectedCardIds = gift;
        Card *card = const_cast<Card *>(skill->createCard(current));
        if (!card) return false;
        CardUseStruct use(card, liubei, recipient);
        card->deleteLater();
        card->setShowSkill("tenyearrende");
        card->setSkillInstanceId(instances.first());
        use.activationRef = ref;
        use.m_validateTargets = true;
        return fixture.room.useCard(use);
    };
    HEG_CHECK(useRende({ids.at(0)}, target));
    HEG_CHECK(liubei->getMark("tenyearrende-PlayClear") == 1);
    HEG_CHECK(fixture.ais.value(liubei)->rendePromptQueries == 0);
    HEG_CHECK(useRende({ids.at(1)}, secondTarget));
    HEG_CHECK(liubei->getMark("tenyearrende-PlayClear") == 2);
    HEG_CHECK(fixture.ais.value(liubei)->rendePromptQueries == 1);
    HEG_CHECK(liubei->getHp() == hp);
    HEG_CHECK(fixture.room.getCardOwner(ids.at(0)) == target
        && fixture.room.getCardOwner(ids.at(1)) == secondTarget);

    request.selectedCardIds = {ids.at(2)};
    fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
    Card *repeat = const_cast<Card *>(skill->createCard(request));
    HEG_CHECK(repeat);
    repeat->setShowSkill("tenyearrende");
    repeat->setSkillInstanceId(instances.first());
    CardUseStruct illegal(repeat, liubei, target);
    repeat->deleteLater();
    illegal.activationRef = ref;
    illegal.m_validateTargets = true;
    HEG_CHECK(!RoomTestAccess::resolveCardInstance(fixture.room, illegal));

    request.selectedCardIds = {ids.at(3)};
    Card *self = const_cast<Card *>(skill->createCard(request));
    HEG_CHECK(self);
    self->setShowSkill("tenyearrende");
    self->setSkillInstanceId(instances.first());
    CardUseStruct selfUse(self, liubei, liubei);
    self->deleteLater();
    selfUse.activationRef = ref;
    selfUse.m_validateTargets = true;
    HEG_CHECK(!RoomTestAccess::resolveCardInstance(fixture.room, selfUse));

    HEG_CHECK(useRende({ids.at(2), ids.at(3)}, fixture.room.getPlayers().at(0)));
    HEG_CHECK(liubei->getMark("tenyearrende-PlayClear") == 4
        && liubei->getHp() == hp
        && fixture.ais.value(liubei)->rendePromptQueries == 1);
    HEG_CHECK(liubei->hasShownGeneral() && !liubei->hasShownGeneral2());
    HEG_CHECK(fixture.room.getCardOwner(ids.at(0)) == target
        && fixture.room.getCardOwner(ids.at(1)) == secondTarget
        && fixture.room.getCardOwner(ids.at(2)) == fixture.room.getPlayers().at(0)
        && fixture.room.getCardOwner(ids.at(3)) == fixture.room.getPlayers().at(0));
    for (int id : ids)
        HEG_CHECK(fixture.room.getCardPlace(id) == Player::PlaceHand
            && !fixture.room.getDiscardPile().contains(id));
    HEG_CHECK(liubei->getSkillInstanceStateValue("tenyearrende", instances.first(), "given", 0) == 4);
    HEG_CHECK(liubei->getSkillInstanceStateValue("tenyearrende", instances.first(), "recipients").toStringList().size() == 3);
    PhaseChangeStruct change;
    change.from = Player::Play;
    change.to = Player::NotActive;
    QVariant data = QVariant::fromValue(change);
    fixture.room.getThread()->trigger(EventPhaseChanging, &fixture.room, liubei, data);
    HEG_CHECK(liubei->getMark("tenyearrende-PlayClear") == 0
        && liubei->property("tenyearrende").toString().isEmpty()
        && !liubei->getSkillInstanceStateValue("tenyearrende", instances.first(), "given").isValid()
        && !liubei->getSkillInstanceStateValue("tenyearrende", instances.first(), "recipients").isValid());
    return true;
}

bool factionNullification(bool countered)
{
    OriginalGameplayFixture fixture;
    ServerPlayer *first = fixture.add("heg_caocao", "heg_xiahoudun");
    ServerPlayer *ally = fixture.add("heg_xuhuang", "heg_zhangliao");
    ServerPlayer *source = fixture.add("heg_liubei", "heg_zhangfei");
    ServerPlayer *outsider = fixture.add("heg_zhouyu", "heg_luxun");
    HEG_CHECK(first && ally && source && outsider);
    fixture.prepare();
    first->showGeneral(true, false, false);
    ally->showGeneral(true, false, false);
    HEG_CHECK(first->isFriendWith(ally));
    const int hegId = fixture.cardInDeck("heg_nullification");
    const int trickId = fixture.cardInDeck("savage_assault");
    HEG_CHECK(hegId >= 0 && trickId >= 0);
    fixture.room.obtainCard(first, hegId, false);
    fixture.room.obtainCard(source, trickId, false);
    HEG_CHECK(fixture.room.getCardOwner(hegId) == first
        && fixture.room.getCardPlace(hegId) == Player::PlaceHand
        && !fixture.room.getDrawPile().contains(hegId));
    HEG_CHECK(fixture.room.getCard(hegId)->isKindOf("Nullification"));
    HEG_CHECK(first->hasNullification());
    const Card *trick = fixture.room.getCard(trickId);
    fixture.ais.value(first)->response = fixture.room.getCard(hegId);
    fixture.room.registerTestOverride(first, "choice", "heg_nullification", QStringLiteral("all"));
    int counterId = -1;
    if (countered) {
        counterId = fixture.cardInDeck("nullification");
        HEG_CHECK(counterId >= 0);
        fixture.room.obtainCard(source, counterId, false);
        fixture.ais.value(source)->response = fixture.room.getCard(counterId);
        fixture.ais.value(source)->respondToPositive = false;
    }
    CardEffectStruct effect;
    effect.card = trick;
    effect.from = source;
    effect.to = first;
    const Card *cancel = fixture.room.isCanceled(effect);
    const QString key = trick->toString();
    if ((cancel != nullptr) != !countered)
        qCritical() << "Faction nullification failed: countered=" << countered
            << "queries=" << fixture.ais.value(first)->nullificationQueries
            << "responsePending=" << (fixture.ais.value(first)->response != nullptr)
            << "cardPlace=" << fixture.room.getCardPlace(hegId)
            << "effectRecorded=" << fixture.room.getTag("UseHistory"
                + fixture.room.getCard(hegId)->toString()).value<CardUseStruct>().no_offset_list;
    HEG_CHECK((cancel != nullptr) == !countered);
    HEG_CHECK(!first->getTag("ComboMovesCard").isValid());
    HEG_CHECK(!source->getTag("ComboMovesCard").isValid());
    HEG_CHECK(fixture.room.getCardPlace(hegId) == Player::DiscardPile);
    if (countered) {
        HEG_CHECK(fixture.room.getCardPlace(counterId) == Player::DiscardPile);
        HEG_CHECK(fixture.room.getTag(key + "PendingNullification").toMap().value("targets").toStringList().isEmpty());
        return true;
    }
    const QStringList covered = fixture.room.getTag(key + "PendingNullification").toMap().value("targets").toStringList();
    HEG_CHECK(covered.contains(first->objectName()) && covered.contains(ally->objectName()));
    HEG_CHECK(!covered.contains(source->objectName()) && !covered.contains(outsider->objectName()));
    const int queries = fixture.ais.value(first)->nullificationQueries;
    effect.to = ally;
    HEG_CHECK(fixture.room.isCanceled(effect) == cancel);
    HEG_CHECK(fixture.ais.value(first)->nullificationQueries == queries);
    effect.to = outsider;
    HEG_CHECK(fixture.room.isCanceled(effect) == nullptr);

    CardUseStruct finished(trick, source);
    QVariant data = QVariant::fromValue(finished);
    fixture.room.getThread()->trigger(CardFinished, &fixture.room, source, data);
    HEG_CHECK(!fixture.room.getTag(key + "PendingNullification").isValid());
    HEG_CHECK(!fixture.room.getTag(key + "HegNullificationCard").isValid());
    return true;
}

bool strategicV2AndTransferContract()
{
    OriginalGameplayFixture fixture;
    HEG_CHECK(fixture.mixedPlayers());
    fixture.prepare();
    QVariant ready;
    HStrategicAdvantagePackage::recordCardRules(GameReady, &fixture.room, nullptr, ready);
    const auto players = fixture.room.getPlayers();
    ServerPlayer *source = players.first(), *target = players.at(1);
    source->setPhase(Player::Play);
    fixture.room.getRoomState()->setCurrentCardUseReason(CardUseStruct::CARD_USE_REASON_PLAY);
    for (const QString &name : {"heg_Blade", "heg_Halberd", "heg_Breastplate", "heg_IronArmor", "heg_JadeSeal", "jingfan"})
        HEG_CHECK(dynamic_cast<const EquipSkillV2 *>(Sanguosha->getSkill(name)));
    for (const QString &name : {"heg_Halberd", "heg_Breastplate", "heg_JadeSeal", "jingfan"})
        HEG_CHECK(dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(name)));
    HEG_CHECK(dynamic_cast<const TargetModSkillV2 *>(Sanguosha->getSkill("heg_halberd-target")));
    SkillCard *registered = Sanguosha->cloneSkillCard("TransferCard");
    HEG_CHECK(registered);
    delete registered;

    int id = -1;
    for (int candidate : fixture.room.getDrawPile()) {
        const Card *card = fixture.room.getCard(candidate);
        if (card->isTransferable() && card->getTypeId() == Card::TypeBasic) { id = candidate; break; }
    }
    HEG_CHECK(id >= 0);
    fixture.room.obtainCard(source, id, false);
    TransferCard transfer;
    transfer.addSubcard(id);
    HEG_CHECK(!transfer.targetFilter({}, source, source));
    HEG_CHECK(transfer.targetFilter({}, target, source));
    target->showGeneral(true, false, false);
    HEG_CHECK(!transfer.targetFilter({}, target, source));
    source->showGeneral(true, false, false);
    HEG_CHECK(transfer.targetFilter({}, target, source));
    // A use restriction on the material is not a restriction on giving it away.
    fixture.room.setPlayerCardLimitation(source, "use", QString::number(id), false, "transfer-contract");
    const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getSkill("heg_transfer"));
    HEG_CHECK(skill);
    const auto instances = source->getSkillInstanceIds("heg_transfer");
    HEG_CHECK(instances.size() == 1);
    ActiveSkillRequest request;
    request.initiator = source;
    request.reason = CardUseStruct::CARD_USE_REASON_PLAY;
    request.activationRef = SkillInstanceRef(source->objectName(), SkillInstanceKey("heg_transfer", instances.first()));
    request.selectedCardIds << id;
    request.selectedTargetNames << target->objectName();
    const Card *converted = RoomTestAccess::resolveActiveRequest(fixture.room, source, skill, request);
    HEG_CHECK(converted);
    CardUseStruct use(converted, source, target);
    const_cast<Card *>(converted)->deleteLater();
    use.m_validateTargets = true;
    const int hand = source->getHandcardNum();
    HEG_CHECK(fixture.room.useCard(use));
    HEG_CHECK(fixture.room.getCardOwner(id) == target && fixture.room.getCardPlace(id) == Player::PlaceHand);
    HEG_CHECK(source->getHandcardNum() == hand); // give one, draw one
    HEG_CHECK(transfer.validate(use) == nullptr); // material no longer owned
    return true;
}

bool strategicKingdomAndEquipmentContract()
{
    OriginalGameplayFixture fixture;
    ServerPlayer *source = fixture.add("heg_caocao", "heg_xiahoudun");
    ServerPlayer *ally = fixture.add("heg_caoren", "heg_xuchu");
    ServerPlayer *small = fixture.add("heg_liubei", "heg_zhangfei");
    ServerPlayer *hidden = fixture.add("heg_zhouyu", "heg_luxun");
    HEG_CHECK(source && ally && small && hidden);
    fixture.prepare();
    source->showGeneral(true, false, false);
    ally->showGeneral(true, false, false);
    small->showGeneral(true, false, false);
    HEG_CHECK(source->getBigKingdoms("contract") == QStringList("wei"));
    const int armor = fixture.cardInDeck("IronArmor"), seal = fixture.cardInDeck("JadeSeal");
    HEG_CHECK(armor >= 0 && seal >= 0);
    fixture.room.moveCardTo(fixture.room.getCard(armor), small, Player::PlaceEquip, true);
    fixture.room.setPlayerChained(small, true, source);
    HEG_CHECK(!small->isChained());
    // The source selector follows the four documented ExpPattern fields.
    const QString pattern = "%IronArmor|.|.|.|target:" + source->objectName();
    fixture.room.setPlayerEquipsNullified(small, pattern, "strategic-contract", false);
    fixture.room.setPlayerChained(small, true, ally);
    HEG_CHECK(!small->isChained());
    fixture.room.setPlayerChained(small, true, source);
    HEG_CHECK(small->isChained());
    fixture.room.setPlayerChained(small, false);
    HEG_CHECK(!small->isChained());
    fixture.room.removePlayerEquipsNullified(small, pattern, "strategic-contract");

    // Careerist Jade Seal uses arbitrary stable IDs, not an sgs* name convention.
    small->setRole("careerist");
    small->setPhase(Player::Play);
    fixture.room.moveCardTo(fixture.room.getCard(seal), small, Player::PlaceEquip, true);
    HEG_CHECK(source->getBigKingdoms("contract") == QStringList(small->objectName()));
    HThreatenEmperor emperor(Card::NoSuit, 0);
    HEG_CHECK(emperor.isAvailable(small) && !emperor.isAvailable(source));
    fixture.room.setPlayerEquipsNullified(small, "%JadeSeal", "strategic-contract", false);
    HEG_CHECK(source->getBigKingdoms("contract") == QStringList("wei"));
    HEG_CHECK(!emperor.isAvailable(small));
    const int horse = fixture.cardInDeck("jingfan");
    HEG_CHECK(horse >= 0 && source->distanceTo(small) == 2);
    fixture.room.moveCardTo(fixture.room.getCard(horse), source, Player::PlaceEquip, true);
    HEG_CHECK(source->distanceTo(small) == 1);
    fixture.room.setPlayerEquipsNullified(source, "%jingfan|.|.|.|target:" + small->objectName(), "strategic-contract", false);
    HEG_CHECK(source->distanceTo(small) == 2);
    return true;
}

bool sourceLessImperialOrderChoicesAndNullification()
{
    OriginalGameplayFixture fixture;
    HEG_CHECK(fixture.mixedPlayers());
    fixture.prepare();
    const auto players = fixture.room.getPlayers();
    ServerPlayer *target = players.first();
    const int orderId = fixture.cardInDeck("imperial_order");
    const int nullificationId = fixture.cardInDeck("nullification");
    HEG_CHECK(orderId >= 0 && nullificationId >= 0);
    const Card *order = fixture.room.getCard(orderId);
    fixture.room.obtainCard(players.last(), nullificationId, false);
    fixture.ais.value(players.last())->response = fixture.room.getCard(nullificationId);
    const int hp = target->getHp();
    HEG_CHECK(!fixture.room.cardEffect(order, nullptr, target, true));
    HEG_CHECK(target->getHp() == hp);
    HEG_CHECK(fixture.room.getCardPlace(nullificationId) == Player::DiscardPile);

    // A blocked head is not an offered reveal; the legal deputy must reveal.
    fixture.room.setPlayerDisableShow(target, "h", "strategic-contract");
    fixture.room.registerTestOverride(target, "card", "EquipCard", -1);
    fixture.room.registerTestOverride(target, "choice", "imperial_order", QStringLiteral("show"));
    fixture.room.registerTestOverride(target, "choice", "GameRule_AskForGeneralShow", QStringLiteral("showdeputy"));
    const int hand = target->getHandcardNum();
    HEG_CHECK(fixture.room.cardEffect(order, nullptr, target, true));
    HEG_CHECK(!target->hasShownGeneral() && target->hasShownGeneral2());
    HEG_CHECK(target->getHandcardNum() == hand + 1 && target->getHp() == hp);

    ServerPlayer *discarder = players.at(1);
    const int equip = fixture.cardInDeck("Breastplate");
    HEG_CHECK(equip >= 0);
    fixture.room.obtainCard(discarder, equip, false);
    fixture.room.registerTestOverride(discarder, "card", "EquipCard", equip);
    const int discardHp = discarder->getHp();
    HEG_CHECK(fixture.room.cardEffect(order, nullptr, discarder, true));
    HEG_CHECK(fixture.room.getCardPlace(equip) == Player::DiscardPile);
    HEG_CHECK(!discarder->hasShownOneGeneral() && discarder->getHp() == discardHp);
    return true;
}

bool strategicCardRuleExpiry()
{
    OriginalGameplayFixture fixture;
    HEG_CHECK(fixture.mixedPlayers());
    HEG_CHECK(fixture.add("heg_zhoutai", "heg_luxun"));
    fixture.prepare();
    ServerPlayer *source = fixture.room.getPlayers().first();
    ServerPlayer *target = fixture.room.getPlayers().at(1);
    ServerPlayer *across = fixture.room.getPlayers().at(2);
    HEG_CHECK(source->distanceTo(across) == 2);
    HLureTiger tiger(Card::NoSuit, 0);
    CardEffectStruct effect;
    effect.card = &tiger; effect.from = source; effect.to = target;
    fixture.room.setPlayerCardLimitation(target, "use", "Slash", false, "unrelated-contract");
    tiger.onEffect(effect);
    HEG_CHECK(target->isRemoved());
    HEG_CHECK(source->distanceTo(across) == 1);
    HEG_CHECK(fixture.room.isProhibited(nullptr, target, fixture.room.getCard(fixture.cardInDeck("imperial_order"))));
    PhaseChangeStruct change; change.from = Player::Finish; change.to = Player::NotActive;
    QVariant data = QVariant::fromValue(change);
    // The turn owner can differ from the Lure Tiger source.
    fixture.room.getThread()->trigger(EventPhaseChanging, &fixture.room, target, data);
    HEG_CHECK(!target->isRemoved() && !source->hasFlag("LureTigerUser"));
    HEG_CHECK(source->distanceTo(across) == 2);
    Slash slash(Card::Spade, 7);
    Jink jink(Card::Heart, 2);
    HEG_CHECK(target->isCardLimited(&slash, Card::MethodUse));
    HEG_CHECK(!target->isCardLimited(&jink, Card::MethodUse));

    source->setPhase(Player::Play);
    HThreatenEmperor emperor(Card::NoSuit, 0);
    effect.card = &emperor; effect.to = source;
    emperor.onEffect(effect);
    HEG_CHECK(source->hasFlag("Global_PlayPhaseTerminated"));
    const auto payment = fixture.giveBasicCards(source, 1);
    HEG_CHECK(payment.size() == 1);
    fixture.room.registerTestOverride(source, "card", "..", payment.first());
    const int pending = fixture.room.snapshotPendingExtraTurns().size();
    HStrategicAdvantagePackage::recordCardRules(EventPhaseChanging, &fixture.room, source, data);
    HEG_CHECK(source->getMark("ThreatenEmperorExtraTurn") == 0);
    HEG_CHECK(fixture.room.getCardPlace(payment.first()) == Player::DiscardPile);
    HEG_CHECK(fixture.room.snapshotPendingExtraTurns().size() == pending + 1);
    HStrategicAdvantagePackage::recordCardRules(EventPhaseChanging, &fixture.room, source, data);
    HEG_CHECK(fixture.room.snapshotPendingExtraTurns().size() == pending + 1);
    return true;
}

bool discardedImperialOrderDefersExactlyOnce()
{
    OriginalGameplayFixture fixture;
    HEG_CHECK(fixture.mixedPlayers());
    fixture.prepare();
    const QList<ServerPlayer *> players = fixture.room.getPlayers();
    players.last()->showGeneral(true, false, false);
    QList<int> hp;
    for (ServerPlayer *player : players) {
        hp << player->getHp();
        fixture.room.registerTestOverride(player, "card", "EquipCard", -1);
        fixture.room.registerTestOverride(player, "choice", "imperial_order", QStringLiteral("losehp"));
    }
    const int id = fixture.cardInDeck("imperial_order");
    HEG_CHECK(id >= 0);
    const Card *order = fixture.room.getCard(id);
    fixture.room.moveCardTo(order, nullptr, Player::DiscardPile,
        CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, QString()), true);
    HEG_CHECK(fixture.room.getTag("ImperialOrderInvoke").toBool());
    HEG_CHECK(players.first()->getPile("#imperial_order").contains(id));
    HEG_CHECK(!fixture.room.getDiscardPile().contains(id));
    for (int i = 0; i < players.size(); ++i) HEG_CHECK(players.at(i)->getHp() == hp.at(i));

    PhaseChangeStruct change;
    change.from = Player::Finish;
    change.to = Player::NotActive;
    QVariant data = QVariant::fromValue(change);
    fixture.room.getThread()->trigger(EventPhaseChanging, &fixture.room, players.first(), data);
    HEG_CHECK(!fixture.room.getTag("ImperialOrderInvoke").toBool());
    for (int i = 0; i < players.size(); ++i)
        HEG_CHECK(players.at(i)->getHp() == hp.at(i) - (i + 1 == players.size() ? 0 : 1));
    fixture.room.getThread()->trigger(EventPhaseChanging, &fixture.room, players.first(), data);
    for (int i = 0; i < players.size(); ++i)
        HEG_CHECK(players.at(i)->getHp() == hp.at(i) - (i + 1 == players.size() ? 0 : 1));
    HEG_CHECK(players.first()->getPile("#imperial_order").contains(id));
    return true;
}

#undef HEG_CHECK

}

int runOriginalHegemonyGameplayTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "Original hegemony gameplay engine initialization failed:" << error;
        return 1;
    }
    const bool hegemony = Config.EnableHegemony, second = Config.Enable2ndGeneral;
    const int delay = Config.AIDelay;
    const bool enableAI = Config.EnableAI;
    const QStringList bans = Config.BanPackages;
    const ServerInfoStruct info = ServerInfo;
    const QVariantMap oldOverrides = Config.valueOverrides();
    auto restore = qScopeGuard([=]() {
        Config.EnableHegemony = hegemony;
        Config.Enable2ndGeneral = second;
        Config.AIDelay = delay;
        Config.EnableAI = enableAI;
        Config.BanPackages = bans;
        Config.setValueOverrides(oldOverrides);
        ServerInfo = info;
    });
    Config.EnableHegemony = Config.Enable2ndGeneral = true;
    ServerInfo.EnableHegemony = ServerInfo.Enable2ndGeneral = true;
    Config.AIDelay = 0;
    // This suite deliberately supplies PhysicalNullificationAI answers. Keep
    // local isolated-AI route preferences from replacing those fixture inputs.
    Config.EnableAI = false;
    for (const QString &name : bans)
        if (name.startsWith("heg_")) Config.BanPackages.removeAll(name);
    QVariantMap overrides = oldOverrides;
    overrides.insert("RewardTheFirstShowingPlayer", false);
    Config.setValueOverrides(overrides);
    const QStringList arguments = QCoreApplication::arguments();
    const int caseOption = arguments.indexOf(QStringLiteral("--case"));
    const QString selected = caseOption < 0 ? QString() : arguments.value(caseOption + 1);
    const QStringList cases = {"wu-skills", "rende", "nullification", "counter-nullification",
                               "imperial-order", "throwing-cost", "finish-interruption", "burning-camps",
                               "draw-events", "peace-spell-uninstall", "silver-lion-uninstall",
                               "wooden-ox-transfer", "ordered-equipment", "wei-xiaoguo", "strategic-transfer",
                               "strategic-equipment", "imperial-order-null-source", "strategic-expiry", "momentum-removal"};
    if (caseOption >= 0 && !cases.contains(selected)) return 64;
    const auto run = [&](const QString &name, auto test) {
        if (!selected.isEmpty() && selected != name) return true;
        qInfo().noquote() << "[hegemony-gameplay] begin" << name;
        const bool passed = test();
        qInfo().noquote() << "[hegemony-gameplay] end" << name << (passed ? "PASS" : "FAIL");
        return passed;
    };
    if (!run(cases.at(0), wuSharedSkillsAndDuoshiV2Contract)) return 2;
    if (!run(cases.at(1), rendeDonorThresholdAndBasicPrompt)) return 3;
    if (!run(cases.at(2), [] { return factionNullification(false); })) return 4;
    if (!run(cases.at(3), [] { return factionNullification(true); })) return 5;
    if (!run(cases.at(4), discardedImperialOrderDefersExactlyOnce)) return 6;
    if (!run(cases.at(5), throwingCostPrecedesPreciseReveal)) return 7;
    if (!run(cases.at(6), customCardFinishIsNotRepeated)) return 8;
    if (!run(cases.at(7), burningCampsHitsOnlyNextFormation)) return 9;
    if (!run(cases.at(8), drawEventsPreservePayloadAndPhaseScope)) return 10;
    if (!run(cases.at(9), peaceSpellUninstallLosesHpAndDraws)) return 11;
    if (!run(cases.at(10), silverLionUninstallRecoversOnce)) return 12;
    if (!run(cases.at(11), woodenOxTransferMovesPile)) return 13;
    if (!run(cases.at(12), orderedEquipmentKeepsOwnerAndTargetIdentity)) return 14;
    if (!run("wei-xiaoguo", weiXiaoguoPaymentAndTargets)) return 15;
    if (!run("strategic-transfer", strategicV2AndTransferContract)) return 16;
    if (!run("strategic-equipment", strategicKingdomAndEquipmentContract)) return 17;
    if (!run("imperial-order-null-source", sourceLessImperialOrderChoicesAndNullification)) return 18;
    if (!run("strategic-expiry", strategicCardRuleExpiry)) return 19;
    if (!run("momentum-removal", momentumRemovalKeepsSurvivingInstances)) return 20;
    qInfo() << "ORIGINAL_HEGEMONY_GAMEPLAY_TEST_RESULT status=PASS";
    return 0;
}
