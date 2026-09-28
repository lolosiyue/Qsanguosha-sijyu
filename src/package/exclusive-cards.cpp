#include "exclusive-cards.h"
//#include "client.h"
#include "engine.h"
//#include "general.h"
#include "clientplayer.h"
#include "room.h"
#include "wrapped-card.h"
#include "roomthread.h"
#include "yjcm2013.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
//#include "yingbian.h"

// Physical equipment has no SkillInstance. Virtual equipment keeps a separate quota for its exact source.
static QString exclusiveEquipmentUsageKey(const QString &name, const SkillContext &ctx)
{
    const SkillInstanceRef &ref = ctx.activationRef;
    return name + (ref.isValid() ? '_' + SkillInstanceUtils::formatName(ref.key.skillName, ref.key.instanceID) : QString()) + "-Clear";
}

class HongduanqiangSkill : public WeaponSkillV2
{
public:
    HongduanqiangSkill() : WeaponSkillV2("_hongduanqiang", "_hongduanqiang") { events << Damage; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getMark(exclusiveEquipmentUsageKey(objectName(), ctx)) < getMaxUsageLimit(ctx); }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->addPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx)); }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->setPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx), 0); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && WeaponSkillV2::triggerable(player) && player->hasTurn()
            && damage.card && damage.card->isKindOf("Slash") && damage.by_user ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << owner;
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { addUsage(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "weapon/_hongduanqiang");
        JudgeStruct judge;
        judge.who = target;
        judge.reason = objectName();
        judge.pattern = ".";
        judge.play_animation = false;
        room->judge(judge);
        if (!judge.card || !target->isAlive()) return false;
        if (judge.card->isRed()) room->recover(target, RecoverStruct(objectName(), owner, getEffectiveAmount(ctx)));
        else if (judge.card->isBlack()) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

Hongduanqiang::Hongduanqiang(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("_hongduanqiang");
}

class LiecuidaoTargetMod : public TargetModSkillV2
{
public:
    LiecuidaoTargetMod() : TargetModSkillV2("#_liecuidao-target")
    {
        frequency = NotFrequent;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *from = ctx.primary;
        const Card *card = ctx.card;
        if (ctx.modType != TargetModSkill::Residue || !from || !card)
            return CorrectSkillResult::noEffect();
        int n = from->getMark("_bintieshuangji-Clear");
        if (from->hasWeapon("_liecuidao")) {
            if (card->hasFlag("Global_SlashAvailabilityChecker")) {
                ++n;
            } else {
                QList<int> ids = card->isVirtualCard() ? card->getSubcards()
                                                        : QList<int>{card->getEffectiveId()};
                const Card *weapon = from->getWeapon();
                if (weapon && ids.contains(weapon->getEffectiveId())) --n;
                ++n;
            }
        }
        if (from->hasTreasure("_sanlve")) ++n;
        return CorrectSkillResult::useAmount(n);
    }
};

class LiecuidaoVS : public ViewAsSkillV2
{
public:
    LiecuidaoVS() : ViewAsSkillV2("_liecuidao", 1)
    {
        setResponseOrUse(true);
    }

    bool isEquipSkill() const override { return true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern.startsWith("@@_liecuidao")
            && request.initiator->hasWeapon("_liecuidao");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card
            && !card->hasFlag("using")
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()))
            && !(card->isEquipped() && card->objectName() == "_liecuidao")
            && request.initiator->canDiscard(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *selected = Sanguosha->getCard(request.selectedCardIds.first());
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        if (!selected || !canSelectCard(empty, selected)) return nullptr;
        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcard(request.selectedCardIds.first());
        return card;
    }
};

class LiecuidaoSkill : public WeaponSkillV2
{
public:
    LiecuidaoSkill() : WeaponSkillV2("_liecuidao", "_liecuidao") { events << DamageCaused; view_as_skill = new LiecuidaoVS; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getMark(exclusiveEquipmentUsageKey(objectName(), ctx)) < getMaxUsageLimit(ctx); }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->addPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx)); }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->setPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx), 0); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && WeaponSkillV2::triggerable(player) && player->hasTurn()
            && damage.to && damage.card && damage.card->isKindOf("Slash") && !damage.chain && !damage.transfer
            && player->canDiscard(player, "he") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const Card *card = room->askForCard(owner, "@@_liecuidao", "@_liecuidao:" + damage.to->objectName(),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->getSubcards().size() != 1) return false;
        ctx.extra_data = card->getSubcards().first();
        ctx.targets << damage.to;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        const Card *weapon = owner->getWeapon();
        if (!isUsable(ctx) || room->getCardOwner(id) != owner || !owner->canDiscard(owner, id)
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || (weapon && weapon->objectName() == objectName() && weapon->getEffectiveId() == id)) return false;
        addUsage(ctx);
        room->throwCard(id, objectName(), owner, owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "weapon/_liecuidao");
        return ctx.original_data->value<DamageStruct>().to == target && target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
    }
};

Liecuidao::Liecuidao(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("_liecuidao");
}

ShuibojianCard::ShuibojianCard()
{
    mute = true;
    setSkillName("_shuibojian");
}

bool ShuibojianCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
    return to_select->hasFlag("_shuibojian_canchoose") && targets.isEmpty();
}

void ShuibojianCard::onUse(Room*room, CardUseStruct &card_use) const
{
    room->setEmotion(card_use.from, "weapon/_shuibojian");
    room->addPlayerMark(card_use.from, "_shuibojian-Clear");
    foreach (ServerPlayer*p, card_use.to)
        room->setPlayerFlag(p, "_shuibojian_extratarget");
}

