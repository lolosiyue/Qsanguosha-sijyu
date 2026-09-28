#include "ling.h"
#include "skill-instance-utils.h"
//#include "general.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "engine.h"
#include "wind.h"
#include "room.h"
#include "roomthread.h"

namespace {
static void addAllInstances(TriggerList &result, ServerPlayer *owner, const QString &name)
{
    if (!owner || !owner->hasSkill(name)) return;
    for (int id : owner->getValidSkillInstanceIds(name))
        result[owner] << SkillInstanceUtils::formatName(name, id);
}
}

LuoyiCard::LuoyiCard()
{
    setSkillName("neoluoyi");
    target_fixed = true;
}

void LuoyiCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->setFlags("neoluoyi");
}

class NeoLuoyi : public ViewAsSkillV2
{
public:
    NeoLuoyi() : ViewAsSkillV2("neoluoyi", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !candidate->isEquipped() && !candidate->hasFlag("using")
            && candidate->isKindOf("EquipCard")
            && request.initiator->handCards().contains(candidate->getEffectiveId()) && !request.initiator->isJilei(candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.selectedCardIds.size() == 1 && canSelectCard(request, Sanguosha->getCard(request.selectedCardIds.first())); }
    TargetMode targetMode() const override { return NoTarget; }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return true; }
    EffectFlow effect(SkillContext &ctx) const override
    { if (ctx.invoker) skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "LuoyiCard"; }
};

class NeoLuoyiBuff : public TriggerSkillV2
{
public:
    NeoLuoyiBuff() : TriggerSkillV2("#neoluoyi")
    {
        events << DamageCaused << EventPhaseChanging;
        global = true;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            room->removeTag("NeoLuoyiEffects");
        return false;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused || !actor || !actor->isAlive()) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from != actor || damage.chain || damage.transfer || !damage.by_user || !damage.card
            || (!damage.card->isKindOf("Slash") && !damage.card->isKindOf("Duel"))) return true;
        // Applied turn effects retain exact provenance even if their granting instance is removed.
        for (const QVariant &value : room->getTag("NeoLuoyiEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("recipient").toString() != actor->objectName()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = ctx.owner;
            ctx.invoker = actor;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0;
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.targets << damage.to;
            contexts << ctx;
        }
        return true;
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.invoker && ctx.invoker->isAlive() && ctx.sourceRef.isValid()
            && room->getTag("NeoLuoyiEffects").toList().contains(ctx.extra_data);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!room || !target || !ctx.owner || !ctx.original_data) return false;
        QVariant &data = *ctx.original_data;
        ServerPlayer *xuchu = ctx.invoker;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.chain || damage.transfer || !damage.by_user) return false;
        const Card *reason = damage.card;
        if (reason && (reason->isKindOf("Slash") || reason->isKindOf("Duel"))) {
            LogMessage log;
            log.type = "#LuoyiBuff";
            log.from = xuchu;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            damage.damage += getEffectiveAmount(ctx);
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);

            data = QVariant::fromValue(damage);
        }

        return false;
    }
};

NeoFanjianCard::NeoFanjianCard()
{
    setSkillName("neofanjian");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void NeoFanjianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *zhouyu = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = zhouyu->getRoom();

    room->broadcastSkillInvoke("fanjian");
    const Card *card = Sanguosha->getCard(getSubcards().first());
    int card_id = card->getEffectiveId();
    Card::Suit suit = room->askForSuit(target, "neofanjian");

    LogMessage log;
    log.type = "#ChooseSuit";
    log.from = target;
    log.arg = Card::Suit2String(suit);
    room->sendLog(log);

    room->getThread()->delay();
    target->obtainCard(this);
    room->showCard(target, card_id);

    if (card->getSuit() != suit)
        room->damage(DamageStruct("neofanjian", zhouyu, target));
}

