#include "tenyear-strengthen.h"
#include "settings.h"
#include <QScopeGuard>
#include <memory>
#include "skill-instance-utils.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "json.h"
#include "exppattern.h"
#include "yjcm2013.h"
#include "wind.h"

TenyearZhihengCard::TenyearZhihengCard()
{
	setSkillName("tenyearzhiheng");
	target_fixed = true;
	will_throw = true;
	mute = true;
}

void TenyearZhihengCard::onUse(Room *room, CardUseStruct &card_use) const
{
	bool allhand = true;
	foreach(int id, card_use.from->handCards()) {
		if (!subcards.contains(id)) {
			allhand = false;
			break;
		}
	}
	if (allhand&&!card_use.from->isKongcheng())
		room->setCardFlag(this, "tenyearzhiheng_all_handcard_" + card_use.from->objectName());
	SkillCard::onUse(room, card_use);
}

void TenyearZhihengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->hasInnateSkill("tenyearzhiheng") || !source->hasSkill("jilve"))
		room->broadcastSkillInvoke("tenyearzhiheng",qsanRandomBounded(2)+1);
	else
		room->broadcastSkillInvoke("jilve", 4);
	int x = subcardsLength();
	if (hasFlag("tenyearzhiheng_all_handcard_" + source->objectName()))
		x++;
	source->drawCards(x, "tenyearzhiheng");
}

class TenyearZhiheng : public ViewAsSkillV2
{
public:
    TargetMode targetMode() const override { return NoTarget; }
	TenyearZhiheng() : ViewAsSkillV2("tenyearzhiheng") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	QString historyKey(const ActiveSkillRequest &) const override { return "TenyearZhihengCard"; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.initiator->canDiscard(request.initiator, "he")
			&& (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
				|| (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@tenyearzhiheng"));
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && !card->hasFlag("using")
			&& (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card))
			&& !request.selectedCardIds.contains(card->getEffectiveId())
			&& request.initiator->canDiscard(request.initiator, card->getEffectiveId());
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty()) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		for (int id : request.selectedCardIds) {
			if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
			selection.selectedCardIds << id;
		}
		return true;
	}

	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!ctx.invoker || !cardSelectionFeasible(request)) return false;
		// Snapshot the hand before atomic payment; move triggers must not change the bonus.
		bool allHand = !ctx.initiator->isKongcheng();
		for (int id : ctx.initiator->handCards())
			if (!request.selectedCardIds.contains(id)) allHand = false;
		ctx.extra_data = QVariantMap{{"count", request.selectedCardIds.size()}, {"all_hand", allHand}};
		return ViewAsSkillV2::pay(room, ctx, request);
	}

	bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return false;
		bool allHand = !ctx.initiator->isKongcheng();
		for (int id : ctx.initiator->handCards())
			if (!request.selectedCardIds.contains(id)) allHand = false;
		ctx.extra_data = QVariantMap{{"count", request.selectedCardIds.size()}, {"all_hand", allHand}};
		ctx.manual_effect = true;
		return true;
	}

	EffectFlow effect(SkillContext &ctx) const override
	{
		// No selected targets, but the draw recipient still passes the V2 target hook.

		skillEffect(ctx, ctx.invoker);
		return FinishSkill;
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
		Room *room = target->getRoom();
		if (ctx.invoker->hasInnateSkill(objectName()) || !ctx.invoker->hasSkill("jilve"))
			room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 1);
		else
			room->broadcastSkillInvoke("jilve", 4);
		const QVariantMap paid = ctx.extra_data.toMap();
		target->drawCards(paid.value("count").toInt() + (paid.value("all_hand").toBool() ? getEffectiveAmount(ctx) : 0), objectName());
		return ContinueEffects;
	}
};

class TenyearJiuyuan : public TriggerSkill
{
public:
	TenyearJiuyuan() : TriggerSkill("tenyearjiuyuan$")
	{
		events << CardUsed;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getKingdom() == "wu";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Peach") || !use.to.contains(player)) return false;
		QList<ServerPlayer *> sunquans;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->hasLordSkill(this)) continue;
			if (player->getHp() > p->getHp() && p->getLostHp() > 0)
				sunquans << p;
		}
		if (sunquans.isEmpty()) return false;
		ServerPlayer *sunquan = room->askForPlayerChosen(player, sunquans, objectName(), "@tenyearjiuyuan-invoke", true);
		if (!sunquan) return false;
		LogMessage log;
		log.type = "#InvokeOthersSkill";
		log.from = player;
		log.to << sunquan;
		log.arg = "tenyearjiuyuan";
		room->sendLog(log);
		if (sunquan->isWeidi()) {
			room->broadcastSkillInvoke("weidi");
			room->notifySkillInvoked(sunquan, "weidi");
		} else {
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(sunquan, objectName());
		}
		room->recover(sunquan, RecoverStruct(objectName(), player));
		player->drawCards(1, objectName());
		return true;
	}
};

TenyearJieyinCard::TenyearJieyinCard()
{
	setSkillName("tenyearjieyin");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TenyearJieyinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (!targets.isEmpty()||!to_select->isMale()) return false;
	const Card *card = Sanguosha->getCard(getEffectiveId());
	if (card->isKindOf("EquipCard")&&!Self->handCards().contains(getEffectiveId())) {
		const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
		return !to_select->getEquip(equip->location()) && !Self->isProhibited(to_select, card);
	}
	return true;
}

void TenyearJieyinCard::onEffect(CardEffectStruct &effect) const
{
	const Card *card = Sanguosha->getCard(getEffectiveId());
	Room *room = effect.from->getRoom();
	QStringList choices;
	if (card->isKindOf("EquipCard")) {
		const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
		if (!effect.to->getEquip(equip->location()) && !effect.from->isProhibited(effect.to, card))
			choices << "enter";
	}
	if (effect.from->canDiscard(effect.from, getEffectiveId()))
		choices << "throw";
	if (room->askForChoice(effect.from,"tenyearjieyin",choices.join("+"),QVariant::fromValue(effect))=="enter") {
		LogMessage log;
		log.type = "$ZhijianEquip";
		log.from = effect.to;
		log.card_str = QString::number(getEffectiveId());
		room->sendLog(log);
		room->moveCardTo(card, effect.from, effect.to, Player::PlaceEquip,
			CardMoveReason(CardMoveReason::S_REASON_PUT,
			effect.from->objectName(), "tenyearjieyin", ""));
	} else {
		CardMoveReason reason(CardMoveReason::S_REASON_THROW, effect.from->objectName(), "tenyearjieyin", "");
		room->throwCard(this, reason, effect.from, nullptr);
	}

	if (effect.from->getHp() == effect.to->getHp()) return;
	RecoverStruct recover("tenyearjieyin", effect.from);
	if (effect.from->getHp() < effect.to->getHp()) {
		room->recover(effect.from, recover, true);
		effect.to->drawCards(1, "tenyearjieyin");
	} else {
		effect.from->drawCards(1, "tenyearjieyin");
		room->recover(effect.to, recover, true);
	}
}

class TenyearJieyin : public ViewAsSkillV2
{
public:
    TenyearJieyin() : ViewAsSkillV2("tenyearjieyin", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearJieyinCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty()) return false;
        const int id = card->getEffectiveId();
        if (!request.initiator->handCards().contains(id) && !request.initiator->hasEquip(card)) return false;
        return card->isKindOf("EquipCard") || (request.initiator->handCards().contains(id)
            && request.initiator->canDiscard(request.initiator, id));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canEnter(const Player *source, const Player *target, const Card *card) const
    {
        const EquipCard *equip = card ? qobject_cast<const EquipCard *>(card->getRealCard()) : nullptr;
        return source && target && equip && !target->getEquip(equip->location()) && !source->isProhibited(target, card);
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !selected.isEmpty() || !target || !target->isAlive() || !target->isMale()
            || !cardSelectionFeasible(request)) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        if (request.initiator->hasEquip(card)) return canEnter(request.initiator, target, card);
        return canEnter(request.initiator, target, card)
            || request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || ctx.targets.size() != 1) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        QStringList choices;
        if (canEnter(ctx.initiator, ctx.targets.first(), card)) choices << "enter";
        if (ctx.initiator->canDiscard(ctx.initiator, card->getEffectiveId())) choices << "throw";
        if (choices.isEmpty()) return false;
        CardEffectStruct preview;
        preview.from = ctx.invoker;
        preview.to = ctx.targets.first();
        preview.card = ctx.use_card;
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"), QVariant::fromValue(preview));
        ctx.manual_effect = true;
        return choices.contains(ctx.choice);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || ctx.targets.size() != 1 || !ctx.targets.first()->isAlive()) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        ServerPlayer *target = ctx.targets.first();
        // The original payment choice must remain legal for the accepted target and material.
        if (ctx.choice == "enter") {
            if (!canEnter(ctx.initiator, target, card)) return false;
            LogMessage log;
            log.type = "$ZhijianEquip";
            log.from = target;
            log.card_str = QString::number(card->getEffectiveId());
            room->sendLog(log);
            room->moveCardTo(card, ctx.initiator, target, Player::PlaceEquip,
                CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.initiator->objectName(), objectName(), QString()));
        } else {
            if (ctx.choice != "throw" || !ctx.initiator->canDiscard(ctx.initiator, card->getEffectiveId())) return false;
            room->throwCard(card, CardMoveReason(CardMoveReason::S_REASON_THROW, ctx.initiator->objectName(), objectName(), QString()), ctx.initiator, nullptr);
        }
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.targets.size() != 1) return FinishSkill;
        ServerPlayer *target = ctx.targets.first();
        if (target->isAlive() && ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getHp() != target->getHp()) {
            const bool sourceLower = ctx.invoker->getHp() < target->getHp();
            // Both actual recipients get their own hook, in the printed recover/draw order.
            const QString payment = ctx.choice;
            if (sourceLower) {
                ctx.choice = "recover";
                skillEffect(ctx, ctx.invoker);
                ctx.choice = "draw";
                skillEffect(ctx, target);
            } else {
                ctx.choice = "draw";
                skillEffect(ctx, ctx.invoker);
                ctx.choice = "recover";
                skillEffect(ctx, target);
            }
            ctx.choice = payment;
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "recover")
            target->getRoom()->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)), true);
        else if (ctx.choice == "draw")
            target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};
class TenyearRendeVS : public ViewAsSkillV2
{
public:
    TenyearRendeVS() : ViewAsSkillV2("tenyearrende") { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? !request.initiator->isKongcheng()
            : request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                && request.pattern == "@@tenyearrende"
                && request.initiator->getMark("tenyearrende_id-PlayClear") > 0;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator && card
            && !card->hasFlag("using")
            && request.initiator->handCards().contains(card->getEffectiveId())
            && !request.selectedCardIds.contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return canActivate(request) && request.selectedCardIds.isEmpty();
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return ViewAsSkillV2::createCard(request);
        const Card *chosen = Sanguosha->getEngineCard(request.initiator->getMark("tenyearrende_id-PlayClear") - 1);
        if (!chosen || !chosen->isKindOf("BasicCard")) return nullptr;
        Card *card = Sanguosha->cloneCard(chosen->objectName());
        if (!card) return nullptr;
        card->setSkillName("_tenyearrende");
        if (request.initiator->isCardLimited(card, Card::MethodUse) || !card->isAvailable(request.initiator)) {
            card->deleteLater();
            return nullptr;
        }
        return card;
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return "TenyearRendeCard";
        if (!request.initiator) return QString();
        const int id = request.initiator->getMark("tenyearrende_id-PlayClear") - 1;
        const Card *card = id >= 0 ? Sanguosha->getEngineCard(id) : nullptr;
        return card ? card->getClassName() : QString();
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && selected.isEmpty() && target && target->isAlive() && target != request.initiator
            && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
                "recipients").toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        ctx.extra_data = request.selectedCardIds.size();
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        // The reward conversion pays through the ordinary card pipeline, not the gift action.
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return canActivate(request) && request.selectedCardIds.isEmpty();
        if (!ctx.owner || !ctx.invoker || ctx.targets.size() != 1 || !cardSelectionFeasible(request)) return false;
        ServerPlayer *target = ctx.targets.first();
        if (!target || !target->isAlive() || target == ctx.initiator
            || ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients")
                .toStringList().contains(target->objectName())) return false;
        // Gift payment belongs to the V2 source, never the generic proxy discard.
        DummyCard gift(request.selectedCardIds);
        room->obtainCard(target, &gift, CardMoveReason(CardMoveReason::S_REASON_GIVE,
            ctx.initiator->objectName(), target->objectName(), objectName(), QString()), false);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || ctx.targets.isEmpty()) return FinishSkill;
        Room *room = owner->getRoom();
        const int oldCount = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "given", 0).toInt();
        const int newCount = oldCount + ctx.extra_data.toInt();
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "given", newCount);
        QStringList recipients = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients").toStringList();
        recipients << ctx.targets.first()->objectName();
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients", recipients);
        // Retain the existing AI hints; per-source state decides legality and reward.
        room->setPlayerMark(owner, "tenyearrende-PlayClear", newCount);
        room->addPlayerMark(ctx.targets.first(), "tenyearrendetarget-PlayClear");
        if (oldCount < 2 && newCount >= 2 && owner->isAlive()) {
            const QList<int> choices = room->getAvailableCardList(owner, "basic", objectName());
            if (choices.isEmpty()) return ContinueEffects;
            room->fillAG(choices, owner);
            const int id = room->askForAG(owner, choices, true, objectName(), "@tenyearrende-basic");
            room->clearAG(owner);
            if (id < 0 || !choices.contains(id)) return ContinueEffects;
            // Scope the declaration to this prompt, including nested activations.
            const int previous = owner->getMark("tenyearrende_id-PlayClear");
            const auto restore = qScopeGuard([&] { room->setPlayerMark(owner, "tenyearrende_id-PlayClear", previous); });
            room->setPlayerMark(owner, "tenyearrende_id-PlayClear", id + 1);
            Room::BorrowedSkillScope borrowed(room, owner, objectName(), ctx.activationRef);
            room->askForUseCard(owner, "@@tenyearrende", "@tenyearrende:" + Sanguosha->getEngineCard(id)->objectName());
        }
        return ContinueEffects;
    }
};

class TenyearRende : public TriggerSkillV2
{
public:
    TenyearRende() : TriggerSkillV2("tenyearrende")
    {
        events << EventPhaseChanging;
        view_as_skill = new TenyearRendeVS;
    }
    void record(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || ctx.owner != player || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().from != Player::Play) return;
        player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "given");
        player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients");
        room->setPlayerMark(player, "tenyearrende-PlayClear", 0);
        room->setPlayerMark(player, "tenyearrende_id-PlayClear", 0);
    }
};

namespace {
bool hasShouyue(const Player *player)
{
    const Player *lord = player ? player->getLord() : nullptr;
    return player && player->getSeemingKingdom() == "shu"
        && lord && lord->hasLordSkill("heg_shouyue") && lord->hasShownGeneral1();
}
}

class TenyearWusheng : public ViewAsSkillV2 {
public:
    TenyearWusheng() : ViewAsSkillV2("tenyearwusheng", 1) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? Slash::IsAvailable(request.initiator)
            : (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !request.initiator
            || (!card->isRed() && !(Config.EnableHegemony && hasShouyue(request.initiator)))) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card);
        return slash.isAvailable(request.initiator);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        Slash *slash = new Slash(original->getSuit(), original->getNumber());
        slash->addSubcard(original);
        slash->setSkillName(objectName());
        slash->setShowSkill(objectName());
        return slash;
    }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->getKingdom() == "wei") index += 2;
        return index;
    }
};

class TenyearWushengMod : public TargetModSkillV2 {
public:
    TenyearWushengMod() : TargetModSkillV2("#tenyearwushengmod")
    { frequency = NotFrequent; setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The native collector validates the exact parent source and its revelation.
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->isKindOf("Slash") && ctx.card->getSuit() == Card::Diamond
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

TenyearYijueCard::TenyearYijueCard()
{
}

bool TenyearYijueCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void TenyearYijueCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isKongcheng()) return;
	Room *room = effect.from->getRoom();
	const Card *show_card = room->askForCardShow(effect.to, effect.from, "tenyearyijue");
	room->showCard(effect.to, show_card->getEffectiveId());

	if (show_card->isRed()) {
		room->obtainCard(effect.from, show_card, true);
		if (effect.to->isAlive() && effect.to->getLostHp() > 0 &&
				effect.from->askForSkillInvoke("tenyearyijue", QString("recover:%1").arg(effect.to->objectName()), false)) {
			room->recover(effect.to, RecoverStruct("tenyearyijue", effect.from));
		}
	} else if (show_card->isBlack()) {
		effect.to->addMark("tenyearyijue");
		room->setPlayerCardLimitation(effect.to, "use,response", ".|.|.|hand", true);
		room->addPlayerMark(effect.to, "@skill_invalidity");

		foreach(ServerPlayer *p, room->getAllPlayers())
			room->filterCards(p, p->getCards("he"), true);
		JsonArray args;
		args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
	}
}

class TenyearYijueVS : public OneCardViewAsSkill
{
public:
	TenyearYijueVS() : OneCardViewAsSkill("tenyearyijue")
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearYijueCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearYijueCard *card = new TenyearYijueCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class TenyearYijue : public TriggerSkill
{
public:
	TenyearYijue() : TriggerSkill("tenyearyijue")
	{
		events << EventPhaseChanging << Death << DamageCaused;
		view_as_skill = new TenyearYijueVS;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event != DamageCaused)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
	{
		if (triggerEvent == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash") || damage.card->getSuit() != Card::Heart) return false;
			if (damage.from->hasSkill(objectName()) && damage.to->getMark("tenyearyijue") > 0 && damage.from == room->getCurrent()) {
				LogMessage log;
				log.type = "#TenyearyijueBuff";
				log.from = damage.from;
				log.to << damage.to;
				log.arg = QString::number(damage.damage);
				log.arg2 = QString::number(damage.damage += damage.to->getMark("tenyearyijue"));
				room->sendLog(log);
				data = QVariant::fromValue(damage);
			}
		} else {
			if (triggerEvent == EventPhaseChanging) {
				PhaseChangeStruct change = data.value<PhaseChangeStruct>();
				if (change.to != Player::NotActive)
					return false;
			} else if (triggerEvent == Death) {
				DeathStruct death = data.value<DeathStruct>();
				if (death.who != target || target != room->getCurrent())
					return false;
			}
			QList<ServerPlayer *> players = room->getAllPlayers(true);
			foreach (ServerPlayer *player, players) {
				int mark = player->getMark("tenyearyijue");
				if (mark == 0) continue;
				player->removeMark("tenyearyijue", mark);
				room->removePlayerMark(player, "@skill_invalidity", mark);

				foreach(ServerPlayer *p, room->getAllPlayers())
					room->filterCards(p, p->getCards("he"), false);

				JsonArray args;
				args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
				room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

				room->removePlayerCardLimitation(player, "use,response", ".|.|.|hand$1");
			}
		}
		return false;
	}
};

class TenyearPaoxiao : public TargetModSkillV2
{
public:
    TenyearPaoxiao() : TargetModSkillV2("tenyearpaoxiao") { setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.currentAmount <= 0) return CorrectSkillResult::noEffect();
        if (ctx.modType == Residue) return CorrectSkillResult::unlimitedResidue();
        if (ctx.modType != DistanceLimit || ctx.primary->getPhase() != Player::Play)
            return CorrectSkillResult::noEffect();
        const ServerPlayer *actor = qobject_cast<const ServerPlayer *>(ctx.primary);
        bool alreadyUsed = false;
        if (actor) {
            Room *room = actor->getRoom();
            if (room->historyScopes().value("phase_id").toLongLong() == 0) return CorrectSkillResult::noEffect();
            // The native target-mod query scope excludes this use exactly once.
            alreadyUsed = room->countHistoryCards(actor, "phase", "Slash") > 0;
        } else {
            // Existing server-synchronized mark is a client presentation projection only.
            alreadyUsed = ctx.primary->getMark("tenyearpaoxiao-PlayClear") > 0;
        }
        return alreadyUsed ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class TenyearTishen : public TriggerSkillV2
{
public:
    TenyearTishen() : TriggerSkillV2("tenyeartishen")
    { events << CardFinished << EventPhaseChanging << EventPhaseEnd; }
    static void syncDisplay(Room *room, ServerPlayer *owner)
    {
        int count = 0;
        for (int id : owner->getSkillInstanceIds("tenyeartishen"))
            if (owner->getSkillInstanceStateValue("tenyeartishen", id, "enabled").toBool()) ++count;
        room->setPlayerMark(owner, "&tenyeartishen", count);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != EventPhaseChanging || player != ctx.owner || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().to != Player::RoundStart) return;
        ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "enabled");
        syncDisplay(room, ctx.owner);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseEnd)
            return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (event != CardFinished) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || !room->CardInPlace(use.card, Player::DiscardPile)) return {};
        const QVariantMap history = room->queryCardUseDamage();
        if (!history.value("complete").toBool() || !history.value("error").toString().isEmpty()) return {};
        QSet<QString> damaged;
        for (const QVariant &entry : history.value("items").toList())
            damaged.insert(entry.toMap().value("data").toMap().value("to").toString());
        TriggerList result;
        for (ServerPlayer *owner : use.to) {
            if (!owner->isAlive() || damaged.contains(owner->objectName()) || !owner->hasSkill(objectName())) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName()))
                if (owner->getSkillInstanceStateValue(objectName(), id, "enabled").toBool())
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return false;
        const TriggerList holders = triggerable(event, room, player, data);
        for (auto it = holders.cbegin(); it != holders.cend(); ++it) {
            for (const QString &fullName : it.value()) {
                QString name;
                const int id = SkillInstanceUtils::parseName(fullName, name);
                SkillContext ctx;
                ctx.skill_name = objectName(); ctx.owner = it.key(); ctx.invoker = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                bool ok = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &ok);
                if (!ok) ctx.amount = getBaseAmount();
                ctx.is_forced = true;
                ctx.original_data = &data; ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd) {
            if (!ctx.owner->askForSkillInvoke(this)) return false;
            QVariantList ids;
            for (const Card *card : ctx.owner->getCards("he"))
                if ((card->isKindOf("TrickCard") || card->isKindOf("OffensiveHorse") || card->isKindOf("DefensiveHorse"))
                    && !card->hasFlag("using") && ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) ids << card->getEffectiveId();
            ctx.extra_data = ids;
        } else {
            ctx.use_card = ctx.original_data->value<CardUseStruct>().card;
            ctx.is_forced = true;
        }
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseEnd) return true;
        DummyCard payment;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            const Card *card = Sanguosha->getCard(id);
            if (room->getCardOwner(id) != ctx.owner || (room->getCardPlace(id) != Player::PlaceHand
                && room->getCardPlace(id) != Player::PlaceEquip) || card->hasFlag("using")
                || !ctx.owner->canDiscard(ctx.owner, id)
                || (!card->isKindOf("TrickCard") && !card->isKindOf("OffensiveHorse") && !card->isKindOf("DefensiveHorse"))) return false;
            payment.addSubcard(id);
        }
        if (payment.subcardsLength() > 0) room->throwCard(&payment, ctx.owner, nullptr);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (event == EventPhaseEnd) {
            room->broadcastSkillInvoke(objectName());
            // This persistent mode belongs to the live copy and ends with that copy.
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "enabled", true);
            syncDisplay(room, ctx.owner);
        } else if (ctx.use_card && room->CardInPlace(ctx.use_card, Player::DiscardPile)) {
            room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
            room->obtainCard(target, ctx.use_card, true);
        }
        return false;
    }
};

class TenyearGuanxing : public TriggerSkillV2
{
public:
    TenyearGuanxing() : TriggerSkillV2("tenyearguanxing")
    {
        events << EventPhaseStart;
        frequency = Frequent;
        m_baseAmount = 5;
    }
    int getPriority(TriggerEvent) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        return player->getPhase() == Player::Start || player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getPhase() == Player::Finish) {
            const QString turn = room->historyScopes().value("turn_id").toString();
            if (turn.isEmpty() || turn == "0" || ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID,
                    "finish_turn").toString() != turn) return false;
        }
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (ctx.owner->isJieGeneral("jiangwei", "zhugeliang") && ctx.owner->isJieGeneral("jiangwei", "wolong")) index += 2;
        room->broadcastSkillInvoke(objectName(), index);
        const int count = qMax(0, getEffectiveAmount(ctx) - (room->alivePlayerCount() < 4 ? 2 : 0));
        if (count == 0) return false;
        const QList<int> cards = room->getNCards(count);
        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = target;
        log.card_str = ListI2S(cards).join("+");
        room->sendLog(log, target);
        const QList<int> top = room->askForGuanxing(target, cards);
        // Eligibility is this source's prior choice, not a shared owner mark.
        if (ctx.owner->getPhase() == Player::Start && top.isEmpty())
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "finish_turn",
                room->historyScopes().value("turn_id").toString());
        return false;
    }
};
class TenyearYajiao : public TriggerSkillV2
{
public:
    TenyearYajiao() : TriggerSkillV2("tenyearyajiao")
    { events << CardUsed << CardResponded; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->hasFlag("CurrentPlayer")) return {};
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        const bool hand = event == CardUsed ? data.value<CardUseStruct>().m_isHandcard : data.value<CardResponseStruct>().m_isHandcard;
        return card && hand ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        const Card *card = event == CardUsed ? ctx.original_data->value<CardUseStruct>().card
            : ctx.original_data->value<CardResponseStruct>().m_card;
        ctx.extra_data = card->getTypeId();
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int originalType = ctx.extra_data.toInt();
        const int amount = getEffectiveAmount(ctx);
        for (int i = 0; i < amount && ctx.owner->isAlive(); ++i) {
            const QList<int> ids = room->getNCards(1, false);
            if (ids.isEmpty()) break;
            const int id = ids.first();
            CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, ctx.owner->objectName(), objectName(), QString()));
            room->moveCardsAtomic(move, true);
            const Card *card = Sanguosha->getCard(id);
            const int revealedType = card->getTypeId();
            // AI presentation is temporary; the activation's card id remains in its own context.
            const int previous = ctx.owner->getMark("tenyearyajiao");
            const auto restore = qScopeGuard([&] { ctx.owner->setMark("tenyearyajiao", previous); });
            ctx.owner->setMark("tenyearyajiao", id);
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(),
                QString("@tenyearyajiao-give:::%1:%2\\%3").arg(card->objectName()).arg(card->getSuitString() + "_char").arg(card->getNumberString()));
            ctx.choice = "give";
            ctx.extra_data = id;
            if (target) skillEffect(event, room, ctx.owner, ctx, target);
            if (revealedType != originalType && ctx.owner->isAlive()) {
                ctx.choice = "discard";
                skillEffect(event, room, ctx.owner, ctx, ctx.owner);
            }
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "discard") {
            room->askForDiscard(target, objectName(), 1, 1, false, true);
        } else if (room->getCardPlace(ctx.extra_data.toInt()) == Player::PlaceTable) {
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), QString());
            room->obtainCard(target, Sanguosha->getCard(ctx.extra_data.toInt()), reason, true);
        }
        return false;
    }
};

class TenyearJizhi : public TriggerSkillV2
{
public:
    TenyearJizhi() : TriggerSkillV2("tenyearjizhi") { frequency = Frequent; events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<CardUseStruct>().card;
        return player && player->isAlive() && player->hasSkill(objectName()) && card && card->isKindOf("TrickCard")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getMark("JilveEvent") <= 0 && !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.owner->getMark("JilveEvent") > 0) room->broadcastSkillInvoke("jilve", 5);
        else room->broadcastSkillInvoke(objectName());
        const QList<int> drawn = target->drawCardsList(getEffectiveAmount(ctx), objectName());
        for (int id : drawn) {
            if (!target->isAlive()) break;
            const Card *card = Sanguosha->getCard(id);
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
                || !card->isKindOf("BasicCard") || !target->canDiscard(target, id)) continue;
            const QVariant previous = target->getTag("tenyearjizhi_id");
            const auto restore = qScopeGuard([&] {
                if (previous.isValid()) target->setTag("tenyearjizhi_id", previous);
                else target->removeTag("tenyearjizhi_id");
            });
            room->fillAG(QList<int>{id}, target);
            target->setTag("tenyearjizhi_id", id);
            const bool discard = room->askForSkillInvoke(target, "tenyearjizhi_discard", "discard", false);
            room->clearAG(target);
            // A nested prompt may move the drawn card before this optional payment.
            if (!discard || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
                || !target->canDiscard(target, id)) continue;
            room->throwCard(card, target, nullptr);
            room->addMaxCards(target, 1);
        }
        return false;
    }
};
class TenyearJianxiong : public TriggerSkillV2
{
public:
	TenyearJianxiong() : TriggerSkillV2("tenyearjianxiong")
	{
		frequency = Frequent;
		events << Damaged;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		return {{player, {objectName()}}};
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner->askForSkillInvoke(this)) return false;
		ctx.targets << ctx.owner;
		return true;
	}

	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		target->drawCards(getEffectiveAmount(ctx), objectName());

		const Card *card = ctx.original_data->value<DamageStruct>().card;
		if (target->isAlive() && card && room->CardInTable(card))
			target->obtainCard(card);
		return false;
	}
};

TenyearQingjianCard::TenyearQingjianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearQingjianCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	if (room->getCurrent())
		room->addPlayerMark(effect.from, "tenyearqingjian-Clear");
	QList<int> ids= getSubcards();
	LogMessage log;
	log.type = "$ShowCard";
	log.from = effect.from;
	log.card_str = ListI2S(ids).join("+");
	room->sendLog(log);
	room->fillAG(ids);
	room->getThread()->delay(1000);
	room->clearAG();
	if(effect.from != effect.to) {
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "tenyearqingjian", "");
		room->obtainCard(effect.to, this, reason, true);
	}
	QList<int> list;
	foreach (int id, ids) {
		const Card *card = Sanguosha->getCard(id);
		if (list.contains(card->getTypeId())) continue;
		list << card->getTypeId();
	}
	if (list.isEmpty() || !room->hasCurrent(true)) return;
	room->addMaxCards(room->getCurrent(), list.length());
}

class TenyearQingjianVS : public ViewAsSkill
{
public:
	TenyearQingjianVS() : ViewAsSkill("tenyearqingjian")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *) const
	{
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		TenyearQingjianCard *c = new TenyearQingjianCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearqingjian";
	}
};

class TenyearQingjian : public TriggerSkill
{
public:
	TenyearQingjian() : TriggerSkill("tenyearqingjian")
	{
		events << CardsMoveOneTime;
		view_as_skill = new TenyearQingjianVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!room->getTag("FirstRound").toBool() && player->getPhase() != Player::Draw && move.to == player && move.to_place == Player::PlaceHand) {
			if (player->isNude() || player->getMark("tenyearqingjian-Clear") > 0) return false;
			room->askForUseCard(player, "@@tenyearqingjian", "@tenyearqingjian");
		}
		return false;
	}
};

class TenyearLuoyi : public TriggerSkillV2
{
public:
    TenyearLuoyi() : TriggerSkillV2("tenyearluoyi")
    { events << EventPhaseStart << EventPhaseChanging; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::RoundStart) {
            // A lifetime generation detects a nested expiry before the reveal choice returns.
            player->setTag("tenyearluoyi_expiry", player->getTag("tenyearluoyi_expiry").toLongLong() + 1);
            player->removeTag("tenyearluoyi_receipts");
            room->setPlayerMark(player, "&tenyearluoyi", 0);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Draw
            && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Revealing is compulsory; the keep/replace-draw decision occurs after the reveal effect.
        ctx.extra_data = ctx.owner->getTag("tenyearluoyi_expiry"); ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const qint64 expiry = ctx.extra_data.toLongLong();
        const QList<int> ids = room->getNCards(3 * amount);
        QList<int> eligible;
        for (int id : ids) {
            const Card *card = Sanguosha->getCard(id);
            if (card->isKindOf("BasicCard") || card->isKindOf("Weapon") || card->isKindOf("Duel")) eligible << id;
        }
        const QVariant oldHint = target->getTag("tenyearluoyi_ids");
        const auto restore = qScopeGuard([&] { target->setTag("tenyearluoyi_ids", oldHint); });
        const auto cleanup = [&] {
            DummyCard leftovers;
            for (int id : ids) if (room->getCardPlace(id) == Player::PlaceTable) leftovers.addSubcard(id);
            if (leftovers.subcardsLength() > 0)
                room->throwCard(&leftovers, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                    target->objectName(), objectName(), QString()), nullptr);
        };
        bool accepted = false;
        try {
            room->sendCompulsoryTriggerLog(target, objectName(), true, true);
            room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), QString())), true);
            target->setTag("tenyearluoyi_ids", ListI2V(ids));
            accepted = target->isAlive() && target->askForSkillInvoke(this);
            if (accepted) {
                room->broadcastSkillInvoke(objectName());
                if (expiry == target->getTag("tenyearluoyi_expiry").toLongLong()) {
                    const qint64 serial = room->getTag("tenyearluoyi_serial").toLongLong() + 1;
                    room->setTag("tenyearluoyi_serial", serial);
                    QVariantList receipts;
                    for (const QVariant &value : target->getTag("tenyearluoyi_receipts").toList()) {
                        const QVariantMap previous = value.toMap();
                        // Renew this copy's applied buff; repeated Draw phases do not stack the same copy.
                        if (previous.value("activation_owner").toString() != ctx.activationRef.ownerObjectName
                            || previous.value("activation_instance").toInt() != ctx.activationRef.key.instanceID) receipts << value;
                    }
                    receipts << QVariantMap{{"serial", serial}, {"amount", amount}, {"holder", target->objectName()}, {"actor", ctx.invoker->objectName()},
                        {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                        {"source_instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                        {"activation_instance", ctx.activationRef.key.instanceID}};
                    target->setTag("tenyearluoyi_receipts", receipts);
                    room->setPlayerMark(target, "&tenyearluoyi", receipts.size());
                }
                DummyCard rejected;
                for (int id : ids)
                    if (!eligible.contains(id) && room->getCardPlace(id) == Player::PlaceTable) rejected.addSubcard(id);
                if (rejected.subcardsLength() > 0)
                    room->throwCard(&rejected, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                        target->objectName(), objectName(), QString()), nullptr);
                DummyCard obtain;
                for (int id : eligible) if (room->getCardPlace(id) == Player::PlaceTable) obtain.addSubcard(id);
                if (target->isAlive() && obtain.subcardsLength() > 0) room->obtainCard(target, &obtain);
            }
        } catch (TriggerEvent) { cleanup(); throw; }
        cleanup();
        return accepted;
    }
};

class TenyearLuoyiBuff : public TriggerSkillV2
{
public:
    TenyearLuoyiBuff() : TriggerSkillV2("#tenyearluoyibuff") { events << DamageCaused; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.from != player || !damage.to || !damage.to->isAlive()
            || !damage.card || (!damage.card->isKindOf("Slash") && !damage.card->isKindOf("Duel"))) return true;
        for (const QVariant &value : player->getTag("tenyearluoyi_receipts").toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("activation_owner").toString(), true);
            if (!ctx.owner) continue;
            ctx.instanceID = receipt.value("activation_instance").toInt(); ctx.invoker = player;
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = receipt;
            ctx.amount = receipt.value("amount").toInt(); ctx.targets << damage.to; ctx.is_forced = true;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("holder").toString(), true);
        return holder && holder->isAlive() && ctx.original_data
            && holder->getTag("tenyearluoyi_receipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!isSourceAvailable(room, ctx) || getEffectiveAmount(ctx) <= 0) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || damage.from->objectName() != ctx.extra_data.toMap().value("holder").toString() || damage.to != target || !damage.card
            || (!damage.card->isKindOf("Slash") && !damage.card->isKindOf("Duel"))) return false;
        LogMessage log; log.type = "#LuoyiBuff"; log.from = ctx.initiator ? ctx.initiator : ctx.invoker;
        log.to << target; log.arg = QString::number(damage.damage);
        damage.damage += getEffectiveAmount(ctx); log.arg2 = QString::number(damage.damage);
        room->sendLog(log); *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class TenyearYiji : public TriggerSkillV2
{
public:
    TenyearYiji() : TriggerSkillV2("tenyearyiji")
    { events << Damaged; frequency = Frequent; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int count = data.value<DamageStruct>().damage;
        return player && player->isAlive() && player->hasSkill(objectName()) && count > 0
            ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        SkillContext draw = ctx;
        draw.choice = "draw";
        draw.extra_data = 0;
        skillEffect(event, room, ctx.owner, draw, ctx.owner);
        int remaining = draw.extra_data.toInt();
        if (remaining <= 0 || !ctx.owner->isAlive()) return false;
        QList<int> available;
        for (const Card *card : ctx.owner->getHandcards())
            if (!card->hasFlag("using")) available << card->getEffectiveId();
        QMap<ServerPlayer *, QList<int>> allocations;
        while (remaining > 0 && !available.isEmpty() && ctx.owner->isAlive()) {
            const QList<int> offered = available;
            const QList<ServerPlayer *> others = room->getOtherPlayers(ctx.owner);
            const CardsMoveStruct selected = room->askForYijiStruct(ctx.owner, available, objectName(),
                false, false, true, remaining, others, CardMoveReason(), "", false, false);
            ServerPlayer *recipient = qobject_cast<ServerPlayer *>(selected.to);
            if (!recipient || !others.contains(recipient) || !recipient->isAlive() || selected.card_ids.isEmpty()) break;
            QList<int> ids;
            for (int id : selected.card_ids) {
                if (ids.size() >= remaining) break;
                if (offered.contains(id) && !ids.contains(id) && room->getCardOwner(id) == ctx.owner
                    && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
            }
            if (ids.isEmpty()) break;
            allocations[recipient] << ids;
            remaining -= ids.size();
            for (int id : ids) available.removeAll(id);
        }
        QList<CardsMoveStruct> moves;
        for (ServerPlayer *recipient : room->getOtherPlayers(ctx.owner)) {
            if (!allocations.contains(recipient)) continue;
            SkillContext gift = ctx;
            gift.choice = "give";
            QVariantList ids;
            for (int id : allocations.value(recipient)) ids << id;
            gift.extra_data = QVariantMap{{"ids", ids}};
            skillEffect(event, room, ctx.owner, gift, recipient);
            if (!gift.extra_data.toMap().value("accepted").toBool()) continue;
            QList<int> accepted;
            for (const QVariant &value : gift.extra_data.toMap().value("ids").toList()) accepted << value.toInt();
            moves << CardsMoveStruct(accepted, ctx.owner, recipient, Player::PlaceHand, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), recipient->objectName(), objectName(), ""));
        }
        // All recipient hooks finish before the original simultaneous give is committed.
        for (int i = moves.size() - 1; i >= 0; --i) {
            CardsMoveStruct &move = moves[i];
            if (!ctx.owner->isAlive() || !move.to->isAlive()) { moves.removeAt(i); continue; }
            for (int j = move.card_ids.size() - 1; j >= 0; --j) {
                const int id = move.card_ids.at(j);
                if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
                    || Sanguosha->getCard(id)->hasFlag("using")) move.card_ids.removeAt(j);
            }
            if (move.card_ids.isEmpty()) moves.removeAt(i);
        }
        if (!moves.isEmpty()) room->moveCardsAtomic(moves, false);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            const int amount = getEffectiveAmount(ctx);
            ctx.extra_data = amount;
            if (amount > 0) target->drawCards(amount, objectName());
        } else {
            QVariantMap receipt = ctx.extra_data.toMap();
            QVariantList ids = receipt.value("ids").toList();
            while (ids.size() > getEffectiveAmount(ctx)) ids.removeLast();
            receipt["ids"] = ids;
            receipt["accepted"] = !ids.isEmpty();
            ctx.extra_data = receipt;
        }
        return false;
    }
};

class TenyearLuoshen : public TriggerSkillV2
{
public:
    TenyearLuoshen() : TriggerSkillV2("tenyearluoshen")
    { events << EventPhaseStart << FinishJudge << EventPhaseProceeding << CardsMoveOneTime << EventPhaseChanging; frequency = Frequent; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariant turn = room->historyScopes().value("turn_id");
            for (ServerPlayer *owner : room->getAllPlayers(true)) {
                QVariantList kept;
                for (const QVariant &value : owner->getTag("tenyearluoshen_applied").toList())
                    if (value.toMap().value("turn") != turn) kept << value;
                owner->setTag("tenyearluoshen_applied", kept);
            }
        } else if (event == CardsMoveOneTime && player) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to != player || move.to_place != Player::PlaceHand || move.reason.m_skillName != objectName()) return true;
            for (const QVariant &value : player->getTag("tenyearluoshen_pending").toList()) {
                const QVariantMap receipt = value.toMap();
                if (!receipt.contains("card") || !move.card_ids.contains(receipt.value("card").toInt())) continue;
                QVariantList applied = player->getTag("tenyearluoshen_applied").toList();
                if (applied.contains(value)) continue;
                // Only a committed gain creates the later ignore-hand receipt.
                applied << value; player->setTag("tenyearluoshen_applied", applied);
                room->setCardTip(receipt.value("card").toInt(), "tenyearluoshen-Clear");
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != FinishJudge && event != EventPhaseProceeding) return false;
        if (!player || !player->isAlive()) return true;
        QVariantList receipts;
        if (event == FinishJudge) {
            const JudgeStruct *judge = data.value<JudgeStruct *>();
            if (!judge || judge->who != player || judge->reason != objectName() || !judge->card || !judge->card->isBlack()) return true;
            receipts = player->getTag("tenyearluoshen_pending").toList();
            if (receipts.isEmpty()) return true;
            receipts = QVariantList{receipts.last()};
        } else {
            if (player->getPhase() != Player::Discard) return true;
            // The accepted exemption belongs to the physical card even after a transfer.
            for (ServerPlayer *storage : room->getAllPlayers(true))
                receipts << storage->getTag("tenyearluoshen_applied").toList();
        }
        for (const QVariant &value : receipts) {
            const QVariantMap receipt = value.toMap();
            if (event == EventPhaseProceeding && (receipt.value("turn") != room->historyScopes().value("turn_id")
                || !player->handCards().contains(receipt.value("card").toInt()))) continue;
            SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("activation_owner").toString(), true);
            if (!ctx.owner) continue;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.invoker = player;
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = receipt;
            ctx.choice = event == FinishJudge ? "obtain" : "ignore"; ctx.amount = 1; ctx.is_forced = true; ctx.targets << player;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("holder").toString(), true);
        if (!holder || (ctx.choice == "obtain" && !holder->isAlive())) return false;
        const QString key = ctx.choice == "obtain" ? "tenyearluoshen_pending" : "tenyearluoshen_applied";
        return holder->getTag(key).toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "obtain" || ctx.choice == "ignore") return true;
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.choice = "judge"; ctx.targets << ctx.owner; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.choice == "ignore") {
            const int id = ctx.extra_data.toMap().value("card").toInt();
            if (isSourceAvailable(room, ctx) && target->handCards().contains(id)) room->ignoreCards(target, id);
            return false;
        }
        if (ctx.choice == "obtain") {
            if (!isSourceAvailable(room, ctx) || !ctx.original_data) return false;
            const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge || judge->who != target || !judge->card || !judge->card->isBlack()) return false;
            const int id = judge->card->getEffectiveId();
            if (room->getCardPlace(id) != Player::PlaceJudge) return false;
            QVariantMap receipt = ctx.extra_data.toMap(); receipt["card"] = id;
            QVariantList pending = target->getTag("tenyearluoshen_pending").toList();
            for (QVariant &value : pending)
                if (value.toMap().value("serial") == receipt.value("serial")) value = receipt;
            target->setTag("tenyearluoshen_pending", pending);
            room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_GOTCARD,
                target->objectName(), objectName(), QString()), true);
            return false;
        }
        if (ctx.choice == "judge_once") {
            const qint64 serial = room->getTag("tenyearluoshen_serial").toLongLong() + 1;
            room->setTag("tenyearluoshen_serial", serial);
            QVariantList pending = target->getTag("tenyearluoshen_pending").toList();
            pending << QVariantMap{{"serial", serial}, {"turn", room->historyScopes().value("turn_id")},
                {"holder", target->objectName()}, {"actor", ctx.invoker->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
                {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_instance", ctx.activationRef.key.instanceID}};
            target->setTag("tenyearluoshen_pending", pending);
            const auto cleanup = qScopeGuard([&] {
                QVariantList kept;
                for (const QVariant &value : target->getTag("tenyearluoshen_pending").toList())
                    if (value.toMap().value("serial").toLongLong() != serial) kept << value;
                target->setTag("tenyearluoshen_pending", kept);
            });
            JudgeStruct judge; judge.pattern = ".|black"; judge.good = true; judge.reason = objectName(); judge.who = target;
            room->judge(judge); ctx.extra_data = judge.isGood();
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        do {
            for (int n = 0; n < amount && target->isAlive(); ++n) {
                SkillContext judgment = ctx; judgment.choice = "judge_once"; judgment.extra_data = false;
                skillEffect(event, room, player, judgment, target);
                if (!judgment.extra_data.toBool()) return false;
            }
        } while (target->isAlive() && target->askForSkillInvoke(this));
        return false;
    }
};

TenyearTuxiCard::TenyearTuxiCard()
{
}

bool TenyearTuxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.length() >= Self->getMark("tenyeartuxi") || to_select == Self)
		return false;

	return !to_select->isKongcheng();
}

void TenyearTuxiCard::onEffect(CardEffectStruct &effect) const
{
	effect.from->addMark("tenyeartuxiNum");
	if (effect.to->isKongcheng()||effect.from->isDead()) return;
    Room *room = effect.to->getRoom();
    int card_id = room->askForCardChosen(effect.from, effect.to, "h", "tenyeartuxi");
	CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION,effect.from->objectName(),effect.to->objectName(),"tenyeartuxi","");
    room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, false);
}

class TenyearTuxi : public TriggerSkillV2 {
public:
    TenyearTuxi() : TriggerSkillV2("tenyeartuxi") { events << DrawNCards; }
    int getPriority(TriggerEvent) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Draw || draw.reason != "draw_phase" || draw.num < 1) return {};
        for (ServerPlayer *target : room->getOtherPlayers(player))
            if (player->canGet(target, "h")) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.owner))
            if (ctx.owner->canGet(p, "h")) candidates << p;
        int n = ctx.original_data->value<DrawStruct>().num;
        room->setPlayerMark(ctx.owner, "tenyeartuxi", n);
        ctx.targets = room->askForPlayersChosen(ctx.owner, candidates, objectName(), 0, n,
                                               "@tuxi-card:::" + QString::number(n), true);
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        room->setPlayerMark(ctx.owner, "tenyeartuxiNum", 0);
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        // Each chosen target replaces a draw even if a nested effect removes its hand.
        draw.num = qMax(0, draw.num - int(ctx.targets.size()));
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        if (ctx.owner->isAlive() && target && ctx.owner->canGet(target, "h")) {
            // Preserve the sole canonical extraction effect and its move reason.
            CardEffectStruct effect;
            effect.from = ctx.owner;
            effect.to = target;
            TenyearTuxiCard card;
            card.onEffect(effect);
        }
        return false;
    }
};

TenyearQingnangCard::TenyearQingnangCard()
{
	setSkillName("tenyearqingnang");
}

bool TenyearQingnangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->isWounded() && to_select->getMark("tenyearqingnang_target-PlayClear") <= 0;
}

void TenyearQingnangCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->recover(effect.to, RecoverStruct("tenyearqingnang", effect.from));
	room->setPlayerMark(effect.to, "tenyearqingnang_target-PlayClear", 1);
	const Card *card = Sanguosha->getCard(getSubcards().first());
	if (card->isRed()) return;
	room->setPlayerMark(effect.from, "tenyearqingnang-PlayClear", 1);
}

class TenyearQingnangVS : public ViewAsSkillV2
{
public:
    TenyearQingnangVS() : ViewAsSkillV2("tenyearqingnang", 1) {}
    bool commitQuota(SkillContext &ctx) const
    {
        const QString key = objectName() + "_quota";
        if (ctx.interceptor_data.value(key).value("committed").toBool()) return true;
        if (!isUsable(ctx)) return false;
        QVariantMap receipt{{"committed", true}};
        ctx.interceptor_data.insert(key, receipt);
        addUsage(ctx);
        return true;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearQingnangCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h")
            && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "closed").toBool();
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "closed").toBool()) return false;
        const QStringList recipients = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients").toStringList();
        for (ServerPlayer *target : ctx.targets)
            if (!target || recipients.contains(target->objectName())) return false;
        return true;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.use_card || ctx.use_card->getSubcards().size() != 1) return;
        QStringList recipients = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients").toStringList();
        for (ServerPlayer *target : ctx.targets)
            if (target && !recipients.contains(target->objectName())) recipients << target->objectName();
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients", recipients);
        const bool closed = ctx.extra_data.isValid() ? ctx.extra_data.toBool()
            : !Sanguosha->getCard(ctx.use_card->getSubcards().first())->isRed();
        if (closed)
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "closed", true);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId())
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && target->isWounded() && selected.isEmpty()
            && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
                "recipients").toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!isUsable(ctx) || !cardSelectionFeasible(request) || ctx.targets.size() != 1
            || !ctx.targets.first()->isAlive() || !ctx.targets.first()->isWounded()) return false;
        const bool closes = !Sanguosha->getCard(request.selectedCardIds.first())->isRed();
        ctx.extra_data = closes;
        // Reserve the per-source target before discard triggers can re-enter this activation.
        const QVariantMap previous = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
        addUsage(ctx);
        if (ViewAsSkillV2::pay(room, ctx, request)) {
            ctx.interceptor_data.insert(objectName() + "_quota", QVariantMap{{"committed", true}});
            return true;
        }
        ctx.owner->setSkillInstanceState(objectName(), ctx.instanceID, previous);
        return false;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->getRoom()->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class TenyearQingnang : public TriggerSkillV2
{
public:
    TenyearQingnang() : TriggerSkillV2("tenyearqingnang")
    { events << EventPhaseChanging << EventSkillInvoking; global = true; view_as_skill = new TenyearQingnangVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        SkillContext active = data.value<SkillContext>();
        if (active.bypass_cost && active.owner && active.activationRef.isValid()
            && active.activationRef.key.skillName == objectName()
            && active.activationRef.ownerObjectName == active.owner->objectName()
            && active.activationRef.key.instanceID == active.instanceID) {
            if (!static_cast<const TenyearQingnangVS *>(view_as_skill)->commitQuota(active)) active.is_canceled = true;
            data = QVariant::fromValue(active);
        }
        return true;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != EventPhaseChanging || !player || player != ctx.owner || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().from != Player::Play) return;
        // Only this root instance's custom quota expires with its Play phase.
        ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "closed");
        ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients");
    }
};
class TenyearLiyu : public TriggerSkillV2
{
public:
    TenyearLiyu() : TriggerSkillV2("tenyearliyu") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to || !damage.to->isAlive()
            || damage.to == player || damage.to->hasFlag("Global_DebutFlag") || !player->canGet(damage.to, "hej")
            || !damage.card || !damage.card->isKindOf("Slash")) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker->isAlive() || !ctx.invoker->canGet(target, "hej")) return false;
        room->broadcastSkillInvoke(objectName());
        const int id = room->askForCardChosen(ctx.invoker, target, "hej", objectName(), false, Card::MethodGet);
        if (room->getCardOwner(id) != target || !ctx.invoker->canGet(target, id)) return false;
        const Player::Place place = room->getCardPlace(id);
        if (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick) return false;
        const Card *card = Sanguosha->getCard(id);
        const bool equipment = card->isKindOf("EquipCard");
        room->obtainCard(ctx.invoker, card, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, ctx.invoker->objectName()), true);
        if (!target->isAlive()) return false;
        if (!equipment) {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        std::unique_ptr<Duel> duel(new Duel(Card::NoSuit, 0));
        duel->setSkillName("_tenyearliyu");
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker))
            if (other != target && ctx.invoker->canUse(duel.get(), other)) candidates << other;
        if (candidates.isEmpty() || !ctx.invoker->isAlive()) return false;
        ServerPlayer *chosen = room->askForPlayerChosen(target, candidates, objectName(), "@tenyearliyu:" + ctx.invoker->objectName());
        if (chosen && chosen->isAlive() && ctx.invoker->isAlive() && ctx.invoker->canUse(duel.get(), chosen)) {
            // Ownership follows the ordinary use pipeline through nested resolution.
            CardUseStruct use(duel.get(), ctx.invoker, chosen);
            use.setOwnedCard(duel.release());
            room->useCardFromSkillEffect(use, ctx);
        }
        return false;
    }
};
class TenyearBiyue : public TriggerSkillV2
{
public:
	TenyearBiyue() : TriggerSkillV2("tenyearbiyue")
	{
		frequency = Frequent;
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
		return {{player, {objectName()}}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
		ctx.targets << ctx.owner;
		return true;
	}

	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		target->drawCards(getEffectiveAmount(ctx) + (target->isKongcheng() ? 1 : 0), objectName());
		return false;
	}
};

class TenyearYaowu : public TriggerSkillV2
{
public:
    TenyearYaowu() : TriggerSkillV2("tenyearyaowu") { events << DamageInflicted; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !damage.card || !damage.card->isKindOf("Slash")) return {};
        if (damage.card->isRed() && (!damage.from || !damage.from->isAlive())) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ctx.choice = damage.card->isRed() ? "red" : "other";
        ctx.targets << (ctx.choice == "red" ? damage.from : ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const bool red = ctx.choice == "red";
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true, red ? 2 : 1);
        QStringList choices{"draw"};
        if (red && target->isWounded()) choices << "recover";
        if (!red || room->askForChoice(target, objectName(), choices.join("+")) == "draw")
            target->drawCards(getEffectiveAmount(ctx), objectName());
        else
            room->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
        return false;
    }
};
class TenyearLiegong : public TriggerSkillV2
{
public:
    TenyearLiegong() : TriggerSkillV2("tenyearliegong")
    { events << TargetSpecified; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !use.card || !use.card->isKindOf("Slash") || use.from != player || use.to.isEmpty()) return {};
        // Each exact copy gets an independent invocation for every Slash recipient.
        return {{player, {objectName() + "*" + QString::number(use.to.length())}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (ctx.trigger_count >= use.to.length()) return false;
        ServerPlayer *target = use.to.at(ctx.trigger_count);
        if (!target || !target->isAlive()) return false;
        const bool noJink = target->getHandcardNum() <= ctx.owner->getHandcardNum();
        const bool damage = target->getHp() >= ctx.owner->getHp();
        if ((!noJink && !damage) || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.extra_data = QVariantMap{{"no_jink", noJink}, {"damage", damage}};
        ctx.targets << target;
        ctx.use_card = use.card;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card != ctx.use_card || !use.to.contains(target)) return false;
        ctx.owner->peiyin(this);
        const QVariantMap flags = ctx.extra_data.toMap();
        if (flags.value("no_jink").toBool()) {
            LogMessage log;
            log.type = "#NoJink";
            log.from = target;
            room->sendLog(log);
            if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        }
        if (flags.value("damage").toBool()) {
            // Applied damage belongs to this card, not the continuing lifetime of its grant.
            QVariantList receipts = use.card->getTag("tenyearliegong_receipts").toList();
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_instance", ctx.activationRef.key.instanceID}, {"actor", ctx.owner->objectName()},
                {"target", target->objectName()}, {"amount", getEffectiveAmount(ctx)}};
            use.card->setTag("tenyearliegong_receipts", receipts);
        }
        return false;
    }
};

class TenyearLiegongDamage : public TriggerSkillV2
{
public:
    TenyearLiegongDamage() : TriggerSkillV2("#tenyearliegong-damage")
    { events << DamageCaused << CardFinished; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardFinished) return false;
        const Card *card = data.value<CardUseStruct>().card;
        if (card) card->removeTag("tenyearliegong_receipts");
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to || !damage.to->isAlive()) return true;
        for (const QVariant &value : damage.card->getTag("tenyearliegong_receipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() != damage.to->objectName()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("activation_owner").toString(), true);
            if (!ctx.owner) continue;
            ctx.invoker = player;
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.instanceID = receipt.value("activation_instance").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.targets << damage.to;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.card && damage.to && damage.to->isAlive()
            && damage.to->objectName() == ctx.extra_data.toMap().value("target").toString()
            && damage.card->getTag("tenyearliegong_receipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Target interception may re-enter damage resolution; never transfer a receipt to another victim.
        if (ctx.initiator && isSourceAvailable(room, ctx)
            && ctx.original_data->value<DamageStruct>().to == target)
            ctx.initiator->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        return false;
    }
};

class TenyearLiegongMod : public TargetModSkillV2
{
public:
	TenyearLiegongMod() : TargetModSkillV2("#tenyearliegongmod")
	{
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.card)
			return CorrectSkillResult::noEffect();
		// Native related-source validation covers both Liegong parents independently.
		return CorrectSkillResult::useAmount(qMax(0, ctx.card->getNumber() - ctx.primary->getAttackRange()) * ctx.currentAmount);
	}
};

class TenyearKuanggu : public TriggerSkillV2 {
public:
    TenyearKuanggu() : TriggerSkillV2("tenyearkuanggu")
    {
        events << DamageDone << Damage << DamageComplete;
        frequency = Frequent;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if ((event != DamageDone && event != DamageComplete) || !ctx.original_data) return;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || !damage.to || ctx.owner != damage.from) return;
        QVariantList ranges = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "damage_ranges").toList();
        // Each damage frame and skill instance keeps its own distance snapshot;
        // nested damage must not overwrite the outer frame's recovery eligibility.
        if (event == DamageDone) {
            const int distance = damage.from->distanceTo(damage.to);
            ranges << QVariant(distance >= 0 && distance <= 1);
        } else if (!ranges.isEmpty()) {
            ranges.removeLast();
        }
        if (ranges.isEmpty()) ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "damage_ranges");
        else ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "damage_ranges", ranges);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const int count = data.value<DamageStruct>().damage;
        if (count <= 0) return {};
        QStringList choices;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const QVariantList ranges = player->getSkillInstanceStateValue(objectName(), id, "damage_ranges").toList();
            if (!ranges.isEmpty() && ranges.last().toBool())
                choices << SkillInstanceUtils::formatName(objectName(), id) + "*" + QString::number(count);
        }
        return choices.isEmpty() ? TriggerList() : TriggerList{{player, choices}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false;
        QStringList choices;
        if (ctx.owner->isWounded()) choices << "recover";
        choices << "draw";
        if (choices.size() > 1 && ctx.owner->getMark("zhongaoUptenyearkuanggu") > 0
            && ctx.owner->canDiscard(ctx.owner, "he")) choices << "beishui";
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"),
            ctx.original_data ? *ctx.original_data : QVariant());
        if (!choices.contains(ctx.choice)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Beishui's discard is a payment, not an unconditional prelude to the reward.
        return ctx.choice != "beishui" || (ctx.owner->canDiscard(ctx.owner, "he")
            && room->askForDiscard(ctx.owner, objectName(), 1, 1, false, true));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        int index = qsanRandomBounded(2) + 1;
        if (ctx.owner->getGeneralName().startsWith("ol_") || ctx.owner->getGeneral2Name().startsWith("ol_")) index += 2;
        room->broadcastSkillInvoke(objectName(), index);
        if (ctx.choice == "beishui") room->addSlashCishu(target, getEffectiveAmount(ctx));
        if (ctx.choice != "draw") room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        if (ctx.choice != "recover" && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

TenyearQimouCard::TenyearQimouCard()
{
	setSkillName("tenyearqimou");
	target_fixed = true;
}

void TenyearQimouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@tenyearqimouMark");
	room->doSuperLightbox(source, "tenyearqimou");
	QStringList choices;
	for (int i = 1; i <= source->getHp(); i++)
		choices << QString::number(i);
	QString choice = room->askForChoice(source, "tenyearqimou", choices.join("+"));
	int n = choice.toInt();
	room->loseHp(HpLostStruct(source, n, "tenyearqimou", source));
	if (source->isAlive()) {
		room->addDistance(source, -n);
		room->addSlashCishu(source, n);
	}
}

class TenyearQimou : public ViewAsSkillV2
{
public:
    TargetMode targetMode() const override { return NoTarget; }
    TenyearQimou() : ViewAsSkillV2("tenyearqimou")
    { frequency = Limited; limit_mark = "@tenyearqimouMark"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearQimouCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getHp() > 0; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        QStringList choices;
        for (int n = 1; n <= ctx.initiator->getHp(); ++n) choices << QString::number(n);
        if (choices.isEmpty()) return false;
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
        return choices.contains(ctx.choice);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        const int count = ctx.choice.toInt();
        if (count <= 0 || count > ctx.initiator->getHp()) return false;
        room->removePlayerMark(ctx.owner, limit_mark);
        room->doSuperLightbox(ctx.owner, objectName());
        room->loseHp(HpLostStruct(ctx.initiator, count, objectName(), ctx.invoker));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        // The native temporary corrections are applied receipts and survive removing this skill.
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = ctx.choice.toInt() * getEffectiveAmount(ctx);
        if (count <= 0) return ContinueEffects;
        target->getRoom()->addDistance(target, -count);
        target->getRoom()->addSlashCishu(target, count);
        return ContinueEffects;
    }
};
TenyearShensuCard::TenyearShensuCard()
{
}

bool TenyearShensuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearshensu");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void TenyearShensuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if(user_string.contains("1")){
		source->skip(Player::Judge, true);
		source->skip(Player::Draw, true);
	}else if(user_string.contains("2")){
		source->skip(Player::Play, true);
	}else{
		source->skip(Player::Discard, true);
		source->turnOver();
	}
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearshensu");
	room->useCard(CardUseStruct(slash, source, targets));
	slash->deleteLater();
}

class TenyearShensuViewAsSkill : public ViewAsSkill
{
public:
	TenyearShensuViewAsSkill() : ViewAsSkill("tenyearshensu")
	{
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@tenyearshensu");
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if (pattern.endsWith("1") || pattern.endsWith("3"))
			return false;
		return selected.isEmpty() && to_select->isKindOf("EquipCard") && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if (pattern.endsWith("1") || pattern.endsWith("3")) {
		} else {
			if (cards.length() != 1)
				return nullptr;
		}
		TenyearShensuCard *card = new TenyearShensuCard;
		card->setUserString(pattern);
		card->addSubcards(cards);
		return card;
	}
};

class TenyearShensu : public TriggerSkill
{
public:
	TenyearShensu() : TriggerSkill("tenyearshensu")
	{
		events << EventPhaseChanging;
		view_as_skill = new TenyearShensuViewAsSkill;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *xiahouyuan, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to == Player::Judge && !xiahouyuan->isSkipped(Player::Judge) && !xiahouyuan->isSkipped(Player::Draw)) {
            room->askForUseCard(xiahouyuan, "@@tenyearshensu1", "@shensu1", 1);
		} else if (change.to == Player::Play && !xiahouyuan->isSkipped(Player::Play)) {
            room->askForUseCard(xiahouyuan, "@@tenyearshensu2", "@shensu2", 2, Card::MethodDiscard);
		} else if (change.to == Player::Discard && !xiahouyuan->isSkipped(Player::Discard)) {
            room->askForUseCard(xiahouyuan, "@@tenyearshensu3", "@tenyearshensu3", 3);
		}
		return false;
	}
};

class TenyearJushou : public TriggerSkillV2
{
public:
    TenyearJushou() : TriggerSkillV2("tenyearjushou") { events << EventPhaseStart; m_baseAmount = 4; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->turnOver();
        if (!target->isAlive()) return false;
        target->drawCards(getEffectiveAmount(ctx), objectName());
        if (!target->isAlive() || target->isKongcheng()) return false;
        const auto legalCards = [target]() {
            QList<int> ids;
            for (const Card *card : target->getHandcards()) {
                if (card->hasFlag("using")) continue;
                if (card->isKindOf("EquipCard") ? card->isAvailable(target) && !target->isCardLimited(card, Card::MethodUse)
                    : target->canDiscard(target, card->getEffectiveId())) ids << card->getEffectiveId();
            }
            return ids;
        };
        QList<int> ids = legalCards();
        if (ids.isEmpty()) {
            LogMessage log;
            log.type = "#TenyearjushouShow";
            log.from = target;
            room->sendLog(log);
            room->showAllCards(target, static_cast<ServerPlayer *>(nullptr));
            return false;
        }
        const Card *card = room->askForCard(target, ListI2S(ids).join(",") + "!", "@tenyearjushou", QVariant(), Card::MethodNone);
        // Re-read actual material after the prompt, including the mandatory fallback.
        ids = legalCards();
        if (ids.isEmpty() || !target->isAlive()) return false;
        if (!card || !ids.contains(card->getEffectiveId())) card = Sanguosha->getCard(ids.at(qsanRandomBounded(ids.size())));
        if (card->isKindOf("EquipCard")) room->useCard(CardUseStruct(card, target));
        else room->throwCard(card, CardMoveReason(CardMoveReason::S_REASON_THROW, target->objectName(), objectName(), QString()), target, nullptr);
        return false;
    }
};
static bool tenyearPayDiscard(Room *room, ServerPlayer *payer, const QString &skillName, const QList<int> &ids,
                              const CardMoveReason *reason);

class TenyearJieweiVS : public ViewAsSkillV2
{
public:
    TenyearJieweiVS() : ViewAsSkillV2("tenyearjiewei", 1) { response_pattern = "nullification"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "nullification"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->getEquips().isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && ViewAsSkillV2::canSelectCard(request, card) && !card->hasFlag("using")
            && request.initiator->getEquips().contains(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request; selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Nullification"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = new Nullification(material->getSuit(), material->getNumber());
        card->addSubcards(request.selectedCardIds); card->setSkillName(objectName());
        return card;
    }
};

class TenyearJiewei : public TriggerSkillV2
{
public:
    TenyearJiewei() : TriggerSkillV2("tenyearjiewei") { events << TurnedOver; view_as_skill = new TenyearJieweiVS; }
    static bool canTransfer(Room *room, ServerPlayer *actor, ServerPlayer *from, ServerPlayer *to, const Card *card)
    {
        if (!actor || !actor->isAlive() || !from || !to || !from->isAlive() || !to->isAlive() || from == to
            || !card || card->hasFlag("using")) return false;
        const int id = card->getEffectiveId();
        if (id < 0 || room->getCardOwner(id) != from || !from->canMove(from, id) || !actor->canMove(from, id)
            || actor->isProhibited(to, card)) return false;
        if (room->getCardPlace(id) == Player::PlaceEquip) {
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (!equip) return false;
            for (int slot : equip->getOccupyLocations())
                if (!to->hasEquipArea(slot) || to->getEquip(slot)) return false;
            return true;
        }
        return room->getCardPlace(id) == Player::PlaceDelayedTrick && to->hasJudgeArea() && !to->containsTrick(card->objectName());
    }
    static QList<int> movable(Room *room, ServerPlayer *actor, ServerPlayer *from)
    {
        QList<int> ids;
        for (const Card *card : from->getCards("ej"))
            for (ServerPlayer *to : room->getAlivePlayers())
                if (canTransfer(room, actor, from, to, card)) { ids << card->getEffectiveId(); break; }
        return ids;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->faceUp()
            && player->canDiscard(player, "he") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList legal;
        for (const Card *card : ctx.owner->getCards("he"))
            if (!card->hasFlag("using") && ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) legal << QString::number(card->getEffectiveId());
        if (legal.isEmpty()) return false;
        const Card *selected = room->askForExchange(ctx.owner, objectName(), 1, 1, true, "@tenyearjiewei", true, legal.join(","));
        if (!selected || selected->subcardsLength() != 1 || !legal.contains(QString::number(selected->getSubcards().first()))) return false;
        ctx.extra_data = selected->getSubcards().first(); ctx.manual_effect = true;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return tenyearPayDiscard(room, ctx.owner, objectName(), {ctx.extra_data.toInt()}, nullptr); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int repeats = getEffectiveAmount(ctx);
        for (int i = 0; i < repeats && ctx.invoker && ctx.invoker->isAlive(); ++i) {
            QList<ServerPlayer *> sources;
            for (ServerPlayer *from : room->getAlivePlayers()) if (!movable(room, ctx.invoker, from).isEmpty()) sources << from;
            if (sources.isEmpty()) break;
            ServerPlayer *from = room->askForPlayerChosen(ctx.invoker, sources, objectName() + "_from", "@movefield-from");
            if (!from) break;
            ctx.choice = "from";
            skillEffect(event, room, ctx.owner, ctx, from);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !ctx.invoker || !ctx.invoker->isAlive()) return false;
        if (ctx.choice == "to") {
            const QVariantMap chosen = ctx.extra_data.toMap();
            ServerPlayer *from = room->findPlayerByObjectName(chosen.value("from").toString());
            const int id = chosen.value("id", -1).toInt();
            if (id < 0) return false;
            const Card *card = Sanguosha->getCard(id);
            if (!card || room->getCardPlace(card->getEffectiveId()) != chosen.value("place").toInt()
                || !canTransfer(room, ctx.invoker, from, target, card)) return false;
            room->moveCardTo(card, from, target, room->getCardPlace(card->getEffectiveId()),
                CardMoveReason(CardMoveReason::S_REASON_TRANSFER, ctx.invoker->objectName(), objectName(), QString()), true);
            return false;
        }
        const QList<int> legal = movable(room, ctx.invoker, target);
        if (legal.isEmpty()) return false;
        QList<int> disabled;
        for (const Card *card : target->getCards("ej")) if (!legal.contains(card->getEffectiveId())) disabled << card->getEffectiveId();
        const int id = room->askForCardChosen(ctx.invoker, target, "ej", objectName(), false, Card::MethodNone, disabled);
        if (!legal.contains(id)) return false;
        const Card *card = Sanguosha->getCard(id);
        const int place = room->getCardPlace(id);
        QList<ServerPlayer *> destinations;
        for (ServerPlayer *to : room->getAlivePlayers()) if (canTransfer(room, ctx.invoker, target, to, card)) destinations << to;
        if (destinations.isEmpty()) return false;
        ServerPlayer *to = room->askForPlayerChosen(ctx.invoker, destinations, objectName() + "_to", "@movefield-to:" + card->objectName());
        if (!to) return false;
        // The giver and recipient each receive a target hook, with live transfer legality at commit.
        SkillContext receive = ctx; receive.choice = "to";
        receive.extra_data = QVariantMap{{"from", target->objectName()}, {"id", id}, {"place", place}};
        skillEffect(event, room, ctx.owner, receive, to);
        return false;
    }
};

TenyearTianxiangCard::TenyearTianxiangCard(QString this_skill_name) : this_skill_name(this_skill_name)
{
	handling_method = Card::MethodDiscard;
}

void TenyearTianxiangCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	Room *room = effect.from->getRoom();
	if (room->askForChoice(effect.from, this_skill_name, "damage+losehp") == "damage") {
		room->damage(DamageStruct(this_skill_name, nullptr, effect.to));
		if (effect.to->isDead()) return;
		int n = qMin(5, effect.to->getLostHp());
		if (n <= 0) return;
		effect.to->drawCards(n, this_skill_name);
	} else {
		room->loseHp(HpLostStruct(effect.to, 1, "tenyeartianxiang", effect.from));
		if (effect.to->isDead()) return;
		room->obtainCard(effect.to, this, true);
	}
}

class TenyearTianxiangViewAsSkill : public ViewAsSkillV2
{
public:
    TenyearTianxiangViewAsSkill() : ViewAsSkillV2("tenyeartianxiang", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@tenyeartianxiang"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && ViewAsSkillV2::canSelectCard(request, card)
            && !card->hasFlag("using") && !request.initiator->isJilei(card)
            && Sanguosha->matchExpPattern(".|heart|.|hand", request.initiator, card)
            && request.initiator->handCards().contains(card->getEffectiveId())
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearTianxiangCard"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Share the identity variant's card, AI protocol and both canonical effects.
        auto *card = new TenyearTianxiangCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }
};

// Only an accepted response enters V2; the dispatcher must not activate twice.
class TenyearTianxiang : public TriggerSkill
{
public:
	TenyearTianxiang() : TriggerSkill("tenyeartianxiang")
	{
		events << DamageInflicted;
		view_as_skill = new TenyearTianxiangViewAsSkill;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *xiaoqiao, QVariant &) const
	{
		if (xiaoqiao->canDiscard(xiaoqiao, "h")) {
			return room->askForUseCard(xiaoqiao, "@@tenyeartianxiang", "@tenyeartianxiang", -1, Card::MethodDiscard);
		}
		return false;
	}
};

TenyearSanyaoCard::TenyearSanyaoCard()
{
    setSkillName("tenyearsanyao");
}

bool TenyearSanyaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	int max = -1000;
	foreach (const Player *p, Self->getAliveSiblings()) {
		if (max < p->getHp())
			max = p->getHp();
	}
	return to_select->getHp() == max && targets.length() < getSubcards().length() && to_select != Self;
}

bool TenyearSanyaoCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == getSubcards().length();
}

void TenyearSanyaoCard::onEffect(CardEffectStruct &effect) const
{
	effect.from->getRoom()->damage(DamageStruct("tenyearsanyao", effect.from, effect.to));
}

class TenyearSanyao : public ViewAsSkillV2
{
public:
    TenyearSanyao() : ViewAsSkillV2("tenyearsanyao", 999) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearSanyaoCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    QList<const Player *> candidates(const Player *source) const
    {
        QList<const Player *> result;
        if (!source) return result;
        int highest = -1000;
        foreach (const Player *other, source->getAliveSiblings()) {
            if (other->getHp() > highest) {
                highest = other->getHp();
                result.clear();
            }
            if (other->getHp() == highest) result << other;
        }
        return result;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using")) return false;
        const int id = card->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && request.selectedCardIds.size() < candidates(request.initiator).size()
            && (request.initiator->handCards().contains(id) || request.initiator->hasEquip(card))
            && request.initiator->canDiscard(request.initiator, id);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && !selected.contains(target) && selected.size() < request.selectedCardIds.size()
            && candidates(request.initiator).contains(target);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty() && selected.size() == request.selectedCardIds.size();
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        // Each chosen recipient resolves independently after the shared material payment.
        target->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class TenyearZhiman : public TriggerSkillV2
{
public:
    TenyearZhiman() : TriggerSkillV2("tenyearzhiman") { events << DamageCaused; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.to
            && damage.to != player && damage.to->isAlive() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<DamageStruct>().to;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        LogMessage log;
        log.type = "#Yishi";
        log.from = ctx.invoker;
        log.arg = objectName();
        log.to << target;
        room->sendLog(log);
        for (int n = 0; n < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && ctx.invoker->canGet(target, "hej"); ++n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "hej", objectName(), false, Card::MethodGet);
            if (room->getCardOwner(id) != target || !ctx.invoker->canGet(target, id)) break;
            const Player::Place place = room->getCardPlace(id);
            if (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick) break;
            room->obtainCard(ctx.invoker, Sanguosha->getCard(id),
                CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, ctx.invoker->objectName()), place != Player::PlaceHand);
        }
        return true;
    }
};
class TenyearZhenjun : public TriggerSkillV2
{
public:
    explicit TenyearZhenjun(const QString &name = "tenyearzhenjun", bool second = false)
        : TriggerSkillV2(name), secondVersion(second) { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || (player->getPhase() != Player::Start && !(secondVersion && player->getPhase() == Player::Finish))) return {};
        for (ServerPlayer *target : room->getAlivePlayers())
            if (player->canDiscard(target, "he")) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (ctx.invoker->canDiscard(target, "he")) candidates << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@tenyearzhenjun-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker->canDiscard(target, "he")) return false;
        room->broadcastSkillInvoke(objectName());
        const int count = qMin(target->getCardCount(), qMax(target->getHandcardNum() - target->getHp(), getEffectiveAmount(ctx)));
        QList<int> cards;
        int equipment = 0;
        for (int n = 0; n < count; ++n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard, cards);
            if (id < 0 || cards.contains(id) || room->getCardOwner(id) != target || !ctx.invoker->canDiscard(target, id)) break;
            const Player::Place place = room->getCardPlace(id);
            if (place != Player::PlaceHand && place != Player::PlaceEquip) break;
            cards << id;
        }
        // Revalidate all selected cards after the final prompt before one atomic discard.
        for (int i = cards.size() - 1; i >= 0; --i) {
            const int id = cards.at(i);
            if (room->getCardOwner(id) != target || !ctx.invoker->canDiscard(target, id)
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip))
                cards.removeAt(i);
            else if (Sanguosha->getCard(id)->isKindOf("EquipCard")) ++equipment;
        }
        if (cards.isEmpty()) return false;
        DummyCard discard(cards);
        room->throwCard(&discard, target, ctx.invoker);
        if (!target->isAlive()) return false;
        if (secondVersion) {
            if (equipment > 0) return false;
            const QString prompt = QString("@secondtenyearzhenjun-discard:%1::%2").arg(target->objectName()).arg(cards.size());
            if (!ctx.invoker->isAlive() || !ctx.invoker->canDiscard(ctx.invoker, "he")
                || !room->askForDiscard(ctx.invoker, objectName(), 1, 1, true, true, prompt))
                target->drawCards(cards.size(), objectName());
        } else {
            const int payment = cards.size() - equipment;
            if (payment <= 0) return false;
            int available = 0;
            for (const Card *card : ctx.invoker->getCards("he"))
                if (ctx.invoker->canDiscard(ctx.invoker, card->getEffectiveId())) ++available;
            if (!ctx.invoker->isAlive() || available < payment
                || !room->askForDiscard(ctx.invoker, objectName(), payment, payment, true, true,
                    "tenyearzhenjun-discard:" + QString::number(payment)))
                target->drawCards(payment, objectName());
        }
        return false;
    }
private:
    bool secondVersion;
};

class SecondTenyearZhenjun : public TenyearZhenjun
{
public:
    SecondTenyearZhenjun() : TenyearZhenjun("secondtenyearzhenjun", true) {}
};
class TenyearJingce : public TriggerSkillV2
{
public:
    TenyearJingce() : TriggerSkillV2("tenyearjingce")
    { frequency = Frequent; events << EventPhaseStart; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
        if (room->historyScopes().value("turn_id").toULongLong() == 0) return {};
        const int count = room->countHistoryCards(player, "turn");
        // Missing history is unknown even when HP is zero or negative.
        return count >= 0 && count >= player->getHp() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class SecondTenyearJingce : public TriggerSkillV2
{
public:
    SecondTenyearJingce() : TriggerSkillV2("secondtenyearjingce")
    { frequency = Frequent; events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
        if (room->historyScopes().value("turn_id").toULongLong() == 0) return {};
        const int count = room->countHistoryCards(player, "turn");
        return count >= 0 && count >= player->getHp() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantMap history = room->queryCardHistory(ctx.owner, "turn");
        if (!history.value("complete").toBool() || !history.value("error").toString().isEmpty()) return false;
        QSet<int> suits;
        for (const QVariant &value : history.value("items").toList()) {
            const QVariantMap card = value.toMap();
            if (!card.contains("suit")) return false;
            const int suit = card.value("suit").toInt();
            suits.insert(suit == Card::NoSuitRed || suit == Card::NoSuitBlack ? Card::NoSuit : suit);
        }
        if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.choice = suits.size() >= ctx.owner->getHp() ? "both"
            : room->askForChoice(ctx.owner, objectName(), "draw+play");
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        if (ctx.choice == "both" || ctx.choice == "draw") target->insertPhase(Player::Draw);
        if (ctx.choice == "both" || ctx.choice == "play") target->insertPhase(Player::Play);
        return false;
    }
};
class TenyearDangxian : public PhaseChangeSkill
{
public:
	TenyearDangxian() : PhaseChangeSkill("tenyeardangxian")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() == Player::RoundStart) {
			//room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			LogMessage log;
			log.type = "#TenyeardangxianPlayPhase";
			log.from = player;
			log.arg = "tenyeardangxian";
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			room->setPlayerFlag(player, "tenyeardangxian");
			player->insertPhase(Player::Play);/*
			room->broadcastProperty(player, "phase");
			RoomThread *thread = room->getThread();
			if (!thread->trigger(EventPhaseStart, room, player)) {
				if (player->hasFlag("tenyeardangxian"))
					room->setPlayerFlag(player, "-tenyeardangxian");
				thread->trigger(EventPhaseProceeding, room, player);
			}
			if (player->hasFlag("tenyeardangxian"))
				room->setPlayerFlag(player, "-tenyeardangxian");
			thread->trigger(EventPhaseEnd, room, player);

			player->setPhase(Player::RoundStart);
			room->broadcastProperty(player, "phase");*/
		} else if (player->getPhase() == Player::Play) {
			if (!player->hasFlag("tenyeardangxian")) return false;
			room->setPlayerFlag(player, "-tenyeardangxian");
			if (player->getMark(objectName()) <= 0)
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			else {
				if (!player->askForSkillInvoke(objectName()))
					return false;
				room->broadcastSkillInvoke(objectName());
			}
			room->loseHp(HpLostStruct(player, 1, objectName(), player));
			if (player->isDead()) return  false;
			QList<int> slash;
			foreach (int id, room->getDiscardPile()) {
				const Card *card = Sanguosha->getCard(id);
				if (!card->isKindOf("Slash")) continue;
				slash << id;
			}
			if (slash.isEmpty()) return false;
			room->obtainCard(player, slash.at(qsanRandomBounded(slash.length())),true);
		}
		return false;
	}
};

class TenyearFuli : public TriggerSkill
{
public:
	TenyearFuli() : TriggerSkill("tenyearfuli")
	{
		events << AskForPeaches;
		frequency = Limited;
		limit_mark = "@tenyearfuliMark";
	}

	int getKingdoms(Room *room) const
	{
		QSet<QString> kingdom_set;
		foreach(ServerPlayer *p, room->getAlivePlayers())
			kingdom_set << p->getKingdom();
		return kingdom_set.size();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *liaohua, QVariant &data) const
	{
		DyingStruct dying_data = data.value<DyingStruct>();
		if (dying_data.who != liaohua || liaohua->getMark("@tenyearfuliMark") <= 0) return false;
		if (liaohua->askForSkillInvoke(this, data)) {
			room->broadcastSkillInvoke(objectName());

			room->doSuperLightbox(liaohua, "tenyearfuli");

			room->removePlayerMark(liaohua, "@tenyearfuliMark");
			int x = getKingdoms(room);
			int n = qMin(x - liaohua->getHp(), liaohua->getMaxHp() - liaohua->getHp());
			if (n > 0) room->recover(liaohua, RecoverStruct(liaohua, nullptr, n, objectName()));
			if (liaohua->getHandcardNum() < x) liaohua->drawCards(x - liaohua->getHandcardNum(), objectName());
			room->addPlayerMark(liaohua, "tenyeardangxian");
			room->changeTranslation(liaohua, "tenyeardangxian", 2);
			if (x >= 3) liaohua->turnOver();
		}
		return false;
	}
};

TenyearChunlaoCard::TenyearChunlaoCard(QString tenyearchunlao) : tenyearchunlao(tenyearchunlao)
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearChunlaoCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->addToPile("wine", this);
}

TenyearChunlaoWineCard::TenyearChunlaoWineCard(QString tenyearchunlao) : tenyearchunlao(tenyearchunlao)
{
	m_skillName = tenyearchunlao;
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearChunlaoWineCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	ServerPlayer *who = room->getCurrentDyingPlayer();
	if (!who) return;

	if (subcards.length() != 0) {
		room->throwCard(subcards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), tenyearchunlao, ""), nullptr);
		Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
		analeptic->setSkillName("_" + tenyearchunlao);
		room->useCard(CardUseStruct(analeptic, who, who, false));
		analeptic->deleteLater();

		const Card *card = Sanguosha->getCard(getSubcards().first());
		if (card->getClassName() == "FireSlash")
			room->recover(source, RecoverStruct(tenyearchunlao, source));
		else if (card->getClassName() == "ThunderSlash")
			source->drawCards(2, tenyearchunlao);
	}
}

class TenyearChunlaoViewAsSkill : public ViewAsSkill
{
public:
	TenyearChunlaoViewAsSkill(const QString &tenyearchunlao) : ViewAsSkill(tenyearchunlao), tenyearchunlao(tenyearchunlao)
	{
		expand_pile = "wine";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern == "@@" + tenyearchunlao
			|| (pattern.contains("peach") && !player->getPile("wine").isEmpty());
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@" + tenyearchunlao)
			return to_select->isKindOf("Slash");
		else {
			ExpPattern pattern(".|.|.|wine");
			if (!pattern.match(Self, to_select)) return false;
			return selected.length() == 0;
		}
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@" + tenyearchunlao) {
			if (cards.length() == 0) return nullptr;

			Card *acard = nullptr;
			if (tenyearchunlao == "tenyearchunlao")
				acard = new TenyearChunlaoCard;
			else if (tenyearchunlao == "secondtenyearchunlao")
				acard = new SecondTenyearChunlaoCard;
			if (!acard) return nullptr;
			acard->addSubcards(cards);
			acard->setSkillName(tenyearchunlao);
			return acard;
		} else {
			if (cards.length() != 1) return nullptr;
			Card *wine = nullptr;
			if (tenyearchunlao == "tenyearchunlao")
				wine = new TenyearChunlaoWineCard;
			else if (tenyearchunlao == "secondtenyearchunlao")
				wine = new SecondTenyearChunlaoWineCard;
			if (!wine) return nullptr;
			wine->addSubcards(cards);
			wine->setSkillName(tenyearchunlao);
			return wine;
		}
	}

private:
	QString tenyearchunlao;
};

class TenyearChunlao : public PhaseChangeSkill
{
public:
	TenyearChunlao() : PhaseChangeSkill("tenyearchunlao")
	{
		view_as_skill = new TenyearChunlaoViewAsSkill("tenyearchunlao");
	}

	bool onPhaseChange(ServerPlayer *chengpu, Room *room) const
	{
		if (chengpu->getPhase() == Player::Finish && !chengpu->isKongcheng() && chengpu->getPile("wine").isEmpty())
			room->askForUseCard(chengpu, "@@tenyearchunlao", "@tenyearchunlao", -1, Card::MethodNone);
		return false;
	}
};

class SecondTenyearLihuoViewAsSkill : public OneCardViewAsSkill
{
public:
	SecondTenyearLihuoViewAsSkill() : OneCardViewAsSkill("secondtenyearlihuo")
	{
		filter_pattern = "%slash";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& (pattern.contains("slash") || pattern.contains("Slash"));
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card *acard = new FireSlash(originalCard->getSuit(), originalCard->getNumber());
		acard->addSubcard(originalCard->getId());
		acard->setSkillName(objectName());
		return acard;
	}
};

class SecondTenyearLihuo : public TriggerSkill
{
public:
	SecondTenyearLihuo() : TriggerSkill("secondtenyearlihuo")
	{
		events << CardFinished << ChangeSlash;
		view_as_skill = new SecondTenyearLihuoViewAsSkill;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == ChangeSlash) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->objectName() != "slash") return false;
			bool has_changed = false;
			QString skill_name = use.card->getSkillName();
			if (!skill_name.isEmpty()) {
				const Skill *skill = Sanguosha->getSkill(skill_name);
				if (skill && !skill->inherits("FilterSkill") && !skill_name.contains("guhuo"))
					has_changed = true;
			}
			if (!has_changed || (use.card->isVirtualCard() && use.card->subcardsLength() == 0)) {
				FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
				fire_slash->setSkillName("secondtenyearlihuo");
				fire_slash->addSubcard(use.card);
				bool can_use = true;
				foreach (ServerPlayer *p, use.to) {
					if (!player->canSlash(p, fire_slash, false)) {
						can_use = false;
						break;
					}
				}
				if (can_use && room->askForSkillInvoke(player, "secondtenyearlihuo", data, false)) {
					use.changeCard(fire_slash);
					data = QVariant::fromValue(use);
				}
				fire_slash->deleteLater();
			}
		} else if (triggerEvent == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasFlag("DamageDone")&&use.card->isKindOf("Slash")&&use.card->getSkillNames().contains(objectName())) {
				room->sendCompulsoryTriggerLog(player, objectName());
				room->loseHp(HpLostStruct(player, 1, objectName(), player));
			}

			if (player->isDead() || !use.card->isKindOf("Slash")) return false;
			if (!use.card->hasFlag("first_card_in_one_turn")) return false;
			if (!room->CardInPlace(use.card, Player::DiscardPile) || !player->askForSkillInvoke(this, "put")) return false;
			room->broadcastSkillInvoke(objectName());
			player->addToPile("wine", use.card);
		}
		return false;
	}
};

class SecondTenyearLihuoTargetMod : public TargetModSkill
{
public:
	SecondTenyearLihuoTargetMod() : TargetModSkill("#secondtenyearlihuo-target")
	{
		frequency = NotFrequent;
	}

	int getExtraTargetNum(const Player *from, const Card *card) const
	{
		if (card->isKindOf("FireSlash")&&from->hasSkills("secondtenyearlihuo|ollihuo"))
			return 1;
		return 0;
	}
};

SecondTenyearChunlaoCard::SecondTenyearChunlaoCard() : TenyearChunlaoCard("secondtenyearchunlao")
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

SecondTenyearChunlaoWineCard::SecondTenyearChunlaoWineCard() : TenyearChunlaoWineCard("secondtenyearchunlao")
{
	m_skillName = "secondtenyearchunlao";
	mute = true;
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

class SecondTenyearChunlao : public TriggerSkill
{
public:
	SecondTenyearChunlao() : TriggerSkill("secondtenyearchunlao")
	{
		events << EventPhaseEnd;
		view_as_skill = new TenyearChunlaoViewAsSkill("secondtenyearchunlao");
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		{
			if (player->getPhase() == Player::Play && !player->isKongcheng() && player->getPile("wine").isEmpty())
				room->askForUseCard(player, "@@secondtenyearchunlao", "@secondtenyearchunlao", -1, Card::MethodNone);
			return false;
		}
	}
};

TenyearJiangchiCard::TenyearJiangchiCard()
{
	target_fixed = true;
	mute = true;
}

void TenyearJiangchiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (getSubcards().isEmpty()) {
		room->broadcastSkillInvoke("tenyearjiangchi", 1);
		source->drawCards(1, "tenyearjiangchi");
		room->setPlayerCardLimitation(source, "use,response", "Slash", true);
	} else if (getSubcards().length() == 1) {
		room->broadcastSkillInvoke("tenyearjiangchi", 2);
		room->addSlashJuli(source, 1000);
		room->addSlashCishu(source, 1);
	}
}

class TenyearJiangchiVS : public ViewAsSkill
{
public:
	TenyearJiangchiVS() : ViewAsSkill("tenyearjiangchi")
	{
	}
	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearjiangchi";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return !Self->isJilei(to_select) && selected.isEmpty();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 0 && cards.length() != 1) return nullptr;
		Card *acard = new TenyearJiangchiCard;
		if (!cards.isEmpty())
			acard->addSubcards(cards);
		return acard;
	}
};

class TenyearJiangchi : public TriggerSkill
{
public:
	TenyearJiangchi() : TriggerSkill("tenyearjiangchi")
	{
		events << EventPhaseEnd;
		view_as_skill = new TenyearJiangchiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *caozhang, QVariant &) const
	{
		if (caozhang->getPhase() != Player::Draw) return false;
		room->askForUseCard(caozhang, "@@tenyearjiangchi", "@tenyearjiangchi");
		return false;
	}
};

// Validate a trigger's material cost against committed moves, including canceled/redirected moves.
static bool tenyearPayDiscard(Room *room, ServerPlayer *payer, const QString &skillName, const QList<int> &ids,
                              const CardMoveReason *reason = nullptr)
{
    if (ids.isEmpty()) return true;
    const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
    const qint64 directParent = room->currentHistoryEventId();
    const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
    if (!skillEvent || !before.value("complete").toBool()) return false;
    QMap<int, int> places;
    DummyCard payment;
    for (int id : ids) {
        if (id < 0 || places.contains(id) || room->getCardOwner(id) != payer
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || Sanguosha->getCard(id)->hasFlag("using") || !payer->canDiscard(payer, id)) return false;
        places.insert(id, int(room->getCardPlace(id))); payment.addSubcard(id);
    }
    if (reason) room->throwCard(&payment, *reason, payer, nullptr);
    else room->throwCard(&payment, skillName, payer);
    QVariantMap filter{{"from", payer->objectName()}, {"after", before.value("watermark")}};
    QSet<int> paid;
    for (;;) {
        const QVariantMap page = room->queryHistoryMoves(filter);
        if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return false;
        for (const QVariant &value : page.value("items").toList()) {
            const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
            const int id = move.value("card_id", -1).toInt();
            if (!places.contains(id) || move.value("from_place").toInt() != places.value(id)
                || move.value("to_place").toInt() != Player::DiscardPile
                || move.value("reason_skill").toString() != skillName
                || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
                || room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() != directParent
                || room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() != skillEvent)
                continue;
            paid.insert(id);
        }
        if (!page.value("has_more").toBool()) return paid.size() == places.size();
        filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
    }
}

class SecondTenyearJiangchi : public TriggerSkillV2
{
public:
    SecondTenyearJiangchi() : TriggerSkillV2("secondtenyearjiangchi") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        if (!phase.toLongLong() || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        QStringList legal;
        for (const Card *card : ctx.owner->getCards("he"))
            if (!card->hasFlag("using") && ctx.owner->canDiscard(ctx.owner, card->getEffectiveId()))
                legal << QString::number(card->getEffectiveId());
        ctx.choice = room->askForChoice(ctx.owner, objectName(), legal.isEmpty() ? "two+one" : "two+one+discard");
        QVariantMap receipt{{"phase", phase}};
        if (ctx.choice == "discard") {
            const Card *card = room->askForExchange(ctx.owner, objectName(), 1, 1, true, "", false, legal.join(","));
            if (!card || card->getSubcards().size() != 1 || !legal.contains(QString::number(card->getSubcards().first())))
                return false;
            receipt["card"] = card->getSubcards().first();
        } else if (ctx.choice != "two" && ctx.choice != "one") return false;
        ctx.extra_data = receipt;
        ctx.targets = {ctx.owner};
        return ctx.owner->isAlive() && room->historyScopes().value("phase_id") == phase;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "discard") return true;
        // Generic Trigger contexts freeze the holder as owner; initiator is an Active-only field.
        ServerPlayer *payer = ctx.owner;
        const int id = ctx.extra_data.toMap().value("card", -1).toInt();
        if (!payer || id < 0 || room->getCardOwner(id) != payer
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || Sanguosha->getCard(id)->hasFlag("using") || !payer->canDiscard(payer, id)) return false;
        return tenyearPayDiscard(room, payer, objectName(), {id});
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariant phase = ctx.extra_data.toMap().value("phase");
        const QVariant turn = room->historyScopes().value("turn_id");
        room->broadcastSkillInvoke(objectName(), ctx.choice == "discard" ? 2 : 1);
        if (ctx.choice != "discard") target->drawCards((ctx.choice == "two" ? 2 : 1) * amount, objectName());
        // Draw/payment callbacks may finish the originating phase; never resurrect its applied effect.
        if (!target->isAlive() || !phase.toLongLong() || room->historyScopes().value("phase_id") != phase) return false;
        if (ctx.choice == "two") {
            const qlonglong serial = room->getTag("secondtenyearjiangchi_serial").toLongLong() + 1;
            room->setTag("secondtenyearjiangchi_serial", serial);
            const QString reason = objectName() + ":" + QString::number(serial);
            QVariantList receipts = target->getTag("secondtenyearjiangchi_applied").toList();
            receipts << QVariantMap{{"phase", phase}, {"turn", turn}, {"reason", reason}};
            target->setTag("secondtenyearjiangchi_applied", receipts);
            room->setPlayerCardLimitation(target, "use,response", "Slash", false, reason);
        } else if (ctx.choice == "discard") {
            const QString helper = "#secondtenyearjiangchi-target";
            const int grant = room->acquireSkillFromEffect(target, helper, ctx, [&](int id) {
                // Commit the expiry receipt before acquire notifications can re-enter phase/death cleanup.
                QVariantList receipts = target->getTag("secondtenyearjiangchi_applied").toList();
                receipts << QVariantMap{{"phase", phase}, {"turn", turn}, {"grant", id}};
                target->setTag("secondtenyearjiangchi_applied", receipts);
            }, true, false, false);
            if (grant > 0 && target->hasSkillInstance(helper, grant))
                room->setSkillInstanceCorrectState(target,
                    SkillInstanceRef(target->objectName(), SkillInstanceKey(helper, grant)), "amount", amount);
        }
        return false;
    }
};

class SecondTenyearJiangchiClear : public TriggerSkillV2
{
public:
    SecondTenyearJiangchiClear() : TriggerSkillV2("#secondtenyearjiangchi-clear")
    { events << EventPhaseChanging << TurnBroken << Death; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *dead = event == Death ? data.value<DeathStruct>().who : nullptr;
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if ((event == Death && (!dead || player != dead))
            || (event == EventPhaseChanging && change.from != Player::Play && change.to != Player::NotActive)) return true;
        const QVariant turn = room->historyScopes().value("turn_id");
        const bool turnEnd = event == TurnBroken || (event == EventPhaseChanging && change.to == Player::NotActive);
        for (ServerPlayer *recipient : room->getAllPlayers(true)) {
            if (dead && recipient != dead) continue;
            QVariantList kept, expired;
            for (const QVariant &value : recipient->getTag("secondtenyearjiangchi_applied").toList()) {
                const QVariantMap receipt = value.toMap();
                const QVariantMap oldPhase = room->historyEvent(receipt.value("phase").toLongLong());
                // Changing already owns the new phase scope. Resolve the saved, finished Play event instead.
                const bool leftPlay = event == EventPhaseChanging && player && change.from == Player::Play
                    && oldPhase.value("status").toString() == "finished"
                    && oldPhase.value("data").toMap().value("player").toString() == player->objectName();
                if (dead || (receipt.value("turn") == turn && (turnEnd || leftPlay))) expired << value;
                else kept << value;
            }
            // Remove receipts first so nested cleanup cannot expire or detach the same grant twice.
            recipient->setTag("secondtenyearjiangchi_applied", kept);
            for (const QVariant &value : expired) {
                const QVariantMap receipt = value.toMap();
                if (receipt.contains("reason")) room->removePlayerCardLimitationByReason(recipient, receipt.value("reason").toString());
                if (receipt.contains("grant")) room->detachSkillFromPlayer(recipient,
                    SkillInstanceUtils::formatName("#secondtenyearjiangchi-target", receipt.value("grant").toInt()), false, true, false);
            }
        }
        return true;
    }
};

class SecondTenyearJiangchiMod : public TargetModSkillV2
{
public:
    SecondTenyearJiangchiMod() : TargetModSkillV2("#secondtenyearjiangchi-target") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int amount = ctx.getStateValue("amount").toInt();
        if (amount <= 0 || ctx.currentAmount <= 0) return CorrectSkillResult::noEffect();
        if (ctx.modType == Residue) return CorrectSkillResult::useAmount(amount * ctx.currentAmount);
        if (ctx.modType == DistanceLimit) return CorrectSkillResult::useAmount(999);
        return CorrectSkillResult::noEffect();
    }
};

TenyearWurongCard::TenyearWurongCard()
{
    setSkillName("tenyearwurong");
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TenyearWurongCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.length() > 0 || to_select == Self)
		return false;
	return !to_select->isKongcheng();
}

void TenyearWurongCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();

	int index = qsanRandomBounded(2) + 1;
	if (effect.from->isJieGeneral("second_tenyear_zhangyi"))
		index += 2;
	room->broadcastSkillInvoke("tenyearwurong", index);

	const Card *c = room->askForExchange(effect.to, "tenyearwurong", 1, 1, false, "@tenyearwurong-show");

	room->showCard(effect.from, subcards.first());
	room->showCard(effect.to, c->getSubcards().first());

	const Card *card1 = Sanguosha->getCard(subcards.first());
	const Card *card2 = Sanguosha->getCard(c->getSubcards().first());

	if (card1->isKindOf("Slash") && !card2->isKindOf("Jink")) {
		room->damage(DamageStruct(objectName(), effect.from, effect.to));
	} else if (!card1->isKindOf("Slash") && card2->isKindOf("Jink")) {
		if (!effect.to->isNude()) {
			int id = room->askForCardChosen(effect.from, effect.to, "he", objectName());
			room->obtainCard(effect.from, id, false);
		}
	}
}

class TenyearWurong : public ViewAsSkillV2
{
public:
    TenyearWurong() : ViewAsSkillV2("tenyearwurong", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearWurongCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && selected.isEmpty() && target && target->isAlive()
            && target != request.initiator && !target->isKongcheng();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "obtain") {
            const QVariantMap selected = ctx.extra_data.toMap();
            const int id = selected.value("id").toInt();
            ServerPlayer *other = room->getCardOwner(id);
            if (other && other->objectName() == selected.value("owner").toString()
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && target->canGetCard(other, id)) room->obtainCard(target, id, false);
            return ContinueEffects;
        }
        if (!ctx.initiator || !ctx.use_card || ctx.use_card->getSubcards().size() != 1 || target->isKongcheng()) return ContinueEffects;
        const int ownId = ctx.use_card->getSubcards().first();
        if (!ctx.initiator->handCards().contains(ownId)) return ContinueEffects;
        const Card *shown = room->askForExchange(target, objectName(), 1, 1, false, "@tenyearwurong-show");
        if (!shown || shown->getSubcards().size() != 1) return ContinueEffects;
        const int otherId = shown->getSubcards().first();
        if (!target->handCards().contains(otherId) || !ctx.initiator->handCards().contains(ownId)) return ContinueEffects;
        const bool slash = Sanguosha->getCard(ownId)->isKindOf("Slash");
        const bool jink = Sanguosha->getCard(otherId)->isKindOf("Jink");
        int audio = qsanRandomBounded(2) + 1;
        if (ctx.invoker->isJieGeneral("second_tenyear_zhangyi")) audio += 2;
        room->broadcastSkillInvoke(objectName(), audio);
        room->showCard(ctx.initiator, ownId);
        room->showCard(target, otherId);
        if (slash && !jink) {
            room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        } else if (!slash && jink) {
            const int amount = getEffectiveAmount(ctx);
            for (int n = 0; n < amount && ctx.invoker->isAlive() && target->isAlive()
                 && ctx.invoker->canGetCard(target, "he"); ++n) {
                const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodGet);
                if (id < 0 || room->getCardOwner(id) != target || !ctx.invoker->canGetCard(target, id)) break;
                // The card's recipient receives the nested target hook as well.
                ctx.choice = "obtain";
                ctx.extra_data = QVariantMap{{"id", id}, {"owner", target->objectName()}};
                skillEffect(ctx, ctx.invoker);
                ctx.choice.clear();
            }
        }
        return ContinueEffects;
    }
};

class SecondTenyearShizhi : public FilterSkill
{
public:
	SecondTenyearShizhi() : FilterSkill("secondtenyearshizhi")
	{

	}

	bool viewFilter(const Card *to_select) const
	{
		if(to_select->isKindOf("Jink")){
			const Player *player = Sanguosha->getCardOwner(to_select->getId());
			return player && player->getHp() == 1;
		}
		return false;
	}

	const Card *viewAs(const Card *original) const
	{
		Slash *slash = new Slash(original->getSuit(), original->getNumber());
		slash->setSkillName(objectName());/*
		WrappedCard *card = Sanguosha->getWrappedCard(original->getId());
		card->takeOver(slash);*/
		return slash;
	}
};

class SecondTenyearShizhiTrigger : public TriggerSkill
{
public:
	SecondTenyearShizhiTrigger() : TriggerSkill("#secondtenyearshizhi")
	{
		events << HpChanged << MaxHpChanged << Revived << Damage;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card&&damage.card->isKindOf("Slash")&&damage.by_user
			&&damage.card->getSkillNames().contains("secondtenyearshizhi")&&player->isWounded()&&player->hasSkill("secondtenyearshizhi")){
				room->sendCompulsoryTriggerLog(player, "secondtenyearshizhi", true, true);
				room->recover(player, RecoverStruct("secondtenyearshizhi", player));
			}
		}else{
			if(player->getHp()==1){
				if(player->getMark("secondtenyearshizhi")<1){
					player->setMark("secondtenyearshizhi",1);
					room->filterCards(player, player->getHandcards(), false);
				}
			}else{
				if(player->getMark("secondtenyearshizhi")>0){
					player->setMark("secondtenyearshizhi",0);
					QList<const Card*>hs = player->getHandcards();
					foreach (const Card *c, hs) {
						if(c->getSkillName()!="secondtenyearshizhi")
							hs.removeOne(c);
					}
					room->filterCards(player, hs, true);
				}
			}
		}
		return false;
	}
};

class TenyearYaoming : public TriggerSkillV2
{
public:
    TenyearYaoming() : TriggerSkillV2("tenyearyaoming") { events << Damage << Damaged << EventSkillInvoking; global = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString branch(const ServerPlayer *owner, const ServerPlayer *target) const
    {
        return target->getHandcardNum() > owner->getHandcardNum() ? "greater"
            : target->getHandcardNum() < owner->getHandcardNum() ? "less" : "equal";
    }
    QStringList used(const SkillContext &ctx) const
    {
        const QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
        const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
        return state.value("turn").toString() == turn ? state.value("branches").toStringList() : QStringList();
    }
    QList<ServerPlayer *> candidates(const SkillContext &ctx) const
    {
        QList<ServerPlayer *> result;
        const QStringList consumed = used(ctx);
        foreach (ServerPlayer *target, ctx.owner->getRoom()->getAlivePlayers()) {
            const QString choice = branch(ctx.owner, target);
            if (consumed.contains(choice)) continue;
            if (choice == "greater" && !ctx.owner->canDiscard(target, "h")) continue;
            if (choice == "equal" && !target->canDiscard(target, "he")) continue;
            result << target;
        }
        return result;
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
        return !turn.isEmpty() && turn != "0" && (ctx.choice.isEmpty()
            ? !candidates(ctx).isEmpty() : !used(ctx).contains(ctx.choice));
    }
    void addUsage(const SkillContext &ctx) const override
    {
        QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
        QStringList branches = used(ctx);
        if (!branches.contains(ctx.choice)) branches << ctx.choice;
        state["turn"] = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
        state["branches"] = branches;
        ctx.owner->setSkillInstanceState(objectName(), ctx.instanceID, state);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        // The dispatcher supplies the exact candidate copy before any invocation prompt.
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    bool commitQuota(SkillContext &ctx) const
    {
        const QString key = objectName() + "_quota";
        if (ctx.interceptor_data.value(key).value("committed").toBool()) return true;
        if (!isUsable(ctx)) return false;
        // Commit before Invoking/Effect callbacks can re-enter this exact copy.
        ctx.interceptor_data.insert(key, QVariantMap{{"committed", true}});
        addUsage(ctx);
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return commitQuota(ctx); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext active = data.value<SkillContext>();
        if (active.bypass_cost && active.owner && active.activationRef.isValid()
            && active.activationRef.key.skillName == objectName()
            && active.activationRef.ownerObjectName == active.owner->objectName()
            && active.activationRef.key.instanceID == active.instanceID) {
            if (!commitQuota(active)) active.is_canceled = true;
            data = QVariant::fromValue(active);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const QList<ServerPlayer *> targets = candidates(ctx);
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@tenyearyaoming-invoke", true, true);
        if (!target || !candidates(ctx).contains(target)) return false;
        ctx.choice = branch(ctx.owner, target);
        ctx.targets << target;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "greater") {
            for (int n = 0; n < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive()
                 && ctx.owner->canDiscard(target, "h"); ++n) {
                const int id = room->askForCardChosen(ctx.owner, target, "h", objectName(), false, Card::MethodDiscard);
                if (!target->handCards().contains(id) || !ctx.owner->canDiscard(target, id)) break;
                room->throwCard(Sanguosha->getCard(id), target, ctx.owner);
            }
        } else if (ctx.choice == "less") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else {
            const Card *discarded = room->askForDiscard(target, objectName(), 2 * getEffectiveAmount(ctx), 1, true, true, "tenyearyaoming-discard");
            if (discarded && target->isAlive()) target->drawCards(discarded->subcardsLength(), objectName());
        }
        return false;
    }
};

class TenyearDanshou : public TriggerSkillV2
{
public:
    TenyearDanshou() : TriggerSkillV2("tenyeardanshou")
    { events << TargetConfirmed << EventPhaseStart << EventSkillInvoking; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    static int targetCount(Room *room, ServerPlayer *owner)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (!turn.toLongLong()) return -1;
        QVariantMap filter{{"kind", "target_confirmed"}, {"turn_id", turn}, {"to", owner->objectName()}};
        QSet<qlonglong> uses;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap().value("data").toMap();
                const QVariantMap card = fact.value("card").toMap();
                const qlonglong use = fact.value("use_event_id").toLongLong();
                if (!card.contains("type") || use <= 0) return -1;
                if (card.value("type").toInt() > Card::TypeSkill) uses.insert(use);
            }
            if (!page.value("has_more").toBool()) return uses.size();
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        }
    }
    bool commitDraw(SkillContext &ctx) const
    {
        if (ctx.choice != "draw") return isUsable(ctx);
        const QString key = objectName() + "_quota";
        if (ctx.interceptor_data.value(key).value("committed").toBool()) return true;
        if (!isUsable(ctx)) return false;
        ctx.interceptor_data.insert(key, QVariantMap{{"committed", true}});
        addUsage(ctx);
        return true;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext active = data.value<SkillContext>();
        if (active.bypass_cost && active.owner && active.activationRef.isValid()
            && active.activationRef.key.skillName == objectName()
            && active.activationRef.ownerObjectName == active.owner->objectName()
            && active.activationRef.key.instanceID == active.instanceID) {
            if (!commitDraw(active)) active.is_canceled = true;
            data = QVariant::fromValue(active);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || event == EventSkillInvoking) return {};
        if (event == TargetConfirmed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            return player->hasSkill(objectName()) && use.to.contains(player) && use.card
                && (use.card->isKindOf("BasicCard") || use.card->isKindOf("TrickCard"))
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        TriggerList result;
        if (player->getPhase() == Player::Finish)
            for (ServerPlayer *owner : room->getAlivePlayers())
                if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *eventActor = ctx.invoker;
        // At another seat's Finish phase the event actor is the victim, not Danshou's damage source.
        ctx.invoker = ctx.owner;
        if (event == TargetConfirmed) {
            const int count = targetCount(room, ctx.owner);
            if (count <= 0 || !room->askForSkillInvoke(ctx.owner, objectName(), QString("tenyeardanshou_invoke:%1").arg(count)))
                return false;
            ctx.choice = "draw";
            ctx.extra_data = count;
            ctx.targets = {ctx.owner};
            return true;
        }
        if (!eventActor || !eventActor->isAlive()) return false;
        const int count = eventActor->getHandcardNum();
        ctx.choice = "damage";
        ctx.targets = {eventActor};
        if (count == 0)
            return room->askForSkillInvoke(ctx.owner, objectName(), QString("tenyeardanshou_damage:%1").arg(eventActor->objectName()));
        QStringList legal;
        for (const Card *card : ctx.owner->getCards("he"))
            if (!card->hasFlag("using") && ctx.owner->canDiscard(ctx.owner, card->getEffectiveId()))
                legal << QString::number(card->getEffectiveId());
        if (legal.size() < count) return false;
        const Card *selected = room->askForExchange(ctx.owner, objectName(), count, count, true,
            QString("@tenyeardanshou-dis:%1::%2").arg(eventActor->objectName()).arg(count), true, legal.join(","));
        if (!selected || selected->getSubcards().size() != count) return false;
        QVariantList cards;
        for (int id : selected->getSubcards()) {
            if (!legal.contains(QString::number(id))) return false;
            cards << id;
        }
        ctx.extra_data = cards;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        if (ctx.choice == "draw") return commitDraw(ctx);
        ServerPlayer *payer = ctx.owner;
        if (!payer) return false;
        DummyCard discard;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (id < 0 || room->getCardOwner(id) != payer
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using") || !payer->canDiscard(payer, id)) return false;
            discard.addSubcard(id);
        }
        return tenyearPayDiscard(room, payer, objectName(), discard.getSubcards());
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        if (ctx.choice == "draw") target->drawCards(ctx.extra_data.toInt() * amount, objectName());
        else room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        return false;
    }
};

class TenyearZenhui : public TriggerSkill
{
public:
	TenyearZenhui() : TriggerSkill("tenyearzenhui")
	{
		events << TargetSpecifying << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (triggerEvent == CardFinished && (use.card->isKindOf("Slash") || use.card->isNDTrick())) {
			use.from->setFlags("-TenyearZenhuiUser_" + use.card->toString());
			return false;
		}
		if (!TriggerSkill::triggerable(player) || player->hasFlag(objectName()))
			return false;

		if (use.to.length() == 1 && (use.card->isKindOf("Slash") || use.card->isNDTrick())) {
			QList<ServerPlayer *> targets = room->getOtherPlayers(use.to.first());
			targets.removeOne(player);
			player->setTag("tenyearzenhui", data);
			ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@tenyearzenhui-invoke:" + use.to.first()->objectName(), true, true);
			if (target) {
				room->broadcastSkillInvoke(objectName());

				bool canbeextra = true;
				if (room->isProhibited(player, target, use.card) || !use.card->targetFilter(QList<const Player *>(), target, player))
					canbeextra = false;
				if (target->isNude() && !canbeextra) return false;
				bool extra_target = true;
				if (!target->isNude()) {
					QString pattern = "..";
					QString prompt = "tenyearzenhui-give:" + player->objectName();
					if (!canbeextra) {
						pattern = "..!";
						prompt = "tenyearzenhui-mustgive:" + player->objectName();
					}
					const Card *card = room->askForCard(target, pattern, prompt, data, Card::MethodNone);
					if (!canbeextra && !card) {
						card = target->getCards("he").at(qsanRandomBounded(target->getCards("he").length()));
					}
					if (card) {
						extra_target = false;
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "tenyearzenhui", "");
						room->obtainCard(player, card, reason, false);

						if (target->isAlive()) {
							LogMessage log;
							log.type = "#BecomeUser";
							log.from = target;
							log.card_str = use.card->toString();
							room->sendLog(log);

							target->setFlags("TenyearZenhuiUser_" + use.card->toString()); // For AI
							use.from = target;
							data = QVariant::fromValue(use);
						}
					}
				}
				if (extra_target) {
					player->setFlags(objectName());
					LogMessage log;
					log.type = "#BecomeTarget";
					log.from = target;
					log.card_str = use.card->toString();
					room->sendLog(log);

					room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

					use.to.append(target);
					room->sortByActionOrder(use.to);
					data = QVariant::fromValue(use);
				}
			}
		}
		return false;
	}
};

class TenyearJiaojin : public TriggerSkillV2
{
public:
    TenyearJiaojin() : TriggerSkillV2("tenyearjiaojin") { events << TargetConfirmed; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
        return turn.isEmpty() || turn == "0"
            || ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "blocked_turn").toString() != turn;
    }
    // This skill has no per-invocation charge: only its successful special effect closes it.
    void addUsage(const SkillContext &) const override {}
    void closeForTurn(const SkillContext &ctx) const
    {
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "blocked_turn",
            ctx.owner->getRoom()->historyScopes().value("turn_id").toString());
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.to.contains(player)
            && use.from && use.from->isAlive() && use.from != player && use.card
            && (use.card->isKindOf("Slash") || use.card->isNDTrick())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; ctx.targets << ctx.owner; return true; }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return use.from && room->askForCard(ctx.owner, ".Equip", "@tenyearjiaojin:" + use.from->objectName()
            + "::" + use.card->objectName(), *ctx.original_data, objectName());
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.to.contains(target)) return false;
        room->broadcastSkillInvoke(objectName());
        if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
        ctx.original_data->setValue(use);
        if (!room->CardInPlace(use.card, Player::PlaceTable)) return false;
        // A female user's card closes this instance before the obtain can nest another use.
        if (use.from && use.from->isFemale() && room->hasCurrent()) closeForTurn(ctx);
        room->obtainCard(target, use.card);
        return false;
    }
};

class TenyearBenxiVS : public ZeroCardViewAsSkill
{
public:
	TenyearBenxiVS() : ZeroCardViewAsSkill("tenyearbenxi")
	{
		response_pattern = "@@tenyearbenxi!";
	}

	const Card *viewAs() const
	{
		return new ExtraCollateralCard;
	}
};

class TenyearBenxi : public TriggerSkill
{
public:
	TenyearBenxi() : TriggerSkill("tenyearbenxi")
	{
		events << CardUsed << DamageCaused << PreCardUsed;
		view_as_skill = new TenyearBenxiVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!player->hasFlag("CurrentPlayer") || use.card->isKindOf("SkillCard")) return false;
			//room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->addDistance(player, -1);
			room->addPlayerMark(player, "&tenyearbenxi-Clear");
		}else if (event == PreCardUsed) {
			if (!player->hasFlag("CurrentPlayer")) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;
			if (use.to.length() != 1) return false;
			bool allone = true;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->distanceTo(p) != 1) {
					allone = false;
					break;
				}
			}
			if (!allone) return false;
			QStringList choices, excepts;
			QList<ServerPlayer *> available_targets;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (use.to.contains(p)) continue;
				if (player->canUse(use.card,p))
					available_targets << p;
			}
			if (!available_targets.isEmpty()) choices << "extra";
			choices << "ignore" << "noresponse" << "draw" <<"cancel";
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);

			for (int i = 0; i < 2; i++) {
				if (choices.isEmpty()) break;
				QString choice = room->askForChoice(player, objectName(), choices.join("+"), data, excepts.join("+"));
				if (choice == "cancel") break;
				choices.removeOne(choice);
				excepts << choice;
				LogMessage log;
				log.type = "#FumianFirstChoice";
				log.from = player;
				log.arg = "tenyearbenxi:" + choice;
				room->sendLog(log);
				if (choice == "extra") {
					ServerPlayer *target;
					if (use.card->isKindOf("Collateral")){
						QStringList tos;
						tos << use.card->toString();
						foreach(ServerPlayer *t, use.to)
							tos << t->objectName();
						tos << objectName();
						room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
						room->askForUseCard(player, "@@tenyearbenxi!", "@tenyearbenxi-extra:" + use.card->objectName());
						target = player->getTag("ExtraCollateralTarget").value<ServerPlayer *>();
						player->removeTag("ExtraCollateralTarget");
						if (!target) {
							QList<ServerPlayer *> victims;
							target = available_targets.at(qsanRandomBounded(available_targets.length()));
							foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
								if (target->canSlash(p))
									victims << p;
							}
							target->setTag("attachTarget", QVariant::fromValue((victims.at(qsanRandomBounded(victims.length())))));
							log.type = "#QiaoshuiAdd";
							log.to << target;
							log.card_str = use.card->toString();
							log.arg = "tenyearbenxi";
							room->sendLog(log);
						}
					}else{
						target = room->askForPlayerChosen(player, available_targets, objectName(), "@tenyearbenxi-extra:" + use.card->objectName());
						log.type = "#QiaoshuiAdd";
						log.to << target;
						log.card_str = use.card->toString();
						log.arg = "tenyearbenxi";
						room->sendLog(log);
					}
					use.to.append(target);
					room->sortByActionOrder(use.to);
					if (use.card->hasFlag("tenyearbenxi_ignore"))
						target->addQinggangTag(use.card);
					data = QVariant::fromValue(use);
				} else if (choice == "ignore") {
					room->setCardFlag(use.card, "tenyearbenxi_ignore");
					foreach (ServerPlayer *p, use.to)
						p->addQinggangTag(use.card);
				} else if (choice == "noresponse") {
					use.no_offset_list << "_ALL_TARGETS";
					data = QVariant::fromValue(use);
				} else
					room->setCardFlag(use.card, "tenyearbenxi_damage");
			}
		} else if (event == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("tenyearbenxi_damage")) return false;
			player->drawCards(1, objectName());
		}
		return false;
	}
};

class TenyearPojun : public TriggerSkill
{
public:
	TenyearPojun() : TriggerSkill("tenyearpojun")
	{
		events << TargetSpecified << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card != nullptr && use.card->isKindOf("Slash") && TriggerSkill::triggerable(player)) {
				foreach (ServerPlayer *t, use.to) {
					if (player->isDead()) return false;
					if (t->isDead()) continue;
					int n = qMin(t->getCards("he").length(), t->getHp());
					if (n > 0 && player->askForSkillInvoke(this, QVariant::fromValue(t))) {
						room->broadcastSkillInvoke(objectName());
						DummyCard *dummy = new DummyCard;
						for (int i = 0; i < n; ++i) {
							int id = room->askForCardChosen(player, t, "he", objectName() + "_dis", false, Card::MethodNone, dummy->getSubcards(), i>0);
							if (id<0) break;
							dummy->addSubcard(id);
						}

						t->addToPile("tenyearpojun", dummy, false);
						dummy->deleteLater();

						QList<int> equips;
						bool has_trick = false;
						foreach (int id, dummy->getSubcards()) {
							if (Sanguosha->getCard(id)->isKindOf("EquipCard"))
								equips << id;
							else if (Sanguosha->getCard(id)->isKindOf("TrickCard"))
								has_trick = true;
						}

						if (!equips.isEmpty()) {
							room->fillAG(equips, player);
							int id = room->askForAG(player, equips, false, objectName());
							room->clearAG(player);
							room->throwCard(id, t, player);
						}

						if (has_trick)
							player->drawCards(1, objectName());
					}
				}
			}
		} else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> to_obtain = p->getPile("tenyearpojun");
				if (!to_obtain.isEmpty()) {
					DummyCard dummy(to_obtain);
					room->obtainCard(p, &dummy, false);
				}
			}
		}
		return false;
	}
};

TenyearYanzhuCard::TenyearYanzhuCard()
{
}

void TenyearYanzhuCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isDead()) return;
	if (effect.from->isDead() && !effect.to->canDiscard(effect.to, "he")) return;
	Room *room = effect.from->getRoom();

	if (effect.from->property("tenyearyanzhu_level_up").toBool()) {
		room->addPlayerMark(effect.to, "&tenyearyanzhu");
		return;
	}

	bool optional = effect.from->isAlive() ? true : false;
	optional = !effect.to->getEquips().isEmpty() ? true : false;
	QString prompt = optional ? "@tenyearyanzhu-discard:" + effect.from->objectName() : "@tenyearyanzhu-discard2";
	if (!room->askForDiscard(effect.to, "tenyearyanzhu", 1, 1, optional, true, prompt)) {
		QList<int> list = effect.to->getEquipsId();
		DummyCard *dummy = new DummyCard(list);
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
		room->obtainCard(effect.from, dummy, reason);
		dummy->deleteLater();
		room->setPlayerProperty(effect.from, "tenyearyanzhu_level_up", true);
		room->setPlayerProperty(effect.from, "tenyearxingxue_level_up", true);
		LogMessage log;
		log.type = "#JiexunChange";
		log.from = effect.from;
		if (effect.from->hasSkill("tenyearyanzhu"), true) {
			log.arg = "tenyearyanzhu";
			room->sendLog(log);
		}
		if (effect.from->hasSkill("tenyearxingxue"), true) {
			log.arg = "tenyearxingxue";
			room->sendLog(log);
		}
		QString translate = Sanguosha->translate(":tenyearyanzhu2");
		room->changeTranslation(effect.from, "tenyearyanzhu", translate);
		QString translate2 = Sanguosha->translate(":tenyearxingxue2");
		room->changeTranslation(effect.from, "tenyearxingxue", translate2);
	} else
		room->addPlayerMark(effect.to, "&tenyearyanzhu");
}

class TenyearYanzhuVS : public ZeroCardViewAsSkill
{
public:
	TenyearYanzhuVS() : ZeroCardViewAsSkill("tenyearyanzhu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearYanzhuCard");
	}

	const Card *viewAs() const
	{
		return new TenyearYanzhuCard;
	}
};

class TenyearYanzhu : public TriggerSkill
{
public:
	TenyearYanzhu() : TriggerSkill("tenyearyanzhu")
	{
		events << EventPhaseChanging << DamageInflicted;
		view_as_skill = new TenyearYanzhuVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getMark("&tenyearyanzhu") > 0;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::RoundStart) return false;
			room->setPlayerMark(player, "&tenyearyanzhu", 0);
		} else {
			LogMessage log;
			log.type = "#TenyearyanzhuDamage";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(player->getMark("&tenyearyanzhu"));
			room->sendLog(log);

			DamageStruct damage = data.value<DamageStruct>();
			damage.damage += player->getMark("&tenyearyanzhu");
			data = QVariant::fromValue(damage);
			room->setPlayerMark(player, "&tenyearyanzhu", 0);
		}
		return false;
	}
};

TenyearXingxueCard::TenyearXingxueCard()
{
}

bool TenyearXingxueCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
	int n = Self->property("tenyearxingxue_level_up").toBool() ? Self->getMaxHp() : Self->getHp();

	return targets.length() < n;
}

void TenyearXingxueCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
	room->drawCards(targets, 1, "tenyearxingxue");

	foreach (ServerPlayer *p, targets) {
		if (p->isDead() || p->getHandcardNum() <= p->getHp()) continue;
		const Card *c = room->askForExchange(p, "tenyearxingxue", 1, 1, true, "@tenyearxingxue-put");
		int id = c->getSubcards().first();
		CardsMoveStruct m(id, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, p->objectName()));
		room->setPlayerFlag(p, "Global_GongxinOperator");
		room->moveCardsAtomic(m, false);
		room->setPlayerFlag(p, "-Global_GongxinOperator");
	}
}

class TenyearXingxueVS : public ZeroCardViewAsSkill
{
public:
	TenyearXingxueVS() : ZeroCardViewAsSkill("tenyearxingxue")
	{
		response_pattern = "@@tenyearxingxue";
	}

	const Card *viewAs() const
	{
		return new TenyearXingxueCard;
	}
};

class TenyearXingxue : public PhaseChangeSkill
{
public:
	TenyearXingxue() : PhaseChangeSkill("tenyearxingxue")
	{
		view_as_skill = new TenyearXingxueVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return PhaseChangeSkill::triggerable(target) && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		int n = target->property("tenyearxingxue_level_up").toBool() ? target->getMaxHp() : target->getHp();
		if (n <= 0) return false;
		room->askForUseCard(target, "@@tenyearxingxue", "@tenyearxingxue:" + QString::number(n));
		return false;
	}
};

class TenyearQingxi : public TriggerSkill
{
public:
	TenyearQingxi() : TriggerSkill("tenyearqingxi")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel")) return false;
		foreach (ServerPlayer *p, use.to) {
			if (player->isDead()) return false;
			if (p->isDead()) continue;
			if (!player->askForSkillInvoke(this, QVariant::fromValue(p))) continue;
			room->broadcastSkillInvoke(objectName());
			if (p->isDead()) continue;
			int n = 0;
			foreach (ServerPlayer *d, room->getOtherPlayers(player)) {
				if (player->inMyAttackRange(d))
					n++;
			}
			int min = player->getWeapon() ? 4 : 2;
			n = qMin(n, min);

			if (n <= 0 || player->isDead()) {
				room->setCardFlag(use.card, "tenyearqingxi_" + p->objectName());
				JudgeStruct judge;
				judge.who = p;
				judge.reason = objectName();
				judge.pattern = ".|red";
				judge.good = false;
				room->judge(judge);

				if (judge.isBad())
					use.no_respond_list << p->objectName();
			} else {
				if (!room->askForDiscard(p, objectName(), n, n, true, false, "@tenyearqingxi-discard:" + QString::number(n))) {
					room->setCardFlag(use.card, "tenyearqingxi_" + p->objectName());
					JudgeStruct judge;
					judge.who = p;
					judge.reason = objectName();
					judge.pattern = ".|red";
					judge.good = false;
					room->judge(judge);

					if (judge.isBad())
						use.no_respond_list << p->objectName();
				} else {
					if (p->isDead() || !player->getWeapon() || !p->canDiscard(player, player->getWeapon()->getEffectiveId())) continue;
					room->throwCard(player->getWeapon(), player, p);
				}
			}
		}
		data = QVariant::fromValue(use);
		return false;
	}
};

class TenyearQingxiDamage : public TriggerSkill
{
public:
	TenyearQingxiDamage() : TriggerSkill("#tenyearqingxi-damage")
	{
		events << DamageInflicted;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card) return false;
		if (!damage.card->hasFlag("tenyearqingxi_" + player->objectName())) return false;
		//room->setCardFlag(damage.card, "-tenyearqingxi_" + player->objectName());
		++damage.damage;
		data = QVariant::fromValue(damage);
		return false;
	}
};

class TenyearDuodao : public TriggerSkillV2
{
public:
    TenyearDuodao() : TriggerSkillV2("tenyearduodao") { events << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.from && use.card
            && use.card->isKindOf("Slash") && use.to.contains(player) && player->canDiscard(player, "he")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return use.from && room->askForCard(ctx.owner, "..", "@tenyearduodao:" + use.from->objectName(),
            *ctx.original_data, objectName());
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        ServerPlayer *source = ctx.original_data->value<CardUseStruct>().from;
        // Resolve the weapon after payment: nested movement may have changed its owner.
        if (source && source->isAlive() && source->getWeapon()) {
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName());
            room->obtainCard(target, source->getWeapon(), reason);
        }
        return false;
    }
};

class TenyearAnjian : public TriggerSkill
{
public:
	TenyearAnjian() : TriggerSkill("tenyearanjian")
	{
		events << TargetSpecified << CardFinished << DamageCaused;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			//damage.damage += damage.card->getTag("Tenyearanjian_Damage").toInt();
			damage.damage += room->getTag("Tenyearanjian_Damage" + damage.card->toString()).toInt();
			data = QVariant::fromValue(damage);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")) return false;
			if (event == TargetSpecified) {
				foreach (ServerPlayer *p, use.to) {
					if (p->inMyAttackRange(player)) continue;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					p->addQinggangTag(use.card);
					room->setCardFlag(use.card, "tenyearanjian_" + p->objectName());
					//int damage = use.card->getTag("Tenyearanjian_Damage").toInt();
					//use.card->setTag("Tenyearanjian_Damage", ++damage);
					int damage = room->getTag("Tenyearanjian_Damage" + use.card->toString()).toInt();
					room->setTag("Tenyearanjian_Damage" + use.card->toString(), ++damage);
				}
			} else
				room->removeTag("Tenyearanjian_Damage" + use.card->toString());
		}
		return false;
	}
};

class TenyearAnjianEffect : public TriggerSkill
{
public:
	TenyearAnjianEffect() : TriggerSkill("#tenyearanjian-effect")
	{
		events << Dying << AskForPeachesDone;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Dying) {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who != player) return false;
			if (!dying.damage || !dying.damage->card || !dying.damage->card->isKindOf("Slash")) return false;
			if (!dying.damage->card->hasFlag("tenyearanjian_" + player->objectName())) return false;
			player->setTag("TenyearanjianForbidden", true);
			room->setPlayerCardLimitation(player, "use", "Peach", false);
		} else {
			if (!player->getTag("TenyearanjianForbidden").toBool()) return false;
			player->removeTag("TenyearanjianForbidden");
			room->removePlayerCardLimitation(player, "use", "Peach$0");
		}
		return false;
	}
};

TenyearShenduanCard::TenyearShenduanCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool TenyearShenduanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	const Card *cc = Sanguosha->getCard(getSubcards().first());
	SupplyShortage *ss = new SupplyShortage(cc->getSuit(), cc->getNumber());
	ss->addSubcard(cc);
	ss->setSkillName("tenyearshenduan");
	ss->deleteLater();
	return ss->targetFilter(targets, to_select, Self) && !Self->isCardLimited(ss, Card::MethodUse);
}

void TenyearShenduanCard::onUse(Room *room, CardUseStruct &card_use) const
{
	room->setPlayerFlag(card_use.to.first(), "tenyearshenduan_target");
}

class TenyearShenduanVS : public OneCardViewAsSkill
{
public:
	TenyearShenduanVS() : OneCardViewAsSkill("tenyearshenduan")
	{
		response_pattern = "@@tenyearshenduan";
		expand_pile = "#tenyearshenduan";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPile("#tenyearshenduan").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const Card *card) const
	{
		//TenyearShenduanCard *c = new TenyearShenduanCard;
		//c->addSubcard(card);
		SupplyShortage *c = new SupplyShortage(card->getSuit(), card->getNumber());
		c->setSkillName("tenyearshenduan");
		c->addSubcard(card);
		return c;
	}
};

class TenyearShenduan : public TriggerSkill
{
public:
	TenyearShenduan() : TriggerSkill("tenyearshenduan")
	{
		events << CardsMoveOneTime;
		view_as_skill = new TenyearShenduanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (Sanguosha->getBanPackages().contains("maneuvering")) return false;

		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place == Player::DiscardPile&&move.from == player
			&& ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {
			QList<int> shenduan_card;
			for (int i = 0; i < move.card_ids.length(); i++) {
				if (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip) {
					if (room->getCardPlace(move.card_ids.at(i)) == Player::DiscardPile) {
						const Card *c = Sanguosha->getCard(move.card_ids.at(i));
						if (c->isBlack() && (c->isKindOf("BasicCard") || c->isKindOf("EquipCard")))
							shenduan_card << move.card_ids.at(i);
					}
				}
			}
			while (!shenduan_card.isEmpty()&&player->isAlive()) {
				room->notifyMoveToPile(player, shenduan_card, objectName(), Player::PlaceUnknown, true);
				const Card *c = room->askForUseCard(player, "@@tenyearshenduan", "@tenyearshenduan");
				if (!c) break;
				shenduan_card.removeOne(c->getEffectiveId());
				foreach (int id, shenduan_card) {
					if(room->getCardOwner(id))
						shenduan_card.removeOne(id);
				}
			}
		}
		return false;
	}
};

class TenyearShenduanTargetMod : public TargetModSkill
{
public:
	TenyearShenduanTargetMod() : TargetModSkill("#tenyearshenduan-target")
	{
		frequency = NotFrequent;
		pattern = "SupplyShortage";
	}

	int getDistanceLimit(const Player *, const Card *card, const Player *) const
	{
		if (card->getSkillName() == "tenyearshenduan")
			return 1000;
		return 0;
	}
};

class TenyearYonglve : public TriggerSkillV2
{
public:
    TenyearYonglve() : TriggerSkillV2("tenyearyonglve") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Judge || player->getJudgingArea().isEmpty()) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->canDiscard(player, "j")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.invoker;
        if (!target || !target->isAlive() || !ctx.owner->canDiscard(target, "j")) return false;
        const QString prompt = (ctx.owner->inMyAttackRange(target) ? "tenyearyonglve_in:" : "tenyearyonglve_out:") + target->objectName();
        if (!ctx.owner->askForSkillInvoke(this, prompt)) return false;
        const int id = room->askForCardChosen(ctx.owner, target, "j", objectName(), false, Card::MethodDiscard);
        if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceDelayedTrick
            || !ctx.owner->canDiscard(target, id)) return false;
        ctx.extra_data = id;
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.invoker || room->getCardOwner(id) != ctx.invoker || room->getCardPlace(id) != Player::PlaceDelayedTrick
            || !ctx.owner->canDiscard(ctx.invoker, id)) return false;
        room->throwCard(id, nullptr, ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        if (!ctx.owner->isAlive()) return false;
        if (ctx.owner->inMyAttackRange(target)) {
            ctx.choice = "draw";
            skillEffect(event, room, ctx.owner, ctx, ctx.owner);
            ctx.choice.clear();
        } else {
            const int count = getEffectiveAmount(ctx);
            for (int i = 0; i < count && ctx.owner->isAlive() && target->isAlive(); ++i) {
                std::unique_ptr<Slash> slash(new Slash(Card::NoSuit, 0));
                slash->setSkillName("_tenyearyonglve");
                if (!ctx.owner->canSlash(target, slash.get(), false)) break;
                // Transfer ownership to the ordinary card-use pipeline.
                CardUseStruct use(slash.get(), ctx.owner, target);
                use.setOwnedCard(slash.release());
                room->useCardFromSkillEffect(use, ctx);
            }
        }
        return false;
    }
};

TenyearQiaoshuiCard::TenyearQiaoshuiCard()
{
}

bool TenyearQiaoshuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select);
}

void TenyearQiaoshuiCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.from->canPindian(effect.to, false)) return;
	Room *room = effect.from->getRoom();
	if (effect.from->pindian(effect.to, "tenyearqiaoshui"))
		room->addPlayerMark(effect.from, "&tenyearqiaoshui-Clear");
	else {
		effect.from->endPlayPhase();
		room->addPlayerMark(effect.from, "tenyearqiaoshui_lose-Clear");
	}
}

TenyearQiaoshuiTargetCard::TenyearQiaoshuiTargetCard()
{
	mute = true;
}

bool TenyearQiaoshuiTargetCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < Self->getMark("tenyearqiaoshui_max_target-Clear") && to_select->hasFlag("tenyearqiaoshui_canchoose");
}

void TenyearQiaoshuiTargetCard::onUse(Room *room, CardUseStruct &card_use) const
{
	room->setPlayerMark(card_use.from, "tenyearqiaoshui_max_target-Clear", 0);
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "tenyearqiaoshui_choose_target");
}

class TenyearQiaoshuiVS : public ZeroCardViewAsSkill
{
public:
	TenyearQiaoshuiVS() : ZeroCardViewAsSkill("tenyearqiaoshui")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canPindian();
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@tenyearqiaoshui");
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern=="@@tenyearqiaoshui2")
			return new ExtraCollateralCard;
		else {
			if (pattern.startsWith("@@tenyearqiaoshui"))
				return new TenyearQiaoshuiTargetCard;
			return new TenyearQiaoshuiCard;
		}
	}
};

class TenyearQiaoshui : public TriggerSkill
{
public:
	TenyearQiaoshui() : TriggerSkill("tenyearqiaoshui")
	{
		events << PreCardUsed << EventPhaseProceeding;
		view_as_skill = new TenyearQiaoshuiVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseProceeding) {
			if (player->getPhase() != Player::Discard) return false;
			if (player->getMark("tenyearqiaoshui_lose-Clear") <= 0) return false;
			QList<int> ids;
			foreach (const Card *c, player->getCards("h")) {
				if (!c->isKindOf("TrickCard")) continue;
				ids << c->getEffectiveId();
			}
			if (ids.isEmpty()) return false;
			room->ignoreCards(player, ids);
			return false;
		}
		const Card *card = nullptr;
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			card = use.card;
		}
		if (card == nullptr || (!card->isKindOf("BasicCard") && !card->isNDTrick())) return false;

		int n = player->getMark("&tenyearqiaoshui-Clear");
		if (n <= 0) return false;
		room->setPlayerMark(player, "&tenyearqiaoshui-Clear", 0);
		room->setPlayerMark(player, "tenyearqiaoshui_max_target-Clear", n);

		CardUseStruct use = data.value<CardUseStruct>();
		if (use.to.isEmpty()) return false;
		room->setCardFlag(card, "tenyearqiaoshui_distance");

		bool canextra = false;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (use.card->isKindOf("AOE") && p == player) continue;
			if (use.to.contains(p) || room->isProhibited(player, p, use.card)) continue;
			if (use.card->targetFixed()) {
				if (!use.card->isKindOf("Peach") || p->getLostHp() > 0) {
					canextra = true;
					break;
				}
			} else {
				if (use.card->targetFilter(QList<const Player *>(), p, player)) {
					canextra = true;
					break;
				}
			}
		}
		room->setCardFlag(use.card, "-tenyearqiaoshui_distance");

		QStringList choices;
		if (canextra)
			choices << "add";
		if (use.to.length() > 1)
			choices << "remove";
		if (choices.isEmpty()) return false;
		choices << "cancel";
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "cancel") return false;

		if (choice == "add") {
			if (card->isKindOf("Collateral")) {
				for (int i = 1; i <= n; i++) {
					bool canextra = false;
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						if (use.to.contains(p)) continue;
						if (player->canUse(use.card,p)) {
							canextra = true;
							break;
						}
					}
					if (!canextra)
						break;
					QStringList tos;
					tos << use.card->toString();
					foreach (ServerPlayer *t, use.to)
						tos << t->objectName();
					tos << objectName();
					room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
					if (!room->askForUseCard(player, "@@tenyearqiaoshui2", "@tenyearqiaoshui1:" + use.card->objectName(), 1)) break;
					ServerPlayer *p = player->getTag("ExtraCollateralTarget").value<ServerPlayer *>();
					player->removeTag("ExtraCollateralTarget");
					if (p) use.to.append(p);
				}
			} else {
				room->setCardFlag(use.card, "tenyearqiaoshui_distance");
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (use.to.contains(p)) continue;
					if (player->canUse(use.card,p))
						room->setPlayerFlag(p, "tenyearqiaoshui_canchoose");
				}
				room->setCardFlag(use.card, "-tenyearqiaoshui_distance");
				if (!room->askForUseCard(player, "@@tenyearqiaoshui1", "@tenyearqiaoshui1:" + use.card->objectName(), 1)) return false;
				LogMessage log;
				foreach(ServerPlayer *p, room->getAlivePlayers()) {
					room->setPlayerFlag(p, "-tenyearqiaoshui_canchoose");
					if (p->hasFlag("tenyearqiaoshui_choose_target")) {
						room->setPlayerFlag(p,"-tenyearqiaoshui_choose_target");
						log.to << p;
					}
				}
				if (log.to.isEmpty()) return false;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "tenyearqiaoshui";
				room->sendLog(log);
				use.to << log.to;
			}
		} else {
			foreach (ServerPlayer *p, use.to)
				room->setPlayerFlag(p, "tenyearqiaoshui_canchoose");
			if (!room->askForUseCard(player, "@@tenyearqiaoshui2", "@tenyearqiaoshui2:" + use.card->objectName(), 2)) return false;
			LogMessage log;
			foreach (ServerPlayer *p, use.to) {
				room->setPlayerFlag(p, "-tenyearqiaoshui_canchoose");
				if (p->hasFlag("tenyearqiaoshui_choose_target")) {
					room->setPlayerFlag(p, "-tenyearqiaoshui_choose_target");
					log.to << p;
					use.to.removeOne(p);
				}
			}
			if (log.to.isEmpty()) return false;
			log.type = "#QiaoshuiRemove";
			log.from = player;
			log.card_str = use.card->toString();
			log.arg = "tenyearqiaoshui";
			room->sendLog(log);
		}
		room->sortByActionOrder(use.to);
		data = QVariant::fromValue(use);
		return false;
	}
};

class TenyearQiaoshuiTargetMod : public TargetModSkill
{
public:
	TenyearQiaoshuiTargetMod() : TargetModSkill("#tenyearqiaoshui-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getDistanceLimit(const Player *, const Card *card, const Player *) const
	{
		if (card->hasFlag("tenyearqiaoshui_distance"))
			return 1000;
		return 0;
	}
};

TenyearXianzhenCard::TenyearXianzhenCard()
{
	m_skillName = "tenyearxianzhen";
}

bool TenyearXianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select);
}

void TenyearXianzhenCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->addPlayerMark(effect.from, m_skillName + "_Used-Clear");

	if (effect.from->pindian(effect.to, m_skillName)) {
		room->addPlayerMark(effect.to, "Armor_Nullified");
		room->addPlayerMark(effect.from, m_skillName + "_from-Clear");
		room->addPlayerMark(effect.to, m_skillName + "_to-Clear");
	} else {
		room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
		room->addPlayerMark(effect.from, m_skillName + "_slash-Clear");
	}
}

SecondTenyearXianzhenCard::SecondTenyearXianzhenCard() : TenyearXianzhenCard()
{
	m_skillName = "secondtenyearxianzhen";
}

class TenyearXianzhenViewAsSkill : public ZeroCardViewAsSkill
{
public:
	TenyearXianzhenViewAsSkill(const QString &xianzhen) : ZeroCardViewAsSkill(xianzhen), xianzhen(xianzhen)
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (xianzhen == "tenyearxianzhen")
			return !player->hasUsed("TenyearXianzhenCard") && player->canPindian();
		else if (xianzhen == "secondtenyearxianzhen")
			return player->getMark("secondtenyearxianzhen_Used-Clear") <= 0 && player->canPindian();
		return false;
	}

	const Card *viewAs() const
	{
		if (xianzhen == "tenyearxianzhen")
			return new TenyearXianzhenCard;
		else if (xianzhen == "secondtenyearxianzhen")
			return new SecondTenyearXianzhenCard;
		return nullptr;
	}
private:
	QString xianzhen;
};

class TenyearXianzhen : public TriggerSkill
{
public:
	TenyearXianzhen(const QString &xianzhen) : TriggerSkill(xianzhen), xianzhen(xianzhen)
	{
		events << EventPhaseChanging << Death << CardUsed << DamageCaused;
		view_as_skill = new TenyearXianzhenViewAsSkill(xianzhen);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getMark(xianzhen + "_from-Clear") > 0;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *gaoshun, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers(true)) {
				if (p->getMark(xianzhen + "_to-Clear") <= 0) continue;
				room->removePlayerMark(p, "Armor_Nullified");
			}
		} else if (triggerEvent == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who->getMark(xianzhen + "_to-Clear") <= 0) return false;
			room->removePlayerMark(death.who, "Armor_Nullified");
		} else if (triggerEvent == DamageCaused) {
			if (gaoshun->isDead() || xianzhen != "secondtenyearxianzhen") return false;
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || damage.to->isDead() || damage.to->getMark("secondtenyearxianzhen_to-Clear") <= 0) return false;
			QString name = damage.card->objectName();
			if (damage.card->isKindOf("Slash"))
				name = "slash";
			if (gaoshun->getMark("secondtenyearxianzhen_" + name + "_" + damage.to->objectName() + "-Clear") > 0) return false;
			LogMessage log;
			log.type = "#YHHankaiDamage";
			log.from = gaoshun;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			log.arg3 = QString::number(damage.damage += 1);
			room->sendLog(log);
			room->notifySkillInvoked(gaoshun, objectName());
			data = QVariant::fromValue(damage);
			room->addPlayerMark(gaoshun, "secondtenyearxianzhen_" + name + "_" + damage.to->objectName() + "-Clear");
		} else {
			if (gaoshun->isDead() || xianzhen != "tenyearxianzhen") return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.to.length() != 1 || use.card->isKindOf("Collateral")) return false;
			if (use.card->isKindOf("Slash") || use.card->isNDTrick()) {
				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *target, room->getAlivePlayers()) {
					if (target->getMark("tenyearxianzhen_to-Clear") <= 0) continue;
					if (!use.card->isKindOf("AOE") && !use.card->isKindOf("GlobalEffect")) {
						if (use.to.contains(target) || room->isProhibited(gaoshun, target, use.card)) continue;
						if (use.card->targetFixed()) {
							if (!use.card->isKindOf("Peach") || target->isWounded())
								targets << target;
						} else {
							if (use.card->targetFilter(QList<const Player *>(), target, gaoshun))
								targets << target;
						}
					}
				}
				while (!targets.isEmpty()&&gaoshun->isAlive()) {
					ServerPlayer *target = room->askForPlayerChosen(gaoshun, targets, objectName(),
									"@tenyearxianzhen-target:" + use.card->objectName(), true);
					if (!target) break;
					LogMessage log;
					log.type = "#QiaoshuiAdd";
					log.from = gaoshun;
					log.to << target;
					log.card_str = use.card->toString();
					log.arg = objectName();
					room->sendLog(log);
					room->doAnimate(1, gaoshun->objectName(), target->objectName());
					use.to << target;
					targets.removeOne(target);
					room->sortByActionOrder(use.to);
					data = QVariant::fromValue(use);
				}
			}
		}
		return false;
	}
private:
	QString xianzhen;
};

class TenyearXianzhenSlash : public TriggerSkill
{
public:
	TenyearXianzhenSlash(const QString &xianzhen) : TriggerSkill("#" + xianzhen + "-slash"), xianzhen(xianzhen)
	{
		events << EventPhaseProceeding;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getMark(xianzhen + "_slash-Clear") > 0;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *gaoshun, QVariant &) const
	{
		if (gaoshun->getPhase() != Player::Discard) return false;
		foreach (const Card *card, gaoshun->getHandcards()) {
			if (card->isKindOf("Slash"))
				room->ignoreCards(gaoshun, card);
		}
		return false;
	}
private:
	QString xianzhen;
};

class TenyearXianzhenTargetMod : public TargetModSkill
{
public:
	TenyearXianzhenTargetMod(const QString &xianzhen) : TargetModSkill("#" + xianzhen + "-target"), xianzhen(xianzhen)
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *, const Player *to) const
	{
		if (from->getMark(xianzhen + "_from-Clear") > 0 && to && to->getMark(xianzhen + "_to-Clear") > 0)
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *to) const
	{
		if (from->getMark(xianzhen + "_from-Clear") > 0 && to && to->getMark(xianzhen + "_to-Clear") > 0)
			return 999;
		return 0;
	}
private:
	QString xianzhen;
};

class SecondTenyearJinjiu : public FilterSkill
{
public:
	SecondTenyearJinjiu() : FilterSkill("secondtenyearjinjiu")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->objectName() == "analeptic";
	}

	const Card *viewAs(const Card *original) const
	{
		Slash *slash = new Slash(original->getSuit(), 13);
		slash->setSkillName(objectName());/*
		WrappedCard *card = Sanguosha->getWrappedCard(original->getId());
		card->takeOver(slash);*/
		return slash;
	}
};

class SecondTenyearJinjiuLimit : public CardLimitSkill
{
public:
	SecondTenyearJinjiuLimit() : CardLimitSkill("#secondtenyearjinjiu-limit")
	{
	}

	bool gaoshun(const Player *target) const
	{
		foreach (const Player *p, target->getAliveSiblings()) {
			if (p->hasFlag("CurrentPlayer") && p->hasSkill("secondtenyearjinjiu"))
				return true;
		}
		return false;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (gaoshun(target))
			return "Analeptic";
		return "";
	}
};

TenyearZishouCard::TenyearZishouCard()
{
	target_fixed = true;
}

void TenyearZishouCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->drawCards(subcardsLength(), "tenyearzishou");
}

class TenyearZishouVS : public ViewAsSkill
{
public:
	TenyearZishouVS() : ViewAsSkill("tenyearzishou")
	{
		response_pattern = "@@tenyearzishou";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Self->isJilei(to_select) || to_select->isEquipped()) return false;
		foreach (const Card *c, selected) {
			if (to_select->getSuit() == c->getSuit())
				return false;
		}
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;

		TenyearZishouCard *c = new TenyearZishouCard;
		c->addSubcards(cards);
		return c;
	}
};

class TenyearZishou : public TriggerSkill
{
public:
	TenyearZishou() : TriggerSkill("tenyearzishou")
	{
		events << DrawNCards << DamageCaused << EventPhaseStart << TargetSpecified;
		view_as_skill = new TenyearZishouVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase"||!player->hasSkill(objectName())) return false;
			QSet<QString> kingdomSet;
			foreach(ServerPlayer *p, room->getAlivePlayers())
				kingdomSet.insert(p->getKingdom());

			int n = kingdomSet.count();
			if (!player->askForSkillInvoke(this, QString("tenyearzishou:" + QString::number(n)))) return false;
			room->broadcastSkillInvoke(objectName());
			room->setPlayerFlag(player, "tenyearzishou");
			draw.num += n;
			data = QVariant::fromValue(draw);
		} else if (event == DamageCaused) {
			if (!player->hasFlag(objectName())) return false;
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.to->isAlive() && damage.to != player) {
				LogMessage log;
				log.type = "#OLzishouPrevent";
				log.from = player;
				log.to << damage.to;
				log.arg = objectName();
				log.arg2 = QString::number(damage.damage);
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player, objectName());
				return true;
			}
		} else if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId() != Card::TypeSkill) {
				foreach (ServerPlayer *p, use.to) {
					if (p != player)
						player->addMark("qieting-Clear");
				}
			}
		} else {
			if (!player->hasFlag(objectName()) || player->getPhase() != Player::Finish) return false;
			if (player->getMark("qieting-Clear") > 0 || !player->canDiscard(player, "h")) return false;
			room->askForUseCard(player, "@@tenyearzishou", "@tenyearzishou", -1, Card::MethodDiscard);
		}
		return false;
	}
};

class TenyearZongshi : public MaxCardsSkillV2
{
public:
    TenyearZongshi() : MaxCardsSkillV2("tenyearzongshi") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.holder != ctx.primary) return CorrectSkillResult::noEffect();
        QSet<QString> kingdoms;
        if (ctx.primary->parent())
            for (const Player *player : ctx.primary->parent()->findChildren<const Player *>())
                if (player->isAlive()) kingdoms << player->getKingdom();
        // The collector has already resolved this exact instance's validity and amount.
        return CorrectSkillResult::useAmount(kingdoms.size() * ctx.currentAmount);
    }
};

class TenyearZongshiProtect : public TriggerSkillV2
{
public:
    TenyearZongshiProtect() : TriggerSkillV2("#tenyearzongshi-protect")
    { events << TargetConfirmed; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill("tenyearzongshi")
            || player->getHandcardNum() < player->getMaxCards() || !use.card || !use.to.contains(player)
            || use.card->getTypeId() < 1) return {};
        return use.card->isKindOf("DelayedTrick") || (!use.card->isBlack() && !use.card->isRed())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets << ctx.owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.to.contains(target)) return false;
        LogMessage log;
        log.type = "#TenyearzongshiAvoid";
        log.from = target;
        log.arg = "tenyearzongshi";
        log.card_str = use.card->toString();
        room->sendLog(log);
        room->notifySkillInvoked(ctx.owner, "tenyearzongshi");
        if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class TenyearXuanfeng : public TriggerSkillV2
{
public:
    TenyearXuanfeng() : TriggerSkillV2("tenyearxuanfeng") { events << CardsMoveOneTime << EventPhaseEnd; }
    static int discardedInPhase(Room *room, ServerPlayer *owner)
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        if (!phase.toLongLong()) return -1;
        QVariantMap query{{"phase_id", phase}, {"from", owner->objectName()}, {"to_place", int(Player::DiscardPile)}};
        QSet<QString> cards;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                    cards.insert(fact.value("event_id").toString() + ":" + move.value("card_id").toString());
            }
            if (!page.value("has_more").toBool()) return cards.size();
            query["watermark"] = page.value("watermark"); query["after"] = page.value("next_after");
        }
    }
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *actor)
    {
        QList<ServerPlayer *> result;
        if (!actor || !actor->isAlive()) return result;
        for (ServerPlayer *target : room->getOtherPlayers(actor))
            for (const Card *card : target->getCards("he"))
                if (!card->hasFlag("using") && actor->canDiscard(target, card->getEffectiveId())) { result << target; break; }
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || candidates(room, player).isEmpty()) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            // This event is broadcast; only the actual former equipment holder offers its copies.
            if (move.from != player || !move.from_places.contains(Player::PlaceEquip)) return {};
        } else if (player->getPhase() != Player::Discard || discardedInPhase(room, player) < 2) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int count = 2 * getEffectiveAmount(ctx);
        QStringList chosen, discarded;
        for (int i = 0; i < count && ctx.invoker && ctx.invoker->isAlive(); ++i) {
            QList<ServerPlayer *> targets = candidates(room, ctx.invoker);
            if (chosen.size() >= 2)
                for (ServerPlayer *target : QList<ServerPlayer *>(targets)) if (!chosen.contains(target->objectName())) targets.removeOne(target);
            if (targets.isEmpty()) break;
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, targets, objectName());
            if (!target) break;
            if (!chosen.contains(target->objectName())) chosen << target->objectName();
            SkillContext discard = ctx; discard.choice = "discard"; discard.extra_data = QString();
            skillEffect(event, room, ctx.owner, discard, target);
            const QString actual = discard.extra_data.toString();
            if (!actual.isEmpty() && !discarded.contains(actual)) discarded << actual;
        }
        if (!ctx.invoker || !ctx.invoker->isAlive() || !ctx.invoker->hasFlag("CurrentPlayer")) return false;
        QList<ServerPlayer *> victims;
        for (const QString &name : discarded) {
            ServerPlayer *target = room->findPlayerByObjectName(name);
            if (target && target->isAlive()) victims << target;
        }
        if (!victims.isEmpty()) {
            ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, victims, "tenyearxuanfeng_damage", "@tenyearxuanfeng-invoke", true);
            if (victim) { ctx.choice = "damage"; skillEffect(event, room, ctx.owner, ctx, victim); }
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !ctx.invoker || !ctx.invoker->isAlive()) return false;
        if (ctx.choice == "damage") {
            room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
            return false;
        }
        if (target == ctx.invoker || !ctx.invoker->canDiscard(target, "he")) return false;
        QList<int> disabled;
        for (const Card *card : target->getCards("he"))
            if (card->hasFlag("using") || !ctx.invoker->canDiscard(target, card->getEffectiveId())) disabled << card->getEffectiveId();
        const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard, disabled);
        if (id < 0 || !target->isAlive() || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || !ctx.invoker->canDiscard(target, id)) return false;
        const int place = room->getCardPlace(id);
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        const qint64 parent = room->currentHistoryEventId();
        room->throwCard(id, target, ctx.invoker);
        if (parent <= 0 || !before.value("error").toString().isEmpty() || !before.value("complete").toBool()) return false;
        QVariantMap query{{"after", before.value("watermark")}, {"from", target->objectName()}, {"to_place", int(Player::DiscardPile)}};
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
                if (move.value("card_id").toInt() == id && move.value("from_place").toInt() == place
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == parent) {
                    // Only an actual discard makes this recipient eligible for the optional damage.
                    ctx.extra_data = target->objectName(); return false;
                }
            }
            if (!page.value("has_more").toBool()) return false;
            query["watermark"] = page.value("watermark"); query["after"] = page.value("next_after");
        }
    }
};

TenyearyongjinCard::TenyearyongjinCard()
{
    target_fixed = true;
    setSkillName("tenyearyongjin");
}

void TenyearyongjinCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const
{
    // Legacy protocol cards are rebuilt by the native V2 active entry.
}

class Tenyearyongjin : public ViewAsSkillV2
{
public:
    Tenyearyongjin() : ViewAsSkillV2("tenyearyongjin") { frequency = Limited; limit_mark = "@tenyearyongjinMark"; setPhaseName("Play"); }
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearyongjinCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        QList<const Player *> players = request.initiator->getAliveSiblings(); players << request.initiator;
        for (const Player *player : players) if (!player->getEquips().isEmpty()) return true;
        return false;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return request.selectedCardIds.isEmpty() ? new TenyearyongjinCard : nullptr; }
    void syncDisplay(const SkillContext &ctx) const
    {
        if (!ctx.owner) return;
        int available = 0;
        for (int id : ctx.owner->getSkillInstanceIds(objectName())) {
            SkillContext copy = ctx; copy.instanceID = id;
            copy.activationRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(copy)) ++available;
        }
        ctx.owner->getRoom()->setPlayerMark(ctx.owner, limit_mark, available);
    }
    void addUsage(const SkillContext &ctx) const override { ViewAsSkillV2::addUsage(ctx); syncDisplay(ctx); }
    void resetUsage(const SkillContext &ctx) const override { ViewAsSkillV2::resetUsage(ctx); syncDisplay(ctx); }
    static QList<int> movable(Room *room, ServerPlayer *actor, ServerPlayer *from)
    {
        QList<int> result;
        for (const Card *card : from->getEquips())
            for (ServerPlayer *to : room->getAlivePlayers())
                if (TenyearJiewei::canTransfer(room, actor, from, to, card)) { result << card->getEffectiveId(); break; }
        return result;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.owner->getRoom();
        room->doSuperLightbox(ctx.owner, objectName());
        const int count = 3 * getEffectiveAmount(ctx);
        for (int i = 0; i < count && ctx.invoker && ctx.invoker->isAlive(); ++i) {
            QList<ServerPlayer *> sources;
            for (ServerPlayer *from : room->getAlivePlayers()) if (!movable(room, ctx.invoker, from).isEmpty()) sources << from;
            if (sources.isEmpty()) break;
            ServerPlayer *from = room->askForPlayerChosen(ctx.invoker, sources, objectName() + "_from", "@movefield-equip-from-optional", true);
            if (!from) break;
            SkillContext transfer = ctx; transfer.choice = "from";
            skillEffect(transfer, from);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !ctx.invoker || !ctx.invoker->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "to") {
            const QVariantMap info = ctx.extra_data.toMap();
            const int id = info.value("id", -1).toInt();
            if (id < 0) return ContinueEffects;
            ServerPlayer *from = room->findPlayerByObjectName(info.value("from").toString());
            const Card *card = Sanguosha->getCard(id);
            if (room->getCardPlace(id) == Player::PlaceEquip && TenyearJiewei::canTransfer(room, ctx.invoker, from, target, card))
                room->moveCardTo(card, from, target, Player::PlaceEquip,
                    CardMoveReason(CardMoveReason::S_REASON_TRANSFER, ctx.invoker->objectName(), objectName(), QString()), true);
            return ContinueEffects;
        }
        const QList<int> legal = movable(room, ctx.invoker, target);
        if (legal.isEmpty()) return ContinueEffects;
        QList<int> disabled;
        for (const Card *card : target->getEquips()) if (!legal.contains(card->getEffectiveId())) disabled << card->getEffectiveId();
        const int id = room->askForCardChosen(ctx.invoker, target, "e", objectName(), false, Card::MethodNone, disabled);
        if (!legal.contains(id)) return ContinueEffects;
        const Card *card = Sanguosha->getCard(id);
        QList<ServerPlayer *> recipients;
        for (ServerPlayer *to : room->getAlivePlayers())
            if (TenyearJiewei::canTransfer(room, ctx.invoker, target, to, card) && room->getCardPlace(id) == Player::PlaceEquip) recipients << to;
        if (recipients.isEmpty()) return ContinueEffects;
        ServerPlayer *to = room->askForPlayerChosen(ctx.invoker, recipients, objectName() + "_to", "@movefield-to:" + card->objectName());
        if (to) {
            // Both ends remain independently interceptable; commit rechecks the physical equipment and every occupied slot.
            SkillContext receive = ctx; receive.choice = "to";
            receive.extra_data = QVariantMap{{"from", target->objectName()}, {"id", id}};
            skillEffect(receive, to);
        }
        return ContinueEffects;
    }
};


class TenyearEnyuan : public TriggerSkillV2
{
public:
    TenyearEnyuan() : TriggerSkillV2("tenyearenyuan") { events << CardsMoveOneTime << Damaged; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            // Move events are broadcast: only the receiving holder offers its own copies.
            if (move.to == player && move.from && move.from->isAlive() && move.from != player
                && move.card_ids.size() >= 2 && move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE
                && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
                return {{player, {objectName()}}};
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from != player && damage.from->isAlive() && damage.damage > 0)
                return {{player, {objectName() + "*" + QString::number(damage.damage)}}};
        }
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = event == CardsMoveOneTime
            ? qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from)
            : ctx.original_data->value<DamageStruct>().from;
        if (!target || !target->isAlive()) return false;
        const bool hadFlag = target->hasFlag("TenyearEnyuanDrawTarget");
        if (event == CardsMoveOneTime) target->setFlags("TenyearEnyuanDrawTarget");
        auto restore = qScopeGuard([target, hadFlag, event] {
            if (event == CardsMoveOneTime && !hadFlag) target->setFlags("-TenyearEnyuanDrawTarget");
        });
        if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.choice = event == CardsMoveOneTime ? "gratitude" : "resentment";
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.choice == "gratitude" || ctx.choice == "draw") {
            target->drawCards(amount, objectName());
            return false;
        }
        if (ctx.choice == "give") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(receipt.value("giver").toString());
            const int id = receipt.value("card").toInt();
            if (!giver || !giver->isAlive() || id < 0 || room->getCardOwner(id) != giver
                || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return false;
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, giver->objectName(), target->objectName(), objectName(), "");
            reason.m_playerId = target->objectName();
            room->moveCardTo(Sanguosha->getCard(id), giver, target, Player::PlaceHand, reason, true);
            if (receipt.value("non_heart").toBool() && target->isAlive()) {
                SkillContext draw = ctx;
                draw.choice = "draw";
                skillEffect(event, room, ctx.owner, draw, target);
            }
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        const Card *card = nullptr;
        if (!target->isKongcheng()) {
            const bool hadTag = target->getTag("tenyearenyuan_data").isValid();
            const QVariant old = target->getTag("tenyearenyuan_data");
            target->setTag("tenyearenyuan_data", *ctx.original_data);
            auto restore = qScopeGuard([target, hadTag, old] {
                if (hadTag) target->setTag("tenyearenyuan_data", old); else target->removeTag("tenyearenyuan_data");
            });
            card = room->askForExchange(target, objectName(), 1, 1, false, "EnyuanGive::" + ctx.owner->objectName(), true);
        }
        const int id = card ? card->getEffectiveId() : -1;
        if (id >= 0 && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand
            && !Sanguosha->getCard(id)->hasFlag("using")) {
            SkillContext gift = ctx;
            gift.choice = "give";
            gift.extra_data = QVariantMap{{"giver", target->objectName()}, {"card", id},
                {"non_heart", Sanguosha->getCard(id)->getSuit() != Card::Heart}};
            skillEffect(event, room, ctx.owner, gift, ctx.owner);
        } else if (target->isAlive()) {
            room->loseHp(HpLostStruct(target, amount, objectName(), ctx.owner));
        }
        return false;
    }
};

TenyearXuanhuoCard::TenyearXuanhuoCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearXuanhuoCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	CardMoveReason r(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "tenyearxuanhuo", "");
	room->obtainCard(effect.to, this, r, false);

	if (effect.from->isDead()) return;
	QList<int> ava = room->getAvailableCardList(effect.to, "basic,trick", "tenyearxuanhuo");

	QStringList names;
	foreach (int id, ava) {
		const Card *card = Sanguosha->getEngineCard(id);
		if ((card->isKindOf("Slash") || card->isKindOf("Duel")) && !names.contains(card->objectName()))
			names << card->objectName();
	}

	DummyCard *handcards = effect.to->wholeHandCards();
	if (names.isEmpty() || room->alivePlayerCount() <= 2) {
		if (!effect.to->isKongcheng()) {
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
			room->obtainCard(effect.from, handcards, reason, false);
		}
	} else {
		QList<ServerPlayer *> targets;
		QList<ServerPlayer *> all_targets = room->getOtherPlayers(effect.to);
		if (all_targets.contains(effect.from))
			all_targets.removeOne(effect.from);
		foreach (QString name, names) {
			Card *card = Sanguosha->cloneCard(name);
			card->setSkillName("_tenyearxuanhuo");
			card->deleteLater();
			foreach (ServerPlayer *p, all_targets) {
				QList<ServerPlayer *> player_list;
				player_list << p;
				if (effect.to->canUse(card, player_list) && !targets.contains(p))
					targets << p;
			}
		}
		if (targets.isEmpty()) {
			if (!effect.to->isKongcheng()) {
				CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
				room->obtainCard(effect.from, handcards, reason, false);
			}
		} else {
			if (room->askForChoice(effect.to, "tenyearxuanhuo", "use+give", QVariant::fromValue(effect.from)) == "give") {
				if (!effect.to->isKongcheng()) {
					CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
					room->obtainCard(effect.from, handcards, reason, false);
				}
			} else {
				effect.from->setTag("tenyearxuanhuo_target", QVariant::fromValue(effect.to));
				ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "tenyearxuanhuo", "@tenyearxuanhuo-target:" + effect.to->objectName());
				effect.from->removeTag("tenyearxuanhuo_target");

				LogMessage log;
				log.type = "#TenyearxuanhuoTarget";
				log.from = effect.from;
				log.to << target;
				log.arg = "tenyearxuanhuo";
				room->sendLog(log);
				room->doAnimate(1, effect.from->objectName(), target->objectName());

				QStringList choices;
				foreach (QString name, names) {
					Card *card = Sanguosha->cloneCard(name);
					card->setSkillName("_tenyearxuanhuo");
					card->deleteLater();
					QList<ServerPlayer *> player_list;
					player_list << target;
					if (effect.to->canUse(card, player_list))
						choices << name;
				}
				if (choices.isEmpty()) {
					if (!effect.to->isKongcheng()) {
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "tenyearxuanhuo", "");
						room->obtainCard(effect.from, handcards, reason, false);
					}
				} else {
					QString choice = room->askForChoice(effect.to, "tenyearxuanhuo", choices.join("+"));
					Card *card = Sanguosha->cloneCard(choice);
					card->setSkillName("_tenyearxuanhuo");
					card->deleteLater();
					room->useCard(CardUseStruct(card, effect.to, target));
				}
			}
		}
	}
}

class TenyearXuanhuoVS : public ViewAsSkill
{
public:
	TenyearXuanhuoVS() : ViewAsSkill("tenyearxuanhuo")
	{
		response_pattern = "@@tenyearxuanhuo";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const {
		return selected.length() < 2 && !to_select->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 2)
			return nullptr;

		TenyearXuanhuoCard *c = new TenyearXuanhuoCard;
		c->addSubcards(cards);
		return c;
	}
};

class TenyearXuanhuo : public TriggerSkill
{
public:
	TenyearXuanhuo() : TriggerSkill("tenyearxuanhuo")
	{
		events << EventPhaseEnd;
		view_as_skill = new TenyearXuanhuoVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;
		if (player->getHandcardNum() < 2 || room->alivePlayerCount() < 3) return false;
		room->askForUseCard(player, "@@tenyearxuanhuo", "@tenyearxuanhuo");
		return false;
	}
};

class TenyearZhuikong : public TriggerSkillV2
{
public:
    TenyearZhuikong() : TriggerSkillV2("tenyearzhuikong") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->isWounded() && owner->canPindian(player))
                result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.owner->isWounded() || !ctx.owner->canPindian(ctx.invoker)
            || !ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.choice == "obtain") {
            const int id = ctx.extra_data.toInt();
            if (id >= 0 && room->getCardPlace(id) == Player::DiscardPile)
                room->obtainCard(target, id);
            return false;
        }
        if (ctx.choice == "slash") {
            ServerPlayer *attacker = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!attacker || !attacker->isAlive()) return false;
            std::unique_ptr<Slash> slash(new Slash(Card::NoSuit, 0)); slash->setSkillName("_tenyearzhuikong");
            if (!attacker->canSlash(target, slash.get(), false)) return false;
            CardUseStruct use(slash.get(), attacker, target); use.setOwnedCard(slash.release());
            room->useCardFromSkillEffect(use, ctx, true);
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        for (int n = 0; n < amount && target->isAlive() && ctx.owner->isAlive()
            && ctx.owner->canPindian(target); ++n) {
            const PindianStruct *contest = ctx.owner->PinDian(target, objectName());
            if (!contest) break;
            const bool won = contest->success;
            const int id = contest->to_card ? contest->to_card->getEffectiveId() : -1;
            if (won) {
                room->setPlayerFlag(target, "tenyearzhuikong");
            } else {
                // Freeze the contest card before obtain callbacks can start another contest.
                SkillContext obtain = ctx; obtain.choice = "obtain"; obtain.extra_data = id;
                skillEffect(event, room, player, obtain, ctx.owner);
                SkillContext retaliation = ctx; retaliation.choice = "slash"; retaliation.extra_data = target->objectName();
                skillEffect(event, room, player, retaliation, ctx.owner);
            }
        }
        return false;
    }
};
class TenyearZhuikongProhibit : public ProhibitSkill
{
public:
	TenyearZhuikongProhibit() : ProhibitSkill("#tenyearzhuikong")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if (card->getTypeId() != Card::TypeSkill && from->hasFlag("tenyearzhuikong"))
			return to != from;
		return false;
	}
};

class TenyearQiuyuan : public TriggerSkillV2
{
public:
    TenyearQiuyuan() : TriggerSkillV2("tenyearqiuyuan") { events << TargetConfirming; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.from && use.card
            && use.card->isKindOf("Slash") && use.to.contains(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (other != use.from && !use.to.contains(other)) candidates << other;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@tenyearqiuyuan-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "give") {
            const QVariantMap gift = ctx.extra_data.toMap();
            ServerPlayer *donor = room->findPlayerByObjectName(gift.value("donor").toString(), true);
            const int id = gift.value("id").toInt();
            if (donor && donor->handCards().contains(id) && !Sanguosha->getCard(id)->hasFlag("using"))
                room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_GIVE,
                    donor->objectName(), target->objectName(), objectName(), QString()));
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        const Card *card = nullptr;
        if (!target->isKongcheng()) {
            const QVariant previous = target->getTag("tenyearqiuyuan_from");
            const auto restore = qScopeGuard([&] { target->setTag("tenyearqiuyuan_from", previous); });
            target->setTag("tenyearqiuyuan_from", QVariant::fromValue(ctx.owner));
            card = room->askForCard(target, "BasicCard+^Slash", "@tenyearqiuyuan-give:" + ctx.owner->objectName(),
                *ctx.original_data, Card::MethodNone);
        }
        if (card && target->handCards().contains(card->getEffectiveId()) && !card->hasFlag("using")
            && card->isKindOf("BasicCard") && !card->isKindOf("Slash")) {
            ctx.choice = "give";
            ctx.extra_data = QVariantMap{{"donor", target->objectName()}, {"id", card->getEffectiveId()}};
            skillEffect(event, room, ctx.owner, ctx, ctx.owner);
        } else {
            // Keep target mutations made by nested responses instead of restoring the initial use snapshot.
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.from || !use.card || use.to.contains(target) || !use.from->canSlash(target, use.card, false)) return false;
            LogMessage log;
            log.type = "#BecomeTarget";
            log.from = target;
            log.card_str = use.card->toString();
            room->sendLog(log);
            use.to << target;
            room->sortByActionOrder(use.to);
            if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
            ctx.original_data->setValue(use);
        }
        return false;
    }
};

TenyearSidiCard::TenyearSidiCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearSidiCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "tenyearsidi", "");
	room->throwCard(this, reason, nullptr);
}

class TenyearSidiVS : public OneCardViewAsSkill
{
public:
	TenyearSidiVS() : OneCardViewAsSkill("tenyearsidi")
	{
		expand_pile = "sidi";
		filter_pattern = ".|.|.|sidi";
		response_pattern = "@@tenyearsidi";
	}

	const Card *viewAs(const Card *card) const
	{
		TenyearSidiCard *c = new TenyearSidiCard;
		c->addSubcard(card);
		return c;
	}
};

class TenyearSidi : public TriggerSkill
{
public:
	TenyearSidi() : TriggerSkill("tenyearsidi")
	{
		events << EventPhaseStart << EventPhaseEnd << PreCardUsed;
		view_as_skill = new TenyearSidiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead()) return false;
			if (player->getPhase()==Player::Finish&&player->hasSkill(objectName())&&!player->isNude()) {
				const Card *card = room->askForCard(player, "^BasicCard", "@tenyearsidi-put", data, Card::MethodNone, nullptr, false, objectName());
				if (card){
					room->broadcastSkillInvoke(objectName());
					player->addToPile("sidi", card);
				}
			}
			if (player->getPhase() == Player::Play) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (!p->hasSkill(objectName()) || p->getPile("sidi").isEmpty()) continue;
					const Card *card = room->askForUseCard(p, "@@tenyearsidi", "@tenyearsidi:" + player->objectName(), -1, Card::MethodNone);
					if (!card) continue;
					room->addPlayerMark(player, "&tenyearsidi+" + card->getColorString() + "-PlayClear");
					p->addMark(player->objectName()+"tenyearsidiUse-PlayClear");
				}
			}
		} else if (player->getPhase() == Player::Play){
			if (event == PreCardUsed){
				const Card *card = data.value<CardUseStruct>().card;
				if(card->isKindOf("TrickCard"))
					player->addMark("tenyearsidiTrick-PlayClear");
				else if(card->isKindOf("Slash"))
					player->addMark("tenyearsidiSlash-PlayClear");
			}else{
				bool tenyearsidiTrick=player->getMark("tenyearsidiTrick-PlayClear")<1,tenyearsidiSlash=player->getMark("tenyearsidiSlash-PlayClear")<1;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->getMark(player->objectName()+"tenyearsidiUse-PlayClear")>0){
						bool send = false;
						if (tenyearsidiTrick) {
							Slash *slash = new Slash(Card::NoSuit, 0);
							slash->setSkillName("_tenyearsidi");
							if (p->canSlash(player, slash, false)){
								room->sendCompulsoryTriggerLog(p, objectName());
								room->useCard(CardUseStruct(slash, p, player));
								send = true;
							}
							slash->deleteLater();
						}
						if (tenyearsidiSlash) {
							if (p->isDead()) continue;
							if (!send) room->sendCompulsoryTriggerLog(p, objectName());
							p->drawCards(2, objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

class TenyearSidiLimit : public CardLimitSkill
{
public:
	TenyearSidiLimit() : CardLimitSkill("#tenyearsidi-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target,const Card *card) const
	{
		if (target->getMark("&tenyearsidi+"+card->getColorString()+"-PlayClear")>0)
			return card->toString();
		return "";
	}
};

TenyearHuaiyiCard::TenyearHuaiyiCard()
{
	setSkillName("tenyearhuaiyi");
	target_fixed = true;
}

void TenyearHuaiyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isKongcheng()) return;
	room->showAllCards(source, static_cast<ServerPlayer *>(nullptr));

	QList<int> blacks;
	QList<int> reds;
	foreach (const Card *c, source->getHandcards()) {
		if (c->isRed())
			reds << c->getId();
		else
			blacks << c->getId();
	}

	if (reds.isEmpty() || blacks.isEmpty()) {
		source->drawCards(1, "tenyearhuaiyi");
		room->addPlayerMark(source, "tenyearhuaiyi-PlayClear");
		return;
	}

	QString to_discard = room->askForChoice(source, "tenyearhuaiyi", "black+red");
	QList<int> *pile = nullptr;
	if (to_discard == "black")
		pile = &blacks;
	else
		pile = &reds;

	int n = pile->length();

	room->setPlayerMark(source, "tenyearhuaiyi_num-PlayClear", n);

	DummyCard dm(*pile);
	room->throwCard(&dm, source);

	room->askForUseCard(source, "@@tenyearhuaiyi", "@tenyearhuaiyi:" + QString::number(n), -1, Card::MethodNone);
}

TenyearHuaiyiSnatchCard::TenyearHuaiyiSnatchCard()
{
	handling_method = Card::MethodNone;
	m_skillName = "tenyearhuaiyi";
}

bool TenyearHuaiyiSnatchCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	int n = Self->getMark("tenyearhuaiyi_num-PlayClear");
	if (targets.length() >= n)
		return false;

	if (to_select == Self)
		return false;

	if (to_select->isNude())
		return false;

	return true;
}

void TenyearHuaiyiSnatchCard::onUse(Room *room, CardUseStruct &card_use) const
{
	ServerPlayer *player = card_use.from;

	QList<ServerPlayer *> to = card_use.to;

	room->sortByActionOrder(to);

	int get = 0;
	foreach (ServerPlayer *p, to) {
		if (player->isDead()) return;
		if (p->isDead() || p->isNude()) continue;
		int id = room->askForCardChosen(player, p, "he", "tenyearhuaiyi");
		player->obtainCard(Sanguosha->getCard(id), false);
		get++;
	}

	if (get >= 2)
		room->loseHp(HpLostStruct(player, 1, "tenyearhuaiyi", player));
}

class TenyearHuaiyi : public ViewAsSkillV2
{
public:
    TargetMode targetMode() const override { return NoTarget; }
    TenyearHuaiyi() : ViewAsSkillV2("tenyearhuaiyi") {}
    bool commitQuota(SkillContext &ctx) const
    {
        const QString key = objectName() + "_quota";
        if (ctx.interceptor_data.value(key).value("committed").toBool()) return true;
        if (!isUsable(ctx)) return false;
        QVariantMap receipt{{"committed", true}};
        receipt.insert("phase", ctx.owner->getRoom()->historyScopes().value("phase_id"));
        ctx.interceptor_data.insert(key, receipt);
        addUsage(ctx);
        return true;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearHuaiyiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const QString phase = ctx.owner->getRoom()->historyScopes().value("phase_id").toString();
        if (phase.isEmpty() || phase == "0") return false;
        const QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
        return state.value("phase").toString() != phase
            || state.value("count").toInt() < (state.value("extra").toBool() ? 2 : 1);
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const QString phase = ctx.owner->getRoom()->historyScopes().value("phase_id").toString();
        QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
        if (state.value("phase").toString() != phase) state.clear();
        state["phase"] = phase; state["count"] = state.value("count").toInt() + 1;
        ctx.owner->setSkillInstanceState(objectName(), ctx.instanceID, state);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { return commitQuota(ctx); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.extra_data = ctx.interceptor_data.value(objectName() + "_quota").value("phase");
        SkillContext reveal = ctx; reveal.choice = "reveal";
        skillEffect(reveal, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        if (ctx.choice == "draw") { target->drawCards(amount, objectName()); return ContinueEffects; }
        if (ctx.choice == "lose_hp") {
            room->loseHp(HpLostStruct(target, amount, objectName(), ctx.invoker));
            return ContinueEffects;
        }
        if (ctx.choice == "obtain") {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName());
            if (id < 0 || room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand
                && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            SkillContext receive = ctx; receive.choice = "receive";
            receive.extra_data = QVariantMap{{"card", id}, {"giver", target->objectName()}};
            skillEffect(receive, ctx.invoker);
            ctx.extra_data = receive.extra_data.metaType().id() == QMetaType::Bool && receive.extra_data.toBool();
            return ContinueEffects;
        }
        if (ctx.choice == "receive") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            const int id = receipt.value("card").toInt();
            ServerPlayer *giver = room->findPlayerByObjectName(receipt.value("giver").toString());
            ctx.extra_data = false;
            if (giver && id >= 0 && room->getCardOwner(id) == giver
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using")) {
                room->obtainCard(target, id, false);
                ctx.extra_data = true;
            }
            return ContinueEffects;
        }
        if (target->isKongcheng()) return ContinueEffects;
        QList<int> red, black;
        for (const Card *card : target->getHandcards())
            (card->isRed() ? red : black) << card->getEffectiveId();
        // Freeze what is shown; ShowCards callbacks may replace the entire hand.
        room->showAllCards(target, static_cast<ServerPlayer *>(nullptr));
        if (!target->isAlive()) return ContinueEffects;
        if (red.isEmpty() || black.isEmpty()) {
            QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
            if (state.value("phase").toString() == ctx.extra_data.toString()) {
                state["extra"] = true;
                ctx.owner->setSkillInstanceState(objectName(), ctx.instanceID, state);
            }
            SkillContext draw = ctx; draw.choice = "draw";
            skillEffect(draw, target);
            return ContinueEffects;
        }
        const QString color = room->askForChoice(target, objectName(), "black+red");
        if (color != "black" && color != "red") return ContinueEffects;
        QList<int> payment;
        for (int id : color == "red" ? red : black)
            if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand
                && !Sanguosha->getCard(id)->hasFlag("using") && target->canDiscard(target, id)) payment << id;
        if (payment.isEmpty()) return ContinueEffects;
        DummyCard discard(payment);
        room->throwCard(&discard, target);
        if (!target->isAlive()) return ContinueEffects;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(target)) if (!other->isNude()) candidates << other;
        if (candidates.isEmpty()) return ContinueEffects;
        QList<ServerPlayer *> selected = room->askForPlayersChosen(target, candidates, objectName(), 0,
            qMin(payment.size(), candidates.size()), "@tenyearhuaiyi:" + QString::number(payment.size()));
        room->sortByActionOrder(selected);
        int obtained = 0;
        for (ServerPlayer *other : selected) {
            if (!target->isAlive()) break;
            if (!other->isAlive() || other->isNude()) continue;
            SkillContext take = ctx; take.choice = "obtain"; take.extra_data = false;
            skillEffect(take, other);
            if (take.extra_data.toBool()) ++obtained;
        }
        if (obtained >= 2 && target->isAlive()) {
            SkillContext lose = ctx; lose.choice = "lose_hp";
            skillEffect(lose, target);
        }
        return ContinueEffects;
    }
};


class TenyearHuaiyiQuota : public TriggerSkillV2
{
public:
    TenyearHuaiyiQuota() : TriggerSkillV2("#tenyearhuaiyi-quota")
    { events << EventSkillInvoking; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        SkillContext active = data.value<SkillContext>();
        if (active.bypass_cost && active.owner && active.activationRef.isValid()
            && active.activationRef.key.skillName == "tenyearhuaiyi"
            && active.activationRef.ownerObjectName == active.owner->objectName()
            && active.activationRef.key.instanceID == active.instanceID) {
            const auto *skill = dynamic_cast<const TenyearHuaiyi *>(Sanguosha->getSkill("tenyearhuaiyi"));
            if (!skill || !skill->commitQuota(active)) active.is_canceled = true;
            data = QVariant::fromValue(active);
        }
        return true;
    }
};
class Tenyearjueqing : public TriggerSkill
{
public:
	Tenyearjueqing() : TriggerSkill("tenyearjueqing")
	{
		events << DamageCaused << Predamage;
	}

	Frequency getFrequency(const Player *target) const
	{
		if (target != nullptr) {
			return target->getMark("tenyearjueqing") <= 0 ? NotFrequent : Compulsory;
		}
		return Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (event == DamageCaused) {
			if (player->getMark(objectName()) > 0) return false;
			if (damage.to->isDead()) return false;
			player->setTag("tenyearjueqing_data", data);
			if (!player->askForSkillInvoke(objectName(), damage.to)) {
				player->removeTag("tenyearjueqing_data");
				return false;
			};
			player->removeTag("tenyearjueqing_data");
			room->broadcastSkillInvoke(objectName());
			room->loseHp(HpLostStruct(player, damage.damage, "tenyearjueqing", player));

			damage.damage = 2 * damage.damage;
			damage.tips << "tenyearjueqing_" + damage.to->objectName();
			data = QVariant::fromValue(damage);
		} else {
			if (player->getMark(objectName()) <= 0) return false;
			if (damage.to->isDead()) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			//room->loseHp(HpLostStruct(damage.to, damage.damage, "tenyearjueqing", player, damage.ignore_hujia));
			room->loseHp(HpLostStruct(damage.to, damage.damage, "tenyearjueqing", player));
			return true;
		}
		return false;
	}
};

class TenyearjueqingComplete : public TriggerSkill
{
public:
	TenyearjueqingComplete() : TriggerSkill("#tenyearjueqing")
	{
		events << DamageComplete;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.from || damage.from->isDead() || !damage.from->hasSkill("tenyearjueqing", true) ||
				!damage.tips.contains("tenyearjueqing_" + damage.to->objectName()) || damage.from->getMark("tenyearjueqing") > 0) return false;
		room->setPlayerMark(damage.from, "tenyearjueqing", 1);
		QString translate = Sanguosha->translate(":tenyearjueqing2");
		room->changeTranslation(damage.from, "tenyearjueqing", translate);
		return false;
	}
};

TenyearGongqiCard::TenyearGongqiCard()
{
    setSkillName("tenyeargongqi");
	target_fixed = true;
	handling_method = Card::MethodDiscard;
}

void TenyearGongqiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->setPlayerFlag(source, "InfinityAttackRange");
	const Card *cd = Sanguosha->getCard(subcards.first());
	room->setPlayerMark(source, "tenyeargongqi_slash_" + cd->getSuitString() + "-Clear", 1);
	if (cd->isKindOf("EquipCard")) {
		QList<ServerPlayer *> targets;
		foreach(ServerPlayer *p, room->getOtherPlayers(source))
			if (source->canDiscard(p, "he")) targets << p;
		if (!targets.isEmpty()) {
			ServerPlayer *to_discard = room->askForPlayerChosen(source, targets, "tenyeargongqi", "@gongqi-discard", true);
			if (to_discard)
				room->throwCard(room->askForCardChosen(source, to_discard, "he", "tenyeargongqi", false, Card::MethodDiscard),
								to_discard, source);
		}
	}
}

class TenyearGongqi : public ViewAsSkillV2
{
public:
    TenyearGongqi() : ViewAsSkillV2("tenyeargongqi", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearGongqiCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card))
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0
            || request.selectedCardIds.first() >= Sanguosha->getCardCount()) return false;
        ActiveSkillRequest empty = request; empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        // Wrapped suit/type may change during discard callbacks; preserve the paid declaration.
        ctx.extra_data = QVariantMap{{"suit", card->getSuitString()}, {"equipment", card->isKindOf("EquipCard")}};
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "discard") {
            for (int n = 0; n < amount && target->isAlive() && ctx.invoker->isAlive()
                && ctx.invoker->canDiscard(target, "he"); ++n) {
                const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
                if (id < 0 || room->getCardOwner(id) != target
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                    || Sanguosha->getCard(id)->hasFlag("using") || !ctx.invoker->canDiscard(target, id)) break;
                room->throwCard(id, target, ctx.invoker);
            }
            return ContinueEffects;
        }
        const QVariantMap material = ctx.extra_data.toMap();
        if (material.value("suit").toString().isEmpty()) return ContinueEffects;
        const QString turn = room->historyScopes().value("turn_id").toString();
        if (turn.isEmpty() || turn == "0") return ContinueEffects;
        const QString helper = "#tenyeargongqi-target";
        const int grant = room->acquireSkillFromEffect(target, helper, ctx, [&](int id) {
            // Store expiry before the acquire notification can re-enter a turn boundary.
            QVariantList receipts = target->getTag("tenyeargongqi_grants").toList();
            receipts << QVariantMap{{"id", id}, {"turn", turn}};
            target->setTag("tenyeargongqi_grants", receipts);
        }, true, false, false);
        if (grant <= 0 || !target->hasSkillInstance(helper, grant)) return ContinueEffects;
        const SkillInstanceRef ref(target->objectName(), SkillInstanceKey(helper, grant));
        if (!room->setSkillInstanceCorrectState(target, ref, "effect",
            QVariantMap{{"suit", material.value("suit")}, {"amount", amount}})) return ContinueEffects;
        if (!target->hasSkillInstance(helper, grant)) return ContinueEffects;
        room->setPlayerFlag(target, "InfinityAttackRange");
        room->setPlayerMark(target, "tenyeargongqi_slash_" + material.value("suit").toString() + "-Clear", 1);
        if (material.value("equipment").toBool()) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker))
                if (ctx.invoker->canDiscard(other, "he")) candidates << other;
            if (!candidates.isEmpty()) {
                ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@gongqi-discard", true);
                if (victim) { SkillContext discard = ctx; discard.choice = "discard"; skillEffect(discard, victim); }
            }
        }
        return ContinueEffects;
    }
};
class TenyearGongqiTargetMod : public TargetModSkillV2
{
public:
    TenyearGongqiTargetMod() : TargetModSkillV2("#tenyeargongqi-target") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // Native related copies have no applied state; each accepted independent grant owns its receipt.
        const QVariantMap effect = ctx.getStateValue("effect").toMap();
        return ctx.modType == Residue && ctx.card && ctx.currentAmount > 0 && effect.value("amount").toInt() > 0
            && effect.value("suit").toString() == ctx.card->getSuitString()
            ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};

TenyearJiefanCard::TenyearJiefanCard()
{
}

bool TenyearJiefanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void TenyearJiefanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if (room->getTag("TurnLengthCount").toInt() == 1)
		room->addPlayerMark(source, "tenyearjiefan_reflash-Clear");

	room->removePlayerMark(source, "@tenyearjiefanMark");
	ServerPlayer *target = targets.first();
	source->setTag("TenyearJiefanTarget", QVariant::fromValue(target));
	room->doSuperLightbox(source, "tenyearjiefan");

	foreach (ServerPlayer *player, room->getAllPlayers()) {
		if (player->isAlive() && player->inMyAttackRange(target))
			room->cardEffect(this, source, player);
	}
	source->removeTag("TenyearJiefanTarget");
}

void TenyearJiefanCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();

	ServerPlayer *target = effect.from->getTag("TenyearJiefanTarget").value<ServerPlayer *>();
	QVariant data = effect.from->getTag("TenyearJiefanTarget");
	if (target && !room->askForCard(effect.to, ".Weapon", "@jiefan-discard::" + target->objectName(), data))
		target->drawCards(1, "tenyearjiefan");
}

class TenyearJiefanVS : public ZeroCardViewAsSkill
{
public:
	TenyearJiefanVS() : ZeroCardViewAsSkill("tenyearjiefan")
	{
		frequency = Limited;
		limit_mark = "@tenyearjiefanMark";
	}

	const Card *viewAs() const
	{
		return new TenyearJiefanCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@tenyearjiefanMark") >= 1;
	}
};

class TenyearJiefan : public TriggerSkill
{
public:
	TenyearJiefan() : TriggerSkill("tenyearjiefan")
	{
		events << EventPhaseChanging;
		frequency = Limited;
		limit_mark = "@tenyearjiefanMark";
		view_as_skill = new TenyearJiefanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		if (player->getMark("tenyearjiefan_reflash-Clear") <= 0) return false;
		//if (player->getMark("@tenyearjiefanMark") > 0) return false;
		room->addPlayerMark(player, "@tenyearjiefanMark");
		return false;
	}
};

class TenyearZhongyong : public TriggerSkill
{
public:
	TenyearZhongyong() : TriggerSkill("tenyearzhongyong")
	{
		events << CardOffset << CardFinished;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardOffset) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->isKindOf("Slash")) return false;
			QVariantList slash = effect.from->getTag("tenyearzhongyong_slash" + effect.card->toString()).toList();
			if (effect.card->isVirtualCard() && effect.card->subcardsLength() > 0) {
				foreach (int id, effect.card->getSubcards()) {
					if (slash.contains(QVariant(id))) continue;
					slash << id;
				}
			} else if (!effect.card->isVirtualCard()) {
				if (!slash.contains(QVariant(effect.card->getEffectiveId())))
					slash << effect.card->getEffectiveId();
			}
			player->setTag("tenyearzhongyong_slash" + effect.card->toString(), slash);

			if (!effect.offset_card) return false;
			QVariantList jink = player->getTag("tenyearzhongyong_jink" + effect.card->toString()).toList();
			if (effect.offset_card->isVirtualCard() && effect.offset_card->subcardsLength() > 0) {
				foreach (int id, effect.offset_card->getSubcards()) {
					if (jink.contains(QVariant(id))) continue;
					jink << id;
				}
			} else if (!effect.offset_card->isVirtualCard()) {
				if (!jink.contains(QVariant(effect.offset_card->getEffectiveId())))
					jink << effect.offset_card->getEffectiveId();
			}
			effect.from->setTag("tenyearzhongyong_jink" + effect.card->toString(), jink);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")) return false;

			QVariantList slash = player->getTag("tenyearzhongyong_slash" + use.card->toString()).toList();
			QVariantList jink = player->getTag("tenyearzhongyong_jink" + use.card->toString()).toList();
			QList<int> slash_ids = ListV2I(slash);
			QList<int> jink_ids = ListV2I(jink);

			foreach (int id, slash_ids) {
				if (room->getCardPlace(id) != Player::DiscardPile)
					slash_ids.removeOne(id);
			}
			foreach (int id, jink_ids) {
				if (room->getCardPlace(id) != Player::DiscardPile)
					jink_ids.removeOne(id);
			}

			QList<int> give_list = slash_ids + jink_ids;
			if (give_list.isEmpty()) return false;

			room->fillAG(give_list, player);
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@tenyearzhongyong-invoke",
								true, true);
			room->clearAG(player);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());

			room->giveCard(player, target, give_list, objectName(), true);

			if (target->isDead()) return false;
			bool red = false, black = false;
			foreach (int id, give_list) {
				const Card *card = Sanguosha->getCard(id);
				if (card->isRed())
					red = true;
				else if (card->isBlack())
					black = true;
				if (red && black)
					break;
			}

			if (red) {
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (player->inMyAttackRange(p) && target->canSlash(p, nullptr, true))
						tos << p;
				}
				if (!tos.isEmpty())
					room->askForUseSlashTo(target, tos, "@ol_zhongyong-slash");
			}

			if (black && target->isAlive())
				target->drawCards(1, objectName());
		}
		return false;
	}
};

class TenyearJigong : public TriggerSkillV2
{
public:
    TenyearJigong() : TriggerSkillV2("tenyearjigong") { events << EventPhaseStart; }
    static int damageInPhase(Room *room, const QString &holder, const QVariant &phase)
    {
        if (!phase.toLongLong()) return -1;
        QVariantMap filter{{"from", holder}, {"phase_id", phase}};
        QSet<qlonglong> seen;
        int damage = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap();
                const qlonglong event = fact.value("event_id").toLongLong();
                if (seen.contains(event)) continue;
                seen.insert(event); damage += fact.value("data").toMap().value("amount").toInt();
            }
            if (!page.value("has_more").toBool()) return damage;
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        }
    }
    static int drawnCards(Room *room, const QVariantMap &receipt)
    {
        if (!receipt.value("draw_event").toLongLong()) return -1;
        QVariantMap filter{{"to", receipt.value("holder")}, {"after", receipt.value("draw_after")}};
        QSet<QString> seen;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
                if (move.value("to_place").toInt() != Player::PlaceHand || move.value("reason_skill").toString() != "tenyearjigong"
                    || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DRAW
                    || room->historyParent(fact.value("event_id").toLongLong(), "draw", true).value("id") != receipt.value("draw_event")) continue;
                seen.insert(fact.value("event_id").toString() + ":" + move.value("card_id").toString());
            }
            if (!page.value("has_more").toBool()) return seen.size();
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        }
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        const QVariant turn = room->historyScopes().value("turn_id");
        if (!phase.toLongLong() || !turn.toLongLong() || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "1+2+3");
        if (ctx.choice.toInt() < 1 || ctx.choice.toInt() > 3) return false;
        ctx.extra_data = QVariantMap{{"phase", phase}, {"turn", turn}};
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        const QVariantMap scope = ctx.extra_data.toMap();
        if (amount <= 0 || scope.value("turn") != room->historyScopes().value("turn_id")
            || scope.value("phase") != room->historyScopes().value("phase_id")) return false;
        const QString helper = "#tenyearjigong";
        QVariantList kept; QList<int> retired;
        for (const QVariant &value : target->getTag("tenyearjigong_receipts").toList()) {
            const QVariantMap previous = value.toMap();
            if (previous.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
                && previous.value("activation_instance").toInt() == ctx.activationRef.key.instanceID
                && previous.value("turn") == scope.value("turn")) retired << previous.value("grant").toInt();
            else kept << value;
        }
        target->setTag("tenyearjigong_receipts", kept);
        for (int id : retired) room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(helper, id), false, true, false);
        if (!target->isAlive() || scope.value("phase") != room->historyScopes().value("phase_id")) return false;
        const int grant = room->acquireSkillFromEffect(target, helper, ctx, [&](int id) {
            // The hand-limit and later recovery share a primitive receipt installed before acquire/draw callbacks.
            QVariantList receipts = target->getTag("tenyearjigong_receipts").toList();
            receipts << QVariantMap{{"grant", id}, {"phase", scope.value("phase")}, {"turn", scope.value("turn")},
                {"holder", target->objectName()}, {"actor", ctx.invoker->objectName()}, {"amount", amount},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_instance", ctx.activationRef.key.instanceID}};
            target->setTag("tenyearjigong_receipts", receipts);
        }, true, false, false);
        if (grant <= 0 || !target->hasSkillInstance(helper, grant)) return false;
        const SkillInstanceRef ref(target->objectName(), SkillInstanceKey(helper, grant));
        const int damage = damageInPhase(room, target->objectName(), scope.value("phase"));
        if (!room->setSkillInstanceCorrectState(target, ref, "effect",
            QVariantMap{{"phase", scope.value("phase")}, {"amount", amount}, {"damage", damage}})) return false;
        if (!target->hasSkillInstance(helper, grant)) return false;
        room->broadcastSkillInvoke(objectName());
        // Snapshot after grant callbacks: only this direct draw belongs to this application.
        const QVariantMap before = room->queryHistoryFacts({{"limit", 1}});
        const qint64 parent = room->currentHistoryEventId();
        target->drawCards(ctx.choice.toInt() * amount, objectName());
        if (parent <= 0 || !before.value("error").toString().isEmpty() || !before.value("complete").toBool()) return false;
        QVariantMap query{{"kind", "draw_result"}, {"after", before.value("watermark")}};
        QSet<qint64> draws;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), result = fact.value("data").toMap();
                const qint64 id = fact.value("event_id").toLongLong();
                if (result.value("player").toString() == target->objectName() && result.value("reason").toString() == objectName()
                    && room->historyEvent(id).value("parent_id").toLongLong() == parent) draws.insert(id);
            }
            if (!page.value("has_more").toBool()) break;
            query["watermark"] = page.value("watermark"); query["after"] = page.value("next_after");
        }
        if (draws.size() != 1) return false;
        QVariantList receipts = target->getTag("tenyearjigong_receipts").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("grant").toInt() != grant) continue;
            // Update only a still-live receipt; nested expiry or renewal must never be undone.
            receipt["draw_after"] = before.value("watermark"); receipt["draw_event"] = *draws.constBegin();
            receipts[i] = receipt;
            target->setTag("tenyearjigong_receipts", receipts);
            break;
        }
        return false;
    }
};

class TenyearJigongMax : public MaxCardsSkillV2
{
public:
    TenyearJigongMax() : MaxCardsSkillV2("#tenyearjigong") {}
    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        const QVariantMap effect = ctx.getStateValue("effect").toMap();
        if (!ctx.holder || effect.isEmpty() || effect.value("amount").toInt() <= 0 || ctx.currentAmount <= 0)
            return CorrectSkillResult::noEffect();
        int damage = effect.value("damage", -1).toInt();
        if (const ServerPlayer *holder = qobject_cast<const ServerPlayer *>(ctx.holder))
            damage = TenyearJigong::damageInPhase(holder->getRoom(), holder->objectName(), effect.value("phase"));
        return damage < 0 ? CorrectSkillResult::noEffect() : CorrectSkillResult::useAmount(damage * ctx.currentAmount);
    }
};

class TenyearJigongRecover : public TriggerSkillV2
{
public:
    TenyearJigongRecover() : TriggerSkillV2("#tenyearjigong-recover")
    { events << EventPhaseStart << EventPhaseChanging << Damage << Death << TurnBroken; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (!player || damage.from != player) return true;
            for (const QVariant &value : player->getTag("tenyearjigong_receipts").toList()) {
                const QVariantMap receipt = value.toMap();
                const int count = TenyearJigong::damageInPhase(room, player->objectName(), receipt.value("phase"));
                room->setSkillInstanceCorrectState(player,
                    SkillInstanceRef(player->objectName(), SkillInstanceKey("#tenyearjigong", receipt.value("grant").toInt())), "effect",
                    QVariantMap{{"phase", receipt.value("phase")}, {"amount", receipt.value("amount")}, {"damage", count}});
            }
            return true;
        }
        ServerPlayer *dead = event == Death ? data.value<DeathStruct>().who : nullptr;
        if (event == Death && (!dead || player != dead)) return true;
        if (event != Death && event != TurnBroken
            && (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)) return true;
        const QVariant turn = room->historyScopes().value("turn_id");
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            if (dead && dead != holder) continue;
            QVariantList kept; QList<int> expired;
            for (const QVariant &value : holder->getTag("tenyearjigong_receipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (dead || receipt.value("turn") == turn) expired << receipt.value("grant").toInt();
                else kept << value;
            }
            holder->setTag("tenyearjigong_receipts", kept);
            for (int id : expired) room->detachSkillFromPlayer(holder, SkillInstanceUtils::formatName("#tenyearjigong", id), false, true, false);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->isWounded() || player->getPhase() != Player::Discard) return true;
        for (const QVariant &value : player->getTag("tenyearjigong_receipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
            const int drawn = TenyearJigong::drawnCards(room, receipt);
            const int damage = TenyearJigong::damageInPhase(room, player->objectName(), receipt.value("phase"));
            if (drawn < 0 || damage < drawn) continue;
            SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("activation_owner").toString(), true);
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            if (!ctx.owner || !ctx.initiator) continue;
            ctx.instanceID = receipt.value("grant").toInt(); ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.targets = {player};
            ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
        return holder && holder->isAlive() && receipt.value("turn") == room->historyScopes().value("turn_id")
            && holder->getTag("tenyearjigong_receipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (isSourceAvailable(room, ctx) && target->isWounded() && getEffectiveAmount(ctx) > 0) {
            room->sendCompulsoryTriggerLog(ctx.initiator, "tenyearjigong", true, true);
            room->recover(target, RecoverStruct("tenyearjigong", ctx.initiator, getEffectiveAmount(ctx)));
        }
        return false;
    }
};

class TenyearJiezhong : public TriggerSkillV2
{
public:
    TenyearJiezhong() : TriggerSkillV2("tenyearjiezhong")
    { events << EventPhaseStart << EventSkillInvoking; global = true; frequency = Limited; limit_mark = "@tenyearjiezhongMark"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    static SkillContext quotaContext(ServerPlayer *owner, int id)
    {
        SkillContext quota;
        quota.skill_name = "tenyearjiezhong";
        quota.owner = quota.invoker = quota.initiator = owner;
        quota.instanceID = id;
        quota.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(quota.skill_name, id));
        quota.sourceRef = SkillInstanceUtils::resolveRootRef(quota.activationRef,
            [owner](const SkillInstanceRef &ref) -> const SkillInstance * {
                ServerPlayer *holder = owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
                return holder ? holder->findSkillInstance(ref.key.skillName, ref.key.instanceID) : nullptr;
            });
        return quota;
    }
    static void syncDisplay(Room *room, ServerPlayer *owner)
    {
        const Skill *skill = Sanguosha->getSkill("tenyearjiezhong");
        if (!skill) return;
        int available = 0;
        for (int id : owner->getSkillInstanceIds("tenyearjiezhong"))
            if (skill->isUsable(quotaContext(owner, id))) ++available;
        room->setPlayerMark(owner, "@tenyearjiezhongMark", available);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        // The dispatcher supplies the exact candidate copy before any invocation prompt.
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    bool commitQuota(SkillContext &ctx) const
    {
        const QString key = objectName() + "_quota";
        if (ctx.interceptor_data.value(key).value("committed").toBool()) return true;
        if (!isUsable(ctx)) return false;
        // Commit before Invoking/Effect callbacks can re-enter this exact copy.
        ctx.interceptor_data.insert(key, QVariantMap{{"committed", true}});
        addUsage(ctx);
        syncDisplay(ctx.owner->getRoom(), ctx.owner);
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return commitQuota(ctx); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext active = data.value<SkillContext>();
        if (active.bypass_cost && active.owner && active.activationRef.isValid()
            && active.activationRef.key.skillName == objectName()
            && active.activationRef.ownerObjectName == active.owner->objectName()
            && active.activationRef.key.instanceID == active.instanceID) {
            if (!commitQuota(active)) active.is_canceled = true;
            data = QVariant::fromValue(active);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            && player->getMaxHp() > player->getHandcardNum() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Positive repeats share the fixed endpoint; zero amount suppresses this recipient.
        if (getEffectiveAmount(ctx) > 0)
            target->drawCards(qMax(0, target->getMaxHp() - target->getHandcardNum()), objectName());
        return false;
    }
};

class TenyearLongyin : public TriggerSkillV2
{
public:
    TenyearLongyin() : TriggerSkillV2("tenyearlongyin") { events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || player->getPhase() != Player::Play || !use.from || !use.card || !use.card->isKindOf("Slash")) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "he")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from) return false;
        QStringList legal;
        for (const Card *card : ctx.owner->getCards("he"))
            if (!card->hasFlag("using") && ctx.owner->canDiscard(ctx.owner, card->getEffectiveId()))
                legal << QString::number(card->getEffectiveId());
        if (legal.isEmpty()) return false;
        const Card *selected = room->askForExchange(ctx.owner, objectName(), 1, 1, true,
            "@tenyearlongyin:" + use.from->objectName(), true, legal.join(","));
        if (!selected || selected->getSubcards().size() != 1) return false;
        const int id = selected->getSubcards().first();
        if (!legal.contains(QString::number(id))) return false;
        ctx.extra_data = QVariantMap{{"id", id}, {"number", Sanguosha->getCard(id)->getNumber()}};
        ctx.targets << use.from;
        if (use.from != ctx.owner) ctx.targets << ctx.owner;
        ctx.manual_effect = true;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (id < 0) return false;
        const Card *card = Sanguosha->getCard(id);
        if (!card || card->hasFlag("using") || room->getCardOwner(id) != ctx.owner
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        QVariantMap paid = ctx.extra_data.toMap();
        paid["number"] = card->getNumber();
        ctx.extra_data = paid;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.choice = "history";
        ServerPlayer *user = ctx.original_data->value<CardUseStruct>().from;
        if (user) skillEffect(event, room, ctx.owner, ctx, user);
        ctx.choice = "reward";
        skillEffect(event, room, ctx.owner, ctx, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (ctx.choice == "history") {
            if (target == use.from) {
                use.m_addHistory = false;
                ctx.original_data->setValue(use);
            }
            return false;
        }
        if (!use.card) return false;
        const bool red = use.card->isRed();
        const int slashNumber = use.card->getNumber();
        if (red) target->drawCards(getEffectiveAmount(ctx), objectName());
        if (!target->isAlive() || ctx.extra_data.toMap().value("number").toInt() != slashNumber) return false;
        const Skill *jiezhong = Sanguosha->getSkill("tenyearjiezhong");
        if (!jiezhong) return false;
        QList<SkillContext> spent;
        for (int id : target->getSkillInstanceIds("tenyearjiezhong")) {
            const SkillContext quota = TenyearJiezhong::quotaContext(target, id);
            if (!jiezhong->isUsable(quota)) spent << quota;
        }
        if (spent.isEmpty()) return false;
        const QString answer = room->askForTriggerOrder(target, objectName(), spent, false, *ctx.original_data);
        QString name;
        const int selectedId = SkillInstanceUtils::parseName(answer.section(':', 0, 0), name);
        for (const SkillContext &quota : spent) {
            if (name != "tenyearjiezhong" || selectedId != quota.instanceID) continue;
            // The selected grant must still exist; never substitute a newly acquired namesake.
            if (!target->hasSkillInstance(name, selectedId) || jiezhong->isUsable(quota)) return false;
            jiezhong->resetUsage(quota);
            TenyearJiezhong::syncDisplay(room, target);
            LogMessage log;
            log.type = "#TenyearLongyinReset";
            log.from = target;
            log.arg = objectName();
            log.arg2 = name;
            room->sendLog(log);
            break;
        }
        return false;
    }
};

class TenyearQieting : public TriggerSkillV2
{
public:
    TenyearQieting() : TriggerSkillV2("tenyearqieting") { events << EventPhaseChanging; }
    static bool noOtherTargets(Room *room, ServerPlayer *actor)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (!turn) return false;
        QVariantMap filter{{"kind", "use_card_targets"}, {"turn_id", turn}, {"from", actor->objectName()}};
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap data = entry.toMap().value("data").toMap();
                const QVariantMap card = data.value("card").toMap();
                if (!card.contains("type")) return false;
                if (card.value("type").toInt() == Card::TypeSkill) continue;
                for (const QVariant &target : data.value("targets").toList())
                    if (target.toString() != actor->objectName()) return false;
            }
            if (!page.value("has_more").toBool()) return true;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    static bool noDamage(Room *room, ServerPlayer *actor)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (!turn) return false;
        const QVariantMap page = room->queryActualDamage({{"turn_id", turn}, {"from", actor->objectName()}, {"limit", 1}});
        return page.value("complete").toBool() && page.value("error").toString().isEmpty()
            && page.value("items").toList().isEmpty();
    }
    static QList<int> movable(ServerPlayer *actor, ServerPlayer *owner)
    {
        QList<int> result;
        for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i)
            if (actor->getEquip(i) && !owner->getEquip(i) && owner->hasEquipArea(i))
                result << actor->getEquip(i)->getEffectiveId();
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
        const bool draw = noOtherTargets(room, player), move = noDamage(room, player);
        TriggerList result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && (draw || (move && !movable(player, owner).isEmpty())))
                result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const bool draw = noOtherTargets(room, ctx.invoker);
        const bool move = noDamage(room, ctx.invoker) && !movable(ctx.invoker, ctx.owner).isEmpty()
            && ctx.owner->askForSkillInvoke(this, "move:" + ctx.invoker->objectName());
        if (!draw && !move) return false;
        ctx.extra_data = QVariantMap{{"draw", draw}, {"move", move}};
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap options = ctx.extra_data.toMap();
        const int amount = getEffectiveAmount(ctx);
        if (options.value("move").toBool()) {
            room->broadcastSkillInvoke(objectName());
            for (int i = 0; i < amount && target->isAlive(); ++i) {
                const QList<int> legal = movable(ctx.invoker, target);
                if (legal.isEmpty()) break;
                QList<int> disabled = ctx.invoker->getEquipsId();
                for (int id : legal) disabled.removeAll(id);
                const int id = room->askForCardChosen(target, ctx.invoker, "e", objectName(), false, Card::MethodNone, disabled);
                // A nested choice can change either equipment area; validate the actual transfer again.
                if (!movable(ctx.invoker, target).contains(id)) break;
                room->moveCardTo(Sanguosha->getCard(id), target, Player::PlaceEquip);
            }
        }
        if (options.value("draw").toBool() && target->isAlive()) {
            room->sendCompulsoryTriggerLog(target, objectName());
            target->drawCards(amount, objectName());
        }
        return false;
    }
};

TenyearXianzhouDamageCard::TenyearXianzhouDamageCard()
{
	mute = true;
	m_skillName = "tenyearxianzhou";
}

void TenyearXianzhouDamageCard::onUse(Room *room, CardUseStruct &card_use) const
{
	//room->sortByActionOrder(card_use.to);
	CardUseStruct use = card_use;
	room->sortByActionOrder(use.to);
	foreach (ServerPlayer *p, use.to)
		room->damage(DamageStruct("tenyearxianzhou", use.from->isAlive() ? use.from : nullptr, p));
}

bool TenyearXianzhouDamageCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	QString name = Self->property("tenyearxianzhou_target").toString();
	if (name.isEmpty()) return false;
	//const Player *target = Self->findChild<const Player *>(name);

	const Player *target = nullptr;
	QList<const Player *> as = Self->getAliveSiblings();
	as << Self;
	foreach (const Player *p, as) {
		if (p->objectName() == name) {
			target = p;
			break;
		}
	}

	if (!target) return false;
	return targets.length() < Self->getMark("tenyearxianzhou") && target->inMyAttackRange(to_select);
}

TenyearXianzhouCard::TenyearXianzhouCard()
{
}

bool TenyearXianzhouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}

void TenyearXianzhouCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->removePlayerMark(effect.from, "@tenyearxianzhouMark");
	room->doSuperLightbox(effect.from, "tenyearxianzhou");

	DummyCard *dummy = new DummyCard(effect.from->getEquipsId());
	int len = dummy->subcardsLength();
	room->setPlayerMark(effect.from, "tenyearxianzhou", len);
	effect.to->obtainCard(dummy);
	dummy->deleteLater();

	room->recover(effect.from, RecoverStruct(effect.from, nullptr, qMin(len, effect.from->getMaxHp() - effect.from->getHp()), "tenyearxianzhou"));
	if (effect.from->isDead() || effect.to->isDead()) return;
	bool attack = false;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (effect.to->inMyAttackRange(p)) {
			attack = true;
			break;
		}
	}
	if (!attack) return;

	room->setPlayerProperty(effect.from, "tenyearxianzhou_target", effect.to->objectName());
	room->askForUseCard(effect.from, "@@tenyearxianzhou", "@tenyearxianzhou:" + effect.to->objectName() + "::" + QString::number(len));
}

class TenyearXianzhou : public ZeroCardViewAsSkill
{
public:
	TenyearXianzhou() : ZeroCardViewAsSkill("tenyearxianzhou")
	{
		frequency = Skill::Limited;
		limit_mark = "@tenyearxianzhouMark";
		response_pattern = "@@tenyearxianzhou";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@tenyearxianzhouMark") > 0 && player->getEquips().length() > 0;
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern == "@@tenyearxianzhou") {
			return new TenyearXianzhouDamageCard;
		} else {
			return new TenyearXianzhouCard;
		}
	}
};

TenyearShenxingCard::TenyearShenxingCard()
{
    setSkillName("tenyearshenxing");
	target_fixed = true;
}

void TenyearShenxingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isAlive())
		room->drawCards(source, 1, "tenyearshenxing");
}

class TenyearShenxing : public ViewAsSkillV2
{
public:
    TenyearShenxing() : ViewAsSkillV2("tenyearshenxing") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &) const override { return true; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearShenxingCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    int materialCount(const ActiveSkillRequest &request) const
    {
        return request.initiator ? qBound(0, request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "count").toInt(), 2) : 0;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using")
            && request.selectedCardIds.size() < materialCount(request)
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card))
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != materialCount(request)) return false;
        ActiveSkillRequest selected = request; selected.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || id >= Sanguosha->getCardCount() || !canSelectCard(selected, Sanguosha->getCard(id))) return false;
            selected.selectedCardIds << id;
        }
        return true;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const int count = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "count").toInt();
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "count", qMin(2, count + 1));
    }
    bool commitUsage(SkillContext &ctx) const
    {
        if (ctx.interceptor_data.value(objectName()).value("committed").toBool()) return true;
        if (!ctx.owner || !isUsable(ctx)) return false;
        ctx.interceptor_data.insert(objectName(), QVariantMap{{"committed", true}});
        addUsage(ctx);
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        const QVariant previous = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "count");
        // Reserve before movement callbacks so a nested use already pays the next price.
        if (!commitUsage(ctx)) return false;
        if (ViewAsSkillV2::pay(room, ctx, request)) return true;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "count", previous);
        ctx.interceptor_data.remove(objectName());
        return false;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
};

class TenyearShenxingRecord : public TriggerSkillV2
{
public:
    TenyearShenxingRecord() : TriggerSkillV2("#tenyearshenxing-record")
    { events << EventPhaseChanging << EventSkillInvoking; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (player && data.value<PhaseChangeStruct>().from == Player::Play)
                for (int id : player->getSkillInstanceIds("tenyearshenxing"))
                    player->removeSkillInstanceStateValue("tenyearshenxing", id, "count");
        } else {
            SkillContext ctx = data.value<SkillContext>();
            if (ctx.bypass_cost && ctx.owner && ctx.activationRef.isValid()
                && ctx.activationRef.key.skillName == "tenyearshenxing"
                && ctx.activationRef.ownerObjectName == ctx.owner->objectName()
                && ctx.activationRef.key.instanceID == ctx.instanceID) {
                const auto *skill = dynamic_cast<const TenyearShenxing *>(Sanguosha->getSkill("tenyearshenxing"));
                if (!skill || !skill->commitUsage(ctx)) ctx.is_canceled = true;
                data = QVariant::fromValue(ctx);
            }
        }
        return true;
    }
};
TenyearBingyiCard::TenyearBingyiCard()
{
    setSkillName("tenyearbingyi");
}

bool TenyearBingyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card::Color color = Card::Colorless;
	foreach (const Card *c, Self->getHandcards()) {
		if (color == Card::Colorless)
			color = c->getColor();
		else if (c->getColor() != color)
			return targets.isEmpty();
	}
	return targets.length() <= Self->getHandcardNum();
}

bool TenyearBingyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
	Card::Color color = Card::Colorless;
	foreach (const Card *c, Self->getHandcards()) {
		if (color == Card::Colorless)
			color = c->getColor();
		else if (c->getColor() != color)
			return false;
	}
	return targets.length() < Self->getHandcardNum();
}

void TenyearBingyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->showAllCards(source, static_cast<ServerPlayer *>(nullptr));

	bool same_number = true;
	QList<const Card *>cards = source->getHandcards();
	int num = cards.first()->getNumber();
	foreach (const Card *c, source->getHandcards()) {
		if (c->getNumber() != num) {
			same_number = false;
			break;
		}
	}

	foreach(ServerPlayer *p, targets)
		room->drawCards(p, 1, "tenyearbingyi");
	if (same_number && source->isAlive())
		source->drawCards(1, "tenyearbingyi");
}

class TenyearBingyiViewAsSkill : public ViewAsSkillV2
{
public:
    TenyearBingyiViewAsSkill() : ViewAsSkillV2("tenyearbingyi", 0) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && !request.initiator->isKongcheng() && request.pattern == "@@tenyearbingyi"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearBingyiCard"; }
    static bool sameColor(const Player *owner)
    {
        if (!owner || owner->isKongcheng()) return false;
        const QList<const Card *> hand = owner->getHandcards();
        if (hand.isEmpty()) return false;
        for (const Card *card : hand)
            if (card->getColor() != hand.first()->getColor()) return false;
        return true;
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target->isAlive() && !selected.contains(target) && sameColor(request.initiator)
            && selected.size() < request.initiator->getHandcardNum();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.initiator && !request.initiator->isKongcheng() && (selected.isEmpty()
            || (sameColor(request.initiator) && selected.size() <= request.initiator->getHandcardNum()));
    }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.choice = "show";
        ctx.extra_data = QVariant();
        skillEffect(ctx, ctx.invoker);
        if (!ctx.extra_data.isValid()) return FinishSkill;
        const QVariantMap shown = ctx.extra_data.toMap();
        const QList<ServerPlayer *> recipients = ctx.targets.mid(0, shown.value("count").toInt());
        ctx.choice = "draw";
        if (shown.value("same_color").toBool())
            for (ServerPlayer *target : recipients) skillEffect(ctx, target);
        if (shown.value("same_number").toBool()) skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "show") {
            const QList<const Card *> cards = target->getHandcards();
            if (cards.isEmpty()) return ContinueEffects;
            const Card::Color color = cards.first()->getColor();
            const int number = cards.first()->getNumber();
            bool sameColor = true, sameNumber = true;
            for (const Card *card : cards) {
                if (card->getColor() != color) sameColor = false;
                if (card->getNumber() != number) sameNumber = false;
            }
            // ShowCards/ChoiceMade may mutate the hand; rewards refer to this displayed batch.
            const QVariantMap shown{{"count", cards.size()}, {"same_color", sameColor}, {"same_number", sameNumber}};
            target->getRoom()->showAllCards(target, static_cast<ServerPlayer *>(nullptr));
            ctx.extra_data = shown;
        } else {
            target->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return ContinueEffects;
    }
};

class TenyearBingyi : public TriggerSkillV2
{
public:
    TenyearBingyi() : TriggerSkillV2("tenyearbingyi")
    { events << EventPhaseStart; view_as_skill = new TenyearBingyiViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && !player->isKongcheng() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isKongcheng()) return false;
        // The parent must pass its own effect hooks before opening the exact child response.
        Room::AcceptedViewAsEffectScope response(room, target, objectName(), ctx);
        if (response.isValid()) room->askForUseCard(target, "@@tenyearbingyi", "@tenyearbingyi");
        return false;
    }
};

#include "mobile.h"
#include "yjcm2014.h"

TenyearZongxuanCard::TenyearZongxuanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void TenyearZongxuanCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const
{
}

class TenyearZongxuanVS : public ViewAsSkill
{
public:
	TenyearZongxuanVS() : ViewAsSkill("tenyearzongxuan")
	{
		expand_pile = "#tenyearzongxuan";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPile("#tenyearzongxuan").contains(to_select->getEffectiveId());
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@tenyearzongxuan");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;

		TenyearZongxuanCard *card = new TenyearZongxuanCard;
		card->addSubcards(cards);
		return card;
	}
};

class TenyearZongxuan : public TriggerSkill
{
public:
	TenyearZongxuan() : TriggerSkill("tenyearzongxuan")
	{
		events << CardsMoveOneTime;
		view_as_skill = new TenyearZongxuanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.from || move.from != player)
			return false;
		if (move.to_place == Player::DiscardPile
			&& ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {

			int i = 0;
			QList<int> zongxuan_card, trick_card;
			foreach (int card_id, move.card_ids) {
				if (room->getCardPlace(card_id) == Player::DiscardPile
					&& (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)) {
					zongxuan_card << card_id;
					if (Sanguosha->getCard(card_id)->isKindOf("TrickCard"))
						trick_card << card_id;
				}
				i++;
			}
			if (zongxuan_card.isEmpty())
				return false;

			QString pattern = "@@tenyearzongxuan";
			if (!trick_card.isEmpty()) {
				ServerPlayer *geter = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@tenyearzongxuan-trick", true, true);
				if (geter) {
					room->broadcastSkillInvoke(objectName());
					pattern = pattern + "!";

					room->fillAG(trick_card, geter);  //偷懒用AG
					int id = room->askForAG(geter, trick_card, false, objectName());
					zongxuan_card.removeOne(id);
					room->clearAG(geter);
					room->obtainCard(geter, id);
				}
			}

			if (player->isDead() || zongxuan_card.isEmpty()) return false;

			room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::PlaceUnknown, true);

			try {
				const Card *c = room->askForUseCard(player, pattern, pattern.endsWith("!") ? "@tenyearzongxuan" : "@mobilezongxuan");
				if (c) {
					QList<int> subcards = c->getSubcards();
					foreach (int id, subcards) {
						if (zongxuan_card.contains(id))
							zongxuan_card.removeOne(id);
					}
					LogMessage log;
					log.type = "$YinshicaiPut";
					log.from = player;
					log.card_str = ListI2S(subcards).join("+");
					room->sendLog(log);

					room->notifyMoveToPile(player, subcards, objectName(), Player::PlaceUnknown, false);

					CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "tenyearzongxuan", "");
					room->moveCardTo(c, nullptr, Player::DrawPile, reason, false, true);
				} else {
					if (pattern.endsWith("!")) {
						int id = zongxuan_card.at(qsanRandomBounded(zongxuan_card.length()));
						CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "tenyearzongxuan", "");
						room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, reason, false);
					}
				}
			}
			catch (TriggerEvent triggerEvent) {
				if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
					if (!zongxuan_card.isEmpty())
						room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::PlaceUnknown, false);
				}
				throw triggerEvent;
			}
			if (!zongxuan_card.isEmpty())
				room->notifyMoveToPile(player, zongxuan_card, objectName(), Player::PlaceUnknown, false);
		}
		return false;
	}
};

class TenyearZhiyan : public TriggerSkillV2
{
public:
    TenyearZhiyan() : TriggerSkillV2("tenyearzhiyan") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@zhiyan-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "basic") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        const QList<int> ids = room->drawCardsList(target, getEffectiveAmount(ctx), objectName(), true, true);
        for (int id : ids) {
            if (!target->isAlive()) break;
            const Card *card = Sanguosha->getCard(id);
            if (target->handCards().contains(id)) room->showCard(target, id);
            if (card->isKindOf("EquipCard")) {
                room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
                // Only the drawn card still in this recipient's hand may enter normal use.
                if (target->isAlive() && target->handCards().contains(id) && !card->hasFlag("using") && target->canUse(card))
                    room->useCard(CardUseStruct(card, target));
            } else if (card->isKindOf("BasicCard") && ctx.owner->isAlive()) {
                ctx.choice = "basic";
                skillEffect(event, room, ctx.owner, ctx, ctx.owner);
                ctx.choice.clear();
            }
        }
        return false;
    }
};

class TenyearQiaoshi : public TriggerSkillV2
{
public:
    TenyearQiaoshi() : TriggerSkillV2("tenyearqiaoshi") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getHandcardNum() == player->getHandcardNum()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || ctx.owner->getHandcardNum() != ctx.invoker->getHandcardNum()
            || !ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
        ctx.targets << ctx.owner << ctx.invoker;
        room->sortByActionOrder(ctx.targets);
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(this);
        bool repeat = true;
        while (repeat && ctx.owner->isAlive() && ctx.invoker && ctx.invoker->isAlive()) {
            QList<int> suits;
            const QList<ServerPlayer *> targets = ctx.targets;
            for (ServerPlayer *target : targets) {
                // A canceled draw hook or exhausted deck is not a matching-suit draw.
                ctx.extra_data = QVariant();
                skillEffect(event, room, ctx.owner, ctx, target);
                if (!ctx.extra_data.isValid()) break;
                suits << ctx.extra_data.toInt();
            }
            repeat = suits.size() == 2 && suits.first() == suits.last() && ctx.owner->isAlive()
                && ctx.invoker->isAlive() && ctx.owner->askForSkillInvoke(this, ctx.invoker);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QList<int> drawn = room->drawCardsList(target, getEffectiveAmount(ctx), objectName());
        if (!drawn.isEmpty()) ctx.extra_data = static_cast<int>(Sanguosha->getCard(drawn.first())->getSuit());
        return false;
    }
};

TenyearYjYanyuCard::TenyearYjYanyuCard()
{
	setSkillName("tenyearyjyanyu");
	will_throw = false;
	can_recast = true;
	handling_method = Card::MethodRecast;
	target_fixed = true;
}

void TenyearYjYanyuCard::onUse(Room *room, CardUseStruct &card_use) const
{
	room->broadcastSkillInvoke("tenyearyjyanyu");
	ServerPlayer *xiahou = card_use.from;

	CardMoveReason reason(CardMoveReason::S_REASON_RECAST, xiahou->objectName());
	reason.m_skillName = getSkillName();
	room->moveCardTo(this, xiahou, nullptr, Player::DiscardPile, reason);
	//xiahou->broadcastSkillInvoke("@recast");

	int id = card_use.card->getSubcards().first();

	LogMessage log;
	log.type = "#UseCard_Recast";
	log.from = xiahou;
	log.card_str = QString::number(id);
	room->sendLog(log);

	xiahou->drawCards(1, "recast");

	xiahou->addMark("tenyearyjyanyu-PlayClear");
}

class TenyearYjYanyuVS : public ViewAsSkillV2
{
public:
    TargetMode targetMode() const override { return NoTarget; }
    TenyearYjYanyuVS() : ViewAsSkillV2("tenyearyjyanyu", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearYjYanyuCard"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && card->getEffectiveId() >= 0
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card))
            && card->isKindOf("Slash") && !card->hasFlag("using") && !request.initiator->isCardLimited(card, Card::MethodRecast);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0
            || request.selectedCardIds.first() >= Sanguosha->getCardCount()) return false;
        ActiveSkillRequest empty = request; empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        ctx.extra_data = request.selectedCardIds.first();
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        const int id = ctx.extra_data.toInt();
        if (id != request.selectedCardIds.first() || room->getCardOwner(id) != ctx.initiator || (room->getCardPlace(id) != Player::PlaceHand
            && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, ctx.initiator->objectName());
        reason.m_skillName = objectName();
        room->moveCardTo(Sanguosha->getCard(id), ctx.initiator, nullptr, Player::DiscardPile, reason);
        LogMessage log;
        log.type = "#UseCard_Recast"; log.from = ctx.initiator; log.card_str = QString::number(id);
        room->sendLog(log);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->getRoom()->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), "recast");
        return ContinueEffects;
    }
};

class TenyearYjYanyu : public TriggerSkillV2
{
public:
    TenyearYjYanyu() : TriggerSkillV2("tenyearyjyanyu")
    { view_as_skill = new TenyearYjYanyuVS; events << EventPhaseEnd; }
    static int recasts(Room *room, ServerPlayer *owner, int instance)
    {
        const qint64 phase = room->historyScopes().value("phase_id").toLongLong();
        if (!phase) return -1;
        QVariantMap filter{{"phase_id", phase}, {"from", owner->objectName()}, {"to_place", int(Player::DiscardPile)}};
        QSet<QString> seen;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                const QVariantMap move = fact.value("data").toMap();
                if (move.value("reason").toInt() != CardMoveReason::S_REASON_RECAST
                    || move.value("reason_skill").toString() != "tenyearyjyanyu") continue;
                if (!move.contains("activation_owner") || !move.contains("activation_instance_id")) return -1;
                if (move.value("activation_owner").toString() != owner->objectName()
                    || move.value("activation_skill").toString() != "tenyearyjyanyu"
                    || move.value("activation_instance_id").toInt() != instance) continue;
                seen.insert(fact.value("event_id").toString() + ":" + move.value("card_id").toString());
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark")); filter.insert("after", page.value("next_after"));
        }
        return qMin(3, int(seen.size()));
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
        QStringList copies;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (recasts(room, player, id) > 0) copies << SkillInstanceUtils::formatName(objectName(), id);
        return copies.isEmpty() ? TriggerList() : TriggerList{{player, copies}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = recasts(room, ctx.owner, ctx.instanceID);
        if (count <= 0) return false;
        QList<ServerPlayer *> males;
        for (ServerPlayer *player : room->getAlivePlayers()) if (player->isMale()) males << player;
        if (males.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, males, objectName(),
            "@tenyearyjyanyu-give:" + QString::number(count), true, true);
        if (!target) return false;
        ctx.extra_data = count; ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class TenyearQianxi : public PhaseChangeSkill
{
public:
	TenyearQianxi() : PhaseChangeSkill("tenyearqianxi")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() == Player::NotActive){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				QString an = p->getTag("tenyearqianxi"+player->objectName()).toString();
				if (an != ""){
					p->removeEquipsNullified("Armor|"+an);
					p->removeTag("tenyearqianxi"+player->objectName());
				}
			}
		}
		if (player->getPhase() != Player::Start) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(this);
		player->drawCards(1, objectName());
		if (!player->canDiscard(player, "he")) return false;
		const Card *card = room->askForDiscard(player, objectName(), 1, 1, false, true);
		if (!card) return false;

		QList<ServerPlayer *> to_choose;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (player->distanceTo(p) == 1)
				to_choose << p;
		}
		if (to_choose.isEmpty()) return false;

		ServerPlayer *t = room->askForPlayerChosen(player, to_choose, objectName());
		room->doAnimate(1, player->objectName(), t->objectName());

		QString color = "";
		if (card->isRed())
			color = "red";
		else if (card->isBlack())
			color = "black";

		room->addPlayerMark(t, "tenyearqianxi_target_" + player->objectName() + "-Clear");
		if (!color.isEmpty()) {
			room->addPlayerMark(t, "&tenyearqianxi+" + color + "-Clear");
			t->addEquipsNullified("Armor|"+color);
			t->setTag("tenyearqianxi"+player->objectName(), color);
		}
		return false;
	}
};

class TenyearQianxiDraw : public TriggerSkill
{
public:
	TenyearQianxiDraw() : TriggerSkill("#tenyearqianxi-draw")
	{
		events << HpRecover;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (!room->hasCurrent()) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead()) continue;
			int mark = player->getMark("tenyearqianxi_target_" + p->objectName() + "-Clear");
			for (int i = 0; i < mark; i++) {
				room->sendCompulsoryTriggerLog(p, "tenyearqianxi", true, true);
				p->drawCards(2, "tenyearqianxi");
			}
		}
		return false;
	}
};

class TenyearQianxiLimit : public CardLimitSkill
{
public:
	TenyearQianxiLimit() : CardLimitSkill("#tenyearqianxi-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target) const
	{
		QStringList colors;
		if (target->getMark("&tenyearqianxi+red-Clear") > 0) colors << "red";
		if (target->getMark("&tenyearqianxi+black-Clear") > 0) colors << "black";
		if (!colors.isEmpty()) return ".|" + colors.join(",") + "|.|hand";
		return "";
	}
};

TenyearJiaozhaoCard::TenyearJiaozhaoCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

void TenyearJiaozhaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->setPlayerMark(source, "ViewAsSkill_tenyearjiaozhaoEffect", 1);

	int selfcardid = getSubcards().first();
	room->showCard(source, selfcardid);

	int level = source->property("tenyearjiaozhao_level").toInt();
	ServerPlayer *target;
	if (level >= 1)
		target = source;
	else {
		int distance = source->distanceTo(source->getNextAlive());
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			if (source->distanceTo(p) < distance)
				distance = source->distanceTo(p);
		}
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			if (source->distanceTo(p) == distance)
				targets << p;
		}
		if (targets.isEmpty()) return;
		target = room->askForPlayerChosen(source, targets, "tenyearjiaozhao", "@jiaozhao-target");
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), target->objectName());
	}

	QStringList alllist;
	QList<int> ids;
	bool basic = source->getMark("tenyearjiaozhao_basic-Clear") > 0;
	bool trick = source->getMark("tenyearjiaozhao_trick-Clear") > 0;
	foreach(int id, Sanguosha->getRandomCards()) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (c->isKindOf("EquipCard") || c->isKindOf("DelayedTrick")) continue;
		if (basic && c->isKindOf("BasicCard")) continue;
		if (trick && c->isKindOf("TrickCard")) continue;
		if (alllist.contains(c->objectName())) continue;
		alllist << c->objectName();
		ids << id;
	}
	if (ids.isEmpty()) return;
	room->broadcastSkillInvoke(getSkillName(),1);

	room->fillAG(ids, target);
	int id = room->askForAG(target, ids, false, "tenyearjiaozhao");
	room->clearAG(target);

	const Card *card = Sanguosha->getEngineCard(id);
	QString name = card->objectName();

	LogMessage log;
	log.type = "#ShouxiChoice";
	log.from = target;
	log.arg = name;
	room->sendLog(log);

	room->setPlayerProperty(source, "tenyearjiaozhao_name", name);
	if (card->isKindOf("BasicCard")) {
		room->setPlayerProperty(source, "tenyearjiaozhao_basic_name", name);
		room->setPlayerMark(source, "tenyearjiaozhao_basic-Clear", selfcardid + 1);
	} else if (card->isKindOf("TrickCard")) {
		room->setPlayerProperty(source, "tenyearjiaozhao_trick_name", name);
		room->setPlayerMark(source, "tenyearjiaozhao_trick-Clear", selfcardid + 1);
	}
}

class TenyearJiaozhaoVS : public OneCardViewAsSkill
{
public:
	TenyearJiaozhaoVS() : OneCardViewAsSkill("tenyearjiaozhao")
	{
		response_or_use = true;
	}

	bool viewFilter(const Card *to_select) const
	{
		QString choice = Self->getTag("tenyearjiaozhao").toString();
		if (choice == "show") return !to_select->isEquipped();
		if (choice.startsWith("use")) {
			int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
			int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
			return to_select->getEffectiveId() == basic || to_select->getEffectiveId() == trick;
		} else if (choice.startsWith("basic")) {
			int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
			return to_select->getEffectiveId() == basic;
		} else if (choice.startsWith("trick")) {
			int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
			return to_select->getEffectiveId() == trick;
		}
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			QString choice = Self->getTag("tenyearjiaozhao").toString();
			if (choice == "show") {
				TenyearJiaozhaoCard *card = new TenyearJiaozhaoCard;
				card->addSubcard(originalCard);
				return card;
			} else if (choice.startsWith("use")) {
				int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
				int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
				if (basic > -1) {
					QString bname = Self->property("tenyearjiaozhao_basic_name").toString();
					Card *use_card = Sanguosha->cloneCard(bname);
					if (!use_card) return nullptr;
					use_card->setCanRecast(false);
					use_card->addSubcard(originalCard);
					use_card->setSkillName("tenyearjiaozhao");
					return use_card;
				} else if (trick > -1) {
					QString tname = Self->property("tenyearjiaozhao_trick_name").toString();
					Card *use_card = Sanguosha->cloneCard(tname);
					if (!use_card) return nullptr;
					use_card->setCanRecast(false);
					use_card->addSubcard(originalCard);
					use_card->setSkillName("tenyearjiaozhao");
					return use_card;
				}
			} else if (choice.startsWith("basic")) {
				int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
				if (basic < 0) return nullptr;
				QString bname = Self->property("tenyearjiaozhao_basic_name").toString();
				Card *use_card = Sanguosha->cloneCard(bname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(originalCard);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			} else if (choice.startsWith("trick")) {
				int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
				if (trick < 0) return nullptr;
				QString tname = Self->property("tenyearjiaozhao_trick_name").toString();
				Card *use_card = Sanguosha->cloneCard(tname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(originalCard);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			}
		} else {
			QString pattern = Sanguosha->getCurrentCardUsePattern();
			if (pattern == "nullification") {
				int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;
				if (trick < 0) return nullptr;
				QString tname = Self->property("tenyearjiaozhao_trick_name").toString();
				if (tname != "nullification") return nullptr;
				Card *use_card = Sanguosha->cloneCard(tname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(trick);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			} else {
				int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
				if (basic < 0) return nullptr;
				QString bname = Self->property("tenyearjiaozhao_basic_name").toString();
				Card *use_card = Sanguosha->cloneCard(bname);
				if (!use_card) return nullptr;
				use_card->setCanRecast(false);
				use_card->addSubcard(basic);
				use_card->setSkillName("tenyearjiaozhao");
				return use_card;
			}
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (!player->hasUsed("JiaozhaoCard")) return true;
		int level = player->property("tenyearjiaozhao_level").toInt();

		if (level < 2 && player->hasUsed("JiaozhaoCard")) {
			Card *use_card = nullptr;
			QString bname = player->property("tenyearjiaozhao_basic_name").toString();
			QString tname = player->property("tenyearjiaozhao_trick_name").toString();
			int basic = player->getMark("tenyearjiaozhao_basic-Clear") - 1;
			int trick = player->getMark("tenyearjiaozhao_trick-Clear") - 1;

			if (!bname.isEmpty() && basic > -1) {
				use_card = Sanguosha->cloneCard(bname);
				if (!use_card) return false;
				use_card->addSubcard(basic);
			} else if (!tname.isEmpty() && trick > -1) {
				use_card = Sanguosha->cloneCard(tname);
				if (!use_card) return false;
				use_card->addSubcard(trick);
			}

			use_card->setCanRecast(false);
			use_card->setSkillName("tenyearjiaozhao");
			use_card->deleteLater();
			return use_card->isAvailable(player);
		}
		return level >= 2;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return false;
		if (pattern.startsWith(".") || pattern.startsWith("@"))
			return false;

		QString bname = player->property("tenyearjiaozhao_basic_name").toString();
		QString tname = player->property("tenyearjiaozhao_trick_name").toString();
		if (bname.isEmpty() && tname.isEmpty()) return false;

		int basic = player->getMark("tenyearjiaozhao_basic-Clear") - 1;
		int trick = player->getMark("tenyearjiaozhao_trick-Clear") - 1;
		if (!bname.isEmpty() && basic < 0) return false;
		if (!tname.isEmpty() && trick < 0) return false;

		if (pattern == "nullification")
			return tname == "nullification";

		int level = player->property("tenyearjiaozhao_level").toInt();
		QString pattern_names = pattern;
		if (pattern.contains("slash") || pattern.contains("Slash"))
			pattern_names = Sanguosha->getSlashNames().join("+");
		else if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0)
			return false;
		else if (pattern == "peach+analeptic")
			return level >= 2 && pattern_names.split("+").contains(bname);
		return pattern_names.split("+").contains(bname);
	}

	bool isEnabledAtNullification(const ServerPlayer *player) const
	{
		QString tname = player->property("tenyearjiaozhao_trick_name").toString();
		if (tname.isEmpty()) return false;
		int trick = player->getMark("tenyearjiaozhao_trick-Clear") - 1;
		return trick > -1 && tname == "nullification";
	}
};

class TenyearJiaozhao : public TriggerSkill
{
public:
	TenyearJiaozhao() : TriggerSkill("tenyearjiaozhao")
	{
		events << EventPhaseChanging;
		view_as_skill = new TenyearJiaozhaoVS;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::tiansuan(objectName());
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to == Player::NotActive) {
			room->setPlayerMark(player, "ViewAsSkill_tenyearjiaozhaoEffect", 0);
			room->setPlayerProperty(player, "tenyearjiaozhao_basic_name", "");
			room->setPlayerProperty(player, "tenyearjiaozhao_trick_name", "");
		}
		return false;
	}
};

class TenyearJiaozhaoPro : public ProhibitSkill
{
public:
	TenyearJiaozhaoPro() : ProhibitSkill("#tenyearjiaozhao")
	{
		frequency = NotFrequent;
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return from == to && card->getSkillName() == "tenyearjiaozhao" && from->property("tenyearjiaozhao_level").toInt() < 2;
	}
};

class TenyearDanxin : public MasochismSkill
{
public:
	TenyearDanxin() : MasochismSkill("tenyeardanxin")
	{
		frequency = Frequent;
	}

	void onDamaged(ServerPlayer *target, const DamageStruct &) const
	{
		if (target->askForSkillInvoke(objectName())){
			Room *room = target->getRoom();
			room->broadcastSkillInvoke(objectName());

			target->drawCards(1, objectName());

			int level = target->property("tenyearjiaozhao_level").toInt();
			if (level<2) {
				LogMessage log;
				log.type = "#JiexunChange";
				log.from = target;
				log.arg = "tenyearjiaozhao";
				room->sendLog(log);
				level++;
				room->setPlayerProperty(target, "tenyearjiaozhao_level", level);
				room->setPlayerMark(target, "&tenyearjiaozhao_level", level);
				room->changeTranslation(target, "tenyearjiaozhao", level);
			}
		}
	}
};

TenyearGanluCard::TenyearGanluCard()
{
    setSkillName("tenyearganlu");
}

void TenyearGanluCard::swapEquip(ServerPlayer *first, ServerPlayer *second) const
{
	Room *room = first->getRoom();
	room->swapEquips(first, second, "tenyearganlu");
}

bool TenyearGanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

bool TenyearGanluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	if (targets.isEmpty()) return true;
	if (targets.length() == 1) {
		if (targets.first()->getEquips().isEmpty())
			return !to_select->getEquips().isEmpty();
		else
			return true;
	}
	return false;
}

void TenyearGanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	LogMessage log;
	log.type = "#GanluSwap";
	log.from = source;
	log.to = targets;
	room->sendLog(log);

	ServerPlayer *first = targets.first(), *last = targets[1];

	swapEquip(first, last);

	if (source->isAlive() && first->isAlive() && last->isAlive() &&
			qAbs(first->getEquips().length() - last->getEquips().length()) > source->getLostHp())
		room->askForDiscard(source, "tenyearganlu", 2, 2);
}

class TenyearGanlu : public ViewAsSkillV2
{
public:
    TenyearGanlu() : ViewAsSkillV2("tenyearganlu", 0) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearGanluCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target->isAlive() && !selected.contains(target) && selected.size() < 2
            && (selected.isEmpty() || !selected.first()->getEquips().isEmpty() || !target->getEquips().isEmpty());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    { return targets.size() == 2 && (!targets.first()->getEquips().isEmpty() || !targets.last()->getEquips().isEmpty()); }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        // The native group dispatcher has already run both recipients' target hooks.
        if (targets.size() != 2 || targets.first() == targets.last()) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        ServerPlayer *first = targets.first(), *second = targets.last();
        LogMessage log;
        log.type = "#GanluSwap";
        log.from = ctx.invoker;
        log.to = targets;
        room->sendLog(log);
        room->swapEquips(first, second, objectName());
        if (ctx.invoker->isAlive() && first->isAlive() && second->isAlive()
            && qAbs(first->getEquips().size() - second->getEquips().size()) > ctx.invoker->getLostHp()) {
            ctx.choice = "discard";
            skillEffect(ctx, ctx.invoker);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "discard" && getEffectiveAmount(ctx) > 0)
            target->getRoom()->askForDiscard(target, objectName(), 2 * getEffectiveAmount(ctx), 2 * getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};

class TenyearBuyi : public TriggerSkillV2
{
public:
    TenyearBuyi() : TriggerSkillV2("tenyearbuyi") { events << Dying << CardsMoveOneTime; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Dying || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DyingStruct dying = data.value<DyingStruct>();
        // Dying is observer-broadcast; this observer offers only its own exact copies.
        return dying.who && dying.who->isAlive() && dying.who->getHp() < 1 && !dying.who->isKongcheng()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !move.is_last_handcard
            || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
            || move.reason.m_skillName != objectName()) return true;
        const QVariantMap receipt = move.reason.m_extraData.toMap().value("buyi_receipt").toMap();
        if (receipt.isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = room->findPlayerByObjectName(receipt.value("activation_owner").toString(), true);
        if (!ctx.owner) return true;
        ctx.invoker = player;
        ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        ctx.instanceID = receipt.value("activation_instance").toInt();
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.amount = receipt.value("amount").toInt();
        ctx.extra_data = receipt;
        ctx.targets << player;
        ctx.current_event = event;
        ctx.original_data = &data;
        ctx.is_forced = true;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.original_data) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        return move.from && move.from->isAlive() && move.from->objectName() == ctx.extra_data.toMap().value("holder").toString()
            && move.is_last_handcard && move.to_place == Player::DiscardPile
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
            && move.reason.m_skillName == objectName()
            && move.reason.m_extraData.toMap().value("buyi_receipt") == ctx.extra_data;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardsMoveOneTime) return true;
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<DyingStruct>().who;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardsMoveOneTime) {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (target->isKongcheng() || target->getHp() > 0) return false;
        ctx.owner->peiyin(this);
        const Card *card = nullptr;
        if (target == ctx.owner) card = room->askForCardShow(target, ctx.owner, objectName());
        else {
            const int id = room->askForCardChosen(ctx.owner, target, "h", objectName());
            if (id >= 0) card = Sanguosha->getCard(id);
        }
        if (!card || card->hasFlag("using") || !target->handCards().contains(card->getEffectiveId())) return false;
        const int id = card->getEffectiveId();
        const bool basic = card->getTypeId() == Card::TypeBasic;
        room->showCard(target, id);
        if (basic || !target->isAlive() || !target->handCards().contains(id)
            || Sanguosha->getCard(id)->hasFlag("using") || !target->canDiscard(target, id)) return false;
        CardMoveReason reason(CardMoveReason::S_REASON_THROW, target->objectName(), objectName(), QString());
        // The last-hand reward follows this exact discard even if nested moves retire its grant.
        reason.m_extraData = QVariantMap{{"buyi_receipt", QVariantMap{
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_instance", ctx.activationRef.key.instanceID}, {"actor", ctx.owner->objectName()},
            {"amount", getEffectiveAmount(ctx)}, {"holder", target->objectName()}}}};
        if (tenyearPayDiscard(room, target, objectName(), {id}, &reason) && target->isAlive())
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        return false;
    }
};
class TenyearZhuhai : public TriggerSkillV2
{
public:
    TenyearZhuhai() : TriggerSkillV2("tenyearzhuhai") { events << EventPhaseStart; }
    static bool materialAvailable(Room *room, ServerPlayer *owner, int id)
    {
        return id >= 0 && (owner->handCards().contains(id) || owner->getHandPile().contains(id))
            && room->getCardOwner(id) == owner && !Sanguosha->getCard(id)->hasFlag("using");
    }
    QStringList choices(ServerPlayer *owner, ServerPlayer *target, int id) const
    {
        if (!target || !target->isAlive() || !materialAvailable(owner->getRoom(), owner, id)) return {};
        const Card *material = Sanguosha->getCard(id);
        Slash slash(material->getSuit(), material->getNumber());
        slash.addSubcard(id); slash.setSkillName(objectName());
        Dismantlement dismantlement(material->getSuit(), material->getNumber());
        dismantlement.addSubcard(id); dismantlement.setSkillName(objectName());
        QStringList result;
        if (owner->canSlash(target, &slash, false)) result << "slash=" + target->objectName();
        if (owner->canUse(&dismantlement, target, true)) result << "dismantlement=" + target->objectName();
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return {};
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (!turn) return {};
        const QVariantMap damage = room->queryActualDamage({{"turn_id", turn}, {"from", player->objectName()}, {"limit", 1}});
        if (!damage.value("complete").toBool() || !damage.value("error").toString().isEmpty()
            || damage.value("items").toList().isEmpty()) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->getOtherPlayers(player)) {
            if (!owner->hasSkill(objectName())) continue;
            for (int id : owner->handCards() + owner->getHandPile()) {
                if (!choices(owner, player, id).isEmpty()) { result[owner] << objectName(); break; }
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList ids, piles;
        for (const QString &pile : ctx.owner->getPileNames())
            if (pile.startsWith("&") || pile == "wooden_ox") piles << pile;
        for (int id : ctx.owner->handCards() + ctx.owner->getHandPile())
            if (!choices(ctx.owner, ctx.invoker, id).isEmpty()) ids << QString::number(id);
        if (ids.isEmpty()) return false;
        // Selection is not a response payment: the generated ordinary card pays its material once.
        const Card *card = room->askForCard(ctx.owner, ids.join(",") + "|.|.|hand," + piles.join(","),
            "@tenyearzhuhai:" + ctx.invoker->objectName(), QVariant::fromValue(ctx.invoker), Card::MethodNone, ctx.invoker, true);
        if (!card || card->subcardsLength() > 1) return false;
        const int id = card->getEffectiveId();
        if (!ids.contains(QString::number(id))) return false;
        const QStringList available = choices(ctx.owner, ctx.invoker, id);
        if (available.isEmpty()) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), available.join("+"), QVariant::fromValue(ctx.invoker));
        if (!available.contains(ctx.choice)) return false;
        ctx.extra_data = id;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return materialAvailable(room, ctx.owner, ctx.extra_data.toInt())
            && choices(ctx.owner, ctx.invoker, ctx.extra_data.toInt()).contains(ctx.choice);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (getEffectiveAmount(ctx) <= 0 || !materialAvailable(room, ctx.owner, id)) return false;
        const QString name = ctx.choice.section('=', 0, 0);
        if (!choices(ctx.owner, target, id).contains(name + "=" + target->objectName())) return false;
        const Card *material = Sanguosha->getCard(id);
        std::unique_ptr<Card> generated(Sanguosha->cloneCard(name, material->getSuit(), material->getNumber()));
        if (!generated) return false;
        generated->addSubcard(id);
        generated->setSkillName(objectName());
        CardUseStruct use(generated.get(), ctx.owner, target);
        use.setOwnedCard(generated.release());
        room->useCardFromSkillEffect(use, ctx, true);
        return false;
    }
};

class TenyearQianxin : public TriggerSkillV2
{
public:
    TenyearQianxin() : TriggerSkillV2("tenyearqianxin")
    { events << Damage << EventSkillInvoking; global = true; frequency = Wake; waked_skills = "tenyearjianyan"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        // The dispatcher supplies the exact candidate copy before any invocation prompt.
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    bool commitQuota(SkillContext &ctx) const
    {
        const QString key = objectName() + "_quota";
        if (ctx.interceptor_data.value(key).value("committed").toBool()) return true;
        if (!isUsable(ctx)) return false;
        const bool forced = !ctx.owner->isWounded();
        if (forced && ctx.owner->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty()) return false;
        // Commit before Invoking/Effect callbacks can re-enter this exact copy.
        ctx.interceptor_data.insert(key, QVariantMap{{"committed", true}});
        addUsage(ctx);
        // canWake consumes its permission and logs; do so only after quota reservation.
        if (forced) ctx.owner->canWake(objectName());
        ctx.owner->getRoom()->addPlayerMark(ctx.owner, objectName());
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return commitQuota(ctx); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext active = data.value<SkillContext>();
        if (active.bypass_cost && active.owner && active.activationRef.isValid()
            && active.activationRef.key.skillName == objectName()
            && active.activationRef.ownerObjectName == active.owner->objectName()
            && active.activationRef.key.instanceID == active.instanceID) {
            if (!commitQuota(active)) active.is_canceled = true;
            data = QVariant::fromValue(active);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->hasSkill(objectName())
            && (player->isWounded() || !player->getTag(objectName() + "_SKILLCANWAKE").toStringList().isEmpty())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; ctx.targets << ctx.owner; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (target->isWounded()) {
            LogMessage log;
            log.type = "#QianxinWake"; log.from = target; log.arg = objectName();
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        room->doSuperLightbox(target, objectName());
        if (room->changeMaxHpForAwakenSkill(target, -amount, objectName()) && target->isAlive())
            room->acquireSkillFromEffect(target, "tenyearjianyan", ctx);
        return false;
    }
};

TenyearJianyanCard::TenyearJianyanCard()
{
    setSkillName("tenyearjianyan");
	target_fixed = true;
}

void TenyearJianyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QStringList choice_list, pattern_list;

	if (source->getMark("tenyearjianyan_type-PlayClear") <= 0) {
		choice_list << "basic" << "trick" << "equip";
		pattern_list << "BasicCard" << "TrickCard" << "EquipCard";
	}
	if (source->getMark("tenyearjianyan_color-PlayClear") <= 0) {
		choice_list << "red" << "black";
		pattern_list << ".|red" << ".|black";
	}
	if (choice_list.isEmpty()) return;

	QString choice = room->askForChoice(source, "tenyearjianyan", choice_list.join("+"));
	int index = choice_list.indexOf(choice);
	QString pattern = pattern_list.at(index);

	if (index <= 2 && choice_list.contains("basic"))
		room->addPlayerMark(source, "tenyearjianyan_type-PlayClear");
	else
		room->addPlayerMark(source, "tenyearjianyan_color-PlayClear");

	LogMessage log;
	log.type = "#JianyanChoice";
	log.from = source;
	log.arg = choice;
	room->sendLog(log);

	int card_id = -1;
	foreach (int id, room->getDrawPile()) {
		const Card *card = Sanguosha->getCard(id);
		if (Sanguosha->matchExpPattern(pattern, nullptr, card)) {
			card_id = id;
			break;
		}
	}
	if (card_id > -1) {
		CardsMoveStruct move(card_id, nullptr, Player::PlaceTable,
			CardMoveReason(CardMoveReason::S_REASON_TURNOVER, source->objectName(), "tenyearjianyan", ""));
		room->moveCardsAtomic(move, true);
		room->getThread()->delay();

		QList<ServerPlayer *> males;
		foreach (ServerPlayer *player, room->getAlivePlayers()) {
			if (player->isMale())
				males << player;
		}
		if (males.isEmpty() || source->isDead()) {
			DummyCard *dummy = new DummyCard();
			dummy->addSubcard(card_id);
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), "tenyearjianyan", "");
			room->throwCard(dummy, reason, nullptr);
			dummy->deleteLater();
		} else {
			const Card *card = Sanguosha->getCard(card_id);
			if (!room->CardInTable(card)) return;

			room->fillAG(QList<int>() << card_id, source);
			source->setMark("tenyearjianyan", card_id); // For AI
			ServerPlayer *target = room->askForPlayerChosen(source, males, "tenyearjianyan",
				QString("@jianyan-give:::%1:%2\\%3").arg(card->objectName())
				.arg(card->getSuitString() + "_char")
				.arg(card->getNumberString()));
			room->clearAG(source);
			room->giveCard(source, target, card, "tenyearjianyan", true);
		}
	} else {
		LogMessage log;
		log.type = "#TenyearjianyanSwapPile";
		room->sendLog(log);
		room->swapPile();
	}
}

class TenyearJianyan : public ViewAsSkillV2
{
public:
    TargetMode targetMode() const override { return NoTarget; }
    TenyearJianyan() : ViewAsSkillV2("tenyearjianyan", 0) {}
    bool commitQuota(SkillContext &ctx) const
    {
        const QString key = objectName() + "_quota";
        if (ctx.interceptor_data.value(key).value("committed").toBool()) return true;
        if (!isUsable(ctx)) return false;
        // Active bypass may skip the chooser as well as payment; choose before reserving its category.
        if (ctx.choice.isEmpty() && ctx.bypass_cost) {
            const QStringList available = choices(ctx);
            if (!ctx.invoker || available.isEmpty()) return false;
            ctx.choice = ctx.owner->getRoom()->askForChoice(ctx.invoker, objectName(), available.join("+"));
        }
        if (!isUsable(ctx) || !choices(ctx).contains(ctx.choice)) return false;
        QVariantMap receipt{{"committed", true}};
        ctx.interceptor_data.insert(key, receipt);
        addUsage(ctx);
        return true;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearJianyanCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    QStringList choices(const SkillContext &ctx) const
    {
        if (!ctx.owner) return {};
        const QString phase = ctx.owner->getRoom()->historyScopes().value("phase_id").toString();
        if (phase.isEmpty() || phase == "0") return {};
        const QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
        const bool current = state.value("phase").toString() == phase;
        QStringList result;
        if (!current || !state.value("type").toBool()) result << "basic" << "trick" << "equip";
        if (!current || !state.value("color").toBool()) result << "red" << "black";
        return result;
    }
    bool checkCustomUsage(const SkillContext &ctx) const override { return !choices(ctx).isEmpty(); }
    void addUsage(const SkillContext &ctx) const override
    {
        const QString phase = ctx.owner->getRoom()->historyScopes().value("phase_id").toString();
        QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.instanceID);
        if (state.value("phase").toString() != phase) state.clear();
        state["phase"] = phase;
        state[ctx.choice == "red" || ctx.choice == "black" ? "color" : "type"] = true;
        ctx.owner->setSkillInstanceState(objectName(), ctx.instanceID, state);
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        const QStringList available = choices(ctx);
        if (available.isEmpty()) return false;
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), available.join("+"));
        return available.contains(ctx.choice);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { return commitQuota(ctx); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        const QMap<QString, QString> patterns{{"basic", "BasicCard"}, {"trick", "TrickCard"}, {"equip", "EquipCard"},
            {"red", ".|red"}, {"black", ".|black"}};
        LogMessage log;
        log.type = "#JianyanChoice";
        log.from = ctx.invoker;
        log.arg = ctx.choice;
        ctx.invoker->getRoom()->sendLog(log);
        ctx.extra_data = patterns.value(ctx.choice);
        ctx.choice = "search";
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "give") {
            const int id = ctx.extra_data.toInt();
            if (room->getCardPlace(id) == Player::PlaceTable)
                room->giveCard(ctx.invoker, target, Sanguosha->getCard(id), objectName(), true);
            return ContinueEffects;
        }
        for (int index = 0; index < getEffectiveAmount(ctx) && target->isAlive(); ++index) {
            int id = -1;
            for (int candidate : room->getDrawPile())
                if (Sanguosha->matchExpPattern(ctx.extra_data.toString(), nullptr, Sanguosha->getCard(candidate))) {
                    id = candidate;
                    break;
                }
            if (id < 0) {
                LogMessage log;
                log.type = "#TenyearjianyanSwapPile";
                room->sendLog(log);
                room->swapPile();
                break;
            }
            room->moveCardsAtomic(CardsMoveStruct(id, nullptr, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), QString())), true);
            const auto cleanup = [&] {
                if (room->getCardPlace(id) == Player::PlaceTable)
                    room->throwCard(Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                        target->objectName(), objectName(), QString()), nullptr);
            };
            try {
                QList<ServerPlayer *> males;
                for (ServerPlayer *player : room->getAlivePlayers())
                    if (player->isMale()) males << player;
                if (target->isAlive() && !males.isEmpty() && room->getCardPlace(id) == Player::PlaceTable) {
                    const Card *card = Sanguosha->getCard(id);
                    const int previous = target->getMark("tenyearjianyan");
                    const auto restore = qScopeGuard([&] {
                        room->clearAG(target);
                        room->setPlayerMark(target, "tenyearjianyan", previous);
                    });
                    room->fillAG({id}, target);
                    room->setPlayerMark(target, "tenyearjianyan", id);
                    ServerPlayer *recipient = room->askForPlayerChosen(target, males, objectName(),
                        QString("@jianyan-give:::%1:%2\\%3").arg(card->objectName())
                            .arg(card->getSuitString() + "_char").arg(card->getNumberString()));
                    if (recipient) {
                        SkillContext gift = ctx;
                        gift.choice = "give";
                        gift.extra_data = id;
                        skillEffect(gift, recipient);
                    }
                }
            } catch (TriggerEvent) {
                cleanup();
                throw;
            }
            cleanup();
        }
        return ContinueEffects;
    }
};


class TenyearJianyanQuota : public TriggerSkillV2
{
public:
    TenyearJianyanQuota() : TriggerSkillV2("#tenyearjianyan-quota")
    { events << EventSkillInvoking; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        SkillContext active = data.value<SkillContext>();
        if (active.bypass_cost && active.owner && active.activationRef.isValid()
            && active.activationRef.key.skillName == "tenyearjianyan"
            && active.activationRef.ownerObjectName == active.owner->objectName()
            && active.activationRef.key.instanceID == active.instanceID) {
            const auto *skill = dynamic_cast<const TenyearJianyan *>(Sanguosha->getSkill("tenyearjianyan"));
            if (!skill || !skill->commitQuota(active)) active.is_canceled = true;
            data = QVariant::fromValue(active);
        }
        return true;
    }
};
TenyearAnxuCard::TenyearAnxuCard()
{
    setSkillName("tenyearanxu");
}

bool TenyearAnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (to_select == Self)
		return false;
	if (targets.isEmpty())
		return true;
	else if (targets.length() == 1)
		return to_select->getHandcardNum() != targets.first()->getHandcardNum();
	else
		return false;
}

bool TenyearAnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void TenyearAnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	QList<ServerPlayer *> selecteds = targets;
	ServerPlayer *from = selecteds.first()->getHandcardNum() < selecteds.last()->getHandcardNum() ? selecteds.takeFirst() : selecteds.takeLast();
	ServerPlayer *to = selecteds.takeFirst();
	int id = room->askForCardChosen(from, to, "h", "tenyearanxu");
	const Card *cd = Sanguosha->getCard(id);
	from->obtainCard(cd);
	room->showCard(from, id);
	if (cd->getSuit() != Card::Spade)
		source->drawCards(1, "tenyearanxu");
	if (from->isAlive() && to->isAlive() && from->getHandcardNum() == to->getHandcardNum())
		room->recover(source, RecoverStruct("tenyearanxu", source));
}

class TenyearAnxu : public ViewAsSkillV2
{
public:
    TenyearAnxu() : ViewAsSkillV2("tenyearanxu", 0) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearAnxuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && target != request.initiator
            && !selected.contains(target) && selected.size() < 2
            && (selected.isEmpty() || selected.first()->getHandcardNum() != target->getHandcardNum());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 2 && selected.first()->getHandcardNum() != selected.last()->getHandcardNum(); }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { ctx.manual_effect = true; return ctx.targets.size() == 2; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.targets.size() != 2) return FinishSkill;
        ServerPlayer *receiver = ctx.targets.first(), *donor = ctx.targets.last();
        if (!receiver->isAlive() || !donor->isAlive() || receiver->getHandcardNum() == donor->getHandcardNum()) return FinishSkill;
        if (receiver->getHandcardNum() > donor->getHandcardNum()) qSwap(receiver, donor);
        ctx.extra_data = donor->objectName();
        ctx.choice = "receive";
        skillEffect(ctx, receiver);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else if (ctx.choice == "recover") {
            room->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
        } else {
            ServerPlayer *donor = room->findPlayerByObjectName(ctx.extra_data.toString());
            if (!donor || donor == target) return ContinueEffects;
            const int count = getEffectiveAmount(ctx);
            for (int i = 0; i < count && target->isAlive() && donor->isAlive() && target->canGetCard(donor, "h"); ++i) {
                const int id = room->askForCardChosen(target, donor, "h", objectName(), false, Card::MethodGet);
                if (id < 0 || !donor->handCards().contains(id) || !target->canGetCard(donor, id)) break;
                const Card::Suit suit = Sanguosha->getCard(id)->getSuit();
                room->obtainCard(target, id, false);
                if (target->handCards().contains(id)) room->showCard(target, id);
                if (suit != Card::Spade && ctx.invoker->isAlive()) {
                    ctx.choice = "draw";
                    skillEffect(ctx, ctx.invoker);
                    ctx.choice = "receive";
                }
            }
            // Compare the actual hands after transfer; the source's reward has its own hook.
            if (target->isAlive() && donor->isAlive() && target->getHandcardNum() == donor->getHandcardNum() && ctx.invoker->isAlive()) {
                ctx.choice = "recover";
                skillEffect(ctx, ctx.invoker);
                ctx.choice = "receive";
            }
        }
        return ContinueEffects;
    }
};

class TenyearZhuiyi : public TriggerSkillV2
{
public:
    TenyearZhuiyi() : TriggerSkillV2("tenyearzhuiyi") { events << Death; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Death is broadcast to observers; only the victim's dispatch offers its copies.
        return player && data.value<DeathStruct>().who == player && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        const QList<ServerPlayer *> targets = death.damage && death.damage->from
            ? room->getOtherPlayers(death.damage->from) : room->getAlivePlayers();
        const int alive = room->alivePlayerCount();
        if (targets.isEmpty() || alive <= 0) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(),
            "@tenyearzhuiyi-invoke:" + QString::number(alive), true, true);
        if (!target) return false;
        ctx.targets << target;
        ctx.extra_data = alive;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.owner->peiyin(this);
        target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        if (target->isAlive()) room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)), true);
        return false;
    }
};

class TenyearJianying : public Jianying
{
public:
	TenyearJianying() : Jianying()
	{
		setObjectName("tenyearjianying");
		jianying = "TenyearJianying";
	}
};

class TenyearShibei : public TriggerSkillV2
{
public:
    TenyearShibei() : TriggerSkillV2("tenyearshibei") { events << Damaged; frequency = Compulsory; }
    static int damageOrdinal(Room *room, ServerPlayer *player)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        const qint64 current = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (!turn || !current) return 0;
        const QVariantMap history = room->queryActualDamage({{"turn_id", turn}, {"to", player->objectName()}, {"limit", 2}});
        if (!history.value("complete").toBool() || !history.value("error").toString().isEmpty()) return 0;
        int ordinal = 0;
        QSet<qint64> seen;
        for (const QVariant &entry : history.value("items").toList()) {
            const qint64 event = entry.toMap().value("event_id").toLongLong();
            if (seen.contains(event)) continue;
            seen.insert(event);
            ++ordinal;
            if (event == current) return ordinal;
        }
        return 0;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && damageOrdinal(room, player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.extra_data = damageOrdinal(room, ctx.owner);
        ctx.targets << ctx.owner;
        return ctx.extra_data.toInt() > 0;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const bool first = ctx.extra_data.toInt() == 1;
        room->sendCompulsoryTriggerLog(ctx.owner, this, first ? 2 : 1);
        if (first) room->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
        else if (getEffectiveAmount(ctx) > 0)
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return false;
    }
};
class TenyearZhanjueVS : public ZeroCardViewAsSkill
{
public:
	TenyearZhanjueVS() : ZeroCardViewAsSkill("tenyearzhanjue")
	{

	}

	const Card *viewAs() const
	{
		Duel *duel = new Duel(Card::SuitToBeDecided, -1);
		foreach (const Card *c, Self->getHandcards()) {
			if (Self->getMark("tenyearzhanjueIgnore_" + QString::number(c->getEffectiveId()) + "-Clear") <= 0)
				duel->addSubcard(c);
		}
		if (duel->subcardsLength() <= 0) return nullptr;
		duel->setSkillName("tenyearzhanjue");
		return duel;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		bool can_use = false;
		foreach (const Card *c, player->getHandcards()) {
			if (player->getMark("tenyearzhanjueIgnore_" + c->toString() + "-Clear") <= 0) {
				can_use = true;
				break;
			}
		}
		return player->getMark("tenyearzhanjuedraw") < 3 && can_use;
	}
};

class TenyearZhanjue : public TriggerSkill
{
public:
	TenyearZhanjue() : TriggerSkill("tenyearzhanjue")
	{
		view_as_skill = new TenyearZhanjueVS;
		events << CardFinished << DamageDone << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->isKindOf("Duel") && damage.card->getSkillNames().contains("tenyearzhanjue") && damage.from) {
				QVariantMap m = room->getTag("tenyearzhanjue").toMap();
				QVariantList l = m.value(damage.card->toString(), QVariantList()).toList();
				l << QVariant::fromValue(damage.to);
				m[damage.card->toString()] = l;
				room->setTag("tenyearzhanjue", m);
			}
		} else if (triggerEvent == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card != nullptr && use.card->isKindOf("Duel") && use.card->getSkillNames().contains("tenyearzhanjue")) {
				QVariantMap m = room->getTag("tenyearzhanjue").toMap();
				QVariantList l = m.value(use.card->toString(), QVariantList()).toList();
				if (!l.isEmpty()) {
					QList<ServerPlayer *> l_copy;
					foreach (const QVariant &s, l)
						l_copy << s.value<ServerPlayer *>();
					l_copy << use.from;
					int n = l_copy.count(use.from);
					room->addPlayerMark(use.from, "tenyearzhanjuedraw", n);
					room->sortByActionOrder(l_copy);
					room->drawCards(l_copy, 1, objectName());
				}
				m.remove(use.card->toString());
				room->setTag("tenyearzhanjue", m);
			}
		} else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive)
				room->setPlayerMark(player, "tenyearzhanjuedraw", 0);
		}
		return false;
	}
};

TenyearQinwangCard::TenyearQinwangCard()
{
	target_fixed = true;
}

void TenyearQinwangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<ServerPlayer *> targets;
	foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
		if (p->isDead() || p->getKingdom() != "shu" || p->isNude()) continue;
		const Card *c = room->askForCard(p, "Slash", "@tenyearqinwang-give:" + source->objectName(), QVariant::fromValue(source),
						Card::MethodNone);
		if (!c) continue;
		targets << p;
		room->giveCard(p, source, c, "tenyearqinwang", true);
		if (source->handCards().contains(c->getEffectiveId()) && room->hasCurrent())
			room->addPlayerMark(source, "tenyearzhanjueIgnore_" + QString::number(c->getEffectiveId()) + "-Clear");
	}
	if (targets.isEmpty() || !source->askForSkillInvoke("tenyearqinwang", "tenyearqinwang", false)) return;
	room->drawCards(targets, 1, "tenyearqinwang");
}

class TenyearQinwang : public ZeroCardViewAsSkill
{
public:
	TenyearQinwang() : ZeroCardViewAsSkill("tenyearqinwang$")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		bool shu = false;
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getKingdom() == "shu") {
				shu = true;
				break;
			}
		}
		return !player->hasUsed("TenyearQinwangCard") && shu;
	}

	const Card *viewAs() const
	{
		return new TenyearQinwangCard;
	}
};

TenyearXiansiCard::TenyearXiansiCard()
{
will_throw = false;
handling_method = Card::MethodNone;
}

bool TenyearXiansiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void TenyearXiansiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->throwCard(subcards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), "tenyearxiansi", ""), nullptr);
	if (source->isDead()) return;
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	room->useCard(CardUseStruct(slash, source, targets), true);
	slash->deleteLater();
}

class TenyearXiansiViewAsSkill : public OneCardViewAsSkill
{
public:
	TenyearXiansiViewAsSkill() : OneCardViewAsSkill("tenyearxiansi")
	{
		expand_pile = "counter";
		filter_pattern = ".|.|.|counter";
	}

	const Card *viewAs(const Card *card) const
	{
		TenyearXiansiCard *c = new TenyearXiansiCard;
		c->addSubcard(card);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player) && !player->getPile("counter").isEmpty() && player->getPile("counter").length() > player->getHp();
	}
};

class TenyearXiansi : public PhaseChangeSkill
{
public:
	TenyearXiansi() : PhaseChangeSkill("tenyearxiansi")
	{
		view_as_skill = new TenyearXiansiViewAsSkill;
		waked_skills = "#tenyearxiansi-attach";
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (!p->isNude())
				targets << p;
		}
		if (targets.isEmpty()) return false;

		QList<ServerPlayer *> tos = room->askForPlayersChosen(player, targets, objectName(), 0, 2, "@tenyearxiansi-invoke", true);
		if (tos.isEmpty()) return false;
		player->peiyin(this, 2);

		foreach (ServerPlayer *p, tos) {
			if (player->isDead()) break;
			if (p->isDead() || p->isNude()) continue;
			int id = room->askForCardChosen(player, p, "he", objectName());
			player->addToPile("counter", id);
		}
		return false;
	}

	int getEffectIndex(const ServerPlayer *, const Card *card) const
	{
		int index = 2;
		if (card->isKindOf("Slash") && card->isVirtualCard() && card->subcardsLength() == 0)
			index = 1;
		return index;
	}
};

class TenyearXiansiAttach : public TriggerSkill
{
public:
	TenyearXiansiAttach() : TriggerSkill("#tenyearxiansi-attach")
	{
		events << GameStart << EventAcquireSkill << EventLoseSkill << Debut;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if ((triggerEvent == GameStart && TriggerSkill::triggerable(player))
			|| triggerEvent == EventAcquireSkill) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (!p->hasSkill("tenyearxiansi_slash", true))
					room->attachSkillToPlayer(p, "tenyearxiansi_slash");
			}
		} else if (triggerEvent == EventLoseSkill && data.toString() == "tenyearxiansi") {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasSkill("tenyearxiansi_slash", true))
					room->detachSkillFromPlayer(p, "tenyearxiansi_slash", true);
			}
		} else if (triggerEvent == Debut) {
			foreach (ServerPlayer *liufeng, room->findPlayersBySkillName("tenyearxiansi")) {
				if (player != liufeng && !player->hasSkill("tenyearxiansi_slash", true)) {
					room->attachSkillToPlayer(player, "tenyearxiansi_slash");
					break;
				}
			}
		}
		return false;
	}
};

TenyearXiansiSlashCard::TenyearXiansiSlashCard()
{
	m_skillName = "tenyearxiansi_slash";
}

bool TenyearXiansiSlashCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();
	return slash->targetsFeasible(targets, Self);
}

bool TenyearXiansiSlashCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();
	if (targets.isEmpty()) {
		return to_select->getPile("counter").length() >= 2 && to_select->hasSkill("tenyearxiansi")
		&& slash->targetFilter(targets, to_select, Self);
	}
	return slash->targetFilter(targets, to_select, Self);
}

const Card *TenyearXiansiSlashCard::validate(CardUseStruct &cardUse) const
{
	Room *room = cardUse.from->getRoom();

	room->throwCard(subcards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, cardUse.from->objectName(), "tenyearxiansi", ""), nullptr);

	Slash *slash = new Slash(Card::SuitToBeDecided, -1);
	slash->setSkillName("_tenyearxiansi");
	slash->deleteLater();

	return slash;
}

class TenyearXiansiSlashViewAsSkill : public ViewAsSkill
{
public:
	TenyearXiansiSlashViewAsSkill() : ViewAsSkill("tenyearxiansi_slash")
	{
		attached_lord_skill = true;
		expand_pile = "%counter";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player) && canSlashLiufeng(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return (pattern.contains("slash") || pattern.contains("Slash"))
			&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& canSlashLiufeng(player);
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (selected.length() >= 2)
			return false;

		foreach (const Player *p, Self->getAliveSiblings()) {
			if (p->hasSkill("tenyearxiansi") && p->getPile("counter").length() > 1) {
				return p->getPile("counter").contains(to_select->getId());
			}
		}

		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() == 2) {
			TenyearXiansiSlashCard *xs = new TenyearXiansiSlashCard;
			xs->addSubcards(cards);
			return xs;
		}
		return nullptr;
	}

private:
	static bool canSlashLiufeng(const Player *player)
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getPile("counter").length()>1&&p->hasSkill("tenyearxiansi")) {
				Slash *slash = new Slash(Card::SuitToBeDecided, -1);
				slash->setSkillName("_tenyearxiansi");
				slash->deleteLater();
				if (slash->targetFilter(QList<const Player *>(), p, player))
					return true;
			}
		}
		return false;
	}
};

class TenyearFenli : public TriggerSkillV2
{
public:
    TenyearFenli() : TriggerSkillV2("tenyearfenli") { events << EventPhaseChanging; }
    bool eligible(Room *room, ServerPlayer *owner, Player::Phase phase) const
    {
        if (!owner || !owner->isAlive() || owner->isSkipped(phase)) return false;
        if (phase != Player::Judge && phase != Player::Play && phase != Player::Discard) return false;
        if (phase == Player::Discard && owner->getEquips().isEmpty()) return false;
        for (ServerPlayer *other : room->getOtherPlayers(owner)) {
            if (phase == Player::Judge && other->getHandcardNum() > owner->getHandcardNum()) return false;
            if (phase == Player::Play && other->getHp() > owner->getHp()) return false;
            if (phase == Player::Discard && other->getEquips().size() > owner->getEquips().size()) return false;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->hasSkill(objectName()) && eligible(room, player, data.value<PhaseChangeStruct>().to)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Player::Phase phase = ctx.original_data->value<PhaseChangeStruct>().to;
        if (!eligible(room, ctx.owner, phase)) return false;
        ctx.choice = phase == Player::Judge ? "judge" : phase == Player::Play ? "play" : "discard";
        if (!ctx.owner->askForSkillInvoke(this, ctx.choice)) return false;
        ctx.extra_data = static_cast<int>(phase);
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Player::Phase phase = static_cast<Player::Phase>(ctx.extra_data.toInt());
        if (target->isSkipped(phase)) return false;
        target->peiyin(this);
        target->skip(phase);
        // Judge and the following Draw are a single accepted option.
        if (phase == Player::Judge && !target->isSkipped(Player::Draw)) target->skip(Player::Draw);
        return false;
    }
};

class TenyearPingkou : public TriggerSkillV2
{
public:
    TenyearPingkou() : TriggerSkillV2("tenyearpingkou") { events << EventPhaseChanging; }
    static int skippedPhases(Room *room, ServerPlayer *owner)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (!turn) return -1;
        QVariantMap filter{{"kind", "phase"}, {"turn_id", turn}, {"player", owner->objectName()}};
        int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryEvents(filter);
            if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return -1;
            for (const QVariant &entry : page.value("items").toList())
                if (entry.toMap().value("outcome").toString() == "skipped") ++count;
            if (!page.value("has_more").toBool()) return count;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::NotActive && skippedPhases(room, player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int maximum = skippedPhases(room, ctx.owner);
        if (maximum <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), 0,
            maximum, "@tenyearpingkou-invoke:" + QString::number(maximum), true);
        ctx.extra_data = maximum;
        ctx.manual_effect = true;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->peiyin(this);
        const QList<ServerPlayer *> recipients = ctx.targets;
        ctx.choice = "damage";
        for (ServerPlayer *target : recipients) skillEffect(event, room, ctx.owner, ctx, target);
        if (ctx.extra_data.toInt() <= recipients.size() || !ctx.owner->isAlive()) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : recipients)
            if (target->isAlive() && !target->getEquips().isEmpty()) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@tenyearpingkou-player");
        if (target) {
            ctx.choice = "discard";
            skillEffect(event, room, ctx.owner, ctx, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "damage") {
            if (amount > 0) room->damage(DamageStruct(objectName(), ctx.owner, target, amount));
        } else {
            room->doAnimate(1, ctx.owner->objectName(), target->objectName());
            for (int i = 0; i < amount && target->isAlive(); ++i) {
                QList<int> ids;
                for (int id : target->getEquipsId())
                    if (target->canDiscard(target, id)) ids << id;
                if (ids.isEmpty()) break;
                room->throwCard(ids.at(qsanRandomBounded(ids.size())), target);
            }
        }
        return false;
    }
};

TenyearFenchengCard::TenyearFenchengCard()
{
    setSkillName("tenyearfencheng");
}

void TenyearFenchengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->removePlayerMark(source, "@tenyearfenchengMark");
	room->doSuperLightbox(source, "tenyearfencheng");
	room->setTag("TenyearFenchengDiscard", 0);

	QList<ServerPlayer *> tos;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if(p==targets.first()||tos.contains(targets.first()))
			tos << p;
	}
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if(!tos.contains(p))
			tos << p;
	}
	source->setFlags("TenyearFenchengUsing");

	try {
		foreach (ServerPlayer *p, tos) {
			if (p->isAlive() && p != source) {
				room->cardEffect(this, source, p);
				room->getThread()->delay();
				if(source->isDead()) return;
			}
		}
		source->setFlags("-TenyearFenchengUsing");
	}
	catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken || triggerEvent == StageChange)
			source->setFlags("-TenyearFenchengUsing");
		throw triggerEvent;
	}
}

void TenyearFenchengCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();

	if(effect.to->canDiscard(effect.to, "he")){
		int length = room->getTag("TenyearFenchengDiscard").toInt() + 1;
		const Card *c = room->askForDiscard(effect.to, "tenyearfencheng", 999, length, true, true, "@fencheng:::" + QString::number(length));
		if (c){
			room->setTag("TenyearFenchengDiscard", c->subcardsLength());
			return;
		}
	}
	room->setTag("TenyearFenchengDiscard", 0);
	room->damage(DamageStruct("tenyearfencheng", effect.from, effect.to, 2, DamageStruct::Fire));
}

class TenyearFencheng : public ViewAsSkillV2
{
public:
    TenyearFencheng() : ViewAsSkillV2("tenyearfencheng", 0)
    { frequency = Limited; limit_mark = "@tenyearfenchengMark"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearFenchengCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.targets.size() != 1 || !ctx.invoker) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        room->doSuperLightbox(ctx.owner, objectName());
        QList<ServerPlayer *> ordered = room->getAlivePlayers();
        const int first = ordered.indexOf(ctx.targets.first());
        if (first < 0) return FinishSkill;
        ordered = ordered.mid(first) + ordered.mid(0, first);
        const QVariant oldHint = room->getTag("TenyearFenchengDiscard");
        const bool oldFlag = ctx.invoker->hasFlag("TenyearFenchengUsing");
        const auto restore = qScopeGuard([&] {
            room->setTag("TenyearFenchengDiscard", oldHint);
            if (!oldFlag) room->setPlayerFlag(ctx.invoker, "-TenyearFenchengUsing");
        });
        room->setPlayerFlag(ctx.invoker, "TenyearFenchengUsing");
        int previous = 0;
        for (ServerPlayer *target : ordered) {
            if (!ctx.invoker->isAlive()) break;
            if (!target->isAlive() || target == ctx.invoker) continue;
            // The chain value belongs to this execution; Room tag is a scoped AI hint only.
            room->setTag("TenyearFenchengDiscard", previous);
            SkillContext recipient = ctx; recipient.extra_data = previous;
            skillEffect(recipient, target);
            previous = recipient.extra_data.toInt();
            if (previous < 0) break; // Incomplete history is not an empty discard.
        }
        return FinishSkill;
    }
    static int discardedCards(Room *room, ServerPlayer *target, qint64 skillEvent, qint64 after, const QList<int> &selected)
    {
        if (!skillEvent) return -1;
        QVariantMap filter{{"from", target->objectName()}, {"after", after}};
        QSet<QString> seen;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap(), move = fact.value("data").toMap();
                if (room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() != skillEvent)
                    continue;
                if (move.value("reason_skill").toString() != "tenyearfencheng"
                    || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
                    || move.value("to_place").toInt() != Player::DiscardPile
                    || !selected.contains(move.value("card_id").toInt())) continue;
                seen.insert(fact.value("event_id").toString() + ":" + move.value("card_id").toString());
            }
            if (!page.value("has_more").toBool()) return seen.size();
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        }
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        const int minimum = ctx.extra_data.toInt() + 1;
        if (target->canDiscard(target, "he")) {
            const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
            const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
            if (!skillEvent || !before.value("complete").toBool()) { ctx.extra_data = -1; return ContinueEffects; }
            const Card *discard = room->askForDiscard(target, objectName(), 999, minimum, true, true,
                "@fencheng:::" + QString::number(minimum));
            if (discard) {
                // A legal discard choice stays chosen, but only committed cards raise the next threshold.
                ctx.extra_data = discardedCards(room, target, skillEvent, before.value("watermark").toLongLong(), discard->getSubcards());
                return ContinueEffects;
            }
        }
        ctx.extra_data = 0;
        room->damage(DamageStruct(objectName(), ctx.invoker, target, 2 * amount, DamageStruct::Fire));
        return ContinueEffects;
    }
};
TenyearMiejiCard::TenyearMiejiCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void TenyearMiejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "", "tenyearmieji", "");
	room->moveCardTo(this, source, nullptr, Player::DrawPile, reason, true);

	if (source->isDead()) return;

	QList<ServerPlayer *> targets;
	foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
		if (p->isKongcheng()) continue;
		targets << p;
	}
	if (targets.isEmpty()) return;

	ServerPlayer *t = room->askForPlayerChosen(source, targets, "tenyearmieji", "@tenyearmieji-target");
	room->doAnimate(1, source->objectName(), t->objectName());

	QList<const Card *> cards = t->getCards("he");
	QList<const Card *> cardsCopy = cards;

	foreach (const Card *c, cardsCopy) {
		if (t->isJilei(c))
			cards.removeOne(c);
	}

	if (cards.length() == 0)
		return;

	bool instanceDiscard = false;
	int instanceDiscardId = -1;

	if (cards.length() == 1)
		instanceDiscard = true;
	else if (cards.length() == 2) {
		bool bothTrick = true;
		int trickId = -1;

		foreach (const Card *c, cards) {
			if (c->getTypeId() != Card::TypeTrick)
				bothTrick = false;
			else
				trickId = c->getId();
		}

		instanceDiscard = !bothTrick;
		instanceDiscardId = trickId;
	}

	if (instanceDiscard) {
		DummyCard d;
		if (instanceDiscardId == -1)
			d.addSubcards(cards);
		else
			d.addSubcard(instanceDiscardId);
		room->throwCard(&d, t);
	} else if (!room->askForCard(t, "@@tenyearmiejidiscard!", "@mieji-discard")) {
		DummyCard d;
		qsanShuffle(cards);
		int trickId = -1;
		foreach (const Card *c, cards) {
			if (c->getTypeId() == Card::TypeTrick) {
				trickId = c->getId();
				break;
			}
		}
		if (trickId > -1)
			d.addSubcard(trickId);
		else {
			d.addSubcard(cards.first());
			d.addSubcard(cards.last());
		}

		room->throwCard(&d, t);
	}
}

class TenyearMieji: public OneCardViewAsSkill
{
public:
	TenyearMieji() : OneCardViewAsSkill("tenyearmieji")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->isKindOf("Weapon") || (to_select->isKindOf("TrickCard") && to_select->isBlack());
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("TenyearMiejiCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		TenyearMiejiCard *card = new TenyearMiejiCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class TenyearMiejiDiscard : public ViewAsSkill
{
public:
	TenyearMiejiDiscard() : ViewAsSkill("tenyearmiejidiscard")
	{

	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@tenyearmiejidiscard!";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Self->isJilei(to_select))
			return false;

		if (selected.length() == 0)
			return true;
		else if (selected.length() == 1) {
			if (selected.first()->getTypeId() == Card::TypeTrick)
				return false;
			else
				return to_select->getTypeId() != Card::TypeTrick;
		} else
			return false;

		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		bool ok = false;
		if (cards.length() == 1)
			ok = cards.first()->getTypeId() == Card::TypeTrick;
		else if (cards.length() == 2) {
			ok = true;
			foreach (const Card *c, cards) {
				if (c->getTypeId() == Card::TypeTrick) {
					ok = false;
					break;
				}
			}
		}

		if (!ok)
			return nullptr;

		DummyCard *dummy = new DummyCard;
		dummy->addSubcards(cards);
		return dummy;
	}
};

TenyearMingceCard::TenyearMingceCard()
{
    setSkillName("tenyearmingce");
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TenyearMingceCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QList<ServerPlayer *> targets;
	if (Slash::IsAvailable(effect.to)) {
		foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
			if (effect.to->canSlash(p, false))
				targets << p;
		}
	}

	ServerPlayer *target = nullptr;
	QStringList choicelist;
	choicelist << "draw";
	if (!targets.isEmpty() && effect.from->isAlive()) {
		target = room->askForPlayerChosen(effect.from, targets, "tenyearmingce", "@dummy-slash2:" + effect.to->objectName());
		target->setFlags("TenyearMingceTarget"); // For AI
		room->doAnimate(1, effect.to->objectName(), target->objectName());

		LogMessage log;
		log.type = "#CollateralSlash";
		log.from = effect.from;
		log.to << target;
		room->sendLog(log);

		choicelist << "use";
	}

	try {
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "tenyearmingce", "");
		room->obtainCard(effect.to, this, reason);
	}
	catch (TriggerEvent triggerEvent) {
		if (triggerEvent == TurnBroken || triggerEvent == StageChange)
			if (target && target->hasFlag("TenyearMingceTarget")) target->setFlags("-TenyearMingceTarget");
		throw triggerEvent;
	}

	QString choice = room->askForChoice(effect.to, "tenyearmingce", choicelist.join("+"));
	if (target && target->hasFlag("TenyearMingceTarget")) target->setFlags("-TenyearMingceTarget");

	if (choice == "use") {
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_tenyearmingce");
		slash->deleteLater();
		if (effect.to->canSlash(target, slash, false)) {
			room->setCardFlag(slash, QString("tenyearmingce_%1_%2").arg(effect.from->objectName()).arg(effect.to->objectName()));
			room->useCard(CardUseStruct(slash, effect.to, target));
		}
	} else if (choice == "draw") {
		QList<ServerPlayer *> drawers;
		drawers << effect.from << effect.to;
		room->sortByActionOrder(drawers);
		room->drawCards(drawers, 1, "tenyearmingce");
	}
}

// Accepted active metadata and public limited-skill projections share this exact-source observer.
class TenyearActiveMetadata : public TriggerSkillV2
{
public:
    explicit TenyearActiveMetadata(const QString &name) : TriggerSkillV2("#" + name + "-metadata"), skillName(name)
    { events << EventSkillInvoking; if (name == "tenyeargongqi") events << EventPhaseChanging; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
            const QString turn = room->historyScopes().value("turn_id").toString();
            for (ServerPlayer *recipient : room->getAllPlayers(true)) {
                QVariantList kept; QList<int> expired;
                for (const QVariant &value : recipient->getTag("tenyeargongqi_grants").toList()) {
                    const QVariantMap receipt = value.toMap();
                    if (receipt.value("turn").toString() == turn) expired << receipt.value("id").toInt();
                    else kept << value;
                }
                recipient->setTag("tenyeargongqi_grants", kept);
                for (int id : expired)
                    room->detachSkillFromPlayer(recipient, SkillInstanceUtils::formatName("#tenyeargongqi-target", id), false, true, false);
            }
            return true;
        }
        SkillContext ctx = data.value<SkillContext>();
        if (!ctx.owner || !ctx.initiator || !ctx.invoker || !ctx.activationRef.isValid()
            || ctx.activationRef.key.skillName != skillName
            || ctx.activationRef.ownerObjectName != ctx.owner->objectName()
            || ctx.activationRef.key.instanceID != ctx.instanceID) return true;
        if (skillName == "tenyearfencheng") {
            room->removePlayerMark(ctx.owner, "@tenyearfenchengMark");
            return true;
        }
        if (!ctx.bypass_cost) return true;
        if (skillName == "tenyearqimou") {
            if (ctx.choice.isEmpty()) {
                QStringList choices;
                for (int n = 1; n <= ctx.initiator->getHp(); ++n) choices << QString::number(n);
                if (!choices.isEmpty()) ctx.choice = room->askForChoice(ctx.invoker, skillName, choices.join("+"));
            }
            const int count = ctx.choice.toInt();
            if (count <= 0 || count > ctx.initiator->getHp()) ctx.is_canceled = true;
            else room->removePlayerMark(ctx.owner, "@tenyearqimouMark");
        } else if (ctx.use_card) {
            const QList<int> selected = ctx.use_card->getSubcards();
            if (skillName == "tenyearzhiheng") {
                bool allHand = !ctx.initiator->isKongcheng();
                for (int id : ctx.initiator->handCards()) if (!selected.contains(id)) allHand = false;
                ctx.extra_data = QVariantMap{{"count", selected.size()}, {"all_hand", allHand}};
            } else if (skillName == "tenyeargongqi" && selected.size() == 1) {
                const Card *card = Sanguosha->getCard(selected.first());
                ctx.extra_data = QVariantMap{{"suit", card->getSuitString()}, {"equipment", card->isKindOf("EquipCard")}};
            } else if (skillName == "tenyearrende") {
                ctx.extra_data = selected.size();
            }
        }
        data = QVariant::fromValue(ctx);
        return true;
    }
private:
    QString skillName;
};

class TenyearMingceVS : public ViewAsSkillV2
{
public:
    TenyearMingceVS() : ViewAsSkillV2("tenyearmingce", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TenyearMingceCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && (card->isKindOf("EquipCard") || card->isKindOf("Slash"))
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0
            || request.selectedCardIds.first() >= Sanguosha->getCardCount()) return false;
        ActiveSkillRequest selection = request; selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool willThrowSelectedCards() const override { return false; }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        if (ctx.choice == "draw") { target->drawCards(amount, objectName()); return ContinueEffects; }
        ServerPlayer *payer = ctx.initiator;
        if (!payer || !ctx.use_card || ctx.use_card->getSubcards().size() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != payer || (room->getCardPlace(id) != Player::PlaceHand
            && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        QList<ServerPlayer *> candidates;
        if (Slash::IsAvailable(target))
            for (ServerPlayer *other : room->getOtherPlayers(target))
                if (target->canSlash(other, false)) candidates << other;
        ServerPlayer *victim = nullptr;
        if (!candidates.isEmpty() && ctx.invoker->isAlive())
            victim = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@dummy-slash2:" + target->objectName());
        const bool previousFlag = victim && victim->hasFlag("TenyearMingceTarget");
        const auto restore = qScopeGuard([&] {
            if (victim && !previousFlag) room->setPlayerFlag(victim, "-TenyearMingceTarget");
        });
        if (victim) {
            room->setPlayerFlag(victim, "TenyearMingceTarget");
            room->doAnimate(1, target->objectName(), victim->objectName());
            LogMessage log; log.type = "#CollateralSlash"; log.from = ctx.invoker; log.to << victim;
            room->sendLog(log);
        }
        // The receiver's hook authorizes the gift. Recheck after target selection callbacks.
        if (room->getCardOwner(id) != payer || (room->getCardPlace(id) != Player::PlaceHand
            && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        room->giveCard(payer, target, Sanguosha->getCard(id), objectName());
        if (!target->isAlive()) return ContinueEffects;
        const QString choice = room->askForChoice(target, objectName(), victim ? "draw+use" : "draw");
        if (choice == "use" && victim && victim->isAlive()) {
            std::unique_ptr<Slash> slash(new Slash(Card::NoSuit, 0));
            slash->setSkillName("_tenyearmingce");
            if (!target->canSlash(victim, slash.get(), false)) return ContinueEffects;
            // A primitive receipt belongs to this generated card and survives loss of its grant.
            slash->setTag("tenyearmingce_receipt", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_instance", ctx.activationRef.key.instanceID},
                {"actor", ctx.invoker->objectName()}, {"receiver", target->objectName()}, {"amount", amount}});
            CardUseStruct use(slash.get(), target, victim);
            use.setOwnedCard(slash.release());
            room->useCardFromSkillEffect(use, ctx, true);
        } else if (choice == "draw") {
            QList<ServerPlayer *> recipients{ctx.invoker, target}; room->sortByActionOrder(recipients);
            for (ServerPlayer *recipient : recipients) {
                SkillContext draw = ctx; draw.choice = "draw";
                skillEffect(draw, recipient);
            }
        }
        return ContinueEffects;
    }
};

class TenyearMingce : public TriggerSkillV2
{
public:
    TenyearMingce() : TriggerSkillV2("tenyearmingce")
    { events << DamageComplete << CardFinished; global = true; view_as_skill = new TenyearMingceVS; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card;
            if (card) card->removeTag("tenyearmingce_receipt");
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DamageComplete) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash")) return true;
        const QVariantMap receipt = damage.card->getTag("tenyearmingce_receipt").toMap();
        if (receipt.isEmpty()) return true;
        const qint64 damageId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (damageId <= 0) return true;
        const QVariantMap facts = room->queryActualDamage({{"event_id", damageId}, {"limit", 1}});
        if (!facts.value("complete").toBool() || !facts.value("error").toString().isEmpty()
            || facts.value("items").toList().isEmpty()) return true;
        const QVariantMap actual = facts.value("items").toList().first().toMap().value("data").toMap();
        if (actual.value("hp_loss").toInt() + actual.value("absorbed").toInt() <= 0) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
        ctx.owner = room->findPlayerByObjectName(receipt.value("activation_owner").toString(), true);
        ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
        ServerPlayer *receiver = room->findPlayerByObjectName(receipt.value("receiver").toString(), true);
        if (!ctx.owner || !ctx.initiator || !receiver) return true;
        ctx.invoker = player; ctx.instanceID = receipt.value("activation_instance").toInt();
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
            SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt;
        ctx.targets = {ctx.initiator, receiver}; room->sortByActionOrder(ctx.targets);
        // No live activationRef: this is fulfillment of an already accepted Slash receipt.
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        const Card *card = ctx.original_data->value<DamageStruct>().card;
        return card && !ctx.extra_data.toMap().isEmpty() && card->getTag("tenyearmingce_receipt") == ctx.extra_data;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (isSourceAvailable(room, ctx)) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class TenyearZhiyu : public TriggerSkill
{
public:
	TenyearZhiyu() : TriggerSkill("tenyearzhiyu")
	{
		events << EventPhaseChanging << EventPhaseStart << Damaged;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damaged){
			if (player->askForSkillInvoke(this, data)) {
				player->drawCards(1, objectName());
				player->peiyin(this);
	
				if (!player->isKongcheng())
					room->showAllCards(player, static_cast<ServerPlayer *>(nullptr));

				const Card *card = nullptr;
				DamageStruct damage = data.value<DamageStruct>();
				if (damage.from && damage.from->isAlive() && !damage.from->isKongcheng())
					card = room->askForDiscard(damage.from, objectName(), 1, 1);
	
				if (player->isKongcheng()) return false;
	
				QList<const Card *> cards = player->getHandcards();
				foreach (const Card *h, cards) {
					if (h->getColor() != cards.first()->getColor())
						return false;
				}
	
				if (card && room->CardInPlace(card, Player::DiscardPile))
					room->obtainCard(player, card);
				room->addPlayerMark(player, "&tenyearzhiyu_buff-SelfClear");
			}
		}else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			int mark = player->getMark("&tenyearzhiyu_buff-SelfClear");
			if (mark <= 0) return false;

			LogMessage log;
			log.type = "#TenyearZhiyuQice";
			log.from = player;
			log.arg = QString::number(mark);
			log.arg2 = "qice";
			room->sendLog(log);

			room->setPlayerMark(player, "SkillDescriptionArg1_qice", mark+1);
			player->setSkillDescriptionSwap("qice","%arg1",QString::number(mark+1));
			room->changeTranslation(player, "qice", 1);
		} else {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			room->setPlayerMark(player, "SkillDescriptionArg1_qice", 0);
			room->changeTranslation(player, "qice", 0);
		}
		return false;
	}
};

TenyearStStandardPackage::TenyearStStandardPackage()
	: Package("tenyear_st_standard")
{
	General *tenyear_sunquan = new General(this, "tenyear_sunquan$", "wu", 4);
	tenyear_sunquan->addSkill(new TenyearZhiheng);
    tenyear_sunquan->addSkill(new TenyearActiveMetadata("tenyearzhiheng"));
    related_skills.insertMulti("tenyearzhiheng", "#tenyearzhiheng-metadata");
	tenyear_sunquan->addSkill(new TenyearJiuyuan);

	General *tenyear_sunshangxiang = new General(this, "tenyear_sunshangxiang", "wu", 3, false);
	tenyear_sunshangxiang->addSkill(new TenyearJieyin);
	tenyear_sunshangxiang->addSkill("xiaoji");

	General *tenyear_liubei = new General(this, "tenyear_liubei$", "shu", 4);
	tenyear_liubei->addSkill(new TenyearRende);
    tenyear_liubei->addSkill(new TenyearActiveMetadata("tenyearrende"));
    related_skills.insertMulti("tenyearrende", "#tenyearrende-metadata");
	tenyear_liubei->addSkill("jijiang");

	General *tenyear_guanyu = new General(this, "tenyear_guanyu", "shu", 4);
	tenyear_guanyu->addSkill(new TenyearWusheng);
	tenyear_guanyu->addSkill(new TenyearWushengMod);
	tenyear_guanyu->addSkill(new TenyearYijue);
	related_skills.insert("tenyearwusheng", "#tenyearwushengmod");

	General *tenyear_zhangfei = new General(this, "tenyear_zhangfei", "shu", 4);
	tenyear_zhangfei->addSkill(new TenyearPaoxiao);
	tenyear_zhangfei->addSkill(new TenyearTishen);

	General *tenyear_zhugeliang = new General(this, "tenyear_zhugeliang", "shu", 3);
	tenyear_zhugeliang->addSkill(new TenyearGuanxing);
	tenyear_zhugeliang->addSkill("kongcheng");

	General *tenyear_zhaoyun = new General(this, "tenyear_zhaoyun", "shu", 4);
	tenyear_zhaoyun->addSkill("longdan");
	tenyear_zhaoyun->addSkill(new TenyearYajiao);

	General *tenyear_huangyueying = new General(this, "tenyear_huangyueying", "shu", 3, false);
	tenyear_huangyueying->addSkill(new TenyearJizhi);
	tenyear_huangyueying->addSkill("qicai");

	General *tenyear_caocao = new General(this, "tenyear_caocao$", "wei", 4);
	tenyear_caocao->addSkill(new TenyearJianxiong);
	tenyear_caocao->addSkill("hujia");

	General *tenyear_xiahoudun = new General(this, "tenyear_xiahoudun", "wei", 4);
	tenyear_xiahoudun->addSkill("ganglie");
	tenyear_xiahoudun->addSkill(new TenyearQingjian);

	General *tenyear_xuchu = new General(this, "tenyear_xuchu", "wei", 4);
	tenyear_xuchu->addSkill(new TenyearLuoyi);
	tenyear_xuchu->addSkill(new TenyearLuoyiBuff);
	related_skills.insert("tenyearluoyi", "#tenyearluoyibuff");

	General *tenyear_guojia = new General(this, "tenyear_guojia", "wei", 3);
	tenyear_guojia->addSkill("tiandu");
	tenyear_guojia->addSkill(new TenyearYiji);

	General *tenyear_zhenji = new General(this, "tenyear_zhenji", "wei", 3, false);
	tenyear_zhenji->addSkill("qingguo");
	tenyear_zhenji->addSkill(new TenyearLuoshen);

	General *tenyear_zhangliao = new General(this, "tenyear_zhangliao", "wei");
	tenyear_zhangliao->addSkill(new TenyearTuxi);

	General *tenyear_huatuo = new General(this, "tenyear_huatuo", "qun", 3);
	tenyear_huatuo->addSkill("jijiu");
	tenyear_huatuo->addSkill(new TenyearQingnang);

	General *tenyear_lvbu = new General(this, "tenyear_lvbu", "qun", 5);
	tenyear_lvbu->addSkill("wushuang");
	tenyear_lvbu->addSkill(new TenyearLiyu);

	General *tenyear_diaochan = new General(this, "tenyear_diaochan", "qun", 3, false);
	tenyear_diaochan->addSkill("lijian");
	tenyear_diaochan->addSkill(new TenyearBiyue);

	General *tenyear_huaxiong = new General(this, "tenyear_huaxiong", "qun", 6);
	tenyear_huaxiong->addSkill(new TenyearYaowu);


	addMetaObject<TenyearZhihengCard>();
	addMetaObject<TenyearJieyinCard>();
	addMetaObject<TenyearYijueCard>();
	addMetaObject<TenyearQingjianCard>();
	addMetaObject<TenyearTuxiCard>();
	addMetaObject<TenyearQingnangCard>();
	addMetaObject<TenyearQimouCard>();
	addMetaObject<TenyearShensuCard>();
	addMetaObject<TenyearTianxiangCard>();
	addMetaObject<TenyearSanyaoCard>();
	addMetaObject<TenyearChunlaoCard>();
	addMetaObject<TenyearChunlaoWineCard>();
	addMetaObject<SecondTenyearChunlaoCard>();
	addMetaObject<SecondTenyearChunlaoWineCard>();
	addMetaObject<TenyearJiangchiCard>();
	addMetaObject<TenyearWurongCard>();
	addMetaObject<TenyearYanzhuCard>();
	addMetaObject<TenyearXingxueCard>();
	addMetaObject<TenyearShenduanCard>();
	addMetaObject<TenyearQiaoshuiCard>();
	addMetaObject<TenyearQiaoshuiTargetCard>();
	addMetaObject<TenyearXianzhenCard>();
	addMetaObject<SecondTenyearXianzhenCard>();
	addMetaObject<TenyearZishouCard>();
	addMetaObject<TenyearyongjinCard>();
	addMetaObject<TenyearXuanhuoCard>();
	addMetaObject<TenyearSidiCard>();
	addMetaObject<TenyearHuaiyiCard>();
	addMetaObject<TenyearHuaiyiSnatchCard>();
	addMetaObject<TenyearGongqiCard>();
	addMetaObject<TenyearJiefanCard>();
	addMetaObject<TenyearXianzhouDamageCard>();
	addMetaObject<TenyearXianzhouCard>();
	addMetaObject<TenyearShenxingCard>();
	addMetaObject<TenyearBingyiCard>();


	General *tenyear_guohuanghou = new General(this, "tenyear_guohuanghou", "wei", 3, false);
	tenyear_guohuanghou->addSkill(new TenyearJiaozhao);
	tenyear_guohuanghou->addSkill(new TenyearJiaozhaoPro);
	tenyear_guohuanghou->addSkill(new TenyearDanxin);
	related_skills.insert("tenyearjiaozhao", "#tenyearjiaozhao");

	addMetaObject<TenyearZongxuanCard>();
	addMetaObject<TenyearYjYanyuCard>();
	addMetaObject<TenyearJiaozhaoCard>();
	addMetaObject<TenyearGanluCard>();
	addMetaObject<TenyearJianyanCard>();
	addMetaObject<TenyearAnxuCard>();
	addMetaObject<TenyearQinwangCard>();
	addMetaObject<TenyearXiansiCard>();
	addMetaObject<TenyearXiansiSlashCard>();
	addMetaObject<TenyearFenchengCard>();
	addMetaObject<TenyearMiejiCard>();
	addMetaObject<TenyearMingceCard>();

	skills << new TenyearJianyan << new TenyearJianyanQuota << new TenyearXiansiSlashViewAsSkill << new TenyearMiejiDiscard;
    related_skills.insertMulti("tenyearjianyan", "#tenyearjianyan-quota");
}
ADD_PACKAGE(TenyearStStandard)

TenyearStWindPackage::TenyearStWindPackage()
	: Package("tenyear_st_wind")
{
	General *tenyear_huangzhong = new General(this, "tenyear_huangzhong", "shu", 4);
	tenyear_huangzhong->addSkill(new TenyearLiegong);
	tenyear_huangzhong->addSkill(new TenyearLiegongDamage);
	related_skills.insert("tenyearliegong", "#tenyearliegong-damage");
	tenyear_huangzhong->addSkill(new TenyearLiegongMod);
	related_skills.insert("tenyearliegong", "#tenyearliegongmod");

	General *tenyear_weiyan = new General(this, "tenyear_weiyan", "shu", 4);
	tenyear_weiyan->addSkill(new TenyearKuanggu);
	tenyear_weiyan->addSkill(new TenyearQimou);
    tenyear_weiyan->addSkill(new TenyearActiveMetadata("tenyearqimou"));
    related_skills.insertMulti("tenyearqimou", "#tenyearqimou-metadata");

	General *tenyear_xiahouyuan = new General(this, "tenyear_xiahouyuan", "wei", 4);
	tenyear_xiahouyuan->addSkill(new TenyearShensu);
	tenyear_xiahouyuan->addSkill(new SlashNoDistanceLimitSkill("tenyearshensu"));
	related_skills.insert("tenyearshensu", "#tenyearshensu-slash-ndl");

	General *tenyear_caoren = new General(this, "tenyear_caoren", "wei", 4);
	tenyear_caoren->addSkill(new TenyearJushou);
	tenyear_caoren->addSkill(new TenyearJiewei);

	General *tenyear_xiaoqiao = new General(this, "tenyear_xiaoqiao", "wu", 3, false);
	tenyear_xiaoqiao->addSkill(new TenyearTianxiang);
	tenyear_xiaoqiao->addSkill("hongyan");

	MigrateToTenyearStWind(this);
}
ADD_PACKAGE(TenyearStWind)

TenyearStYJ2011Package::TenyearStYJ2011Package()
	: Package("tenyear_st_yj2011")
{
	General *tenyear_xushu = new General(this, "tenyear_xushu", "shu", 4);
	tenyear_xushu->addSkill(new TenyearZhuhai);
	tenyear_xushu->addSkill(new TenyearQianxin);

	General *tenyear_fazheng = new General(this, "tenyear_fazheng", "shu", 3);
	tenyear_fazheng->addSkill(new TenyearEnyuan);
	tenyear_fazheng->addSkill(new TenyearXuanhuo);

	General *tenyear_masu = new General(this, "tenyear_masu", "shu", 3);
	tenyear_masu->addSkill(new TenyearSanyao);
	tenyear_masu->addSkill(new TenyearZhiman);

	General *tenyear_zhangchunhua = new General(this, "tenyear_zhangchunhua", "wei", 3, false);
	tenyear_zhangchunhua->addSkill(new Tenyearjueqing);
	tenyear_zhangchunhua->addSkill(new TenyearjueqingComplete);
	tenyear_zhangchunhua->addSkill("nosshangshi");
	related_skills.insert("tenyearjueqing", "#tenyearjueqing");

	General *tenyear_yujin = new General(this, "tenyear_yujin", "wei", 4);
	tenyear_yujin->addSkill(new TenyearZhenjun);

	General *second_tenyear_yujin = new General(this, "second_tenyear_yujin", "wei", 4);
	second_tenyear_yujin->addSkill(new SecondTenyearZhenjun);

	General *tenyear_wuguotai = new General(this, "tenyear_wuguotai", "wu", 3, false);
	tenyear_wuguotai->addSkill(new TenyearGanlu);
	tenyear_wuguotai->addSkill(new TenyearBuyi);

	General *tenyear_lingtong = new General(this, "tenyear_lingtong", "wu", 4);
	tenyear_lingtong->addSkill(new TenyearXuanfeng);
	tenyear_lingtong->addSkill(new Tenyearyongjin);

	General *tenyear_xusheng = new General(this, "tenyear_xusheng", "wu", 4);
	tenyear_xusheng->addSkill(new TenyearPojun);

	General *tenyear_chengong = new General(this, "tenyear_chengong", "qun", 3);
	tenyear_chengong->addSkill(new TenyearMingce);
	tenyear_chengong->addSkill("zhichi");

	General *tenyear_gaoshun = new General(this, "tenyear_gaoshun", "qun", 4);
	tenyear_gaoshun->addSkill(new TenyearXianzhen("tenyearxianzhen"));
	tenyear_gaoshun->addSkill(new TenyearXianzhenSlash("tenyearxianzhen"));
	tenyear_gaoshun->addSkill(new TenyearXianzhenTargetMod("tenyearxianzhen"));
	tenyear_gaoshun->addSkill("jinjiu");
	related_skills.insert("tenyearxianzhen", "#tenyearxianzhen-slash");
	related_skills.insert("tenyearxianzhen", "#tenyearxianzhen-target");

	General *second_tenyear_gaoshun = new General(this, "second_tenyear_gaoshun", "qun", 4);
	second_tenyear_gaoshun->addSkill(new TenyearXianzhen("secondtenyearxianzhen"));
	second_tenyear_gaoshun->addSkill(new TenyearXianzhenSlash("secondtenyearxianzhen"));
	second_tenyear_gaoshun->addSkill(new TenyearXianzhenTargetMod("secondtenyearxianzhen"));
	second_tenyear_gaoshun->addSkill(new SecondTenyearJinjiu);
	second_tenyear_gaoshun->addSkill(new SecondTenyearJinjiuLimit);
	related_skills.insert("secondtenyearxianzhen", "#secondtenyearxianzhen-slash");
	related_skills.insert("secondtenyearxianzhen", "#secondtenyearxianzhen-target");
	related_skills.insert("secondtenyearjinjiu", "#secondtenyearjinjiu-limit");
}
ADD_PACKAGE(TenyearStYJ2011)

TenyearStYJ2012Package::TenyearStYJ2012Package()
	: Package("tenyear_st_yj2012")
{
	General *tenyear_liaohua = new General(this, "tenyear_liaohua", "shu", 4);
	tenyear_liaohua->addSkill(new TenyearDangxian);
	tenyear_liaohua->addSkill(new TenyearFuli);

	General *tenyear_madai = new General(this, "tenyear_madai", "shu", 4);
	tenyear_madai->addSkill(new TenyearQianxi);
	tenyear_madai->addSkill(new TenyearQianxiDraw);
	tenyear_madai->addSkill(new TenyearQianxiLimit);
	tenyear_madai->addSkill("mashu");
	related_skills.insert("tenyearqianxi", "#tenyearqianxi-draw");
	related_skills.insert("tenyearqianxi", "#tenyearqianxi-limit");

	General *tenyear_wangyi = new General(this, "tenyear_wangyi", "wei", 4, false);
	tenyear_wangyi->addSkill("zhenlie");
	tenyear_wangyi->addSkill("olmiji");

	General *tenyear_caozhang = new General(this, "tenyear_caozhang", "wei", 4);
	tenyear_caozhang->addSkill(new TenyearJiangchi);

	General *second_tenyear_caozhang = new General(this, "second_tenyear_caozhang", "wei", 4);
	second_tenyear_caozhang->addSkill(new SecondTenyearJiangchi);
	second_tenyear_caozhang->addSkill(new SecondTenyearJiangchiClear);
	second_tenyear_caozhang->addSkill(new SecondTenyearJiangchiMod);
	related_skills.insert("secondtenyearjiangchi", "#secondtenyearjiangchi-clear");
	related_skills.insert("secondtenyearjiangchi", "#secondtenyearjiangchi-target");

	General *tenyear_xunyou = new General(this, "tenyear_xunyou", "wei", 3);
	tenyear_xunyou->addSkill("qice");
	tenyear_xunyou->addSkill(new TenyearZhiyu);

	General *tenyear_bulianshi = new General(this, "tenyear_bulianshi", "wu", 3, false);
	tenyear_bulianshi->addSkill(new TenyearAnxu);
	tenyear_bulianshi->addSkill(new TenyearZhuiyi);

	General *tenyear_chengpu = new General(this, "tenyear_chengpu", "wu", 4);
	tenyear_chengpu->addSkill("lihuo");
	tenyear_chengpu->addSkill(new TenyearChunlao);

	General *second_tenyear_chengpu = new General(this, "second_tenyear_chengpu", "wu", 4);
	second_tenyear_chengpu->addSkill(new SecondTenyearLihuo);
	second_tenyear_chengpu->addSkill(new SecondTenyearLihuoTargetMod);
	second_tenyear_chengpu->addSkill(new SecondTenyearChunlao);
	related_skills.insert("secondtenyearlihuo", "#secondtenyearlihuo-target");

	General *tenyear_handang = new General(this, "tenyear_handang", "wu", 4);
	tenyear_handang->addSkill(new TenyearGongqi);
    tenyear_handang->addSkill(new TenyearActiveMetadata("tenyeargongqi"));
    related_skills.insertMulti("tenyeargongqi", "#tenyeargongqi-metadata");
	tenyear_handang->addSkill(new TenyearGongqiTargetMod);
	tenyear_handang->addSkill(new TenyearJiefan);
	related_skills.insert("tenyeargongqi", "#tenyeargongqi-target");

	General *tenyear_liubiao = new General(this, "tenyear_liubiao", "qun", 3);
	tenyear_liubiao->addSkill(new TenyearZishou);
	tenyear_liubiao->addSkill(new TenyearZongshi);
	tenyear_liubiao->addSkill(new TenyearZongshiProtect);
	related_skills.insert("tenyearzongshi", "#tenyearzongshi-protect");





}
ADD_PACKAGE(TenyearStYJ2012)

TenyearStYJ2013Package::TenyearStYJ2013Package()
	: Package("tenyear_st_yj2013")
{
	General *tenyear_guanping = new General(this, "tenyear_guanping", "shu", 4);
	tenyear_guanping->addSkill(new TenyearJiezhong);
	tenyear_guanping->addSkill(new TenyearLongyin);

	General *tenyear_liufeng = new General(this, "tenyear_liufeng", "shu", 4);
	tenyear_liufeng->addSkill(new TenyearXiansi);
	tenyear_liufeng->addSkill(new TenyearXiansiAttach);

	General *tenyear_jianyong = new General(this, "tenyear_jianyong", "shu", 3);
	tenyear_jianyong->addSkill(new TenyearQiaoshui);
	tenyear_jianyong->addSkill(new TenyearQiaoshuiTargetMod);
	tenyear_jianyong->addSkill("zongshih");
	related_skills.insert("tenyearqiaoshui", "#tenyearqiaoshui-target");

	General *tenyear_guohuai = new General(this, "tenyear_guohuai", "wei", 4);
	tenyear_guohuai->addSkill(new TenyearJingce);

	General *second_tenyear_guohuai = new General(this, "second_tenyear_guohuai", "wei", 4);
	second_tenyear_guohuai->addSkill(new SecondTenyearJingce);
	second_tenyear_guohuai->addSkill("#tenyearjingce-record");
	related_skills.insert("secondtenyearjingce", "#tenyearjingce-record");

	General *tenyear_zhuran = new General(this, "tenyear_zhuran", "wu", 4);
	tenyear_zhuran->addSkill(new TenyearDanshou);

	General *tenyear_panzhangmazhong = new General(this, "tenyear_panzhangmazhong", "wu", 4);
	tenyear_panzhangmazhong->addSkill(new TenyearDuodao);
	tenyear_panzhangmazhong->addSkill(new TenyearAnjian);
	tenyear_panzhangmazhong->addSkill(new TenyearAnjianEffect);
	related_skills.insert("tenyearanjian", "#tenyearanjian-effect");

	General *tenyear_yufan = new General(this, "tenyear_yufan", "wu", 3);
	tenyear_yufan->addSkill(new TenyearZongxuan);
	tenyear_yufan->addSkill(new TenyearZhiyan);

	General *tenyear_fuhuanghou = new General(this, "tenyear_fuhuanghou", "qun", 3, false);
	tenyear_fuhuanghou->addSkill(new TenyearZhuikong);
	tenyear_fuhuanghou->addSkill(new TenyearZhuikongProhibit);
	tenyear_fuhuanghou->addSkill(new TenyearQiuyuan);
	related_skills.insert("tenyearzhuikong", "#tenyearzhuikong");

	General *tenyear_liru = new General(this, "tenyear_liru", "qun", 3);
	tenyear_liru->addSkill("juece");
	tenyear_liru->addSkill(new TenyearFencheng);
    tenyear_liru->addSkill(new TenyearActiveMetadata("tenyearfencheng"));
    related_skills.insertMulti("tenyearfencheng", "#tenyearfencheng-metadata");
	tenyear_liru->addSkill(new TenyearMieji);






}
ADD_PACKAGE(TenyearStYJ2013)

TenyearStYJ2014Package::TenyearStYJ2014Package()
	: Package("tenyear_st_yj2014")
{
	General *tenyear_wuyi = new General(this, "tenyear_wuyi", "shu", 4);
	tenyear_wuyi->addSkill(new TenyearBenxi);

	General *tenyear_zhoucang = new General(this, "tenyear_zhoucang", "shu", 4);
	tenyear_zhoucang->addSkill(new TenyearZhongyong);

	General *tenyear_caozhen = new General(this, "tenyear_caozhen", "wei", 4);
	tenyear_caozhen->addSkill(new TenyearSidi);
	tenyear_caozhen->addSkill(new TenyearSidiLimit);
	related_skills.insert("tenyearsidi", "#tenyearsidi-limit");

	General *tenyear_hanhaoshihuan = new General(this, "tenyear_hanhaoshihuan", "wei", 4);
	tenyear_hanhaoshihuan->addSkill(new TenyearShenduan);
	tenyear_hanhaoshihuan->addSkill(new TenyearShenduanTargetMod);
	tenyear_hanhaoshihuan->addSkill(new TenyearYonglve);
	related_skills.insert("tenyearshenduan", "#tenyearshenduan-target");

	General *tenyear_sunluban = new General(this, "tenyear_sunluban", "wu", 3, false);
	tenyear_sunluban->addSkill(new TenyearZenhui);
	tenyear_sunluban->addSkill(new TenyearJiaojin);

	General *tenyear_zhuhuan = new General(this, "tenyear_zhuhuan", "wu", 4);
	tenyear_zhuhuan->addSkill(new TenyearFenli);
	tenyear_zhuhuan->addSkill(new TenyearPingkou);

	General *tenyear_guyong = new General(this, "tenyear_guyong", "wu", 3);
	tenyear_guyong->addSkill(new TenyearShenxing);
    tenyear_guyong->addSkill(new TenyearShenxingRecord);
    related_skills.insertMulti("tenyearshenxing", "#tenyearshenxing-record");
	tenyear_guyong->addSkill(new TenyearBingyi);

	General *tenyear_caifuren = new General(this, "tenyear_caifuren", "qun", 3, false);
	tenyear_caifuren->addSkill(new TenyearQieting);
	tenyear_caifuren->addSkill(new TenyearXianzhou);

	General *tenyear_jushou = new General(this, "tenyear_jushou", "qun", 3);
	tenyear_jushou->addSkill(new TenyearJianying);
	tenyear_jushou->addSkill(new TenyearShibei);

}
ADD_PACKAGE(TenyearStYJ2014)

TenyearStYJ2015Package::TenyearStYJ2015Package()
	: Package("tenyear_st_yj2015")
{
	General *tenyear_xiahoushi = new General(this, "tenyear_xiahoushi", "shu", 3, false);
	tenyear_xiahoushi->addSkill(new TenyearQiaoshi);
	tenyear_xiahoushi->addSkill(new TenyearYjYanyu);

	General *tenyear_liuchen = new General(this, "tenyear_liuchen$", "shu", 4);
	tenyear_liuchen->addSkill(new TenyearZhanjue);
	tenyear_liuchen->addSkill(new TenyearQinwang);

	General *tenyear_zhangyi = new General(this, "tenyear_zhangyi", "shu", 4);
	tenyear_zhangyi->addSkill(new TenyearWurong);
	tenyear_zhangyi->addSkill("shizhi");

	General *second_tenyear_zhangyi = new General(this, "second_tenyear_zhangyi", "shu", 5);
	second_tenyear_zhangyi->addSkill("tenyearwurong");
	second_tenyear_zhangyi->addSkill(new SecondTenyearShizhi);
	second_tenyear_zhangyi->addSkill(new SecondTenyearShizhiTrigger);
	related_skills.insert("secondtenyearshizhi", "#secondtenyearshizhi");

	General *tenyear_caoxiu = new General(this, "tenyear_caoxiu", "wei", 4);
	tenyear_caoxiu->addSkill(new TenyearQingxi);
	tenyear_caoxiu->addSkill(new TenyearQingxiDamage);
	tenyear_caoxiu->addSkill("qianju");
	related_skills.insert("tenyearqingxi", "#tenyearqingxi-damage");

	General *tenyear_quancong = new General(this, "tenyear_quancong", "wu", 4);
	tenyear_quancong->addSkill(new TenyearYaoming);

	General *tenyear_sunxiu = new General(this, "tenyear_sunxiu$", "wu", 3);
	tenyear_sunxiu->addSkill(new TenyearYanzhu);
	tenyear_sunxiu->addSkill(new TenyearXingxue);
	tenyear_sunxiu->addSkill("zhaofu");

	General *tenyear_gongsunyuan = new General(this, "tenyear_gongsunyuan", "qun", 4);
	tenyear_gongsunyuan->addSkill(new TenyearHuaiyi);
    tenyear_gongsunyuan->addSkill(new TenyearHuaiyiQuota);
    related_skills.insertMulti("tenyearhuaiyi", "#tenyearhuaiyi-quota");

	General *tenyear_guotupangji = new General(this, "tenyear_guotupangji", "qun", 3);
	tenyear_guotupangji->addSkill(new TenyearJigong);
	tenyear_guotupangji->addSkill(new TenyearJigongMax);
	tenyear_guotupangji->addSkill(new TenyearJigongRecover);
	tenyear_guotupangji->addSkill("shifei");
	related_skills.insert("tenyearjigong", "#tenyearjigong");
	related_skills.insert("tenyearjigong", "#tenyearjigong-recover");










}
ADD_PACKAGE(TenyearStYJ2015)