class ShuibojianVS : public ViewAsSkillV2
{
public:
    ShuibojianVS() : ViewAsSkillV2("_shuibojian") { setResponseOrUse(true); }
    bool isEquipSkill() const override { return true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (request.pattern == "@@_shuibojian" || request.pattern == "@@_shuibojian1")
            && request.initiator->hasWeapon(objectName()) && !request.initiator->property("shuibojian_candidates").toMap().isEmpty();
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive() || selected.contains(target)) return false;
        const QVariantMap candidates = request.initiator->property("shuibojian_candidates").toMap();
        if (request.pattern == "@@_shuibojian1") {
            if (selected.isEmpty()) return candidates.contains(target->objectName());
            return selected.size() == 1 && candidates.value(selected.first()->objectName()).toStringList().contains(target->objectName());
        }
        return selected.size() < request.initiator->property("shuibojian_maximum").toInt() && candidates.contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return request.pattern == "@@_shuibojian1" ? targets.size() == 2
            : !targets.isEmpty() && targets.size() <= request.initiator->property("shuibojian_maximum").toInt();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        ActiveSkillCard *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        card->setUserString(request.pattern);
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.invoker) return false;
        const qint64 id = ctx.invoker->getTag("ShuibojianExecution").toLongLong();
        const SkillContext parent = room->getSkillExecutionContext(id);
        const auto *skill = dynamic_cast<const EquipSkillV2 *>(Sanguosha->getSkill(objectName()));
        if (!skill || parent.owner != ctx.invoker || parent.skill_name != objectName() || !parent.original_data
            || !skill->isSourceAvailable(room, parent) || !skill->isUsable(parent)) return false;
        skill->addUsage(parent);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.use_card) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        const qint64 id = ctx.invoker->getTag("ShuibojianExecution").toLongLong();
        SkillContext parent = room->getSkillExecutionContext(id);
        if (parent.owner != ctx.invoker || !parent.original_data) return ContinueEffects;
        CardUseStruct use = parent.original_data->value<CardUseStruct>();
        if (!use.card) return ContinueEffects;
        ServerPlayer *added = target;
        const SkillCard *proxy = qobject_cast<const SkillCard *>(ctx.use_card);
        if (proxy && proxy->getUserString() == "@@_shuibojian1") {
            const QVariantMap candidates = ctx.invoker->property("shuibojian_candidates").toMap();
            if (ctx.targets.value(0) == target) {
                if (candidates.contains(target->objectName())) {
                    parent.extra_data = target->objectName();
                    room->setSkillExecutionContext(id, parent);
                }
                return ContinueEffects;
            }
            ServerPlayer *killer = room->findPlayerByObjectName(parent.extra_data.toString());
            if (!killer || !candidates.value(killer->objectName()).toStringList().contains(target->objectName())
                || !killer->canSlash(target, nullptr, false)) return ContinueEffects;
            killer->setTag("attachTarget", QVariant::fromValue(target));
            added = killer;
        } else if (!ctx.invoker->canUse(use.card, target)) return ContinueEffects;
        if (use.to.contains(added)) return ContinueEffects;
        use.to << added;
        *parent.original_data = QVariant::fromValue(use);
        LogMessage log;
        log.type = "#QiaoshuiAdd";
        log.from = ctx.invoker;
        log.to << added;
        log.card_str = use.card->toString();
        log.arg = objectName();
        room->sendLog(log);
        room->setEmotion(ctx.invoker, "weapon/_shuibojian");
        return ContinueEffects;
    }
};

class ShuibojianSkill : public WeaponSkillV2
{
public:
    ShuibojianSkill() : WeaponSkillV2("_shuibojian", "_shuibojian") { events << PreCardUsed; view_as_skill = new ShuibojianVS; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getMark(exclusiveEquipmentUsageKey(objectName(), ctx)) < getMaxUsageLimit(ctx); }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->addPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx)); }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->setPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx), 0); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasTurn() && WeaponSkillV2::triggerable(player)
            && use.card && !use.to.isEmpty() && (use.card->isKindOf("Slash") || use.card->isNDTrick())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const bool collateral = use.card->isKindOf("Collateral");
        QVariantMap candidates;
        for (ServerPlayer *target : room->getAlivePlayers()) {
            if (use.to.contains(target)) continue;
            if (collateral) {
                int extra = 0;
                if (!use.card->targetFilter({}, target, owner, extra) && extra <= 0) continue;
                QStringList victims;
                for (ServerPlayer *victim : room->getOtherPlayers(target)) {
                    int ignored = 0;
                    if (use.card->targetFilter({target}, victim, owner, ignored)) victims << victim->objectName();
                }
                if (!victims.isEmpty()) candidates.insert(target->objectName(), victims);
            } else if (owner->canUse(use.card, target)) candidates.insert(target->objectName(), true);
        }
        if (candidates.isEmpty() || getEffectiveAmount(ctx) <= 0) return false;
        const QVariant oldCandidates = owner->property("shuibojian_candidates"), oldMaximum = owner->property("shuibojian_maximum");
        const QVariant oldCollateral = owner->property("extra_collateral"), oldExecution = owner->getTag("ShuibojianExecution");
        SkillContext execution = ctx;
        execution.extra_data = QVariant();
        const auto executionGuard = room->beginSkillExecution(execution, *ctx.original_data);
        // Only primitive selection data crosses into the UI; the live CardUseStruct stays in the execution registry.
        owner->setTag("ShuibojianExecution", execution.executionID);
        room->setPlayerProperty(owner, "shuibojian_candidates", candidates);
        room->setPlayerProperty(owner, "shuibojian_maximum", getEffectiveAmount(ctx));
        QStringList previousTargets{use.card->toString()};
        for (ServerPlayer *target : use.to) previousTargets << target->objectName();
        room->setPlayerProperty(owner, "extra_collateral", previousTargets.join('+') + '+' + objectName());
        auto restore = qScopeGuard([&] {
            room->setPlayerProperty(owner, "shuibojian_candidates", oldCandidates);
            room->setPlayerProperty(owner, "shuibojian_maximum", oldMaximum);
            room->setPlayerProperty(owner, "extra_collateral", oldCollateral);
            if (oldExecution.isValid()) owner->setTag("ShuibojianExecution", oldExecution);
            else owner->removeTag("ShuibojianExecution");
        });
        room->askForUseCard(owner, collateral ? "@@_shuibojian1" : "@@_shuibojian", "@_shuibojian:" + use.card->objectName());
        CardUseStruct updated = ctx.original_data->value<CardUseStruct>();
        const CardUseStruct resolved = execution.original_data->value<CardUseStruct>();
        for (ServerPlayer *target : resolved.to)
            if (target->isAlive() && !use.to.contains(target) && !updated.to.contains(target)) updated.to << target;
        room->sortByActionOrder(updated.to);
        *ctx.original_data = QVariant::fromValue(updated);
        return false; // The nested V2 activation owns payment, target hooks, and usage.
    }
};

Shuibojian::Shuibojian(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("_shuibojian");
}

void Shuibojian::onUninstall(ServerPlayer*player) const
{
    if (player->isAlive() && player->hasWeapon(objectName(), nullptr, false)){
		Room*room = player->getRoom();
		if (player->isWounded()) {
			LogMessage log;
			log.type = "#TriggerEquipSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->setEmotion(player, "weapon/_shuibojian");
			room->notifySkillInvoked(player, "_shuibojian");
		}
		room->recover(player, RecoverStruct(nullptr, this, 1, objectName()));
	}
}