class NeoFanjian : public ViewAsSkillV2
{
public:
    NeoFanjian() : ViewAsSkillV2("neofanjian", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !candidate->isEquipped() && !candidate->hasFlag("using")
            && request.initiator->handCards().contains(candidate->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.selectedCardIds.size() == 1 && canSelectCard(request, Sanguosha->getCard(request.selectedCardIds.first())); }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    { return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "NeoFanjianCard"; }
};

ViewAsSkillV2::EffectFlow NeoLuoyi::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (!target || !target->isAlive() || !ctx.invoker || !ctx.activationRef.isValid()) return FinishSkill;
    Room *room = ctx.invoker->getRoom();
    QVariantMap receipt;
    const qint64 serial = room->getTag("NeoLuoyiSequence").toLongLong()+1; room->setTag("NeoLuoyiSequence",serial); receipt.insert("serial",serial);
    receipt.insert("owner", ctx.activationRef.ownerObjectName);
    receipt.insert("instance", ctx.activationRef.key.instanceID);
    receipt.insert("source_owner", ctx.sourceRef.ownerObjectName);
    receipt.insert("source_skill", ctx.sourceRef.key.skillName);
    receipt.insert("source_instance", ctx.sourceRef.key.instanceID);
    receipt.insert("recipient", target->objectName());
    receipt.insert("amount", getEffectiveAmount(ctx));
    QVariantList effects = room->getTag("NeoLuoyiEffects").toList();
    effects << receipt;
    room->setTag("NeoLuoyiEffects", effects);
    target->setFlags("neoluoyi"); // Public AI compatibility only; receipts own the effect.
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow NeoFanjian::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (!ctx.invoker || !target || !ctx.use_card || ctx.use_card->getSubcards().isEmpty()) return FinishSkill;
    ServerPlayer *zhouyu = ctx.invoker; Room *room = zhouyu->getRoom();
    room->broadcastSkillInvoke("fanjian");
    const Card *card = Sanguosha->getCard(ctx.use_card->getSubcards().first());
    const int cardId = card->getEffectiveId(); const Card::Suit materialSuit = card->getSuit();
    const Card::Suit suit = room->askForSuit(target, "neofanjian");
    if (!ctx.initiator || !target->isAlive() || room->getCardOwner(cardId) != ctx.initiator
        || room->getCardPlace(cardId) != Player::PlaceHand) return FinishSkill;
    LogMessage log; log.type = "#ChooseSuit"; log.from = target; log.arg = Card::Suit2String(suit); room->sendLog(log);
    room->getThread()->delay(); target->obtainCard(ctx.use_card); room->showCard(target, cardId);
    if (target->isAlive() && materialSuit != suit) room->damage(DamageStruct("neofanjian", zhouyu, target, getEffectiveAmount(ctx)));
    return FinishSkill;
}

class Yishi : public TriggerSkillV2
{
public:
    Yishi() : TriggerSkillV2("yishi")
    {
        events << DamageCaused;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != DamageCaused || !player || !player->isAlive()) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash") && damage.by_user && !damage.chain
            && !damage.transfer && damage.to && !damage.to->isAllNude())
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<DamageStruct>().to;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!room || !ctx.owner || !ctx.original_data) return false;
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        if (ctx.choice.startsWith("obtain:")) {
            const int id = ctx.choice.section(':', 1).toInt();
            if (!target || !target->isAlive() || room->getCardOwner(id) != damage.to) return false;
            const Player::Place place = room->getCardPlace(id);
            if (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick) return false;
            room->obtainCard(target, Sanguosha->getCard(id),
                CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, player->objectName()), place != Player::PlaceHand);
            ctx.choice = "obtained";
            return false;
        }

        if (damage.card && damage.card->isKindOf("Slash")
            && damage.by_user && !damage.chain && !damage.transfer && target && !target->isAllNude()) {
            room->broadcastSkillInvoke(objectName(), 1);
            LogMessage log;
            log.type = "#Yishi";
            log.from = player;
            log.arg = objectName();
            log.to << target;
            room->sendLog(log);
            int card_id = room->askForCardChosen(player, target, "hej", objectName());
            if (room->getCardOwner(card_id) != target || !target->getCards("hej").contains(Sanguosha->getCard(card_id))) return false;
            if (room->getCardPlace(card_id) == Player::PlaceDelayedTrick)
                room->broadcastSkillInvoke(objectName(), 2);
            else if (room->getCardPlace(card_id) == Player::PlaceEquip)
                room->broadcastSkillInvoke(objectName(), 3);
            else
                room->broadcastSkillInvoke(objectName(), 4);
            ctx.choice = "obtain:" + QString::number(card_id);
            skillEffect(event, room, actor, ctx, player);
            return ctx.choice == "obtained";
        }
        return false;
    }
};