class HunduwanbiSkill : public WeaponSkillV2
{
public:
    HunduwanbiSkill() : WeaponSkillV2("_hunduwanbi", "_hunduwanbi") { events << TargetSpecified; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &) const override { return true; }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->addPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx)); }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->setPlayerMark(ctx.owner, exclusiveEquipmentUsageKey(objectName(), ctx), 0); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !WeaponSkillV2::triggerable(player) || use.from != player || !use.card || !use.card->isKindOf("Slash")) return {};
        QStringList targets;
        for (ServerPlayer *target : use.to) if (target->isAlive()) targets << target->objectName();
        return targets.isEmpty() ? TriggerList() : TriggerList{{player, {objectName() + "->" + targets.join('+')}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int next = owner->getMark(exclusiveEquipmentUsageKey(objectName(), ctx)) + 1;
        return ctx.preferredTarget && ctx.preferredTarget->isAlive() && owner->askForSkillInvoke(this,
            QString("_hunduwanbi:%1::%2").arg(ctx.preferredTarget->objectName()).arg(next));
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        // Reserve this activation before target hooks can trigger another use of the weapon.
        addUsage(ctx);
        ctx.extra_data = qMin(5, owner->getMark(exclusiveEquipmentUsageKey(objectName(), ctx)));
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "weapon/_hunduwanbi");
        const int amount = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        if (amount > 0) room->loseHp(HpLostStruct(target, amount, objectName(), owner));
        return false;
    }
};

Hunduwanbi::Hunduwanbi(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("_hunduwanbi");
}

class TianleirenSkill : public WeaponSkillV2
{
public:
    TianleirenSkill() : WeaponSkillV2("_tianleiren", "_tianleiren") { events << TargetSpecified; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !WeaponSkillV2::triggerable(player) || use.from != player || !use.card || !use.card->isKindOf("Slash")) return {};
        QStringList targets;
        for (ServerPlayer *target : use.to) if (target->isAlive()) targets << target->objectName();
        return targets.isEmpty() ? TriggerList() : TriggerList{{player, {objectName() + "->" + targets.join('+')}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { return ctx.preferredTarget && ctx.preferredTarget->isAlive() && owner->askForSkillInvoke(this, ctx.preferredTarget); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "reward") {
            room->recover(target, RecoverStruct(objectName(), owner, getEffectiveAmount(ctx)));
            if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        room->setEmotion(owner, "weapon/_tianleiren");
        const Card *weapon = owner->getWeapon();
        const int id = weapon && weapon->objectName() == objectName() ? weapon->getEffectiveId() : -1;
        const bool alreadyUsing = id >= 0 && Sanguosha->getCard(id)->hasFlag("using");
        JudgeStruct judge;
        judge.who = target;
        judge.reason = objectName();
        judge.pattern = ".|black";
        judge.good = false;
        {
            // Restore the exact card's pre-existing flag even if judgment interrupts the turn.
            if (id >= 0 && !alreadyUsing) room->setCardFlag(id, "using");
            auto restore = qScopeGuard([&] { if (id >= 0 && !alreadyUsing) room->setCardFlag(id, "-using"); });
            room->judge(judge);
        }
        if (!judge.card) return false;
        if (judge.card->getSuit() == Card::Spade) {
            if (target->isAlive()) room->damage(DamageStruct(objectName(), nullptr, target, 3 * getEffectiveAmount(ctx), DamageStruct::Thunder));
        } else if (judge.card->getSuit() == Card::Club) {
            if (target->isAlive()) room->damage(DamageStruct(objectName(), nullptr, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
            if (owner->isAlive()) {
                const QString previous = ctx.choice;
                auto restore = qScopeGuard([&] { ctx.choice = previous; });
                ctx.choice = "reward";
                skillEffect(event, room, owner, ctx, owner);
            }
        }
        return false;
    }
};

Tianleiren::Tianleiren(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("_tianleiren");
}

Meirenji::Meirenji(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("__meirenji");
    damage_card = true;
}

bool Meirenji::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
    return to_select->isMale() && !to_select->isKongcheng() && to_select != Self
	&& targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void Meirenji::onEffect(CardEffectStruct &effect) const
{
    Room*room = effect.to->getRoom();
    foreach (ServerPlayer*p, room->getAllPlayers()) {
        if (effect.to->isDead() || effect.to->isKongcheng()) break;
        if (p->isDead() || !p->isFemale()) continue;
        int id = room->askForCardChosen(p, effect.to, "h", objectName());
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
        room->obtainCard(p, Sanguosha->getCard(id), reason, false);
        if (p->isAlive() && effect.from->isAlive() && !p->isKongcheng()) {
            const Card*card = room->askForCard(p, ".|.|.|hand!", "@__meirenji-give:" + effect.from->objectName(), QVariant::fromValue(effect.from), Card::MethodNone);
            if(!card) card = p->getHandcards().at(qsanRandomBounded(p->getHandcardNum()));
			if(card) room->giveCard(p, effect.from, card, objectName());
        }
    }
    if (effect.from->isDead() || effect.to->isDead()) return;
    if (effect.from->getHandcardNum() > effect.to->getHandcardNum())
        room->damage(DamageStruct(this, effect.to, effect.from));
    else if (effect.from->getHandcardNum() < effect.to->getHandcardNum())
        room->damage(DamageStruct(this, effect.from, effect.to));
}

Xiaolicangdao::Xiaolicangdao(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("__xiaolicangdao");
    damage_card = true;
}

bool Xiaolicangdao::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
    return to_select != Self && targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this);
}

void Xiaolicangdao::onEffect(CardEffectStruct &effect) const
{
    effect.to->drawCards(qMin(5, effect.to->getLostHp()), objectName());
    if (effect.to->isDead()) return;
    effect.to->getRoom()->damage(DamageStruct(this, effect.from, effect.to));
}

class PilicheSkill : public WeaponSkillV2
{
public:
    PilicheSkill() : WeaponSkillV2("_piliche", "_piliche") { events << Damage; }
    static QList<int> discardable(ServerPlayer *owner, ServerPlayer *target)
    {
        QList<int> candidates, result;
        if (!target || !target->isAlive()) return result;
        if (target->getArmor()) candidates << target->getArmor()->getEffectiveId();
        if (target->getDefensiveHorse()) candidates << target->getDefensiveHorse()->getEffectiveId();
        for (int id : candidates) if (owner->canDiscard(target, id)) result << id;
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !WeaponSkillV2::triggerable(player) || damage.from != player || !damage.card || damage.card->isKindOf("SkillCard") || damage.card->isKindOf("DelayedTrick")
            || discardable(player, damage.to).isEmpty()) return {};
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (discardable(owner, target).isEmpty() || !owner->askForSkillInvoke(this, target)) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &, ServerPlayer *target) const override
    {
        // Re-evaluate the recipient's equipment after the target hook, never discard a moved card.
        const QList<int> ids = discardable(owner, target);
        if (ids.isEmpty()) return false;
        room->setEmotion(owner, "weapon/_piliche");
        DummyCard cards(ids);
        room->throwCard(&cards, target, owner);
        return false;
    }
};

Piliche::Piliche(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_piliche");
}

class SecondPilicheSkill : public WeaponSkillV2
{
public:
    SecondPilicheSkill() : WeaponSkillV2("_secondpiliche", "_secondpiliche") { events << Damage; }
    static QList<int> discardable(ServerPlayer *owner, ServerPlayer *target)
    {
        QList<int> candidates, result;
        if (!target || !target->isAlive()) return result;
        candidates = target->getEquipsId();
        for (int id : candidates) if (owner->canDiscard(target, id)) result << id;
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !WeaponSkillV2::triggerable(player) || damage.from != player
            || discardable(player, damage.to).isEmpty()) return {};
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (discardable(owner, target).isEmpty() || !owner->askForSkillInvoke(this, target)) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &, ServerPlayer *target) const override
    {
        // Re-evaluate the recipient's equipment after the target hook, never discard a moved card.
        const QList<int> ids = discardable(owner, target);
        if (ids.isEmpty()) return false;
        room->setEmotion(owner, "weapon/_secondpiliche");
        DummyCard cards(ids);
        room->throwCard(&cards, target, owner);
        return false;
    }
};

SecondPiliche::SecondPiliche(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_secondpiliche");
}

class SichengliangyuSkill : public TreasureSkillV2
{
public:
    SichengliangyuSkill() : TreasureSkillV2("_sichengliangyu", "_sichengliangyu")
    { events << EventPhaseChanging; global = true; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasTreasure(objectName()) && owner->getHandcardNum() < owner->getHp()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this)) return false;
        const Card *treasure = owner->getTreasure();
        ctx.extra_data = treasure && treasure->objectName() == objectName() ? treasure->getEffectiveId() : -1;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        const int id = ctx.extra_data.toInt();
        if (owner->isAlive() && id >= 0 && room->getCardOwner(id) == owner
            && room->getCardPlace(id) == Player::PlaceEquip && owner->canDiscard(id)) room->throwCard(id, objectName(), owner);
        return false;
    }
};

Sichengliangyu::Sichengliangyu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_sichengliangyu");
}

class TiejixuanyuSkill : public TreasureSkillV2
{
public:
    TiejixuanyuSkill() : TreasureSkillV2("_tiejixuanyu", "_tiejixuanyu")
    { events << EventPhaseChanging; global = true; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().to != Player::NotActive
            || !player->canDiscard(player, "he")) return {};
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return {};
        // The printed condition is damage dealt during this turn, not damage received during the round.
        const QVariantMap damage = room->queryActualDamage({{"turn_id", turn}, {"from", player->objectName()}, {"limit", 1}});
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()
            || !damage.value("items").toList().isEmpty()) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->getOtherPlayers(player)) if (owner->hasTreasure(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !owner->askForSkillInvoke(this, ctx.invoker)) return false;
        const Card *treasure = owner->getTreasure();
        ctx.extra_data = treasure && treasure->objectName() == objectName() ? treasure->getEffectiveId() : -1;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->canDiscard(target, "he")) return false;
        room->askForDiscard(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx), false, true);
        const int id = ctx.extra_data.toInt();
        if (owner->isAlive() && id >= 0 && room->getCardOwner(id) == owner
            && room->getCardPlace(id) == Player::PlaceEquip && owner->canDiscard(id)) room->throwCard(id, objectName(), owner);
        return false;
    }
};

Tiejixuanyu::Tiejixuanyu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_tiejixuanyu");
}

class FeilunzhanyuSkill : public TreasureSkillV2
{
public:
    FeilunzhanyuSkill() : TreasureSkillV2("_feilunzhanyu", "_feilunzhanyu") { events << EventPhaseChanging; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || player->isNude() || data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
        const QVariantMap used = room->queryCardHistory(player, "turn");
        if (!used.value("complete").toBool() || !used.value("attribution_complete").toBool()) return {};
        bool nonbasic = false;
        for (const QVariant &value : used.value("items").toList()) {
            const QVariantMap card = value.toMap();
            if (!card.contains("type")) return {};
            nonbasic = nonbasic || card.value("type").toInt() != Card::TypeBasic;
        }
        TriggerList result;
        if (nonbasic) for (ServerPlayer *owner : room->getOtherPlayers(player)) if (owner->hasTreasure(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!ctx.invoker || ctx.invoker->isNude() || !owner->askForSkillInvoke(this, ctx.invoker)) return false;
        const Card *treasure = owner->getTreasure();
        ctx.extra_data = treasure && treasure->objectName() == objectName() ? treasure->getEffectiveId() : -1;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isNude() || !owner->isAlive()) return false;
        const Card *cards = room->askForExchange(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx), true,
            "@_feilunzhanyu-give:" + owner->objectName());
        if (cards) room->giveCard(target, owner, cards, objectName());
        const int id = ctx.extra_data.toInt();
        if (owner->isAlive() && id >= 0 && room->getCardOwner(id) == owner
            && room->getCardPlace(id) == Player::PlaceEquip && owner->canDiscard(id)) room->throwCard(id, objectName(), owner);
        return false;
    }
};

Feilunzhanyu::Feilunzhanyu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_feilunzhanyu");
}

class QiongshuSkill : public TreasureSkillV2
{
public:
    QiongshuSkill() : TreasureSkillV2("_qiongshu", "_qiongshu") { events << DamageInflicted; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int amount = data.value<DamageStruct>().damage;
        return player && player->isAlive() && TreasureSkillV2::triggerable(player) && amount > 0
            && player->canDiscard(player, "he") && player->getCardCount() >= amount ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int amount = ctx.original_data->value<DamageStruct>().damage;
        QStringList allowed;
        for (const Card *card : owner->getCards("he"))
            if (!card->isKindOf("Qiongshu") && owner->canDiscard(card->getEffectiveId())) allowed << QString::number(card->getEffectiveId());
        if (allowed.size() < amount) return false;
        const Card *selected = room->askForExchange(owner, objectName(), amount, amount, true,
            "@_qiongshu-discard:" + QString::number(amount), true, allowed.join(','));
        if (!selected || selected->subcardsLength() != amount) return false;
        ctx.extra_data = ListI2V(selected->getSubcards());
        ctx.targets << owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const QList<int> ids = ListV2I(ctx.extra_data.toList());
        if (ids.isEmpty()) return false;
        for (int id : ids)
            if (room->getCardOwner(id) != owner || !owner->canDiscard(id)
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        DummyCard cards(ids);
        room->throwCard(&cards, objectName(), owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to) return false;
        room->setEmotion(owner, "treasure/_qiongshu");
        room->broadcastSkillInvoke(this);
        return target->damageRevises(*ctx.original_data, -damage.damage);
    }
};

Qiongshu::Qiongshu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_qiongshu");
}

class XishuSkill : public TreasureSkillV2
{
public:
    XishuSkill() : TreasureSkillV2("_xishu", "_xishu") { events << EventPhaseChanging; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && TreasureSkillV2::triggerable(player)
            && data.value<PhaseChangeStruct>().to == Player::Judge
            && (!player->isSkipped(Player::Judge) || !player->isSkipped(Player::Discard))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QStringList choices;
        if (!owner->isSkipped(Player::Judge)) choices << "judge";
        if (!owner->isSkipped(Player::Discard)) choices << "discard";
        if (choices.isEmpty() || !owner->askForSkillInvoke(objectName() + "$-1")) return false;
        ctx.choice = room->askForChoice(owner, objectName(), choices.join('+'));
        ctx.targets << owner;
        return choices.contains(ctx.choice);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(owner, "treasure/_xishu");
        target->skip(ctx.choice == "judge" ? Player::Judge : Player::Discard);
        return false;
    }
};