class Zhulou : public TriggerSkillV2
{
public:
    Zhulou() : TriggerSkillV2("zhulou") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Finish)
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false; ctx.targets << ctx.owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!room || !target || !target->isAlive()) return false;
        ServerPlayer *gongsun = target;
        gongsun->drawCards(2 * getEffectiveAmount(ctx), objectName());
        room->broadcastSkillInvoke(objectName());
        if (!room->askForCard(gongsun, ".Weapon", "@zhulou-discard"))
            room->loseHp(HpLostStruct(gongsun, getEffectiveAmount(ctx), objectName(), gongsun));
        return false;
    }
};

class Tannang : public DistanceSkillV2
{
public:
    Tannang() : DistanceSkillV2("tannang") { setHolderSelector(CorrectSkill_Primary); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        const Player *from = context.getHolder();
        if (!from || !from->hasSkill(objectName()) || from->getLostHp() <= 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(-from->getLostHp() * context.getCurrentAmount());
    }
};

class NeoJushou : public TriggerSkillV2
{
public:
    NeoJushou() : TriggerSkillV2("neojushou") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Finish)
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false; ctx.targets << ctx.owner; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        target->drawCards((2 + target->getLostHp()) * getEffectiveAmount(ctx), objectName());
        if (target->isAlive()) target->turnOver();
        return false;
    }
};

class NeoGanglie : public TriggerSkillV2
{
public:
    NeoGanglie() : TriggerSkillV2("neoganglie")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == Damaged && player && player->isAlive() && data.canConvert<DamageStruct>()) addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *from = damage.from;
        room->broadcastSkillInvoke("ganglie");
        JudgeStruct judge; judge.pattern = ".|heart"; judge.good = false; judge.reason = objectName(); judge.who = ctx.owner; room->judge(judge);
        if (!from || from->isDead() || !judge.isGood()) return false;
        ctx.targets << from;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *from) const override
    {
        QStringList choices; choices << "damage"; if (from->getHandcardNum() > 1) choices << "throw";
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        if (choice == "damage") room->damage(DamageStruct(objectName(), ctx.owner, from, getEffectiveAmount(ctx)));
        else room->askForDiscard(from, objectName(), 2 * getEffectiveAmount(ctx), 2 * getEffectiveAmount(ctx));
        return false;
    }
};

LingPackage::LingPackage()
    : Package("ling")
{
    General *neo_xiahoudun = new General(this, "neo_xiahoudun", "wei");
    neo_xiahoudun->addSkill(new NeoGanglie);

    General *neo_xuchu = new General(this, "neo_xuchu", "wei");
    neo_xuchu->addSkill(new NeoLuoyi);
    neo_xuchu->addSkill(new NeoLuoyiBuff);
    related_skills.insert("neoluoyi", "#neoluoyi");

    General *neo_caoren = new General(this, "neo_caoren", "wei");
    neo_caoren->addSkill(new NeoJushou);

    General *neo_guanyu = new General(this, "neo_guanyu", "shu");
    neo_guanyu->addSkill("wusheng");
    neo_guanyu->addSkill(new Yishi);

    General *neo_zhangfei = new General(this, "neo_zhangfei", "shu");
    neo_zhangfei->addSkill("paoxiao");
    neo_zhangfei->addSkill(new Tannang);

    General *neo_zhaoyun = new General(this, "neo_zhaoyun", "shu");
    neo_zhaoyun->addSkill("longdan");
    neo_zhaoyun->addSkill("yicong");

    General *neo_zhouyu = new General(this, "neo_zhouyu", "wu", 3);
    neo_zhouyu->addSkill("nosyingzi");
    neo_zhouyu->addSkill(new NeoFanjian);

    General *neo_gongsunzan = new General(this, "neo_gongsunzan", "qun");
    neo_gongsunzan->addSkill(new Zhulou);
    neo_gongsunzan->addSkill("yicong");

    addMetaObject<LuoyiCard>();
    addMetaObject<NeoFanjianCard>();
}

ADD_PACKAGE(Ling)