Xishu::Xishu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_xishu");
}

class JinshuSkill : public TreasureSkillV2
{
public:
    JinshuSkill() : TreasureSkillV2("_jinshu", "_jinshu") { events << EventPhaseEnd; frequency = Compulsory; m_baseAmount = 5; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && TreasureSkillV2::triggerable(player) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (owner->getHandcardNum() >= qMin(getEffectiveAmount(ctx), owner->getMaxCards())) return false;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = qMin(getEffectiveAmount(ctx), target->getMaxCards()) - target->getHandcardNum();
        if (count > 0) { room->sendCompulsoryTriggerLog(owner, this); room->setEmotion(owner, "treasure/_jinshu"); target->drawCards(count, objectName()); }
        return false;
    }
};

Jinshu::Jinshu(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_jinshu");
}

class TenyearPilicheSkill : public TreasureSkillV2
{
public:
    TenyearPilicheSkill() : TreasureSkillV2("_tenyearpiliche", "_tenyearpiliche")
    { events << ConfirmDamage << PreHpRecover << CardUsed; frequency = Compulsory; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *owner = player;
        const Card *card = nullptr;
        if (event == PreHpRecover) {
            const RecoverStruct recovery = data.value<RecoverStruct>();
            owner = recovery.who;
            card = recovery.card;
            if (!player || !player->isWounded() || !owner || !owner->hasFlag("CurrentPlayer")) return {};
        } else if (event == ConfirmDamage) {
            const DamageStruct damage = data.value<DamageStruct>();
            owner = damage.from;
            card = damage.card;
            if (!damage.by_user || !owner || !owner->hasFlag("CurrentPlayer")) return {};
        } else {
            const CardUseStruct use = data.value<CardUseStruct>();
            owner = use.from;
            card = use.card;
            if (!owner || owner->hasFlag("CurrentPlayer")) return {};
        }
        return owner && owner->isAlive() && TreasureSkillV2::triggerable(owner) && card && card->isKindOf("BasicCard")
            ? TriggerList{{owner, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        // Recovery is delivered to the event recipient, while the equipment belongs to the card user.
        ServerPlayer *target = event == ConfirmDamage ? ctx.original_data->value<DamageStruct>().to
            : event == PreHpRecover ? ctx.invoker : owner;
        if (!target || !target->isAlive()) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        if (event == ConfirmDamage) {
            if (target == ctx.original_data->value<DamageStruct>().to) target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        } else if (event == PreHpRecover) {
            if (target != ctx.invoker) return false;
            RecoverStruct recovery = ctx.original_data->value<RecoverStruct>();
            recovery.recover += getEffectiveAmount(ctx);
            ctx.original_data->setValue(recovery);
        } else target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

TenyearPiliche::TenyearPiliche(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_tenyearpiliche");
}

ZhizheBasic::ZhizheBasic(Suit suit, int number) : BasicCard(suit, number)
{
    setObjectName("_zhizhe_basic");
}

QString ZhizheBasic::getSubtype() const
{
    return "zhizhe_card";
}

bool ZhizheBasic::isAvailable(const Player*) const
{
    return false;
}

ZhizheTrick::ZhizheTrick(Card::Suit suit, int number) : TrickCard(suit, number)
{
    setObjectName("_zhizhe_trick");
}

QString ZhizheTrick::getSubtype() const
{
    return "zhizhe_card";
}

bool ZhizheTrick::isAvailable(const Player*) const
{
    return false;
}

ZhizheSuijiyingbian::ZhizheSuijiyingbian(Card::Suit suit, int number) : Suijiyingbian(suit, number)
{
    setObjectName("_zhizhe_suijiyingbian");
}

QString ZhizheSuijiyingbian::getSubtype() const
{
    return "zhizhe_card";
}

ZhizheWeapon::ZhizheWeapon(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("_zhizhe_weapon");
}

QString ZhizheWeapon::getSubtype() const
{
    return "zhizhe_card";
}

ZhizheArmor::ZhizheArmor(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("_zhizhe_armor");
}

QString ZhizheArmor::getSubtype() const
{
    return "zhizhe_card";
}

ZhizheTreasure::ZhizheTreasure(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_zhizhe_treasure");
}

QString ZhizheTreasure::getSubtype() const
{
    return "zhizhe_card";
}

class BintieshuangjiSkill : public WeaponSkillV2
{
public:
    BintieshuangjiSkill() : WeaponSkillV2("_bintieshuangji", "_bintieshuangji") { events << CardOffset; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && WeaponSkillV2::triggerable(player) && effect.from == player && effect.card && effect.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &) const override
    { room->loseHp(owner, 1, true, owner, objectName()); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card *slash = ctx.original_data->value<CardEffectStruct>().card;
        QList<int> ids = slash->isVirtualCard() ? slash->getSubcards() : QList<int>{slash->getEffectiveId()};
        QList<int> obtainable;
        for (int id : ids) if (id >= 0 && (room->getCardPlace(id) == Player::PlaceTable || room->getCardPlace(id) == Player::DiscardPile)) obtainable << id;
        if (!obtainable.isEmpty()) { DummyCard cards(obtainable); room->obtainCard(target, &cards); }
        if (target->isAlive()) {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            // This is an applied Slash allowance; it remains after the weapon leaves play.
            room->addPlayerMark(target, "_bintieshuangji-Clear", getEffectiveAmount(ctx));
        }
        return false;
    }
};

Bintieshuangji::Bintieshuangji(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("_bintieshuangji");
}

class SanlveMax : public MaxCardsSkillV2
{
public:
    SanlveMax() : MaxCardsSkillV2("#_sanlve_max")
    {
        setHolderSelector(CorrectSkill_System);
        m_baseAmount = 1;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.primary && ctx.primary->hasTreasure("_sanlve")
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class SanlveAttack : public AttackRangeSkillV2
{
public:
    SanlveAttack() : AttackRangeSkillV2("#_sanlve_attack")
    {
        setHolderSelector(CorrectSkill_System);
        m_baseAmount = 1;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.primary && ctx.primary->hasTreasure("_sanlve")
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

Sanlve::Sanlve(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_sanlve");
}

class ZhaogujingVS : public ViewAsSkillV2
{
public:
    ZhaogujingVS() : ViewAsSkillV2("_zhaogujing") { setResponseOrUse(true); }
    bool isEquipSkill() const override { return true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@_zhaogujing" && request.initiator->hasTreasure("_zhaogujing")
            && !request.initiator->property("ZhaogujingDeclaredCard").toString().isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        // The reveal fixes the name on the server. A client userString cannot substitute another card.
        const QString name = request.initiator->property("ZhaogujingDeclaredCard").toString();
        if (!request.userString.isEmpty() && request.userString != name) return nullptr;
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return nullptr;
        card->setSkillName(objectName());
        if ((!card->isKindOf("BasicCard") && !card->isNDTrick()) || !card->isAvailable(request.initiator)) {
            delete card;
            return nullptr;
        }
        return card;
    }
};

class ZhaogujingSkill : public TreasureSkillV2
{
public:
    ZhaogujingSkill() : TreasureSkillV2("_zhaogujing", "_zhaogujing")
    { events << EventPhaseEnd; view_as_skill = new ZhaogujingVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && TreasureSkillV2::triggerable(player) && player->getPhase() == Player::Play && !player->isKongcheng()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const Card *card = room->askForCard(owner, "BasicCard,TrickCard+^DelayedTrick|.|.|hand", "_zhaogujing0:",
            *ctx.original_data, Card::MethodNone);
        if (!card || !owner->handCards().contains(card->getEffectiveId()) || (!card->isKindOf("BasicCard") && !card->isNDTrick())) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets << owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!owner->handCards().contains(id)) return false;
        const QString name = Sanguosha->getCard(id)->objectName();
        room->showCard(owner, id);
        ctx.extra_data = name;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariant previous = target->property("ZhaogujingDeclaredCard");
        auto restore = qScopeGuard([&] { room->setPlayerProperty(target, "ZhaogujingDeclaredCard", previous); });
        room->setPlayerProperty(target, "ZhaogujingDeclaredCard", ctx.extra_data);
        room->setEmotion(target, "treasure/_zhaogujing");
        room->askForUseCard(target, "@@_zhaogujing", "_zhaogujing0:" + ctx.extra_data.toString());
        return false;
    }
};

Zhaogujing::Zhaogujing(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("_zhaogujing");
}

class DagongcheJinjiSkill : public WeaponSkillV2
{
protected:
    bool usesEventSource(const SkillContext &ctx) const override
    {
        if (ctx.current_event != BeforeCardsMove || !ctx.original_data) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.from != ctx.owner) return false;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceEquip
                && Sanguosha->getCard(move.card_ids.at(i))->objectName() == objectName()) return true;
        return false;
    }
public:
    DagongcheJinjiSkill() : WeaponSkillV2("_dagongche_jinji", "_dagongche_jinji") { events << BeforeCardsMove << DamageCaused; }
    Frequency getFrequency(const Player *player = nullptr) const override { return player ? Compulsory : NotFrequent; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return {};
        const Card *weapon = player->getWeapon();
        if (event == BeforeCardsMove) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceEquip && weapon && weapon->objectName() == objectName())
                return TriggerList{{player, {objectName()}}};
            if (move.from != player || move.reason.m_skillName == "quchong" || move.reason.m_skillName == "BreakCard") return {};
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceEquip
                    && Sanguosha->getCard(move.card_ids.at(i))->objectName() == objectName()) return TriggerList{{player, {objectName()}}};
            return {};
        }
        const DamageStruct damage = data.value<DamageStruct>();
        return player->isAlive() && weapon && weapon->objectName() == objectName() && WeaponSkillV2::triggerable(player)
            && damage.to && damage.damage > 0 && room->getTag("durable" + weapon->toString()).toInt() > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == BeforeCardsMove) return true;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!owner->askForSkillInvoke(this, damage.to)) return false;
        const Card *weapon = owner->getWeapon();
        if (!weapon || weapon->objectName() != objectName()) return false;
        ctx.extra_data = weapon->getEffectiveId();
        ctx.targets << damage.to;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == BeforeCardsMove) return true;
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceEquip) return false;
        const QString key = "durable" + Sanguosha->getCard(id)->toString();
        const int durability = room->getTag(key).toInt();
        if (durability <= 0) return false;
        room->setTag(key, durability - 1);
        if (durability == 1) room->breakCard(QList<int>{id}, owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != BeforeCardsMove) return false;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const Card *weapon = owner->getWeapon();
        if (move.to == owner && move.to_place == Player::PlaceEquip && weapon && weapon->objectName() == objectName()) {
            move.to_place = Player::DiscardPile;
            *ctx.original_data = QVariant::fromValue(move);
            room->sendCompulsoryTriggerLog(owner, this);
        } else if (move.from == owner && move.reason.m_skillName != "quchong" && move.reason.m_skillName != "BreakCard") {
            QList<int> protectedCards;
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceEquip
                    && Sanguosha->getCard(move.card_ids.at(i))->objectName() == objectName()) protectedCards << move.card_ids.at(i);
            if (protectedCards.isEmpty()) return false;
            move.removeCardIds(protectedCards);
            *ctx.original_data = QVariant::fromValue(move);
            for (int id : protectedCards) {
                const QString key = "durable" + Sanguosha->getCard(id)->toString();
                const int remaining = qMax(0, room->getTag(key).toInt() - 1);
                room->setTag(key, remaining);
                if (remaining == 0) room->breakCard(QList<int>{id}, owner);
            }
            room->sendCompulsoryTriggerLog(owner, this);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        return target->damageRevises(*ctx.original_data, qMin(3, room->getTag("TurnLengthCount").toInt()) * getEffectiveAmount(ctx));
    }
};

DagongcheJinji::DagongcheJinji(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_dagongche_jinji");
}

void DagongcheJinji::onInstall(ServerPlayer*player) const
{
	Room*room = player->getRoom();
	room->setTag("durable"+toString(),2);
	Weapon::onInstall(player);
	QList<int>ids = player->getEquipsId();
	ids.removeAll(getEffectiveId());
	room->throwCard(ids,objectName(),nullptr);
}

void DagongcheJinji::onUninstall(ServerPlayer*player) const
{
	Room*room = player->getRoom();
	room->setTag("dagongche"+toString(),"");
	Weapon::onUninstall(player);
}

class DagongcheShouyuSkill : public WeaponSkillV2
{
protected:
    bool usesEventSource(const SkillContext &ctx) const override
    {
        if (ctx.current_event != BeforeCardsMove || !ctx.original_data) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.from != ctx.owner) return false;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceEquip
                && Sanguosha->getCard(move.card_ids.at(i))->objectName() == objectName()) return true;
        return false;
    }
public:
    DagongcheShouyuSkill() : WeaponSkillV2("_dagongche_shouyu", "_dagongche_shouyu") { events << BeforeCardsMove << DamageInflicted; }
    Frequency getFrequency(const Player *player = nullptr) const override { return player ? Compulsory : NotFrequent; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return {};
        const Card *weapon = player->getWeapon();
        if (event == BeforeCardsMove) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceEquip && weapon && weapon->objectName() == objectName())
                return TriggerList{{player, {objectName()}}};
            if (move.from != player || move.reason.m_skillName == "quchong" || move.reason.m_skillName == "BreakCard") return {};
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceEquip
                    && Sanguosha->getCard(move.card_ids.at(i))->objectName() == objectName()) return TriggerList{{player, {objectName()}}};
            return {};
        }
        const DamageStruct damage = data.value<DamageStruct>();
        return player->isAlive() && weapon && weapon->objectName() == objectName() && WeaponSkillV2::triggerable(player)
            && damage.to && damage.damage > 0 && room->getTag("durable" + weapon->toString()).toInt() > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == BeforeCardsMove) return true;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();

        const Card *weapon = owner->getWeapon();
        if (!weapon || weapon->objectName() != objectName()) return false;
        ctx.extra_data = weapon->getEffectiveId();
        ctx.targets << damage.to;
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != BeforeCardsMove) return false;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const Card *weapon = owner->getWeapon();
        if (move.to == owner && move.to_place == Player::PlaceEquip && weapon && weapon->objectName() == objectName()) {
            move.to_place = Player::DiscardPile;
            *ctx.original_data = QVariant::fromValue(move);
            room->sendCompulsoryTriggerLog(owner, this);
        } else if (move.from == owner && move.reason.m_skillName != "quchong" && move.reason.m_skillName != "BreakCard") {
            QList<int> protectedCards;
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceEquip
                    && Sanguosha->getCard(move.card_ids.at(i))->objectName() == objectName()) protectedCards << move.card_ids.at(i);
            if (protectedCards.isEmpty()) return false;
            move.removeCardIds(protectedCards);
            *ctx.original_data = QVariant::fromValue(move);
            for (int id : protectedCards) {
                const QString key = "durable" + Sanguosha->getCard(id)->toString();
                const int remaining = qMax(0, room->getTag(key).toInt() - 1);
                room->setTag(key, remaining);
                if (remaining == 0) room->breakCard(QList<int>{id}, owner);
            }
            room->sendCompulsoryTriggerLog(owner, this);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceEquip) return false;
        const QString key = "durable" + Sanguosha->getCard(id)->toString();
        const int durability = qMax(0, room->getTag(key).toInt());
        const int prevented = qMin(damage.damage, durability);
        if (prevented <= 0) return false;
        room->setTag(key, durability - prevented);
        room->sendCompulsoryTriggerLog(owner, this);
        if (durability == prevented) room->breakCard(QList<int>{id}, owner);
        return target->damageRevises(*ctx.original_data, -prevented);
    }
};

DagongcheShouyu::DagongcheShouyu(Suit suit, int number)
    : Weapon(suit, number, 9)
{
    setObjectName("_dagongche_shouyu");
}

void DagongcheShouyu::onInstall(ServerPlayer*player) const
{
	Room*room = player->getRoom();
	room->setTag("durable"+toString(),4);
	Weapon::onInstall(player);
	QList<int>ids = player->getEquipsId();
	ids.removeAll(getEffectiveId());
	room->throwCard(ids,objectName(),nullptr);
}

void DagongcheShouyu::onUninstall(ServerPlayer*player) const
{
	Room*room = player->getRoom();
	room->setTag("dagongche"+toString(),"");
	Weapon::onUninstall(player);
}

class ZhuobangSkillvs : public ViewAsSkillV2
{
public:
	ZhuobangSkillvs() : ViewAsSkillV2("__zhuobang")
	{
	}

	bool isEquipSkill() const override { return true; }

    const ViewAsSkillV2 *delegate(const Player *player) const
	{
        return player ? dynamic_cast<const ViewAsSkillV2 *>(
            Sanguosha->getViewAsSkill(player->property("__zhuobangSkill").toString())) : nullptr;
    }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const ViewAsSkillV2 *vs = delegate(request.initiator);
		return request.initiator && request.initiator->hasWeapon("__zhuobang") && vs
            && vs->canActivate(request);
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		const ViewAsSkillV2 *vs = delegate(request.initiator);
		return vs && vs->canSelectCard(request, card);
	}

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const ViewAsSkillV2 *vs = delegate(request.initiator);
        return vs && vs->cardSelectionFeasible(request);
    }

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		const ViewAsSkillV2 *vs = delegate(request.initiator);
		return vs ? vs->createCard(request) : nullptr;
	}
};

class ZhuobangSkill : public WeaponSkillV2
{
public:
    ZhuobangSkill() : WeaponSkillV2("__zhuobang", "__zhuobang")
    {
		view_as_skill = new ZhuobangSkillvs;
		for (int i = (int)GameStart; i < (int)NumOfEvents; i++)
			events << (TriggerEvent)i;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != BeforeCardsMove) return false;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || !move.from_places.contains(Player::PlaceEquip)) return false;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceEquip
                && Sanguosha->getEngineCard(move.card_ids.at(i))->objectName() == objectName())
                room->setPlayerProperty(qobject_cast<ServerPlayer *>(move.from), "pingjian_triggerskill", "");
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event == BeforeCardsMove) return {};
        TriggerList result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->hasWeapon(objectName())) {
                const QString name = p->property("__zhuobangSkill").toString();
                const TriggerSkill *delegateSkill = Sanguosha->getTriggerSkill(name);
                if (!name.isEmpty() && name != objectName() && delegateSkill
                    && delegateSkill->hasEvent(event))
                    result.insert(p, QStringList(objectName()));
            }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QString name = player->property("__zhuobangSkill").toString();
        const TriggerSkillV2 *skill = qobject_cast<const TriggerSkillV2 *>(Sanguosha->getTriggerSkill(name));
        if (!skill || !skill->hasEvent(event) || !ctx.original_data) return false;
        return skill->trigger(event, room, ctx.invoker, *ctx.original_data, player);
    }
};

Zhuobang::Zhuobang(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("__zhuobang");
}

int Zhuobang::getRange(const Player*player) const
{
    if(player) return qMax(range,player->property("__zhuobangRange").toInt());
	return range;
}

class YoubiSkillvs : public ViewAsSkillV2
{
public:
	YoubiSkillvs() : ViewAsSkillV2("__youbi")
	{
	}

	bool isEquipSkill() const override { return true; }

    const ViewAsSkillV2 *delegate(const Player *player) const
	{
        return player ? dynamic_cast<const ViewAsSkillV2 *>(
            Sanguosha->getViewAsSkill(player->property("__youbiSkill").toString())) : nullptr;
    }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const ViewAsSkillV2 *vs = delegate(request.initiator);
		return request.initiator && request.initiator->hasWeapon("__youbi") && vs
            && vs->canActivate(request);
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		const ViewAsSkillV2 *vs = delegate(request.initiator);
		return vs && vs->canSelectCard(request, card);
	}

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const ViewAsSkillV2 *vs = delegate(request.initiator);
        return vs && vs->cardSelectionFeasible(request);
    }

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		const ViewAsSkillV2 *vs = delegate(request.initiator);
		return vs ? vs->createCard(request) : nullptr;
	}
};

class YoubiSkill : public WeaponSkillV2
{
public:
    YoubiSkill() : WeaponSkillV2("__youbi", "__youbi")
    {
		view_as_skill = new YoubiSkillvs;
		for (int i = (int)GameStart; i < (int)NumOfEvents; i++)
			events << (TriggerEvent)i;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != BeforeCardsMove) return false;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || !move.from_places.contains(Player::PlaceEquip)) return false;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceEquip
                && Sanguosha->getEngineCard(move.card_ids.at(i))->objectName() == objectName())
                room->setPlayerProperty(qobject_cast<ServerPlayer *>(move.from), "pingjian_triggerskill", "");
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event == BeforeCardsMove) return {};
        TriggerList result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->hasWeapon(objectName())) {
                const QString name = p->property("__youbiSkill").toString();
                const TriggerSkill *delegateSkill = Sanguosha->getTriggerSkill(name);
                if (!name.isEmpty() && name != objectName() && delegateSkill
                    && delegateSkill->hasEvent(event))
                    result.insert(p, QStringList(objectName()));
            }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QString name = player->property("__youbiSkill").toString();
        const TriggerSkillV2 *skill = qobject_cast<const TriggerSkillV2 *>(Sanguosha->getTriggerSkill(name));
        if (!skill || !skill->hasEvent(event) || !ctx.original_data) return false;
        return skill->trigger(event, room, ctx.invoker, *ctx.original_data, player);
    }
};

Youbi::Youbi(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("__youbi");
}

int Youbi::getRange(const Player*player) const
{
    if(player) return qMax(range,player->property("__youbiRange").toInt());
    return range;
}










class ExclusiveEquipSkill : public TriggerSkillV2
{
    static QList<int> destroyed(Room *room, const CardsMoveOneTimeStruct &move)
    {
        QList<int> result;
        if (!move.from || move.reason.m_eventName == "break_equip") return result;
        static const QStringList outsideEquip{"_qiongshu", "_xishu", "_jinshu"};
        static const QStringList leavingEquip{"_piliche", "_sichengliangyu", "_tiejixuanyu", "_feilunzhanyu",
            "_tenyearpiliche", "god_ship", "__zhuobang", "__youbi"};
        static const QStringList discarded{"_hongduanqiang", "_liecuidao", "_shuibojian", "_hunduwanbi", "_tianleiren"};
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const Card *card = Sanguosha->getEngineCard(move.card_ids.at(i));
            const QString name = card->objectName(), converted = room->ZhizheCardViewAsEquip(card);
            if ((move.to_place != Player::PlaceEquip && (outsideEquip.contains(name) || outsideEquip.contains(converted)))
                || (move.from_places.value(i) == Player::PlaceEquip && (leavingEquip.contains(name) || leavingEquip.contains(converted)))
                || (move.to_place == Player::DiscardPile && (discarded.contains(name) || discarded.contains(converted)))) result << move.card_ids.at(i);
        }
        return result;
    }
public:
    ExclusiveEquipSkill() : TriggerSkillV2("exclusiveequipskill") { events << BeforeCardsMove; frequency = Compulsory; global = true; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 9; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (destroyed(room, move).isEmpty()) return true;
        ServerPlayer *owner = room->findPlayerByObjectName(move.from->objectName(), true);
        if (!owner) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = owner;
        ctx.invoker = ctx.initiator = player ? player : owner;
        ctx.original_data = &data;
        ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.original_data && !destroyed(room, ctx.original_data->value<CardsMoveOneTimeStruct>()).isEmpty(); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = destroyed(room, move);
        if (ids.isEmpty()) return false;
        // The rule changes a card destination; it does not manufacture a player skill instance.
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        move.reason.m_eventName = "break_equip";
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, move.reason), true);
        return false;
    }
};

ExclusiveCardsPackage::ExclusiveCardsPackage()
    : Package("exclusive_cards", Package::CardPack)
{
    QList<Card*> cards;

    cards << new Hunduwanbi(Card::Spade, 1)
		<< new Tianleiren(Card::Spade, 1)
		<< new Shuibojian(Card::Club, 1)
		<< new Hongduanqiang(Card::Heart, 1)
		<< new Liecuidao(Card::Diamond, 1)
		<< new Feilunzhanyu(Card::Spade, 5)
		<< new Tiejixuanyu(Card::Club, 5)
		<< new Sichengliangyu(Card::Heart, 5)
        << new Qiongshu(Card::Spade, 12)
		<< new Xishu(Card::Club, 12)
		<< new Jinshu(Card::Heart, 12)
		<< new Piliche(Card::Diamond, 9)
		<< new SecondPiliche(Card::Diamond, 9)
        << new TenyearPiliche(Card::Diamond, 9)
        << new Bintieshuangji(Card::Diamond, 13)
        << new Sanlve(Card::Spade, 5)
        << new Zhaogujing(Card::Diamond, 4)
        << new DagongcheJinji(Card::NoSuit, 0)
        << new DagongcheShouyu(Card::NoSuit, 0);

    cards << new Meirenji(Card::NoSuit, 0) << new Xiaolicangdao(Card::NoSuit, 0);

    for (int i = 0; i < 5; i++) {
        cards << new ZhizheBasic(Card::NoSuit, 0);
        cards << new ZhizheTrick(Card::NoSuit, 0);
        cards << new ZhizheSuijiyingbian(Card::NoSuit, 0);
        cards << new ZhizheWeapon(Card::NoSuit, 0);
        cards << new ZhizheArmor(Card::NoSuit, 0);
        Horse*horse = new DefensiveHorse(Card::NoSuit, 0, 0);
        horse->setObjectName("_zhizhe_defensivehorse");
        cards << horse;
        horse = new OffensiveHorse(Card::NoSuit, 0, 0);
        horse->setObjectName("_zhizhe_offensivehorse");
        cards << horse;
        cards << new ZhizheTreasure(Card::NoSuit, 0);
    }

    foreach(Card*card, cards)
        card->setParent(this);

    addMetaObject<ShuibojianCard>();

    skills << new HongduanqiangSkill << new LiecuidaoTargetMod << new LiecuidaoSkill << new ShuibojianSkill
           << new HunduwanbiSkill << new TianleirenSkill << new PilicheSkill << new SecondPilicheSkill
           << new SichengliangyuSkill << new TiejixuanyuSkill << new FeilunzhanyuSkill << new QiongshuSkill
           << new XishuSkill << new JinshuSkill << new TenyearPilicheSkill << new BintieshuangjiSkill
		   << new SanlveMax << new SanlveAttack << new ZhaogujingSkill << new DagongcheJinjiSkill
		   << new DagongcheShouyuSkill << new ZhuobangSkill << new YoubiSkill;

    skills << new ExclusiveEquipSkill;

    related_skills.insert("_liecuidao", "#_liecuidao-target");
}
ADD_PACKAGE(ExclusiveCards)
