#include "mobile.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
#include "maneuvering.h"
#include "ol.h"
#include "wind.h"
#include "clientplayer.h"
#include "yjcm2013.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "settings.h"
#include "skill-instance-utils.h"
#include <algorithm>
#include <climits>
#include <memory>


// A response belonging to an existing instance pins that instance, without
// manufacturing a temporary attached child with a fresh quota.
class MobileResponseInstanceScope
{
public:
	MobileResponseInstanceScope(Room *room, ServerPlayer *owner, const SkillInstanceRef &ref)
		: room(room), owner(owner), mark(ViewAsSkillV2::borrowedActivationMarkName(ref.key.skillName)),
		  previous(owner->getMark(mark))
	{
		room->setPlayerMark(owner, mark, ref.key.instanceID);
	}
	~MobileResponseInstanceScope() { room->setPlayerMark(owner, mark, previous); }
private:
	Room *room;
	ServerPlayer *owner;
	QString mark;
	int previous;
};

// Active custom quotas are author-owned even when a lifecycle interceptor bypasses payment.
class MobileActiveQuota : public TriggerSkillV2
{
public:
	explicit MobileActiveQuota(const QString &skill) : TriggerSkillV2("#" + skill + "-quota"), activeName(skill)
	{
		events << EventSkillInvoking;
		global = true;
	}
	bool recordEvent(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
	{
		const SkillContext accepted = data.value<SkillContext>();
		if (!accepted.bypass_cost || accepted.skill_name != activeName || !accepted.activationRef.isValid()
			|| accepted.activationRef.key.skillName != activeName || !accepted.use_card) return true;
		// The response Slash is the second half of the already paid Zhilve action.
		if (activeName == "xingzhilve" && accepted.use_card->isKindOf("Slash")) return true;
		const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(activeName));
		if (skill) skill->addUsage(accepted);
		return true;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
private:
	QString activeName;
};
class YingjianVS : public ViewAsSkillV2
{
public:
	YingjianVS() : ViewAsSkillV2("yingjian")
	{
		response_pattern = "@@yingjian";
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& request.pattern == "@@yingjian";
	}

	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("yingjian");
		return slash;
	}
};

class Yingjian : public TriggerSkillV2
{
public:
	Yingjian() : TriggerSkillV2("yingjian")
	{
		events << EventPhaseStart;
		view_as_skill = new YingjianVS;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || player->getPhase() != Player::Start
			|| !player->hasSkill(objectName())) return {};
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("yingjian");
		slash->deleteLater();

		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->canSlash(p, slash, false)) {
				return TriggerList{{player, QStringList{objectName()}}};
			}
		}
		return {};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		// The card choice remains a separate response after this trigger is accepted.
		Room::AcceptedViewAsEffectScope selection(room, ctx.owner, objectName(), ctx);
		if (!selection.isValid()) return false;
		room->askForUseCard(ctx.owner, "@@yingjian", "@yingjian");
		return false;
	}
};

class Fenyin : public TriggerSkillV2
{
public:
	Fenyin() : TriggerSkillV2("fenyin")
	{
		events << CardUsed;
		frequency = Frequent;
		global = true;
	}
	QVariantMap previousCard(Room *room, ServerPlayer *player) const
	{
		const QVariantMap use = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		const QVariantMap current = room->queryHistoryFacts({{"kind", "use_card"}, {"event_id", use.value("id")}});
		const QVariantList facts = current.value("items").toList();
		if (!current.value("complete").toBool() || facts.isEmpty()) return {};
		const qint64 before = facts.first().toMap().value("sequence").toLongLong() - 1;
		if (before <= 0) return {};
		// Anchor before this use: a nested card use must not replace its predecessor.
		QVariantMap query{{"kind", "use_card"}, {"turn_id", use.value("turn_id")},
			{"from", player->objectName()}, {"watermark", before}};
		QVariantMap previous;
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return {};
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap card = entry.toMap().value("data").toMap().value("card").toMap();
				if (card.contains("type") && card.value("type").toInt() != Card::TypeSkill) previous = card;
			}
			if (!page.value("has_more").toBool()) return previous;
			query.insert("after", page.value("next_after"));
		}
	}

	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->hasFlag("CurrentPlayer")) return true;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->isKindOf("SkillCard")) return true;

		// This mark is only the visible color; the rule reads immutable room history.
		if (!previousCard(room, player).isEmpty() && player->hasSkill(objectName())){
			foreach (QString m, player->getMarkNames()) {
				if(m.contains("&fenyin+:+"))
					room->setPlayerMark(player,m,0);
			}
			room->setPlayerMark(player,"&fenyin+:+"+use.card->getColorString()+"-Clear",1);
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasFlag("CurrentPlayer")
			|| !player->hasSkill(objectName())) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		const QVariantMap previous = previousCard(room, player);
		if (!use.card || use.card->isKindOf("SkillCard") || previous.isEmpty()
			|| !previous.contains("red") || !previous.contains("black")
			|| (previous.value("red").toBool() == use.card->isRed()
				&& previous.value("black").toBool() == use.card->isBlack())) return {};
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.owner};
		return ctx.owner->askForSkillInvoke(this, *ctx.original_data);
	}

	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		room->broadcastSkillInvoke(objectName());
		ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};

ShanjiaCard::ShanjiaCard(QString shanjia) : shanjia(shanjia)
{
	setSkillName(shanjia);
}

bool ShanjiaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("_" + shanjia);
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

OLShanjiaCard::OLShanjiaCard() : ShanjiaCard("olshanjia")
{
}

class ShanjiaViewAsSkill : public ViewAsSkillV2
{
public:
	ShanjiaViewAsSkill(const QString &name) : ViewAsSkillV2(name) { response_pattern = "@@" + name; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& request.pattern == "@@" + objectName();
	}
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		// The response is an ordinary Slash; its enclosing prompt controls history payment.
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("_" + objectName());
		return slash;
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};
class Shanjia : public TriggerSkillV2
{
public:
	Shanjia(const QString &shanjia) : TriggerSkillV2(shanjia), shanjia(shanjia)
	{
		events << EventPhaseStart;
		m_baseAmount = 3;
		view_as_skill = new ShanjiaViewAsSkill(shanjia);
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || player->getPhase() != Player::Play
			|| !player->hasSkill(objectName())) return {};
		return TriggerList{{player, QStringList{objectName()}}};
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner->askForSkillInvoke(objectName());
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		Room::AcceptedViewAsEffectScope selection(room, player, objectName(), ctx);
		room->broadcastSkillInvoke(objectName());
		player->drawCards(getEffectiveAmount(ctx), objectName());

		int n = getEffectiveAmount(ctx) - player->getMark("&" + shanjia) - player->getMark(shanjia + "Mark");
		bool flag = true;
		if (n > 0) {
			const Card *card = room->askForDiscard(player, objectName(), n, n , false, true, shanjia + "-discard:" + QString::number(n));
			if (!card) return false;
			foreach(int id, card->getSubcards()) {
				const Card *c = Sanguosha->getCard(id);
				if (c->isKindOf("BasicCard") || c->isKindOf("TrickCard")) {
					flag = false;
					break;
				}
			}
		}

		if (flag) {
			Card *slash = Sanguosha->cloneCard("slash");
			slash->setSkillName("_" + shanjia);
			slash->deleteLater();

			foreach(ServerPlayer *p, room->getAlivePlayers()) {
				if (player->canSlash(p, slash, shanjia=="shanjia"?true:false)) {
					if (!selection.isValid()) break;
					room->askForUseCard(player, "@@" + shanjia, "@" + shanjia, -1, Card::MethodUse, false);
					break;
				}
			}
		}
		return false;
	}

private:
	QString shanjia;
};

PingcaiCard::PingcaiCard()
{
	setSkillName("pingcai");
	target_fixed = true;
	mute = true;
}

class Pingcai : public ViewAsSkillV2
{
public:
    Pingcai() : ViewAsSkillV2("pingcai") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            || request.pattern.startsWith("@@pingcai"));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        Card *card = const_cast<Card *>(ViewAsSkillV2::createCard(request));
        card->setMute(true);
        return card;
    }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "PingcaiCard"; }
    EffectFlow effect(SkillContext &ctx) const override;
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
private:
    bool isOK(Room *room, const QString &name) const;
    bool shuijingJudge(Room *room) const;
};
bool Pingcai::isOK(Room *room, const QString &name) const
{
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->getGeneralName().contains(name)||p->getGeneral2Name().contains(name))
			return true;
	}
	return false;
}

bool Pingcai::shuijingJudge(Room *room) const
{
	if (isOK(room, "simahui") && room->canMoveField("e"))
		return true;
	else {
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getArmor()){
				foreach (ServerPlayer *p2, room->getOtherPlayers(p)) {
					if (!p2->getArmor() && p2->hasEquipArea(1))
						return true;
				}
			}
		}
	}
	return false;
}

ViewAsSkillV2::EffectFlow Pingcai::effect(SkillContext &ctx) const
{
	ServerPlayer *source = ctx.invoker;
	Room *room = source->getRoom();
	ctx.manual_effect = true; // Choice-dependent recipients are resolved after the original menu.
	room->broadcastSkillInvoke("pingcai", 1);
	QString choices = "pcwolong+pcfengchu+pcxuanjian";
	if (shuijingJudge(room))
		choices = "pcwolong+pcfengchu+pcshuijing+pcxuanjian";
	QString choice = room->askForChoice(source, "pingcai", choices);
	ctx.extra_data = QVariantMap{{"pingcai_choice", choice}};
	LogMessage log;
	log.type = "#FumianFirstChoice";
	log.from = source;
	log.arg = choice;
	room->sendLog(log);
	if (source->isDead()) return FinishSkill;

	QList<ServerPlayer *> targets, tos;
	if (choice == "pcwolong") {
		int n = 1;
		if (isOK(room, "wolong")) n++;
		tos = room->askForPlayersChosen(source,room->getAlivePlayers(),"pcwolong",1,n,"pcwolong0:"+QString::number(n));
		room->broadcastSkillInvoke("pingcai", 2);
		foreach (ServerPlayer *p, tos)
			room->doAnimate(1, source->objectName(), p->objectName());
		foreach (ServerPlayer *p, tos)
			skillEffect(ctx, p);
	} else if (choice == "pcfengchu") {
		int n = 3;
		if (isOK(room, "pangtong")) n++;
		tos = room->askForPlayersChosen(source,room->getAlivePlayers(),"pcfengchu",1,n,"pcfengchu0:"+QString::number(n));
		room->broadcastSkillInvoke("pingcai", 3);
		foreach (ServerPlayer *p, tos)
			room->doAnimate(1, source->objectName(), p->objectName());
		foreach (ServerPlayer *p, tos)
			skillEffect(ctx, p);
	} else if (choice == "pcshuijing") {
		if (isOK(room, "simahui"))
			room->moveField(source, "pingcai", false, "e");
		else {
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getArmor())
					targets << p;
			}
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (!p->getArmor() && p->hasEquipArea(1))
					tos << p;
			}
			if (targets.isEmpty() || tos.isEmpty()) return FinishSkill;
			ServerPlayer *from = room->askForPlayerChosen(source, targets, "pingcai_shuijing_from", "@pingcai-shuijing");
			const Card *armor = from->getArmor();
			if (!armor) return FinishSkill;
			ServerPlayer *to = room->askForPlayerChosen(source, tos, "pingcai_shuijing_to", "@movefield-to:" + armor->objectName());
			if (!to->hasEquipArea(1) || to->getArmor()) return FinishSkill;
			room->moveCardTo(armor, to, Player::PlaceEquip, true);
		}
		room->broadcastSkillInvoke("pingcai", 4);
	} else {
		ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), "pcxuanjian", "@pingcai-xuanjian");
		room->doAnimate(1, source->objectName(), target->objectName());
		room->broadcastSkillInvoke("pingcai", 5);
		skillEffect(ctx, target);
		if (isOK(room, "xushu"))
			source->drawCards(getEffectiveAmount(ctx), "pingcai");
	}
	return FinishSkill;
}

ViewAsSkillV2::EffectFlow Pingcai::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    Room *room = ctx.invoker->getRoom();
    const QString choice = ctx.extra_data.toMap().value("pingcai_choice").toString();
    if (choice == "pcwolong")
        room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx), DamageStruct::Fire));
    else if (choice == "pcfengchu") {
        if (!target->isChained()) room->setPlayerChained(target);
    } else if (choice == "pcxuanjian") {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        room->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
    }
    return ContinueEffects;
}
class Yinshiy : public TriggerSkillV2
{
public:
	Yinshiy() : TriggerSkillV2("yinshiy")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Player::Phase phase = data.value<PhaseChangeStruct>().to;
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || (phase != Player::Start && phase != Player::Judge && phase != Player::Finish)
            || player->isSkipped(phase)) return {};
        return TriggerList{{player, QStringList{objectName()}}};
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        // Re-read the live phase event after the V2 interception stages.
        const Player::Phase phase = ctx.original_data->value<PhaseChangeStruct>().to;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->skip(phase);
        return false;
    }
};

class YinshiyPro : public ProhibitSkill
{
public:
	YinshiyPro() : ProhibitSkill("#yinshiy-pro")
	{
	}

	bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return card->isKindOf("DelayedTrick")&&to->hasSkill("yinshiy");
	}
};

BaiyiCard::BaiyiCard()
{
	setSkillName("baiyi");
}

bool BaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return to_select != Self && targets.length() < 2;
}

bool BaiyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

class Baiyi : public ViewAsSkillV2
{
public:
	Baiyi() : ViewAsSkillV2("baiyi")
	{
		limit_mark = "@baiyiMark";
		frequency = Limited;
	}
	LimitScope getLimitScope() const override { return Limit_Game; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->isWounded();
	}

	QString historyKey(const ActiveSkillRequest &) const override { return "BaiyiCard"; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return candidate && candidate != request.initiator && selected.length() < 2;
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 2;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		room->doSuperLightbox(ctx.invoker, objectName());
		room->removePlayerMark(ctx.invoker, "@baiyiMark");
		return true;
	}
	TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
	EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
	{
		// Both selected seats must survive; never swap a surviving seat with itself.
		if (targets.length() == 2)
			ctx.invoker->getRoom()->swapSeat(targets.first(), targets.last());
		return ContinueEffects;
	}
};

JinglveCard::JinglveCard()
{
	setSkillName("jinglve");
}

bool JinglveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && !to_select->isKongcheng() && to_select != Self;
}

class JinglveVS : public ViewAsSkillV2
{
public:
	JinglveVS() : ViewAsSkillV2("jinglve") {}
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "JinglveCard"; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return candidate && selected.isEmpty() && !candidate->isKongcheng() && candidate != request.initiator;
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
};

ViewAsSkillV2::EffectFlow JinglveVS::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
	CardEffectStruct effect;
	effect.from = ctx.invoker;
	effect.to = target;
	QStringList names = effect.from->getTag("Jinglve_targets").toStringList();
	if (!names.contains(effect.to->objectName())) {
		names << effect.to->objectName();
		effect.from->setTag("Jinglve_targets", names);
	}
	if (effect.to->isDead() || effect.to->isKongcheng() || effect.from->isDead()) return ContinueEffects;
	Room *room = effect.from->getRoom();
	int id = room->doGongxin(effect.from, effect.to, effect.to->handCards(), "jinglve");
	if (id < 0) return ContinueEffects;

	LogMessage log;
	log.type = "$JinglveMark";
	log.from = effect.from;
	log.to << effect.to;
	log.card_str = QString::number(id);
	room->sendLog(log, effect.from);

	const Card *card = Sanguosha->getEngineCard(id);
	QString mark = "&jinglve+:+" + card->objectName() + "+" + card->getSuitString() + "_char" + "+" + card->getNumberString();
	room->addPlayerMark(effect.to, mark, 1, QList<ServerPlayer *>() << effect.from);

	QVariantList sishi = effect.to->getTag("Sishi" + effect.from->objectName()).toList();
	if (!sishi.contains(id)) {
		sishi << id;
		effect.to->setTag("Sishi" + effect.from->objectName(), sishi);
	}
	return ContinueEffects;
}

class Jinglve : public TriggerSkillV2
{
public:
	Jinglve() : TriggerSkillV2("jinglve")
	{
		events << CardUsed << CardsMoveOneTime << EventPhaseChanging;
		frequency = Compulsory;
		view_as_skill = new JinglveVS;
	}

	bool pending(TriggerEvent event, Room *room, ServerPlayer *owner, ServerPlayer *eventPlayer,
		const QVariant &data) const
	{
		if (!owner || !owner->isAlive() || !eventPlayer || !eventPlayer->isAlive()) return false;
		if (event == CardUsed) {
			const Card *card = data.value<CardUseStruct>().card;
			if (!card || card->isKindOf("SkillCard")) return false;
			const QVariantList marked = eventPlayer->getTag("Sishi" + owner->objectName()).toList();
			const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getId()};
			for (int id : ids) if (marked.contains(id)) return true;
		} else if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive || !owner->hasSkill(objectName())) return false;
			const QVariantList marked = eventPlayer->getTag("Sishi" + owner->objectName()).toList();
			for (const Card *card : eventPlayer->getCards("hej"))
				if (marked.contains(card->getEffectiveId())) return true;
		} else if (event == CardsMoveOneTime && owner == eventPlayer) {
			const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place != Player::DiscardPile) return false;
			for (ServerPlayer *target : room->getPlayers()) {
				const QVariantList marked = target->getTag("Sishi" + owner->objectName()).toList();
				for (int id : move.card_ids) if (marked.contains(id)) return true;
			}
		}
		return false;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
		QList<SkillContext> &contexts) const override
	{
		// Sishi is an applied card record: loss of the original grant does not erase it.
		for (ServerPlayer *owner : room->getAlivePlayers()) {
			if (!pending(event, room, owner, player, data)) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = owner;
			ctx.invoker = player;
			ctx.initiator = owner;
			ctx.original_data = &data;
			ctx.current_event = event;
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		return ctx.original_data && pending(ctx.current_event, room, ctx.owner, ctx.invoker, *ctx.original_data);
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		QVariant &data = *ctx.original_data;
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			QList<int> subcards;
			if (use.card->isVirtualCard())
				subcards = use.card->getSubcards();
			else
				subcards << use.card->getId();
			if (subcards.isEmpty()) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p != ctx.owner) continue;
				QVariantList sishi = player->getTag("Sishi" + p->objectName()).toList();
				if (sishi.isEmpty()) continue;
				foreach (int id, subcards) {
					if (sishi.contains(QVariant(id))) {
						sishi.removeOne(id);
						player->setTag("Sishi" + p->objectName(), sishi);
						SishiRemoveMark(id, player);
						LogMessage log;
						log.type = "#JinglveUse";
						log.from = p;
						log.to << player;
						log.arg = objectName();
						log.arg2 = use.card->objectName();
						log.card_str = QString::number(id);
						room->sendLog(log);
						room->broadcastSkillInvoke(objectName());
						room->notifySkillInvoked(p, objectName());
						use.nullified_list <<"_ALL_TARGETS";
						data = QVariant::fromValue(use);
					}
				}
			}
		} else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p != ctx.owner) continue;
				if (p->isAlive() && p->hasSkill(objectName())) {
					QVariantList sishi = player->getTag("Sishi" + p->objectName()).toList();
					if (sishi.isEmpty()) continue;
					QList<int> sishi_ids = ListV2I(sishi);
					DummyCard *dummy = new DummyCard;
					foreach (const Card *c, player->getCards("hej")) {
						if (sishi_ids.contains(c->getEffectiveId()))
							dummy->addSubcard(c);
					}
					dummy->deleteLater();
					if (dummy->subcardsLength() <= 0) continue;
					player->removeTag("Sishi" + p->objectName());
					room->sendCompulsoryTriggerLog(p, objectName());
					p->obtainCard(dummy, false);
				}
			}
		} else if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place == Player::DiscardPile){
				foreach (ServerPlayer *p, room->getPlayers()) {
					QVariantList sishi = p->getTag("Sishi" + player->objectName()).toList();
					if (sishi.isEmpty()) continue;
					DummyCard *dummy = new DummyCard;
					foreach (int id, move.card_ids) {
						if(sishi.contains(id)){
							sishi.removeOne(id);
							p->setTag("Sishi" + player->objectName(), sishi);
							SishiRemoveMark(id, p);
							if (room->getCardPlace(id) == Player::DiscardPile)
								dummy->addSubcard(id);
						}
					}
					dummy->deleteLater();
                    if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE){
						if(dummy->subcardsLength()>0&&player->isAlive()&&player->hasSkill(objectName())){
							room->sendCompulsoryTriggerLog(player, objectName());
							room->obtainCard(player, dummy, true);
						}
					}
				}
			}
		}
		return false;
	}

	void SishiRemoveMark(int id, ServerPlayer *owner) const
	{
		const Card *card = Sanguosha->getEngineCard(id);
		QString mark = "&jinglve+:+" + card->objectName() + "+" + card->getSuitString() + "_char" + "+" + card->getNumberString();
		owner->getRoom()->removePlayerMark(owner, mark);
	}
};

class Shanli : public TriggerSkillV2
{
public:
	Shanli() : TriggerSkillV2("shanli")
	{
		frequency = Wake;
		events << EventPhaseStart << EventSkillInvoking;
	}
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
		if (event == EventSkillInvoking) return {};
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart
            || !player->hasSkill(objectName())) return {};
        const QVariantMap baiyi = room->queryHistoryFacts({{"kind", "use_card"},
            {"from", player->objectName()}, {"skill_name", "baiyi"}, {"limit", 1}});
        if (!(baiyi.value("complete").toBool() && !baiyi.value("items").toList().isEmpty()
            && player->getTag("Jinglve_targets").toStringList().length() >= 2)
            && !player->canWake(objectName())) return {};
        return TriggerList{{player, QStringList{objectName()}}};
    }
        bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        // Limit_Game accounts for the exact copy; the visible awakening mark stays aggregate.
		room->sendCompulsoryTriggerLog(player, this);
		room->doSuperLightbox(player, objectName());
		room->addPlayerMark(player, objectName());

		if (room->changeMaxHpForAwakenSkill(player, -getEffectiveAmount(ctx), objectName())) {
			if (player->isDead()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@shanli-invoke");
			room->doAnimate(1, player->objectName(), target->objectName());
			QStringList all_lord_skills;
			foreach (QString lord, Sanguosha->getLords()) {
				foreach (const Skill *skill, Sanguosha->getGeneral(lord)->getVisibleSkillList()) {
					if (skill->isLordSkill() && skill->isVisible() && !all_lord_skills.contains(skill->objectName()))
						all_lord_skills << skill->objectName();
				}
			}

			QStringList lord_skills;
			for (int i = 0; i < 3; i++) {
				if (all_lord_skills.isEmpty()) break;
				QString lordskill = all_lord_skills.at(qsanRandomBounded(all_lord_skills.length()));
				all_lord_skills.removeOne(lordskill);
				lord_skills << lordskill;
			}
			if (lord_skills.isEmpty()) return false;

			QString skill = room->askForChoice(player, objectName(), lord_skills.join("+"), QVariant::fromValue(target));
			if (target->hasLordSkill(skill, true)) return false;
			room->acquireSkill(target, skill);
		}
		return false;
	}
};

class MobileKuangcai : public TriggerSkillV2
{
public:
	MobileKuangcai() : TriggerSkillV2("mobilekuangcai")
	{
		events << CardUsed << EventPhaseChanging << EventPhaseStart;
		waked_skills = "#mobilekuangcai_mod";
		frequency = Frequent;
	}
	Frequency getFrequency(const Player *player = nullptr) const override
	{
		return player && player->getMark("MobileKuangcaiUse-PlayClear") > 0 ? Compulsory : Frequent;
	}

	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == EventPhaseChanging && player && player->getTag("MobileKuangcaiUse").toBool()) {
			// Restore the single room timer once, including after the source skill is lost.
			player->setTag("MobileKuangcaiUse", false);
			Config.OperationTimeout = player->getTag("MobileKuangcaiTimeout").toInt();
			room->doNotify(player, QSanProtocol::S_COMMAND_OPERATION_TIMEOUT, Config.OperationTimeout);
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
		QList<SkillContext> &contexts) const override
	{
		if (event != CardUsed) return false;
		const Card *card = data.value<CardUseStruct>().card;
		if (player && card && !card->isKindOf("SkillCard") && player->getMark("MobileKuangcaiUse-PlayClear") > 0) {
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = ctx.invoker = ctx.initiator = player;
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.amount = player->getMark("mobilekuangcai_amount-PlayClear");
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
		return ctx.owner && ctx.owner == ctx.invoker && ctx.current_event == CardUsed
			&& ctx.owner->getMark("MobileKuangcaiUse-PlayClear") > 0;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
			|| !player->hasSkill(objectName()) || player->getTag("MobileKuangcaiUse").toBool()) return {};
		return TriggerList{{player, QStringList{objectName()}}};
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return event == CardUsed || ctx.owner->askForSkillInvoke(this);
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == CardUsed) {
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(getEffectiveAmount(ctx),objectName());
			Config.OperationTimeout -= 1;
			if(Config.OperationTimeout<1) room->setPlayerFlag(player, "Global_PlayPhaseTerminated");
			else room->doNotify(player,QSanProtocol::S_COMMAND_OPERATION_TIMEOUT, Config.OperationTimeout);
		} else if (event == EventPhaseStart) {
			if (player->getPhase()==Player::Play&&player->isAlive()){
				room->broadcastSkillInvoke(objectName());//NullificationCountDown
				room->addPlayerMark(player, "MobileKuangcaiUse-PlayClear",999);
				// This accepted phase effect survives removal of its original source.
				room->setPlayerMark(player, "mobilekuangcai_amount-PlayClear", getEffectiveAmount(ctx));
				player->setTag("MobileKuangcaiUse", true);
				player->setTag("MobileKuangcaiTimeout", Config.OperationTimeout);
				Config.OperationTimeout = 5;
				room->doNotify(player,QSanProtocol::S_COMMAND_OPERATION_TIMEOUT, 5);
			}
		}
		return false;
	}
};

class MobileKuangcaiMod : public TargetModSkillV2
{
public:
	MobileKuangcaiMod() : TargetModSkillV2("#mobilekuangcai_mod")
	{
		pattern = ".";
		setHolderSelector(CorrectSkill_System);
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.primary || (ctx.modType != Residue && ctx.modType != DistanceLimit))
			return CorrectSkillResult::noEffect();
		// The accepted turn effect remains until PlayClear, regardless of later skill loss.
		return CorrectSkillResult::useAmount(ctx.primary->getMark("MobileKuangcaiUse-PlayClear"));
	}
};

class MobileShejian : public TriggerSkillV2
{
public:
	MobileShejian() : TriggerSkillV2("mobileshejian")
	{
		events << EventPhaseEnd;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Discard
			|| !player->hasSkill(objectName())) return {};
		QList<int> ids;
		QStringList suits;
		const QVariant phase = room->historyScopes().value("phase_id");
		if (phase.toLongLong() == 0) return {};
		QVariantMap query{{"phase_id", phase}, {"from", player->objectName()}};
		QList<int> discarded;
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) return {};
			if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap move = entry.toMap().value("data").toMap();
				if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
					discarded << move.value("card_id").toInt();
			}
			if (!page.value("has_more").toBool()) break;
			query.insert("after", page.value("next_after"));
		}
		// The phase journal supplies candidates; current card positions remain authoritative.
		for (int id : discarded) {
			if (ids.contains(id) || room->getCardPlace(id) != Player::DiscardPile) continue;
			ids << id;
			const QString suit = Sanguosha->getCard(id)->getSuitString();
			if (suits.contains(suit)) return {};
			suits << suit;
		}
		if (ids.length() < 2) return {};
		for (ServerPlayer *target : room->getOtherPlayers(player))
			if (player->canDiscard(target, "he")) return TriggerList{{player, QStringList{objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> targets;
		for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
			if (ctx.owner->canDiscard(target, "he")) targets << target;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "MobileShejian0:", true, true);
		if (!target) return false;
		ctx.targets << target;
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		for (int i = 0; i < getEffectiveAmount(ctx) && player->canDiscard(target, "he"); ++i) {
			const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
			if (id >= 0) room->throwCard(id, objectName(), target, player);
		}
		return false;
	}
};

static QHash<QString, QString> ChangshiSkills;

class Danggu : public TriggerSkillV2
{
public:
	Danggu() : TriggerSkillV2("danggu")
	{
		events << GameStart << Revived;
		frequency = Compulsory;
	}

	void jieDang(ServerPlayer *player, Room *room, QStringList cs) const
	{
		room->setPlayerMark(player,"&chang_shi",cs.length());
		QStringList cs2s,cssk,taunts;
		foreach (QString s, player->getTag("DangguSkills").toStringList()) {
			if(s.startsWith("-")) continue;
			cssk << "-"+s;
		}
		QString cs1 = cs[qsanRandomBounded(cs.length())];
		if(cs1=="cs_gaowang")
			taunts << "cs_hanli" << "cs_duangui" << "cs_guosheng" << "cs_bilan";
		else if(cs1=="cs_duangui")
			taunts << "cs_guosheng";
		else if(cs1=="cs_guosheng")
			taunts << "cs_duangui";
		else if(cs1=="cs_bilan")
			taunts << "cs_hanli";
		else if(cs1=="cs_hanli")
			taunts << "cs_bilan";
		player->setAvatarIcon(cs1);
		cssk << ChangshiSkills[cs1];
		cs.removeOne(cs1);
		qsanShuffle(cs);
		foreach (QString g, cs) {
			cs2s << g;
			if(!taunts.contains(g)) cs1 = "OK";
			if(cs2s.length()>=4) break;
		}
		if(cs1!="OK") taunts.clear();
		while(cs2s.length()>0){
			QString cs2 = room->askForGeneral(player,cs2s);
			cs2s.removeOne(cs2);
			if(taunts.contains(cs2)){
				room->playAudioEffect("audio/card/common/"+cs2+"_taunt.ogg");
			}else{
				cssk << ChangshiSkills[cs2];
				cs.removeOne(cs2);
				player->setAvatarIcon(cs2,true);
				break;
			}
		}
		player->setTag("DangguSkills", cssk);
		room->handleAcquireDetachSkills(player,cssk,false);
		player->setTag("ChangshiCards", cs);
		room->setPlayerMark(player,"&chang_shi",cs.length());
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())
			|| (event == Revived && player->getTag("ChangshiCards").toStringList().isEmpty())) return {};
		return TriggerList{{player, QStringList{objectName()}}};
	}
	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (triggerEvent == GameStart) {
			room->sendCompulsoryTriggerLog(player,this);
			ChangshiSkills.insert("cs_zhangrang","cstaoluan");
			ChangshiSkills.insert("cs_zhaozhong","cschiyan");
			ChangshiSkills.insert("cs_sunzhang","cszimou");
			ChangshiSkills.insert("cs_bilan","cspicai");
			ChangshiSkills.insert("cs_xiayun","csyaozhuo");
			ChangshiSkills.insert("cs_hanli","csxiaolu");
			ChangshiSkills.insert("cs_lisong","cskuiji");
			ChangshiSkills.insert("cs_duangui","cschihe");
			ChangshiSkills.insert("cs_guosheng","csniqu");
			ChangshiSkills.insert("cs_gaowang","csmiaoyu");
			QStringList cs = ChangshiSkills.keys();
			foreach (QString g, cs) {
				room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, player->objectName(), g);
				room->addPlayerMark(player,"&chang_shi");
				room->getThread()->delay(233);
			}
			player->setTag("ChangshiCards", cs);
			jieDang(player,room,cs);
		} else if (triggerEvent == Revived) {
			QStringList cs = player->getTag("ChangshiCards").toStringList();
			if(cs.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player,this);
			jieDang(player,room,cs);
			player->drawCards(getEffectiveAmount(ctx),objectName());
		}
		return false;
	}
};

class Mowang : public TriggerSkillV2
{
public:
	Mowang() : TriggerSkillV2("mowang")
	{
		events << BeforeGameOverJudge << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool usesEventPriority() const override { return true; }
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
		QList<SkillContext> &contexts) const override
	{
		if (!player) return true;
		if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
			ServerPlayer *next = player->getNext();
			if (next && room->isRest(next) && next->getTag("MowangRestActive").toBool()) {
				SkillContext ctx;
				ctx.skill_name = objectName();
				ctx.owner = next;
				ctx.invoker = player;
				ctx.initiator = next;
				ctx.original_data = &data;
				ctx.current_event = event;
				contexts << ctx;
				// Finish revival before admitting the current player's death, as in the original rule.
				return true;
			}
		}
		bool eligible = false;
		if (event == BeforeGameOverJudge) {
			eligible = data.value<DeathStruct>().who == player && player->getMaxHp() > 0
				&& !player->getTag("ChangshiCards").toStringList().isEmpty() && !player->isRest();
		} else if (event == EventPhaseChanging) {
			eligible = data.value<PhaseChangeStruct>().to == Player::NotActive && player->isAlive() && !player->isRest();
		}
		if (!eligible || !player->hasSkill(objectName())) return true;
		for (int id : player->getValidSkillInstanceIds(objectName())) {
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = ctx.invoker = ctx.initiator = player;
			ctx.instanceID = id;
			ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
			ctx.sourceRef = ctx.activationRef;
			ctx.original_data = &data;
			ctx.current_event = event;
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
		return ctx.owner && ctx.invoker && ctx.current_event == EventPhaseChanging && ctx.original_data
			&& ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive
			&& ctx.invoker->getNext() == ctx.owner && room->isRest(ctx.owner)
			&& ctx.owner->getTag("MowangRestActive").toBool();
	}

	int getPriority(TriggerEvent event) const
	{
		if (event != BeforeGameOverJudge) return -2;
		return TriggerSkill::getPriority(event);
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (!ctx.activationRef.isValid()) {
			room->sendCompulsoryTriggerLog(player, this);
			room->unrestPlayer(player, true, false);
			player->removeTag("MowangRestActive");
			player->removeTag("RestTurn");
			return false;
		}
		if (triggerEvent == BeforeGameOverJudge) {
			DeathStruct death = data.value<DeathStruct>();
			if(death.who==player&&player->getMaxHp()>0&&player->hasSkill(objectName())
				&&!player->getTag("ChangshiCards").toStringList().isEmpty()
				&&!player->isRest()){
				room->sendCompulsoryTriggerLog(player,this);
				room->restPlayer(player, objectName(), true);
				QString csai = player->property("avatarIcon").toString();
				if(!csai.isEmpty()) player->setAvatarIcon(csai+"2");
				csai = player->property("avatarIcon2").toString();
				if(!csai.isEmpty()) player->setAvatarIcon(csai+"2",true);
				player->setTag("RestTurn", room->getTag("TurnLengthCount"));
				player->setTag("MowangRestActive", true);
				return true;
			}
		} else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
    
			if (change.to != Player::NotActive || player->isDead() || !player->hasSkill(objectName()) || player->isRest()) return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->killPlayer(player);
		}
		return false;
	}
};

class CsTaoluan : public ViewAsSkillV2
{
public:
	CsTaoluan() : ViewAsSkillV2("cstaoluan", 1)
	{
	}
	LimitScope getLimitScope() const override { return Limit_Phase; }
	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::guhuo(objectName(), true, true);
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.initiator->getCardCount() <= 0) return false;
		if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getPhase() != Player::Play)
			return false;
		// The inherited declaration contract validates both dialog selections and response patterns.
		return !usableNames(request).isEmpty();
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		return candidate && request.selectedCardIds.isEmpty();
	}
};

class CsChiyan : public TriggerSkillV2
{
public:
	CsChiyan() : TriggerSkillV2("cschiyan")
	{
		events << TargetSpecified << EventPhaseChanging << DamageCaused;
		frequency = Compulsory;
	}

	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
		QList<SkillContext> &contexts) const override
	{
		if (event != EventPhaseChanging) return false;
		if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
		for (ServerPlayer *target : room->getAllPlayers()) {
			if (target->getPile(objectName()).isEmpty()) continue;
			// Return the pending piles once even when every original skill copy is gone.
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = ctx.invoker = ctx.initiator = player;
			ctx.original_data = &data;
			ctx.current_event = event;
			contexts << ctx;
			break;
		}
		return true;
	}
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
		return ctx.owner && ctx.current_event == EventPhaseChanging && ctx.original_data
			&& ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == TargetSpecified) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (!player || !player->isAlive() || !player->hasSkill(objectName())
				|| !use.card || !use.card->isKindOf("Slash")) return {};
			for (ServerPlayer *target : use.to)
				if (target->isAlive() && target->getCardCount() > 0)
					return TriggerList{{player, QStringList{objectName()}}};
		} else if (event == DamageCaused) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.from && damage.from->isAlive() && damage.from->hasSkill(objectName())
				&& damage.to && damage.to->isAlive() && damage.card && damage.card->isKindOf("Slash")
				&& damage.by_user && damage.from->getHandcardNum() >= damage.to->getHandcardNum()
				&& damage.from->getEquips().length() >= damage.to->getEquips().length())
				return TriggerList{{damage.from, QStringList{objectName()}}};
		}
		return {};
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == TargetSpecified) ctx.targets = ctx.original_data->value<CardUseStruct>().to;
		return true;
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> to_obtain = p->getPile(objectName());
				if (!to_obtain.isEmpty()) {
					DummyCard dummy(to_obtain);
					room->obtainCard(p, &dummy, false);
				}
			}
		} else if (triggerEvent == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to->isAlive()&&damage.from==ctx.owner&&damage.from->isAlive()
			&&damage.card&&damage.card->isKindOf("Slash")&&damage.by_user
			&&damage.from->getHandcardNum()>=damage.to->getHandcardNum()
			&&damage.from->getEquips().length()>=damage.to->getEquips().length()){
				room->sendCompulsoryTriggerLog(damage.from, objectName());
				damage.damage += getEffectiveAmount(ctx);
				data = QVariant::fromValue(damage);
			}
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!player->isAlive() || target->getCardCount() <= 0 || !player->askForSkillInvoke(this, target)) return false;
		room->broadcastSkillInvoke(objectName());
		for (int i = 0; i < getEffectiveAmount(ctx) && target->getCardCount() > 0; ++i) {
			const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodNone);
			if (id >= 0) target->addToPile(objectName(), id, false);
		}
		return false;
	}
};

class CsZimou : public TriggerSkillV2
{
public:
	CsZimou() : TriggerSkillV2("cszimou")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const Card *card = data.value<CardUseStruct>().card;
		if (player && player->getPhase() == Player::Play && player->hasSkill(objectName())
			&& card && !card->isKindOf("SkillCard")) {
			const int count = room->countHistoryCards(player, "phase");
			if (count >= 0) room->setPlayerMark(player, "&cszimou-PlayClear", count);
		}
		// Retain the visible count, without a second history accumulator.
		return true;
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const Card *card = data.value<CardUseStruct>().card;
		if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasSkill(objectName())
			|| !card || card->isKindOf("SkillCard")) return {};
		const int mark = room->countHistoryCards(player, "phase");
		return mark == 2 || mark == 4 || mark == 6
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.extra_data = room->countHistoryCards(player, "phase");
		return true;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const int mark = ctx.extra_data.toInt();
		if (mark==2||mark==4||mark==6) {
			room->sendCompulsoryTriggerLog(player, this);
			QList<int> card_ids;
			foreach (int id, room->getDrawPile()) {
				const Card *card = Sanguosha->getCard(id);
				if (mark == 2 && card->isKindOf("Analeptic"))
					card_ids << id;
				else if (mark == 4 && card->isKindOf("Slash"))
					card_ids << id;
				else if (mark == 6 && card->isKindOf("Duel"))
					card_ids << id;
			}
			if (card_ids.isEmpty()) return false;
			for (int i = 0; i < getEffectiveAmount(ctx) && !card_ids.isEmpty(); ++i) {
				const int id = card_ids.takeAt(qsanRandomBounded(card_ids.length()));
				if (room->getCardPlace(id) == Player::DrawPile) room->obtainCard(player, id, true);
			}
		}
		return false;
	}
};

CsPicaiCard::CsPicaiCard()
{
	setSkillName("cspicai");
	target_fixed = true;
}

class CsPicai : public ViewAsSkillV2
{
public:
	CsPicai() : ViewAsSkillV2("cspicai") {}
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			|| request.pattern.startsWith("@@cspicai"));
	}
	TargetMode targetMode() const override { return NoTarget; }
	QString historyKey(const ActiveSkillRequest &) const override { return "CsPicaiCard"; }
	EffectFlow effect(SkillContext &ctx) const override;
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		DummyCard cards(ListV2I(ctx.extra_data.toList()));
		target->obtainCard(&cards);
		return ContinueEffects;
	}
};

ViewAsSkillV2::EffectFlow CsPicai::effect(SkillContext &ctx) const
{
	ServerPlayer *source = ctx.invoker;
	Room *room = source->getRoom();
	ctx.manual_effect = true;
	QStringList pcj;
	DummyCard *dummy = new DummyCard;
	while(source->isAlive()){
		JudgeStruct judge;
		judge.who = source;
		judge.reason = objectName();
		judge.throw_card = false;
		if(pcj.isEmpty())
			judge.pattern = ".|.";
		else{
			judge.good = false;
			judge.pattern = ".|"+pcj.join(",");
		}
		room->judge(judge);
		pcj << judge.card->getSuitString();
		dummy->addSubcard(judge.card->getEffectiveId());
		if(judge.isGood()&&source->isAlive()&&source->askForSkillInvoke(objectName())){
		}else break;
	}
	dummy->deleteLater();
	if(dummy->subcardsLength()>0){
		if(source->isAlive()){
			ServerPlayer *to = room->askForPlayerChosen(source,room->getAlivePlayers(),objectName(),"cspicai0:",true);
			if(to){
				ctx.extra_data = ListI2V(dummy->getSubcards());
				skillEffect(ctx, to);
				// Interception may cancel the recipient; retire only cards still awaiting disposition.
				QList<int> pending;
				for (int id : dummy->getSubcards()) {
					const Player::Place place = room->getCardPlace(id);
					if (place == Player::PlaceJudge || place == Player::PlaceTable) pending << id;
				}
				if (!pending.isEmpty()) {
					DummyCard rest(pending);
					room->throwCard(&rest, nullptr);
				}
				return FinishSkill;
			}
		}
		room->throwCard(dummy,nullptr);
	}
	return FinishSkill;
}

CsYaozhuoCard::CsYaozhuoCard()
{
    setSkillName("csyaozhuo");
}

bool CsYaozhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && Self->canPindian(to_select);
}

class CsYaozhuoVS : public ViewAsSkillV2
{
public:
    CsYaozhuoVS() : ViewAsSkillV2("csyaozhuo") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        for (const Player *target : request.initiator->getAliveSiblings())
            if (request.initiator->canPindian(target)) return true;
        return false;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "CsYaozhuoCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *candidate) const override
    {
        return candidate && selected.isEmpty() && candidate != request.initiator
            && request.initiator->canPindian(candidate);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (source->canPindian(target)) {
            Room *room = source->getRoom();
            if (source->pindian(target, objectName()))
                room->setPlayerMark(target, "&csyaozhuo", getEffectiveAmount(ctx));
            else
                room->askForDiscard(source, objectName(), 2, 2, false, true);
        }
        return ContinueEffects;
    }
};

class CsYaozhuo : public TriggerSkillV2
{
public:
    CsYaozhuo() : TriggerSkillV2("csyaozhuo")
    {
        events << EventPhaseChanging;
        frequency = Compulsory;
        view_as_skill = new CsYaozhuoVS;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        // The already-applied mark survives loss of its originating skill.
        if (player && player->getMark("&csyaozhuo") > 0
            && data.value<PhaseChangeStruct>().to == Player::Draw) {
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner == ctx.invoker && ctx.owner->getMark("&csyaozhuo") > 0
            && ctx.original_data && ctx.original_data->value<PhaseChangeStruct>().to == Player::Draw;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->skip(Player::Draw);
        room->setPlayerMark(player, "&csyaozhuo", 0);
        return false;
    }
};
CsXiaoluCard::CsXiaoluCard()
{
	setSkillName("csxiaolu");
	target_fixed = true;
}

CsXiaolu2Card::CsXiaolu2Card()
{
	setSkillName("csxiaolu");
}

bool CsXiaolu2Card::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}

bool CsXiaolu2Card::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	return !targets.isEmpty()||!Self->isJilei(this);
}

class CsXiaolu : public ViewAsSkillV2
{
public:
	CsXiaolu() : ViewAsSkillV2("csxiaolu", 2)
	{
		m_baseAmount = 2;
	}
	LimitScope getLimitScope() const override { return Limit_Phase; }
	int getMaxUsageLimit(const SkillContext &ctx) const override
	{
		// The mandatory redistribution belongs to the accepted activation, not another use.
		return (ctx.use_card ? ctx.use_card->subcardsLength() > 0
			: Sanguosha->getCurrentCardUsePattern().startsWith("@@csxiaolu")) ? 2 : 1;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		if (!ctx.use_card || ctx.use_card->subcardsLength() == 0) Skill::addUsage(ctx);
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && (request.pattern.startsWith("@@csxiaolu")
			|| request.reason == CardUseStruct::CARD_USE_REASON_PLAY);
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		return request.initiator && candidate && request.pattern.startsWith("@@csxiaolu")
			&& request.selectedCardIds.length() < 2 && !candidate->isEquipped();
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.length() == (request.pattern.startsWith("@@csxiaolu") ? 2 : 0);
	}
	bool willThrowSelectedCards() const override { return false; }
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		return request.pattern.startsWith("@@csxiaolu") ? "CsXiaolu2Card" : "CsXiaoluCard";
	}
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.pattern.startsWith("@@csxiaolu") && candidate
			&& selected.isEmpty() && candidate != request.initiator;
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		if (!request.pattern.startsWith("@@csxiaolu")) return selected.isEmpty();
		DummyCard cards(request.selectedCardIds);
		return selected.length() == 1 || (selected.isEmpty() && !request.initiator->isJilei(&cards));
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		Room *room = ctx.invoker->getRoom();
		if (ctx.use_card->subcardsLength() > 0) {
			if (ctx.targets.isEmpty()) {
				room->throwCard(ctx.use_card, objectName(), ctx.invoker);
				return FinishSkill;
			}
			return ContinueEffects;
		}
		// Draw first: declining the later redistribution does not cancel these cards.
		ctx.invoker->drawCards(getEffectiveAmount(ctx), objectName());
		if (ctx.invoker->isDead() || room->askForUseCard(ctx.invoker, "@@csxiaolu!", "csxiaolu0:", -1, Card::MethodNone))
			return FinishSkill;
		DummyCard discarded;
		for (int id : ctx.invoker->handCards()) {
			if (discarded.subcardsLength() < 2 && ctx.invoker->canDiscard(ctx.invoker, id))
				discarded.addSubcard(id);
		}
		if (discarded.subcardsLength() > 0) room->throwCard(&discarded, objectName(), ctx.invoker);
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		ctx.invoker->getRoom()->giveCard(ctx.invoker, target, ctx.use_card, objectName());
		return ContinueEffects;
	}

};

class CsKuiji : public ViewAsSkillV2
{
public:
	CsKuiji() : ViewAsSkillV2("cskuiji", 4)
	{
		expand_pile = "#cskuiji";
		response_pattern = "@@cskuiji";
	}
	LimitScope getLimitScope() const override { return Limit_Phase; }
	int getMaxUsageLimit(const SkillContext &ctx) const override
	{
		// The second request selects material for the same activation.
		return (ctx.use_card ? ctx.use_card->subcardsLength() > 0
			: Sanguosha->getCurrentCardUsePattern() == "@@cskuiji") ? 2 : 1;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		if (!ctx.use_card || ctx.use_card->subcardsLength() == 0) Skill::addUsage(ctx);
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && (request.pattern == "@@cskuiji"
			|| request.reason == CardUseStruct::CARD_USE_REASON_PLAY);
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || request.pattern != "@@cskuiji"
			|| candidate->isEquipped() || request.selectedCardIds.length() >= 4
			|| request.initiator->isJilei(candidate)) return false;
		for (int id : request.selectedCardIds)
			if (Sanguosha->getCard(id)->getSuit() == candidate->getSuit()) return false;
		return true;
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.length() == (request.pattern == "@@cskuiji" ? 4 : 0);
	}
	bool willThrowSelectedCards() const override { return false; }
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		return request.pattern == "@@cskuiji" ? "CsKuijiDisCard" : "CsKuijiCard";
	}
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.pattern != "@@cskuiji" && candidate && selected.isEmpty()
			&& candidate != request.initiator && !candidate->isKongcheng();
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		return selected.length() == (request.pattern == "@@cskuiji" ? 0 : 1);
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		// The response only selects four cards; the outer effect moves both owners' cards atomically.
		return ctx.use_card->subcardsLength() > 0 ? FinishSkill : ContinueEffects;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
};

CsKuijiCard::CsKuijiCard()
{
	setSkillName("cskuiji");
}

bool CsKuijiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

ViewAsSkillV2::EffectFlow CsKuiji::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
	CardEffectStruct effect;
	effect.from = ctx.invoker;
	effect.to = target;
	Room *room = effect.from->getRoom();
	QList<int> hand = effect.to->handCards();
	if (!hand.isEmpty()) {
		LogMessage log;
		log.type = "$ViewAllCards";
		log.from = effect.from;
		log.to << effect.to;
		log.card_str = ListI2S(hand).join("+");
		room->sendLog(log, effect.from);
		room->notifyMoveToPile(effect.from, hand, "cskuiji", Player::PlaceHand, true);
	}
	const Card *c = room->askForUseCard(effect.from, "@@cskuiji", "cskuiji0:" + effect.to->objectName());
	if (!hand.isEmpty())
		room->notifyMoveToPile(effect.from, hand, "cskuiji", Player::PlaceHand, false);
	if (!c) return ContinueEffects;
	QList<int> from_ids, to_ids;
	foreach (int id, c->getSubcards()) {
		if (hand.contains(id))
			to_ids << id;
		else
			from_ids << id;
	}
	QList<CardsMoveStruct> moves;
	if (!from_ids.isEmpty()) {
		CardMoveReason reason1(CardMoveReason::S_REASON_THROW, effect.from->objectName(), "cskuiji", "");
		CardsMoveStruct move1(from_ids, effect.from, nullptr, Player::PlaceHand, Player::DiscardPile, reason1);
		moves << move1;
		LogMessage log;
		log.type = "$DiscardCard";
		log.from = effect.from;
		log.card_str = ListI2S(from_ids).join("+");
		room->sendLog(log);
	}
	if (!to_ids.isEmpty()) {
		CardMoveReason reason2(CardMoveReason::S_REASON_DISMANTLE, effect.from->objectName(), effect.to->objectName(), "cskuiji", "");
		CardsMoveStruct move2(to_ids, effect.to, nullptr, Player::PlaceHand, Player::DiscardPile, reason2);
		moves << move2;
		LogMessage log;
		log.type = "$DiscardCardByOther";
		log.from = effect.from;
		log.to << effect.to;
		log.card_str = ListI2S(to_ids).join("+");
		room->sendLog(log);
	}
	room->moveCardsAtomic(moves, true);
	return ContinueEffects;
}

CsKuijiDisCard::CsKuijiDisCard()
{
	target_fixed = true;
	m_skillName = "cskuiji";
}

class CsChihe : public TriggerSkillV2
{
public:
	CsChihe() : TriggerSkillV2("cschihe")
	{
		events << TargetSpecified;
		waked_skills = "#cschihe,#cschihe_limit";
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return use.card && use.card->isKindOf("Slash") && use.to.length() == 1
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		const CardUseStruct use = ctx.original_data ? ctx.original_data->value<CardUseStruct>() : CardUseStruct();
		return use.to.length() == 1 && player->askForSkillInvoke(this, use.to.first());
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		player->peiyin(this);
		room->setCardFlag(use.card, "cschiheUsed");
		QStringList records;
		int damage = 0;
		foreach (int id, room->showDrawPile(player, 2, objectName(), false)) {
			const QString suit = Sanguosha->getCard(id)->getSuitString();
			records << suit;
			if (suit == use.card->getSuitString()) ++damage;
		}
		room->getThread()->delay(999);
		if (damage > 0) room->setCardFlag(use.card, "cschiheAddDamage_" + QString::number(damage));
		room->setPlayerProperty(use.to.first(), "CsChiheTargetRecords", records.join(","));
		return false;
	}
};

class CsChiheEffect : public TriggerSkillV2
{
public:
	CsChiheEffect() : TriggerSkillV2("#cschihe")
	{
		events << ConfirmDamage << CardOffset << CardOnEffect;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && (event == ConfirmDamage || event == CardOffset || event == CardOnEffect)
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == CardOffset || event == CardOnEffect)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (event == CardOnEffect || event == CardOffset) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->hasFlag("cschiheUsed")) return false;
			room->setPlayerProperty(effect.to, "CsChiheTargetRecords", "");
		}else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("cschiheUsed") || damage.to->isDead()) return false;
			int d = 0;
			foreach (QString flag, damage.card->getFlags()) {
				if (!flag.startsWith("cschiheAddDamage_")) continue;
				QStringList flags = flag.split("_");
				d = flags.last().toInt();
				if (d > 0) break;
			}
			if (d <= 0) return false;
			LogMessage log;
			log.type = "#YHHankaiDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = "cschihe";
			log.arg2 = QString::number(damage.damage);
			log.arg3 = QString::number(damage.damage += d);
			room->sendLog(log);
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class CsChiheLimit : public CardLimitSkill
{
public:
	CsChiheLimit() : CardLimitSkill("#cschihe_limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		QString record = target->property("CsChiheTargetRecords").toString();
		if (!record.isEmpty()) return "Jink|" + record;
		return "";
	}
};

CsNiquCard::CsNiquCard()
{
	setSkillName("csniqu");
}

bool CsNiquCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void CsNiquCard::onEffect(CardEffectStruct &effect) const
{
	if(effect.to->isAlive()){
		Room*room = effect.to->getRoom();
		room->damage(DamageStruct(getSkillName(),effect.from,effect.to,1,DamageStruct::Fire));
	}
}

class CsNiqu : public ViewAsSkillV2
{
public:
	CsNiqu() : ViewAsSkillV2("csniqu")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("CsNiquCard");
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.initiator && selected.isEmpty() && candidate != request.initiator;
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		ActiveSkillCard *card = new ActiveSkillCard;
		card->setSkillName(objectName());
		return card;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (target && target->isAlive())
			ctx.invoker->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx), DamageStruct::Fire));
		return ContinueEffects;
	}
};

class CsMiaoyuVS : public ViewAsSkillV2
{
public:
	CsMiaoyuVS() : ViewAsSkillV2("csmiaoyu")
	{
		setResponseOrUse(true);
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator) return false;
		const QString &pattern = request.pattern;
		if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
			return (pattern.contains("slash") || pattern.contains("Slash")) || pattern == "jink"
				|| (pattern.contains("peach") && request.initiator->getMark("Global_PreventPeach") == 0)
				|| pattern == "nullification";
		return request.initiator->isWounded() || Slash::IsAvailable(request.initiator);
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		if (!request.initiator || !card || request.selectedCardIds.length() >= 2 || card->hasFlag("using")) return false;
		if (!request.selectedCardIds.isEmpty())
			return card->getSuit() == Sanguosha->getCard(request.selectedCardIds.first())->getSuit();
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
			if (request.initiator->isWounded() && card->getSuit() == Card::Heart) return true;
			if (card->getSuit() == Card::Diamond) {
				FireSlash *slash = new FireSlash(Card::SuitToBeDecided, -1);
				slash->addSubcard(card->getEffectiveId());
				const bool available = slash->isAvailable(request.initiator);
				slash->deleteLater();
				return available;
			}
			return false;
		}
		if (request.pattern == "jink") return card->getSuit() == Card::Club;
		if (request.pattern == "nullification") return card->getSuit() == Card::Spade;
		if (request.pattern.contains("peach")) return card->getSuit() == Card::Heart;
		return request.pattern.contains("slash") || request.pattern.contains("Slash")
			? card->getSuit() == Card::Diamond : false;
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.length() == 2;
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		Card *new_card = nullptr;
		const Card *first = request.selectedCardIds.isEmpty() ? nullptr : Sanguosha->getCard(request.selectedCardIds.first());
		if (!first) return nullptr;
		switch (first->getSuit()) {
		case Card::Spade: {
			new_card = new Nullification(Card::SuitToBeDecided, 0);
			break;
		}
		case Card::Heart: {
			new_card = new Peach(Card::SuitToBeDecided, 0);
			break;
		}
		case Card::Club: {
			new_card = new Jink(Card::SuitToBeDecided, 0);
			break;
		}
		case Card::Diamond: {
			new_card = new FireSlash(Card::SuitToBeDecided, 0);
			break;
		}
		default:
			break;
		}
		if (new_card) {
			new_card->setSkillName(objectName());
			new_card->addSubcards(request.selectedCardIds);
		}
		return new_card;
	}
};

class CsMiaoyu : public TriggerSkillV2
{
public:
	CsMiaoyu() : TriggerSkillV2("csmiaoyu")
	{
		events << PreHpRecover << ConfirmDamage << CardUsed << CardResponded;
		view_as_skill = new CsMiaoyuVS;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName())
			&& (event == PreHpRecover || event == ConfirmDamage || event == CardUsed || event == CardResponded)
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (event == PreHpRecover) {
			RecoverStruct recover = data.value<RecoverStruct>();
			if (!recover.card || !recover.card->getSkillNames().contains(objectName())) return false;
			if (recover.card->subcardsLength() != 2) return false;
			foreach (int id, recover.card->getSubcards()) {
				if (!Sanguosha->getCard(id)->isRed()) return false;
			}
			int old = recover.recover;
			++recover.recover;
			int now = qMin(recover.recover, player->getMaxHp() - player->getHp());
			if (now <= 0) return true;
			if (recover.who && now > old) {
				LogMessage log;
				log.type = "#NewlonghunRecover";
				log.from = recover.who;
				log.to << player;
				log.arg = objectName();
				log.arg2 = QString::number(now);
				room->sendLog(log);
			}

			recover.recover = now;
			data = QVariant::fromValue(recover);
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->getSkillNames().contains(objectName()) || damage.to->isDead()) return false;
			if (damage.card->subcardsLength() != 2) return false;
			foreach (int id, damage.card->getSubcards()) {
				if (!Sanguosha->getCard(id)->isRed()) return false;
			}

			LogMessage log;
			log.type = "#NewlonghunDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);

			data = QVariant::fromValue(damage);
		} else {
			if (!room->hasCurrent() || !player->canDiscard(room->getCurrent(), "he")) return false;
			const Card *card = nullptr;
			if (event == CardUsed)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;

			if (!card || card->isKindOf("SkillCard") || !card->getSkillNames().contains(objectName())) return false;
			if (card->subcardsLength() != 2) return false;
			foreach (int id, card->getSubcards()) {
				if (!Sanguosha->getCard(id)->isBlack()) return false;
			}
			int id = room->askForCardChosen(player, room->getCurrent(), "he", objectName(), false, Card::MethodDiscard);
			room->throwCard(id, room->getCurrent(), player);
		}
		return false;
	}
};

class ZGGongli : public TriggerSkillV2
{
public:
	ZGGongli() : TriggerSkillV2("zggongli")
	{
		frequency = Compulsory;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return false; }
	static bool GlTrigger(Player *player,const QString &mt)
	{
		if(!isNormalGameMode(player->getGameMode())&&player->hasSkill("zggongli")){
			foreach (const Player *p, player->getAliveSiblings()) {
				if(p->getGeneralName().contains(mt)&&player->isYourFriend(p))
					return true;
			}
		}
		return false;
	}
};

class Yance : public TriggerSkillV2
{
public:
	Yance() : TriggerSkillV2("yance")
	{
		events << RoundStart << EventPhaseStart << CardUsed << ChoiceMade;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}
	void yanceUse(ServerPlayer *player, Room *room) const
	{
		if(room->askForChoice(player,objectName(),"yance1+yance2")=="yance1"){
			QList<int>ids = room->getDrawPile();
			qsanShuffle(ids);
			foreach (int id, ids) {
				if (Sanguosha->getCard(id)->isKindOf("TrickCard")){
					room->obtainCard(player,id);
					break;
				}
			}
		}else{
			int n = player->getTag("yanceNum").toInt();
			if(!player->getTag("yanceUse").toBool()){
				n = 3;
				if(ZGGongli::GlTrigger(player,"you_pangtong")) n++;
				player->setTag("yanceNum", n);
				player->setTag("yanceUse", true);
			}
			if(n<1) return;
			QStringList choices;
			player->setMark("yanceTrue",0);
			player->setMark("yanceDraw",0);
			player->setMark("yanceUse",0);
			for (int i = 0; i < n; i++) {
				QString choice = room->askForChoice(player,objectName(),"red+black+BasicCard+TrickCard+EquipCard");
				choices << choice;
			}
			player->setTag("yanceChoice", choices);
			QVariant data = "yanceUsed";
			room->getThread()->trigger(EventForDiy,room,player,data);
		}
	}
	void yanceFinished(ServerPlayer *player, Room *room) const
	{
		QStringList choices = player->getTag("yanceChoice").toStringList();
		int n = player->getMark("yanceTrue");
		int x = player->getMark("fangqiuNum");
		if(n==0){
			room->loseHp(player,1+x,true,player,objectName());
			player->setTag("yanceNum", player->getTag("yanceNum").toInt()-1-x);
		}
		if(choices.length()/2>n)
			room->askForDiscard(player,objectName(),2+x,2+x,false,true);
		else{
			int m = x;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				foreach (QString cn, choices) {
					if(c->getColorString()==cn||c->isKindOf(cn.toLocal8Bit().data())){
						room->obtainCard(player,c);
						if(m>0) m--;
						else c = nullptr;
						break;
					}
				}
				if(c==nullptr) break;
			}
			if(n==choices.length()){
				player->drawCards(2+x,objectName());
				player->setTag("yanceNum", qMin(7,player->getTag("yanceNum").toInt()+1+x));
				if(x>0&&n>3&&player->hasSkill("fangqiu",true))
					room->setPlayerMark(player,"@fangqiu",1);
			}
		}
		player->setMark("fangqiuNum",0);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == RoundStart) {
			if(data.toInt()!=1||player->getMark("yanceUse_lun")>0||!player->hasSkill(objectName())) return false;
			if(player->askForSkillInvoke(this,data)){
				player->addMark("yanceUse_lun");
				player->peiyin(this);
				yanceUse(player,room);
			}
		} else if (event == EventPhaseStart) {
			if(player->getPhase()!=Player::Start||player->getMark("yanceUse_lun")>0||!player->hasSkill(objectName())) return false;
			if(player->askForSkillInvoke(this,data)){
				player->addMark("yanceUse_lun");
				player->peiyin(this);
				yanceUse(player,room);
			}
		} else if (event == ChoiceMade) {
			QStringList choices = data.toString().split(":");
			if(choices[0]=="skillChoice"&&choices[1]==objectName()&&choices[2]=="yance2"){
				choices = player->getTag("yanceChoice").toStringList();
				if(player->getMark("yanceUse")>=choices.length()) return false;
				yanceFinished(player,room);
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					QStringList choices = p->getTag("yanceChoice").toStringList();
					if(p->getMark("yanceUse")>=choices.length()) continue;
					LogMessage log;
					log.type = "#yanceChoice";
					log.from = p;
					log.arg = objectName();
					log.arg2 = choices[p->getMark("yanceUse")];
					room->sendLog(log);
					if(use.card->getColorString()==log.arg2||(p->getMark("yanceTrue")<1&&ZGGongli::GlTrigger(p,"you_xushu"))
					||use.card->isKindOf(log.arg2.toLocal8Bit().data())){
						if(p->getMark("yanceDraw")<5){
							p->addMark("yanceDraw");
							p->drawCards(1,objectName());
						}
						p->addMark("yanceTrue");
					}
					p->addMark("yanceUse");
					if(p->getMark("yanceUse")>=choices.length())
						yanceFinished(p,room);
				}
			}
		}
		return false;
	}
};

class Fangqiu : public TriggerSkillV2
{
public:
	Fangqiu() : TriggerSkillV2("fangqiu")
	{
		events << ChoiceMade << EventForDiy;
		limit_mark = "@fangqiu";
		frequency = Limited;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		return event == EventForDiy && player && player->isAlive() && player->getMark("@fangqiu") > 0
			&& data.toString() == "yanceUsed" ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
	{
		return player->askForSkillInvoke(this);
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		player->peiyin(this);
		room->doSuperLightbox(player, "fangqiu");
		room->removePlayerMark(player, "@fangqiu");
		player->addMark("fangqiuNum");
		LogMessage log;
		log.type = "#yanceChoice";
		log.from = player;
		log.arg = "yance";
		foreach (QString cn, player->getTag("yanceChoice").toStringList()) { log.arg2 = cn; room->sendLog(log); }
		return false;
	}
};

class MobileManjuan : public TriggerSkillV2
{
public:
	MobileManjuan() : TriggerSkillV2("mobile_manjuan")
	{
		events << CardsMoveOneTime;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		return move.to_place == Player::PlaceHand && move.card_ids.length() > 1 && move.to == player
			&& player->getMark("mobile_manjuan_lun") < 5
			&& move.reason.m_skillName != objectName() && move.reason.m_skillName != "InitialHandCards"
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		QStringList ids = ListI2S(move.card_ids);
		const Card *chosen = room->askForExchange(player, objectName(), ids.length(), 1, true,
			"mobile_manjuan0", true, ids.join(","));
		if (!chosen) return false;
		ctx.extra_data = QVariant::fromValue(chosen->getSubcards());
		return true;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const QList<int> selected = ctx.extra_data.value<QList<int>>();
		if (selected.isEmpty()) return false;
		player->addMark("mobile_manjuan_lun");
		player->skillInvoked(this);
		DummyCard chosen(selected);
		room->moveCardTo(&chosen, nullptr, Player::DrawPile, false);
		if (player->isDead()) return false;
		QList<int> dps = room->getDiscardPile();
		qsanShuffle(dps);
		DummyCard replacement;
		foreach (int cid, selected) {
			const Card *c = Sanguosha->getCard(cid);
			foreach (int id, dps) {
				if (c->getType() != Sanguosha->getCard(id)->getType()) { replacement.addSubcard(id); break; }
			}
			if (replacement.subcardsLength() > 4) break;
		}
		room->moveCardTo(&replacement, player, Player::PlaceHand,
			CardMoveReason(CardMoveReason::S_REASON_GOTBACK, player->objectName(), objectName(), ""), true);
		return false;
	}
};

class PTGongli : public Skill
{
public:
	PTGongli() : Skill("ptgongli", Compulsory)
	{
		frequency = Compulsory;
	}
	static bool GlTrigger(Player *player,const QString &mt)
	{
		if(!isNormalGameMode(player->getGameMode())&&player->hasSkill("ptgongli")){
			foreach (const Player *p, player->getAliveSiblings()) {
				if(p->getGeneralName().contains(mt)&&player->isYourFriend(p))
					return true;
			}
		}
		return false;
	}
};

class YangmingVs : public ViewAsSkillV2
{
public:
	YangmingVs() : ViewAsSkillV2("yangming", 1) { response_pattern = "@@yangming"; expand_pile = "yangming"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& request.pattern == "@@yangming"
			&& !request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
				request.activationRef.key.instanceID, "pending_cards").toList().isEmpty();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || !request.selectedCardIds.isEmpty()) return false;
		const SkillInstanceRef ref = request.activationRef;
		const QVariantList pending = request.initiator->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pending_cards").toList();
		const QStringList used = request.initiator->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_suits").toStringList();
		if (!pending.contains(candidate->getEffectiveId()) || !request.initiator->getPile("yangming").contains(candidate->getEffectiveId())
			|| used.contains(candidate->getSuitString())) return false;
		std::unique_ptr<Card> preview(Sanguosha->cloneCard(candidate->objectName(), candidate->getSuit(), candidate->getNumber()));
		if (!preview) return false;
		preview->setSkillName(objectName());
		preview->setActivationSkill(ref.key.skillName, ref.key.instanceID);
		preview->addSubcard(candidate);
		return preview->isAvailable(request.initiator);
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest empty = request;
		empty.selectedCardIds.clear();
		return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
		Card *card = Sanguosha->cloneCard(original->objectName(), original->getSuit(), original->getNumber());
		if (!card) return nullptr;
		card->setSkillName(objectName());
		card->addSubcard(original);
		return card;
	}
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.size() == 1 ? Sanguosha->getCard(request.selectedCardIds.first())->getClassName() : QString();
	}
	LimitScope getLimitScope() const override { return Limit_Custom; }
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		return ctx.owner && (!ctx.use_card || !ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
			ctx.activationRef.key.instanceID, "used_suits").toStringList().contains(ctx.use_card->getSuitString()));
	}
	void addUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.use_card) return;
		QStringList used = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
			ctx.activationRef.key.instanceID, "used_suits").toStringList();
		if (!used.contains(ctx.use_card->getSuitString())) used << ctx.use_card->getSuitString();
		ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "used_suits", used);
	}
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!ctx.use_card || !cardSelectionFeasible(request) || !checkCustomUsage(ctx)) return false;
		addUsage(ctx);
		return true;
	}
};

class YangmingMod : public TargetModSkillV2
{
public:
	YangmingMod() : TargetModSkillV2("#yangming-mod", ".") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.primary || !ctx.card || ctx.modType != Residue || ctx.card->getSkillName() != "yangming")
			return CorrectSkillResult::noEffect();
		return ctx.primary->getSkillInstanceCorrectStateValue(ctx.card->getActivationSkillName(),
			ctx.card->getActivationSkillInstanceId(), "resolving").toBool()
			? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
	}
};

class Yangming : public TriggerSkillV2
{
public:
	Yangming() : TriggerSkillV2("yangming") { events << EventPhaseEnd; view_as_skill = new YangmingVs; }
	int drawCount(Room *room, ServerPlayer *owner) const
	{
		const QVariantMap scopes = room->historyScopes();
		if (scopes.value("phase_id").toLongLong() == 0 || scopes.value("turn_id").toLongLong() == 0) return -1;
		int lost = 0;
		QSet<int> suits;
		for (int pass = 0; pass < 2; ++pass) {
			QVariantMap query = pass == 0 ? QVariantMap{{"phase_id", scopes.value("phase_id")}, {"from", owner->objectName()}}
				: QVariantMap{{"turn_id", scopes.value("turn_id")}};
			for (;;) {
				const QVariantMap page = room->queryHistoryMoves(query);
				if (!page.value("complete").toBool()) return -1;
				for (const QVariant &item : page.value("items").toList()) {
					const QVariantMap data = item.toMap().value("data").toMap();
					if (pass == 0) {
						if (!data.contains("from_place")) return -1;
						if (data.value("from_place").toInt() == Player::PlaceHand) ++lost;
					} else if (data.value("to_place").toInt() == Player::DiscardPile) {
						const QVariantMap card = data.value("card").toMap();
						if (!card.contains("suit")) return -1;
						const int suit = card.value("suit").toInt();
						if (suit >= Card::Spade && suit <= Card::Diamond) suits.insert(suit);
					}
				}
				if (!page.value("has_more").toBool()) break;
				query.insert("after", page.value("next_after"));
				query.insert("watermark", page.value("watermark"));
			}
			if (pass == 0 && lost < 3) return 0;
		}
		return suits.size() + (PTGongli::GlTrigger(owner, "you_zhugeliang") ? 1 : 0);
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
			&& drawCount(room, player) > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const int count = drawCount(room, ctx.owner);
		if (count < 0) { qWarning("Yangming move history unavailable"); return false; }
		ctx.extra_data = count;
		return count > 0 && ctx.owner->askForSkillInvoke(this, *ctx.original_data);
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *owner = ctx.owner;
		Room::AcceptedViewAsEffectScope selector(room, owner, objectName(), ctx);
		if (!selector.isValid()) return false;
		const SkillInstanceRef ref = selector.activationRef();
		const QVariant oldPending = owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pending_cards");
		const QVariant oldSuits = owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_suits");
		const QVariant oldResolving = owner->getSkillInstanceCorrectStateValue(ref.key.skillName, ref.key.instanceID, "resolving");
		const QList<int> cards = room->getNCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx));
		QVariantList pending;
		for (int id : cards) pending << id;
		owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pending_cards", pending);
		owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_suits", QStringList());
		room->setSkillInstanceCorrectState(owner, ref, "resolving", true);
		// The scoped child owns only this prompt sequence; its parent attribution remains frozen.
		auto restore = [&]() {
			owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pending_cards", oldPending);
			owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_suits", oldSuits);
			room->setSkillInstanceCorrectState(owner, ref, "resolving", oldResolving);
		};
		auto leftovers = [&]() {
			QList<int> remaining;
			for (int id : cards) if (owner->getPile("yangming").contains(id)) remaining << id;
			return remaining;
		};
		QStringList used;
		try {
			owner->peiyin(this);
			owner->addToPile("yangming", cards);
			while (owner->isAlive() && !leftovers().isEmpty())
				if (!room->askForUseCard(owner, "@@yangming", "yangming0", -1, Card::MethodUse, false)) break;
			used = owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_suits").toStringList();
		} catch (...) {
			restore();
			try { DummyCard rest(leftovers()); if (rest.subcardsLength() > 0) room->throwCard(&rest, objectName(), nullptr); } catch (...) {}
			throw;
		}
		const QList<int> remaining = leftovers();
		restore();
		if (!remaining.isEmpty()) { DummyCard rest(remaining); room->throwCard(&rest, objectName(), nullptr); }
		if (owner->isAlive() && PTGongli::GlTrigger(owner, "you_xushu"))
			for (int id : remaining)
				if (room->getCardPlace(id) == Player::DiscardPile && !used.contains(Sanguosha->getCard(id)->getSuitString())) {
					room->obtainCard(owner, id); break;
				}
		return false;
	}
};
class Qihui : public TriggerSkillV2
{
public:
	Qihui() : TriggerSkillV2("qihui") { events << CardUsed; frequency = Compulsory; }
	static QStringList types(const Player *owner, int id)
	{
		return owner->getSkillInstanceStateValue("qihui", id, "recorded_types").toStringList();
	}
	static void setTypes(Room *room, ServerPlayer *owner, int id, const QStringList &values)
	{
		owner->setSkillInstanceStateValue("qihui", id, "recorded_types", values);
		QStringList display;
		for (int instance : owner->getSkillInstanceIds("qihui"))
			for (const QString &type : types(owner, instance))
				if (!display.contains(type + "_char")) display << type + "_char";
		for (const QString &mark : owner->getMarkNames())
			if (mark.startsWith("&qihui+:+")) room->setPlayerMark(owner, mark, 0);
		if (!display.isEmpty()) room->setPlayerMark(owner, "&qihui+:+" + display.join("+"), 1);
	}
	static QStringList chooseTwo(Room *room, ServerPlayer *owner, const QString &skill, QStringList values)
	{
		QStringList selected;
		for (int i = 0; i < 2 && !values.isEmpty(); ++i) {
			QStringList choices;
			for (const QString &type : values) choices << "qihui0=" + type + "_char";
			const QString choice = room->askForChoice(owner, skill, choices.join("+"));
			if (!choices.contains(choice)) return {};
			QString type = choice.section('=', 1, 1);
			type.chop(5); // strip the existing _char translation suffix
			selected << type;
			values.removeOne(type);
		}
		return selected;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || use.card->getTypeId() == Card::TypeSkill)
			return result;
		for (int id : player->getSkillInstanceIds(objectName()))
			if (!types(player, id).contains(use.card->getType())) result[player] << SkillInstanceUtils::formatName(objectName(), id);
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = {player};
		const SkillInstanceRef ref = getUsageRef(ctx);
		return ref.isValid() && ref.ownerObjectName == player->objectName()
			&& !types(player, ref.key.instanceID).contains(ctx.original_data->value<CardUseStruct>().card->getType());
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		const int id = getUsageRef(ctx).key.instanceID;
		QStringList values = types(ctx.owner, id);
		const QString type = ctx.original_data->value<CardUseStruct>().card->getType();
		if (values.contains(type)) return false;
		values << type;
		setTypes(room, ctx.owner, id, values);
		room->sendCompulsoryTriggerLog(ctx.owner, this);
		if (values.size() < 3) return false;
		const QStringList selected = chooseTwo(room, ctx.owner, objectName(), values);
		if (selected.size() != 2) return false;
		for (const QString &value : selected) values.removeOne(value);
		setTypes(room, ctx.owner, id, values);
		const QString choice = room->askForChoice(ctx.owner, objectName(), "qihui1+qihui2+qihui3");
		if (choice == "qihui1") room->recover(ctx.owner, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
		else if (choice == "qihui2") ctx.owner->drawCards(2 * getEffectiveAmount(ctx), objectName());
		else if (choice == "qihui3") {
			QVariantList receipts = ctx.owner->getTag("mobile_qihui_receipts").toList();
			const quint64 serial = room->getTag("mobile_qihui_serial").toULongLong() + 1;
			room->setTag("mobile_qihui_serial", serial);
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"serial", serial}, {"activation_owner", ctx.activationRef.ownerObjectName},
				{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
			ctx.owner->setTag("mobile_qihui_receipts", receipts);
			// A public applied-effect projection; the exact origins remain in the receipt ledger.
			room->setPlayerMark(ctx.owner, "qihui3Bf", receipts.size());
		}
		return false;
	}
};

class QihuiNext : public TriggerSkillV2
{
public:
	QihuiNext() : TriggerSkillV2("#qihui-next") { events << PreCardUsed << CardFinished << EventPhaseChanging; frequency = Compulsory; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging) {
			for (ServerPlayer *holder : room->getAllPlayers(true)) {
				QVariantList keep;
				int available = 0;
				for (const QVariant &entry : holder->getTag("mobile_qihui_receipts").toList()) {
					const qint64 id = entry.toMap().value("use").toLongLong();
					if (id > 0 && room->historyEvent(id).value("status").toString() == "finished") continue;
					keep << entry;
					if (id <= 0) ++available;
				}
				holder->setTag("mobile_qihui_receipts", keep);
				room->setPlayerMark(holder, "qihui3Bf", available);
			}
			return true;
		}
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!player || !use.card || use.card->getTypeId() == Card::TypeSkill) return true;
		const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
		if (useId <= 0) return true;
		QVariantList receipts;
		int ordinal = 0;
		for (const QVariant &entry : player->getTag("mobile_qihui_receipts").toList()) {
			QVariantMap receipt = entry.toMap();
			if (event == CardFinished && receipt.value("use").toLongLong() == useId) continue;
			if (event == PreCardUsed && receipt.value("use").toLongLong() <= 0) {
				receipt.insert("use", useId);
				receipt.insert("ordinal", ordinal++);
			}
			receipts << receipt;
		}
		player->setTag("mobile_qihui_receipts", receipts);
		int available = 0;
		for (const QVariant &entry : receipts) if (entry.toMap().value("use").toLongLong() <= 0) ++available;
		room->setPlayerMark(player, "qihui3Bf", available);
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		if (event != PreCardUsed || !player || !player->isAlive() || !use.card || use.card->getTypeId() == Card::TypeSkill) return true;
		const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
		for (const QVariant &receipt : player->getTag("mobile_qihui_receipts").toList()) {
			const QVariantMap saved = receipt.toMap();
			if (useId <= 0 || saved.value("use").toLongLong() != useId) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(saved.value("owner").toString(), SkillInstanceKey(saved.value("skill").toString(), saved.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.trigger_count = saved.value("ordinal").toInt();
			ctx.preferredTarget = player;
			ctx.preferredTargetSeat = player->getSeat();
			ctx.targets = {player};
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.extra_data = receipt;
			// This is fulfillment of an accepted effect, not a new activation of a retired source.
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.sourceRef.isValid() && ctx.owner->getTag("mobile_qihui_receipts").toList().contains(ctx.extra_data);
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		QVariantList receipts = ctx.owner->getTag("mobile_qihui_receipts").toList();
		if (!receipts.removeOne(ctx.extra_data)) return false;
		ctx.owner->setTag("mobile_qihui_receipts", receipts);
		int available = 0;
		for (const QVariant &entry : receipts) if (entry.toMap().value("use").toLongLong() <= 0) ++available;
		room->setPlayerMark(ctx.owner, "qihui3Bf", available);
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		use.m_addHistory = false;
		ctx.original_data->setValue(use);
		return false;
	}
};

class QihuiMod : public TargetModSkillV2
{
public:
	QihuiMod() : TargetModSkillV2("#qihui-mod", ".") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == Residue && ctx.primary && ctx.primary->getMark("qihui3Bf") > 0
			? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
	}
};

class Xiaxing : public TriggerSkillV2
{
public:
	Xiaxing() : TriggerSkillV2("xiaxing") { events << CardsMoveOneTime << GameStart; }
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == GameStart) return TriggerList{{player, {objectName()}}};
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile) return {};
		bool affordable = false;
		for (int id : player->getSkillInstanceIds("qihui")) if (Qihui::types(player, id).size() >= 2) affordable = true;
		if (!affordable) return {};
		for (int id : move.card_ids)
			if (Sanguosha->getCard(id)->objectName() == "_xuanjian" && room->getCardPlace(id) == Player::DiscardPile)
				return TriggerList{{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.owner};
		if (event == GameStart) return true;
		if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
		QStringList sources;
		for (int id : ctx.owner->getSkillInstanceIds("qihui"))
			if (Qihui::types(ctx.owner, id).size() >= 2) sources << SkillInstanceUtils::formatName("qihui", id);
		if (sources.isEmpty()) return false;
		const QString selected = sources.size() == 1 ? sources.first() : room->askForChoice(ctx.owner, objectName(), sources.join("+"));
		if (!sources.contains(selected)) return false;
		const int source = SkillInstanceUtils::parseInstanceId(selected);
		const QStringList removed = Qihui::chooseTwo(room, ctx.owner, objectName(), Qihui::types(ctx.owner, source));
		int card = -1;
		for (int id : ctx.original_data->value<CardsMoveOneTimeStruct>().card_ids)
			if (Sanguosha->getCard(id)->objectName() == "_xuanjian" && room->getCardPlace(id) == Player::DiscardPile) { card = id; break; }
		if (removed.size() != 2 || card < 0) return false;
		ctx.extra_data = QVariantMap{{"source", source}, {"removed", removed}, {"card", card}};
		return true;
	}
	bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.owner};
		if (event == GameStart) return true;
		const QVariantMap selected = ctx.extra_data.toMap();
		const int source = selected.value("source").toInt();
		QStringList values = Qihui::types(ctx.owner, source);
		if (room->getCardPlace(selected.value("card").toInt()) != Player::DiscardPile) return false;
		for (const QString &type : selected.value("removed").toStringList()) if (!values.removeOne(type)) return false;
		Qihui::setTypes(room, ctx.owner, source, values);
		return true;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		if (event == CardsMoveOneTime) {
			const int id = ctx.extra_data.toMap().value("card").toInt();
			if (room->getCardPlace(id) == Player::DiscardPile) { ctx.owner->peiyin(this); room->obtainCard(ctx.owner, id); }
			return false;
		}
		for (int id : Sanguosha->getRandomCards(true)) {
			const Card *card = Sanguosha->getCard(id);
			if (card->objectName() != "_xuanjian" || room->getCardOwner(id)) continue;
			room->sendCompulsoryTriggerLog(ctx.owner, this);
			room->obtainCard(ctx.owner, id);
			if (ctx.owner->isAlive() && ctx.owner->handCards().contains(id) && card->isAvailable(ctx.owner)) room->useCardFromSkillEffect(CardUseStruct(card, ctx.owner), ctx, true);
			break;
		}
		return false;
	}
};
QinyingCard::QinyingCard()
{
	setSkillName("qinying");
	handling_method = Card::MethodRecast;
	will_throw = false;
}

bool QinyingCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	Card*dc = Sanguosha->cloneCard("duel");
	dc->setSkillName("qinying");
	dc->deleteLater();
	return dc->targetFilter(targets,to,Self);
}

void QinyingCard::onUse(Room *room, CardUseStruct &use) const
{
	// V2 owns recast and the follow-up Duel; this method remains only for the
	// named SkillCard metaobject used by legacy AI reconstruction.
	SkillCard::onUse(room, use);
}

void QinyingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	Q_UNUSED(room);
	Q_UNUSED(source);
	// The V2 callback is the sole business implementation.
}

class QinyingVs : public ViewAsSkillV2
{
public:
	QinyingVs() : ViewAsSkillV2("qinying")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("QinyingCard");
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
	{
		return request.initiator && to_select && !request.initiator->isCardLimited(to_select, Card::MethodRecast);
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return !request.selectedCardIds.isEmpty();
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		if (!request.initiator || !candidate) return false;
		Card *duel = Sanguosha->cloneCard("duel");
		duel->setSkillName("qinying");
		const bool result = duel->targetFilter(selected, candidate, request.initiator);
		duel->deleteLater();
		return result;
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		Card *duel = Sanguosha->cloneCard("duel");
		duel->setSkillName("qinying");
		const bool result = duel->targetsFeasible(selected, request.initiator);
		duel->deleteLater();
		return result;
	}
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		ActiveSkillCard *card = new ActiveSkillCard;
		card->setSkillName(objectName());
		return card;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		if (!ctx.invoker || !ctx.use_card || ctx.use_card->subcardsLength() <= 0) return false;
		LogMessage log;
		log.type = "$RecastCard";
		log.from = ctx.invoker;
		log.card_str = ListI2S(ctx.use_card->getSubcards()).join("+");
		room->sendLog(log);
		room->moveCardTo(ctx.use_card, ctx.invoker, nullptr, Player::DiscardPile,
			CardMoveReason(CardMoveReason::S_REASON_RECAST, ctx.invoker->objectName(), objectName(), ""));
		ctx.invoker->drawCards(ctx.use_card->subcardsLength(), "recast");
		ctx.invoker->setMark("QinyingNum", ctx.use_card->subcardsLength());
		return ctx.invoker->isAlive();
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		if (!ctx.invoker || ctx.targets.isEmpty() || ctx.invoker->isDead()) return FinishSkill;
		Card *duel = Sanguosha->cloneCard("duel");
		duel->setSkillName("_qinying");
		CardUseStruct use(duel, ctx.invoker, ctx.targets);
		ctx.invoker->getRoom()->useCard(use);
		return FinishSkill;
	}
};

class Qinying : public TriggerSkillV2
{
public:
	Qinying() : TriggerSkillV2("qinying")
	{
		events << CardAsked;
		view_as_skill = new QinyingVs;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == CardAsked && player && player->isAlive() && player->getMark("QinyingNum") > 0
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == CardAsked) {
			QVariant &data = *ctx.original_data;
			QStringList ask = data.toStringList();
			if(ask.contains("slash")&&ask.contains("response")&&ask.last().contains("qinying")){
				if(player->getMark("QinyingNum")>0){
					QList<int>ids;
					QStringList ban = player->getTag("stgongliBan").toStringList();
					foreach (const Card*c, player->getCards("hej")) {
						if(ban.contains(c->getType()))
							ids << c->getId();
					}
					if(ids.length()<player->getCardCount(true,true)&&player->askForSkillInvoke("qinying0","qinying",false)){
						int id = room->askForCardChosen(player,player,"hej",objectName(),true,Card::MethodDiscard,ids);
						if(id>-1){
							player->removeMark("QinyingNum");
							room->throwCard(id,objectName(),player);
							Card*dc = Sanguosha->cloneCard("slash");
							dc->setSkillName("_qinying");
							dc->deleteLater();
							room->provide(dc);
							return true;
						}
					}
				}
			}
		}
		return false;
	}
};

class Lunxiong : public TriggerSkillV2
{
public:
	Lunxiong() : TriggerSkillV2("lunxiong")
	{
		events << Damage << Damaged;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName())
			&& (event == Damage || event == Damaged) ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		QList<const Card*>hs = player->getHandcards();
		if(hs.isEmpty()) return false;
		const Card*mc = hs.last();
		foreach (const Card*c, hs) {
			if(c->getNumber()>mc->getNumber())
				mc = c;
		}
		foreach (const Card*c, hs) {
			if(c->getId()!=mc->getId()&&c->getNumber()>=mc->getNumber())
				return false;
		}
		if(mc->getNumber()>player->getMark("&lunxiong")
			&&room->askForCard(player,mc->toString(),"lunxiong0",data,objectName())){
			room->setPlayerMark(player,"&lunxiong",mc->getNumber());
			player->drawCards(3,objectName());
		}
		return false;
	}
};

class STGongli : public TriggerSkillV2
{
public:
	STGongli() : TriggerSkillV2("stgongli")
	{
		events << GameStart;
		frequency = Compulsory;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == GameStart && player && player->isAlive() && player->hasSkill(objectName())
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (event == GameStart) {
			if(player->hasSkill("qinying",true)){
				room->sendCompulsoryTriggerLog(player,this);
				int n = 0;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getGeneralName().startsWith("mobileyou_")) n++;
				}
				QStringList ban,choices;
				choices << "basic" << "trick" << "equip";
				for (int i = 0; i < qMin(n,3); i++) {
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					choices.removeOne(choice);
					ban << choice;
				}
				player->setTag("stgongliBan", ban);
			}
		}
		return false;
	}
};

class Shunyi : public TriggerSkillV2
{
public:
	Shunyi() : TriggerSkillV2("shunyi")
	{
		events << CardUsed << EventPhaseChanging;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && (event == CardUsed || event == EventPhaseChanging)
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event != CardUsed || !player->hasSkill(objectName())) return true;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		return use.card && !use.card->isVirtualCard() && use.m_isHandcard
			&& use.card->getNumber() > player->getMark("shunyiUse-Clear")
			&& (use.card->getSuit() == Card::Diamond || player->getTag("CJgongli").toStringList().contains(use.card->getSuitString()))
			&& player->askForSkillInvoke(this, *ctx.original_data);
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card && use.card->getTypeId()>0&&!use.card->isVirtualCard()){
				if(use.m_isHandcard&&use.card->getNumber()>player->getMark("shunyiUse-Clear")
				&& (use.card->getSuit()==Card::Diamond||player->getTag("CJgongli").toStringList().contains(use.card->getSuitString()))){
					Card*dc = new DummyCard;
					foreach (const Card*c, player->getHandcards()) {
						if(c->getSuit()==use.card->getSuit())
							dc->addSubcard(c);
					}
					dc->deleteLater();
					if(dc->subcardsLength()>0){
						player->peiyin(this);
						player->addToPile(objectName(),dc,false);
						player->drawCards(1,objectName());
					}
				}
			}
		}else{
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->getPile(objectName()).length()>0){
						Card*dc = new DummyCard(p->getPile(objectName()));
						p->obtainCard(dc,false);
						dc->deleteLater();
					}
				}
			}
		}
		return false;
	}
};

BiweiCard::BiweiCard()
{
	setSkillName("biwei");
}

bool BiweiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self;
}

void BiweiCard::onEffect(CardEffectStruct &effect) const
{
	if(effect.to->isAlive()){
		Room*room = effect.to->getRoom();
		Card*dc = new DummyCard;
		foreach (const Card*c, effect.to->getHandcards()) {
			if(c->getNumber()>=getNumber()&&!effect.to->isJilei(c))
				dc->addSubcard(c);
		}
		if(dc->subcardsLength()>0){
			room->throwCard(dc,getSkillName(),effect.to);
		}else{
			room->addPlayerHistory(effect.from,"BiweiCard",-1);
		}
		dc->deleteLater();
	}
}

class Biwei : public ViewAsSkillV2
{
public:
	Biwei() : ViewAsSkillV2("biwei")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("BiweiCard") && !request.initiator->isKongcheng();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || !candidate || candidate->isEquipped() || request.selectedCardIds.length() >= 1)
			return false;
		const QList<const Card *> hand = request.initiator->getHandcards();
		if (hand.isEmpty() || request.initiator->isJilei(candidate)) return false;
		int max = candidate->getNumber();
		foreach (const Card *card, hand) max = qMax(max, card->getNumber());
		if (candidate->getNumber() != max) return false;
		foreach (const Card *card, hand)
			if (card->getId() != candidate->getId() && card->getNumber() >= max) return false;
		return true;
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.length() == 1;
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		return request.initiator && candidate && candidate != request.initiator && selected.isEmpty();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.length() == 1;
	}
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		ActiveSkillCard *card = new ActiveSkillCard;
		card->setSkillName(objectName());
		return card;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (target && target->isAlive() && ctx.use_card && ctx.use_card->subcardsLength() > 0) {
			const int number = Sanguosha->getCard(ctx.use_card->getSubcards().first())->getNumber();
			DummyCard *discard = new DummyCard;
			foreach (const Card *card, target->getHandcards())
				if (card->getNumber() >= number && !target->isJilei(card)) discard->addSubcard(card);
			if (discard->subcardsLength() > 0) ctx.invoker->getRoom()->throwCard(discard, objectName(), target);
			else ctx.invoker->getRoom()->addPlayerHistory(ctx.invoker, "BiweiCard", -1);
			discard->deleteLater();
		}
		return ContinueEffects;
	}
};

class CJGongli : public TriggerSkillV2
{
public:
	CJGongli() : TriggerSkillV2("cjgongli")
	{
		events << GameStart;
		frequency = Compulsory;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == GameStart && player && player->isAlive() && player->hasSkill(objectName())
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (event == GameStart) {
			if(player->hasSkill("shunyi",true)){
				room->sendCompulsoryTriggerLog(player,this);
				int n = 0;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getGeneralName().startsWith("mobileyou_")) n++;
				}
				QStringList ban,choices;
				choices << "diamond" << "spade" << "club";
				for (int i = 0; i < qMin(n,3); i++) {
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					choices.removeOne(choice);
					ban << choice;
				}
				player->setTag("CJgongli", ban);
			}
		}
		return false;
	}
};

class XSGongli : public TriggerSkillV2
{
public:
	XSGongli() : TriggerSkillV2("xsgongli")
	{
		frequency = Compulsory;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return false; }
	static bool GlTrigger(const Player *player,const QString &mt)
	{
		if(!isNormalGameMode(player->getGameMode())&&player->hasSkill("xsgongli")){
			foreach (const Player *p, player->getAliveSiblings()) {
				if(p->getGeneralName().contains(mt)&&player->isYourFriend(p))
					return true;
			}
		}
		return false;
	}
};

class XuanjianVS : public ViewAsSkillV2
{
public:
	XuanjianVS() : ViewAsSkillV2("_xuanjian", 1)
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && !request.initiator->isKongcheng()
			&& ((request.reason == CardUseStruct::CARD_USE_REASON_PLAY && Slash::IsAvailable(request.initiator))
				|| (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern.contains("slash")));
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && !card->isEquipped();
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.length() == 1;
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.selectedCardIds.isEmpty()) return nullptr;
		const Card *selected = Sanguosha->getCard(request.selectedCardIds.first());
		Card *card = Sanguosha->cloneCard("slash");
		card->setSkillName(objectName());
		if (XSGongli::GlTrigger(request.initiator, "you_zhugeliang")) card->addSubcard(selected);
		else foreach (const Card *h, request.initiator->getHandcards()) if (h->getSuit() == selected->getSuit()) card->addSubcard(h);
		return card;
	}
};

Xuanjian::Xuanjian(Suit suit, int number)
	: Weapon(suit, number, 3)
{
	setObjectName("_xuanjian");
}

class Caiqiu : public TriggerSkillV2
{
public:
	Caiqiu() : TriggerSkillV2("caiqiu")
	{
		events << CardFinished << RoundStart;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && (event == CardFinished || event == RoundStart)
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->getMark(use.card->objectName()+"caiqiuCn_lun")>0&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this,qsanRandomBounded(2)+3);
					room->loseHp(p,1,true,p,objectName());
				}
			}
		}else{
			if (player->isAlive()&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this,qsanRandomBounded(2)+1);
				QList<int>ids = room->getNCards(room->getPlayers().length());
				room->fillAG(ids,player);
				Card*dc = dummyCard();
				room->returnToTopDrawPile(ids);
				while (ids.length()>0) {
					int id = room->askForAG(player,ids,true,objectName());
					if(id<0) break;
					ids.removeAll(id);
					dc->addSubcard(id);
					room->takeAG(player,id,false,QList<ServerPlayer*>()<<player);
					player->addMark(Sanguosha->getCard(id)->objectName()+"caiqiuCn_lun");
				}
				room->clearAG(player);
				player->obtainCard(dc);
			}
		}
		return false;
	}
};

class Xichang : public TriggerSkill
{
public:
	Xichang() : TriggerSkill("xichang")
	{
		events << GameStart << CardsMoveOneTime;
		frequency = Compulsory;
		waked_skills = "weizhuang";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == GameStart) {
			QString choice = room->askForChoice(player,objectName(),"guidian+dongjiao+xiuge");
			int n = qsanRandomBounded(2)+1;
			if(choice=="dongjiao") n += 2;
			else if(choice=="xiuge") n += 4;
			room->sendCompulsoryTriggerLog(player,this,n);
			if(choice!="guidian"&&player->getGeneralName().endsWith("cuifu")){
				player->setAvatarIcon(choice+"_cuifu");
			}
			room->setPlayerProperty(player,"xichangBf",choice);
			room->setPlayerMark(player,"&xichang+:+"+choice,1);
			room->attachSkillToPlayer(player,"weizhuang");
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&move.reason.m_reason!=CardMoveReason::S_REASON_DRAW){
				QList<int>ids;
				foreach (int id, player->handCards()) {
					if(move.card_ids.contains(id)) ids << id;
				}
				if(ids.isEmpty()) return false;
				room->sendCompulsoryTriggerLog(player,this);
				foreach (int id, ids){
					room->setCardFlag(id,"visible");
					room->setCardTip(id,"mingzhi");
				}
				QList<CardsMoveStruct> moves;
				CardsMoveStruct move1(ids,player,nullptr,Player::PlaceHand,Player::PlaceTable,
				CardMoveReason(CardMoveReason::S_REASON_SHOW,player->objectName(),objectName(),""));
				moves.append(move1);
				CardsMoveStruct move2(ids,nullptr,player,Player::PlaceTable,Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_SHOW,player->objectName(),objectName(),""));
				moves.append(move2);
				room->notifyMoveCards(true, moves, true);
				room->notifyMoveCards(false, moves, true);
				QVariant data2 = "mingzhi:"+ListI2S(ids).join("+");
				room->getThread()->trigger(EventForDiy,room,player,data2);
			}
		}
		return false;
	}
};

WeizhuangCard::WeizhuangCard()
{
	setSkillName("weizhuang");
}

bool WeizhuangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("weizhuang");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool WeizhuangCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE){
		Card *card = Sanguosha->cloneCard(user_string.split("+").first());
		if(card){
			card->setSkillName("_weizhuang");
			card->deleteLater();
			return card->targetFixed();
		}
	}
	return true;
}

bool WeizhuangCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("weizhuang");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card *WeizhuangCard::validate(CardUseStruct &use) const
{
	Q_UNUSED(use);
	return nullptr;
}

const Card *WeizhuangCard::validateInResponse(ServerPlayer *source) const
{
	Q_UNUSED(source);
	return nullptr;
}

class Weizhuangvs : public ViewAsSkillV2
{
public:
	Weizhuangvs() : ViewAsSkillV2("weizhuang", 1)
	{
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.initiator->property("xichangBf").toString() != "xiuge") return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
			return request.initiator->hasTurn() && (request.pattern.contains("slash") || request.pattern.contains("jink")
				|| request.pattern.contains("peach") || request.pattern.contains("analeptic"));
		return request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
	{
		if (!request.initiator || !to_select || to_select->isEquipped() || request.selectedCardIds.length() >= 1) return false;
		QStringList suits;
		foreach (const Card *h, request.initiator->getHandcards()) if (h->hasTip("mingzhi") && !suits.contains(h->getSuitString())) suits << h->getSuitString();
		foreach (const Card *c, request.initiator->getEquips()) if (!suits.contains(c->getSuitString())) suits << c->getSuitString();
		return suits.length() == 4 || !request.initiator->isJilei(to_select);
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.length() == 1; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
		const Player *candidate) const override
	{
		if (!request.initiator || !candidate || !selected.isEmpty()) return false;
		foreach (QString name, request.pattern.split("+")) {
			Card *card = Sanguosha->cloneCard(name);
			const bool ok = card && card->targetFilter(selected, candidate, request.initiator);
			if (card) card->deleteLater();
			if (ok) return true;
		}
		return false;
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{
		return selected.isEmpty() || selected.length() == 1;
	}
	bool willThrowSelectedCards() const override { return false; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		ActiveSkillCard *card = new ActiveSkillCard;
		card->setSkillName(objectName());
		card->setUserString(request.pattern.isEmpty() ? "slash" : request.pattern);
		return card;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		if (!ctx.invoker || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return false;
		QStringList suits;
		foreach (const Card *h, ctx.invoker->getHandcards()) if (h->hasTip("mingzhi") && !suits.contains(h->getSuitString())) suits << h->getSuitString();
		foreach (const Card *c, ctx.invoker->getEquips()) if (!suits.contains(c->getSuitString())) suits << c->getSuitString();
		const Card *material = Sanguosha->getCard(ctx.use_card->getSubcards().first());
		if (suits.length() == 4) room->showCard(ctx.invoker, material->getEffectiveId());
		else room->throwCard(ctx.use_card, objectName(), ctx.invoker);
		QStringList choices;
		foreach (QString name, (qobject_cast<const SkillCard *>(ctx.use_card) ? qobject_cast<const SkillCard *>(ctx.use_card)->getUserString() : QString()).split("+"))
			if (ctx.invoker->getMark("weizhuang_juguan_remove_" + name + "-Clear") < 1) choices << name;
		if (choices.isEmpty()) return false;
		ctx.choice = choices.first();
		if (choices.length() > 1) ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
		ctx.invoker->skillInvoked(objectName(), qsanRandomBounded(6) + 13);
		room->addPlayerMark(ctx.invoker, "weizhuang_juguan_remove_" + ctx.choice + "-Clear");
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		if (!ctx.invoker || ctx.invoker->isDead()) return FinishSkill;
		QString name = ctx.choice.isEmpty() ? (qobject_cast<const SkillCard *>(ctx.use_card) ? qobject_cast<const SkillCard *>(ctx.use_card)->getUserString() : QString()).split("+").first() : ctx.choice;
		Card *card = Sanguosha->cloneCard(name);
		if (!card) return FinishSkill;
		card->setSkillName("_weizhuang");
		if (ctx.use_card->subcardsLength() > 0)
			card->setFlags("weizhuangSuit" + Sanguosha->getCard(ctx.use_card->getSubcards().first())->getSuitString());
		ctx.invoker->getRoom()->useCard(CardUseStruct(card, ctx.invoker, ctx.targets));
		return FinishSkill;
	}
};

class WeizhuangTargetMod : public TargetModSkillV2
{
public:
	WeizhuangTargetMod() : TargetModSkillV2("#weizhuang-target") { pattern = "."; }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == Residue && ctx.card && ctx.card->getSkillName() == "weizhuang"
			? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
	}
};
class Weizhuang : public TriggerSkillV2
{
public:
	Weizhuang() : TriggerSkillV2("weizhuang&")
	{
		events << CardFinished << EventPhaseStart << TargetSpecified
		<< EventForDiy << DrawNCards << ConfirmDamage << HpRecover;
		view_as_skill = new Weizhuangvs;
	}
	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::juguan(objectName(), "slash,jink,peach,analeptic");
	}
	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return qsanRandomBounded(6)+11;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && (event == CardFinished || event == EventPhaseStart || event == TargetSpecified
			|| event == EventForDiy || event == DrawNCards || event == ConfirmDamage || event == HpRecover)
			? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())&&player->isAlive()){
				foreach (int id, room->getDrawPile()+room->getDiscardPile()) {
					if(use.card->hasFlag("weizhuangSuit"+Sanguosha->getCard(id)->getSuitString())){
						room->obtainCard(player,id);
						break;
					}
				}
			}
			if(use.card->isKindOf("EquipCard")&&player->property("xichangBf").toString()=="dongjiao"
			&&player->isAlive()&&player->getMark("dongjiao3-Clear")<1&&player->hasTurn()){
				QStringList ts;
				foreach (const Card*c, player->getHandcards()) {
					if(c->hasTip("mingzhi")){
						if(ts.contains(c->getType())) continue;
						ts.append(c->getType());
					}
				}
				foreach (const Card*c, player->getEquips()) {
					if(ts.contains(c->getType())) continue;
					ts.append(c->getType());
				}
				if(ts.length()<3) return false;
				QList<ServerPlayer *>tps;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->hasEquip()){
						tps << p;
						continue;
					}
					foreach (const Card*ch, p->getHandcards()) {
						if(ch->hasTip("mingzhi")){
							tps << p;
							break;
						}
					}
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,"dongjiao3","dongjiao3",true);
				if(tp){
					int n = qsanRandomBounded(6)+5;
					player->peiyin(objectName(),n);
					player->addMark("dongjiao3-Clear");
					room->doAnimate(1,player->objectName(),tp->objectName());
					tp->drawCards(2,objectName());
				}
			}
		}else if(event == TargetSpecified){
			if(player->property("xichangBf").toString()!="dongjiao") return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard")&&player->hasTurn()&&player->getMark("dongjiao2-Clear")<1){
				QStringList ts;
				foreach (const Card*c, player->getHandcards()) {
					if(c->hasTip("mingzhi")){
						if(ts.contains(c->getType())) continue;
						ts.append(c->getType());
					}
				}
				foreach (const Card*c, player->getEquips()) {
					if(ts.contains(c->getType())) continue;
					ts.append(c->getType());
				}
				if(ts.length()<2) return false;
				foreach (ServerPlayer *p, use.to) {
					if(p->isNude()) use.to.removeOne(p);
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,use.to,"dongjiao2","dongjiao2",true);
				if(tp){
					int n = qsanRandomBounded(6)+5;
					player->peiyin(objectName(),n);
					player->addMark("dongjiao2-Clear");
					room->doAnimate(1,player->objectName(),tp->objectName());
					int id = room->askForCardChosen(player,tp,"he",objectName());
					if(id>=0) room->obtainCard(player,id,false);
				}
			}
		}else if(event == HpRecover){
			RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.card&&recover.card->isKindOf("BasicCard")&&recover.who&&recover.who->isAlive()&&recover.who->property("xichangBf").toString()=="dongjiao"
			&&(recover.card->hasFlag("dongjiao1")||(player->hasTurn()&&recover.who->getMark("dongjiao1-Clear")<1))){
				bool has = recover.who->hasEquip();
				foreach (const Card*h, recover.who->getHandcards()) {
					if(h->hasTip("mingzhi")){
						has = true;
						break;
					}
				}
				if(has||recover.card->hasFlag("dongjiao1")){
					room->setCardFlag(recover.card,"dongjiao1");
					int n = qsanRandomBounded(6)+5;
					recover.who->peiyin(objectName(),n);
					recover.who->addMark("dongjiao1-Clear");
					recover.recover++;
					data.setValue(recover);
				}
			}
		}else if(event == ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("BasicCard")&&player->property("xichangBf").toString()=="dongjiao"
			&&(damage.card->hasFlag("dongjiao1")||(player->hasTurn()&&player->getMark("dongjiao1-Clear")<1))){
				bool has = player->hasEquip();
				foreach (const Card*h, player->getHandcards()) {
					if(h->hasTip("mingzhi")){
						has = true;
						break;
					}
				}
				if(has||damage.card->hasFlag("dongjiao1")){
					room->setCardFlag(damage.card,"dongjiao1");
					int n = qsanRandomBounded(6)+5;
					player->peiyin(objectName(),n);
					player->addMark("dongjiao1-Clear");
					player->damageRevises(data,1);
				}
			}
		}else if(event == DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason=="draw_phase"&&player->getMark("guidian1")!=0) {
				draw.num += player->getMark("guidian1");
				data.setValue(draw);
			}
		}else if(event == EventForDiy){
			if(data.toString().contains("mingzhi:")){
				QStringList ids = data.toString().split(":").last().split("+");
				int x = room->getPlayers().length();
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					for (int i = 0; i < ids.length(); i++) {
						p->addMark("mingzhiNum");
						if(p->getMark("mingzhiNum")<=x) continue;
						p->setMark("mingzhiNum",0);
						if(p->getMark("guidianNum")>x||p->property("xichangBf").toString()!="guidian") continue;
						p->addMark("guidianNum");
						int n = qsanRandomBounded(4)+1;
						p->peiyin(objectName(),n);
						QString choice = room->askForChoice(p,"guidian","1+2+3+4");
						if(choice=="1")
							p->addMark("guidian"+choice);
						else if(choice=="2")
							room->addPlayerMark(p,"guidian"+choice);
						else if(choice=="3")
							room->addMaxCards(p,1,false);
						else
							room->setPlayerProperty(p,"hp",p->getHp()+1);
						if(p->isDead()) break;
					}
				}
			}
		}else if(event == EventPhaseStart){
			if (player->getPhase()==Player::Finish){
				bool has = player->hasEquip();
				foreach (const Card*h, player->getHandcards()) {
					if(h->hasTip("mingzhi")){
						has = true;
						break;
					}
				}
				if(has){
					foreach (ServerPlayer *p, room->getAllPlayers()) {
						if(p->property("xichangBf").toString()!="guidian") continue;
						if(p->askForSkillInvoke("guidian",data,false)){
							int n = qsanRandomBounded(4)+1;
							p->peiyin(objectName(),n);
							QString choice = room->askForChoice(p,"guidian","1+2+3+4");
							if(choice=="1")
								p->addMark("guidian"+choice,-1);
							else if(choice=="2")
								room->addPlayerMark(p,"guidian"+choice,-1);
							else if(choice=="3")
								room->addMaxCards(p,-1,false);
							else{
								room->setPlayerProperty(p,"hp",p->getHp()-1);
							}
							if(p->isDead()) continue;
							const TriggerSkill*cq = Sanguosha->getTriggerSkill("caiqiu");
							if(cq) cq->trigger(RoundStart,room,p,data);
						}
					}
				}
			}
		}
		return false;
	}
};

class Shiju : public TriggerSkillV2
{
public:
	Shiju() : TriggerSkillV2("shiju")
	{
		events << CardFinished << CardUsed;
		frequency = Compulsory;
		waked_skills = "kubai,#KubaiBf";
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName())
			&& (event == CardFinished || event == CardUsed)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.owner;
		if (!room || !player || !ctx.original_data) return false;
		QVariant &data = *ctx.original_data;
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			QStringList cs = room->getTag("shijuUseCard").toStringList();
			if(cs.length()>1){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(!p->hasSkill(objectName(),true)) continue;
					foreach (QString m, p->getMarkNames()) {
						if(m.contains("&shiju+"))
							room->setPlayerMark(p,m,0);
					}
					room->setPlayerMark(p,"&shiju+"+cs[1]+"_char+"+cs[2],1);
				}
			}
			if(use.card->hasFlag("shijuBf")&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				if(use.card->hasFlag("shijuBf1"))
					room->obtainCard(player,room->drawCard());
				if(use.card->hasFlag("shijuBf2"))
					room->obtainCard(player,room->drawCard(false));
				if(use.card->hasFlag("shijuBf1")&&use.card->hasFlag("shijuBf2")&&use.card->hasFlag("shijuBf3")){
					if(player->hasSkill("kubai",true)){
						int n = player->property("kubaiUp").toInt()+1;
						room->setPlayerProperty(player,"kubaiUp",n);
						if(n<4) room->changeTranslation(player,"kubai",n);
					}else{
						room->setPlayerProperty(player,"kubaiUp",1);
						room->acquireSkill(player,"kubai");
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				QStringList cs,cs2 = room->getTag("shijuUseCard").toStringList();
				cs << use.card->getType() << use.card->getSuitString() << use.card->objectName();
				room->setTag("shijuUseCard",cs);
				if(cs.length()==cs2.length()){
					for (int i = 0; i < cs.length(); i++) {
						if(cs.at(i)==cs2.at(i)||(i==2&&use.card->sameNameWith(cs2.at(i)))){
							use.card->setFlags("shijuBf");
							use.card->setFlags("shijuBf"+QString::number(i+1));
						}
					}
				}
			}
		}
		return false;
	}
};

class Kubai : public TriggerSkillV2
{
public:
	Kubai() : TriggerSkillV2("kubai")
	{
		events << CardUsed;
		frequency = Compulsory;
		waked_skills = "#KubaiBf,#KubaiLimit";
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == CardUsed && player && player->isAlive() && player->hasSkill(objectName())
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.owner;
		if (!room || !player || !ctx.original_data) return false;
		QVariant &data = *ctx.original_data;
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")){
				if(player->hasSkill(objectName())){
					int n = player->property("kubaiUp").toInt();
					if(n<=1){
						if(player->getMark(use.card->getColorString()+"_kubaiColor-Clear")==1){
							room->sendCompulsoryTriggerLog(player,this,qsanRandomBounded(2)+1);
							player->drawCards(1,objectName());
						}
					}else if(n==2){
						if(player->getMark(use.card->getSuitString()+"_kubaiSuit-Clear")==1){
							room->sendCompulsoryTriggerLog(player,this,qsanRandomBounded(2)+3);
							player->drawCards(1,objectName());
						}
					}else{
						if(player->getMark(use.card->getNumberString()+"_kubaiNumber-Clear")==1){
							room->sendCompulsoryTriggerLog(player,this,qsanRandomBounded(2)+5);
							player->drawCards(1,objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

class KubaiBf : public TriggerSkillV2
{
public:
	KubaiBf() : TriggerSkillV2("#KubaiBf")
	{
		events << CardUsed << EventAcquireSkill;
	}
	int getPriority(TriggerEvent) const
	{
		return 4;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && (event == CardUsed || event == EventAcquireSkill)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.owner;
		if (!room || !player || !ctx.original_data) return false;
		QVariant &data = *ctx.original_data;
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")){
				player->addMark(use.card->getColorString()+"_kubaiColor-Clear");
				player->addMark(use.card->getSuitString()+"_kubaiSuit-Clear");
				player->addMark(use.card->getNumberString()+"_kubaiNumber-Clear");
				if(player->hasSkill("kubai",true)){
					int n = player->property("kubaiUp").toInt();
					QStringList strs;
					foreach (QString m, player->getMarkNames()) {
						if(m.contains("&kubai+"))
							room->setPlayerMark(player,m,0);
						else if(m.contains("_kubai")){
							QStringList ms = m.split("_");
							if(n<=1){
								if(m.contains("_kubaiColor"))
									strs << ms.first();
							}else if(n==2){
								if(m.contains("_kubaiSuit"))
									strs << ms.first()+"_char";
							}else{
								if(m.contains("_kubaiNumber"))
									strs << ms.first();
							}
						}
					}
					room->setPlayerMark(player,"&kubai+"+strs.join("+")+"-Clear",1);
				}
			}
		}else{
			if(player->hasSkill("kubai",true)){
				int n = player->property("kubaiUp").toInt();
				QStringList strs;
				foreach (QString m, player->getMarkNames()) {
					if(m.contains("&kubai+"))
						room->setPlayerMark(player,m,0);
					else if(m.contains("_kubai")){
						QStringList ms = m.split("_");
						if(n<=1){
							if(m.contains("_kubaiColor"))
								strs << ms.first();
						}else if(n==2){
							if(m.contains("_kubaiSuit"))
								strs << ms.first()+"_char";
						}else{
							if(m.contains("_kubaiNumber"))
								strs << ms.first();
						}
					}
				}
				if(strs.length()>0)
					room->setPlayerMark(player,"&kubai+"+strs.join("+")+"-Clear",1);
			}
		}
		return false;
	}
};

class KubaiLimit : public CardLimitSkill
{
public:
	KubaiLimit() : CardLimitSkill("#KubaiLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target, const Card*card) const
	{
		foreach (const Player *p, target->getAliveSiblings()) {
			if(p->hasFlag("CurrentPlayer")&&p->hasSkill("kubai")){
				int n = p->property("kubaiUp").toInt();
				foreach (QString m, p->getMarkNames()) {
					if(m.contains("&kubai+")&&p->getMark(m)>0){
						m.remove("_char");
						m.remove("-Clear");
						QStringList ms = m.split("+");
						if(n>=3){
							if(ms.contains(card->getNumberString()))
								break;
						}else if(n==2){
							if(ms.contains(card->getSuitString()))
								break;
						}else{
							if(ms.contains(card->getColorString()))
								break;
						}
						return card->toString();
					}
				}
			}
		}
		return "";
	}
};

/*static double calculate_pi(int num_terms) {
    double pi = 0.0;
    for (int k = 0; k < num_terms; k++) {
        pi += (1.0 / pow(16, k)) * 
			(4.0 / (8 * k + 1) - 
			2.0 / (8 * k + 4) - 
			1.0 / (8 * k + 5) - 
			1.0 / (8 * k + 6));
    }// Even this pi calculation overwhelms the game engine.
    return pi;
}*/
static QString sxstr = "1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989";

class MobileGeyuan : public TriggerSkillV2
{
public:
	int committedDrawCount(const SkillContext &ctx) const
	{
		const SkillInstanceRef ref = getUsageRef(ctx);
		ServerPlayer *holder = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
		return holder && ref.isValid() ? qMin(3, holder->getMark(SkillInstanceUtils::formatUsageMarkKey(ref.key.skillName, ref.key.instanceID, "-Clear"))) : 0;
	}
	MobileGeyuan() : TriggerSkillV2("mobilegeyuan") { events << CardUsed << EventSkillInvoking; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Turn; }
	int getMaxUsageLimit(const SkillContext &) const override { return INT_MAX; }
	static int position(const Player *player, int instance)
	{
		return player->getSkillInstanceStateValue("mobilegeyuan", instance, "position").toInt();
	}
	static int digit(const Player *player, int instance)
	{
		const int index = position(player, instance);
		return index >= 0 && index < sxstr.size() ? sxstr.at(index).digitValue() : -1;
	}
	static void advance(Room *room, ServerPlayer *player, int instance)
	{
		player->setSkillInstanceStateValue("mobilegeyuan", instance, "position", position(player, instance) + 1);
		// The shared mark is a projection of all source instances, never the cursor authority.
		for (const QString &mark : player->getMarkNames())
			if (mark.startsWith("&mobilegeyuan+:+")) room->setPlayerMark(player, mark, 0);
		for (int id : player->getSkillInstanceIds("mobilegeyuan")) {
			const int value = digit(player, id);
			if (value >= 0) room->setPlayerMark(player, QString("&mobilegeyuan+:+%1").arg(value), 1);
		}
	}
	bool matches(const SkillContext &ctx) const
	{
		const Card *card = ctx.original_data ? ctx.original_data->value<CardUseStruct>().card : nullptr;
		if (!card || card->getTypeId() == Card::TypeSkill || card->getNumber() < 1) return false;
		const int value = digit(ctx.owner, ctx.sourceRef.key.instanceID);
		return card->getNumber() == value || (value == 0 && card->getNumber() > 9 && ctx.owner->hasSkill("chongcha"));
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		const Card *card = data.value<CardUseStruct>().card;
		return player && player->isAlive() && player->hasSkill(objectName()) && card && card->getTypeId() != Card::TypeSkill
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!matches(ctx) || !player->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
		ctx.targets = {player};
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!matches(ctx) || !isUsable(ctx)) return false;
		addUsage(ctx);
		ctx.extra_data = committedDrawCount(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		const int count = ctx.extra_data.isValid() ? ctx.extra_data.toInt() : committedDrawCount(ctx);
		// Advance before drawing: a nested use observes the next digit of this exact source.
		advance(room, player, ctx.sourceRef.key.instanceID);
		if (count > 0) target->drawCards(count * getEffectiveAmount(ctx), objectName());
		return false;
	}
};

ChongchaCard::ChongchaCard()
{
	setSkillName("chongcha");
	target_fixed = true;
}
void ChongchaCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class Chongcha : public ViewAsSkillV2
{
public:
	Chongcha() : ViewAsSkillV2("chongcha", 1) { waked_skills = "#ChongchaLimit"; setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	int getMaxUsageLimit(const SkillContext &) const override { return 1; }
	static QStringList sources(const Player *player)
	{
		QStringList result;
		for (int id : player->getSkillInstanceIds("mobilegeyuan"))
			if (MobileGeyuan::digit(player, id) >= 0) result << SkillInstanceUtils::formatName("mobilegeyuan", id);
		return result;
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play && request.initiator->canDiscard("he") && !sources(request.initiator).isEmpty();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty() && !request.initiator->isJilei(card)
			&& (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		ChongchaCard *card = new ChongchaCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "ChongchaCard"; }
	bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		const QStringList choices = sources(ctx.owner);
		if (choices.isEmpty()) return false;
		const QString chosen = choices.size() == 1 ? choices.first() : room->askForChoice(ctx.owner, objectName(), choices.join("+"));
		if (!choices.contains(chosen)) return false;
		ctx.extra_data = SkillInstanceUtils::parseInstanceId(chosen);
		return true;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request) || !ctx.owner->hasSkillInstance("mobilegeyuan", ctx.extra_data.toInt())) return false;
		const int id = request.selectedCardIds.first();
		if (room->getCardOwner(id) != ctx.owner || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
		room->throwCard(id, objectName(), ctx.owner, ctx.owner);
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		return skillEffect(ctx, ctx.owner);
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		const int id = ctx.extra_data.toInt();
		if (target->isAlive() && target->hasSkillInstance("mobilegeyuan", id))
			MobileGeyuan::advance(target->getRoom(), target, id);
		return ContinueEffects;
	}
};
class ChongchaLimit : public CardLimitSkill
{
public:
	ChongchaLimit() : CardLimitSkill("#ChongchaLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "ignore";
	}

	QString limitPattern(const Player *target, const Card*card) const
	{
		if(card->getNumber()>9&&target->hasSkill("chongcha")){
			return card->toString();
		}
		return "";
	}
};

class Shouyue : public TriggerSkillV2
{
public:
	Shouyue() : TriggerSkillV2("shouyue") { events << EventPhaseStart << HpChanged; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		const bool lostHp = data.canConvert<DamageStruct>() || data.canConvert<HpLostStruct>();
		if (event == EventPhaseStart ? player->getPhase() != Player::Draw : !lostHp) return {};
		return TriggerList{{player, {objectName()}}};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName() + "$-1", "shouyue0", true, true);
		if (!target) return false;
		const bool restore = (target->isChained() || !target->faceUp())
			&& room->askForChoice(player, objectName(), "shouyue1+shouyue2") == "shouyue2";
		ctx.extra_data = QVariantMap{{"target", target->objectName()}, {"restore", restore}};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		QVariantMap choice = ctx.extra_data.toMap();
		ServerPlayer *target = room->findPlayerByObjectName(choice.value("target").toString());
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		if (!choice.value("restore").toBool()) {
			choice.insert("step", "draw");
			ctx.extra_data = choice;
			skillEffect(event, room, player, ctx, player);
		}
		ctx.modified_amount = amount;
		ctx.modified_amount_set = modified;
		choice.insert("step", "recipient");
		ctx.extra_data = choice;
		if (target && target->isAlive()) skillEffect(event, room, player, ctx, target);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target->isAlive()) return false;
		const QVariantMap choice = ctx.extra_data.toMap();
		if (choice.value("step").toString() == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
		else if (choice.value("restore").toBool()) {
			// Restoring the general removes chaining; it must not chain an upright target.
			if (target->isChained()) room->setPlayerChained(target, false);
			if (target->isAlive() && !target->faceUp()) target->turnOver();
		} else if (target->hasSkill("qinyin", true)) target->drawCards(getEffectiveAmount(ctx), objectName());
		else room->acquireSkill(target, "qinyin");
		return false;
	}
};

DieyinCard::DieyinCard()
{
	setSkillName("dieyin");
	target_fixed = true;
}

void DieyinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class Dieyin : public ViewAsSkillV2
{
public:
	Dieyin() : ViewAsSkillV2("dieyin") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	int getMaxUsageLimit(const SkillContext &) const override { return 1; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play && request.initiator->faceUp();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	TargetMode targetMode() const override { return NoTarget; }
	QString historyKey(const ActiveSkillRequest &) const override { return "DieyinCard"; }
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		return ctx.owner && ctx.owner->isAlive() && ctx.owner->faceUp();
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		return skillEffect(ctx, ctx.owner);
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target->isAlive() || !target->faceUp()) return ContinueEffects;
		target->turnOver();
		if (!target->isAlive()) return ContinueEffects;
		QStringList choices;
		for (int i = 1; i < 7; ++i) choices << QString("phase%1").arg(i);
		const QString choice = target->getRoom()->askForChoice(target, objectName(), choices.join("+"));
		if (!choices.contains(choice)) return ContinueEffects;
		// insertPhase schedules after the current Play phase, retaining its quota boundary.
		target->insertPhase(static_cast<Player::Phase>(choice.mid(5).toInt()));
		return ContinueEffects;
	}
};
JinchuCard::JinchuCard()
{
	setSkillName("jinchu");
	will_throw = false;
	handling_method = Card::MethodNone;
}
bool JinchuCard::targetFilter(const QList<const Player *> &targets, const Player *candidate, const Player *self) const
{
	return ActiveSkillCard::targetFilter(targets, candidate, self);
}
void JinchuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class Jinchuvs : public ViewAsSkillV2
{
public:
	Jinchuvs() : ViewAsSkillV2("jinchu", 1) { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	int getMaxUsageLimit(const SkillContext &ctx) const override
	{
		const SkillInstanceRef ref = getUsageRef(ctx);
		return 1 + (ctx.owner ? ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_uses").toInt() : 0);
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play && !request.initiator->isKongcheng();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty()
			&& request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.selectedCardIds.size() == 1
			&& request.initiator->handCards().contains(request.selectedCardIds.first());
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		JinchuCard *card = new JinchuCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
	{
		return targets.isEmpty() && candidate && candidate != request.initiator && candidate->isAlive() && candidate->getHandcardNum() > 1;
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "JinchuCard"; }
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return false;
		const int id = request.selectedCardIds.first();
		if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand) return false;
		ctx.extra_data = QVariantMap{{"card", id}, {"step", "compare"}};
		return true;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		ServerPlayer *source = ctx.owner;
		QVariantMap saved = ctx.extra_data.toMap();
		const int id = saved.value("card").toInt();
		if (saved.value("step").toString() == "discard_self") {
			if (!target->isAlive() || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
				|| !target->canDiscard(id)) return ContinueEffects;
			room->throwCard(id, target, target);
			// Only the instance which actually discarded its revealed card gains another use.
			if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) {
				const SkillInstanceRef ref = getUsageRef(ctx);
				const int extra = target->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_uses").toInt();
				target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_uses", extra + 1);
			}
			return ContinueEffects;
		}
		if (!source->isAlive() || !target->isAlive() || target->getHandcardNum() < 2
			|| room->getCardOwner(id) != source || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
		const Card *selection = room->askForExchange(target, objectName(), 2, 2, false, "jinchu0");
		if (!selection || selection->getSubcards().size() != 2) return ContinueEffects;
		const QList<int> shown = selection->getSubcards();
		for (int other : shown)
			if (room->getCardOwner(other) != target || room->getCardPlace(other) != Player::PlaceHand) return ContinueEffects;
		room->showCard(source, id);
		room->showCard(target, shown);
		const int number = Sanguosha->getCard(id)->getNumber();
		int minimum = number, maximum = number;
		for (int other : shown) {
			minimum = qMin(minimum, Sanguosha->getCard(other)->getNumber());
			maximum = qMax(maximum, Sanguosha->getCard(other)->getNumber());
		}
		if (number == minimum || number == maximum) {
			QList<int> discard;
			for (int other : shown)
				if (room->getCardOwner(other) == target && room->getCardPlace(other) == Player::PlaceHand && source->canDiscard(target, other)) discard << other;
			if (!discard.isEmpty()) room->throwCard(discard, objectName(), target, source);
			const int amount = ctx.modified_amount;
			const bool modified = ctx.modified_amount_set;
			saved.insert("step", "discard_self");
			ctx.extra_data = saved;
			skillEffect(ctx, source);
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
		} else {
			QVariantList receipts = source->getTag("mobile_jinchu_pending").toList();
			for (int i = receipts.size() - 1; i >= 0; --i) {
				const QVariantMap old = receipts.at(i).toMap();
				if (old.value("instance").toInt() == ctx.sourceRef.key.instanceID && old.value("target").toString() == target->objectName()) receipts.removeAt(i);
			}
			receipts << QVariantMap{{"owner", source->objectName()}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"target", target->objectName()}, {"amount", getEffectiveAmount(ctx)}};
			source->setTag("mobile_jinchu_pending", receipts);
			room->setPlayerMark(target, "&jinchu+#" + source->objectName() + "-Clear", 1);
		}
		return ContinueEffects;
	}
};

class Jinchu : public TriggerSkillV2
{
public:
	Jinchu() : TriggerSkillV2("jinchu")
	{
		events << CardUsed << ConfirmDamage << CardFinished << EventPhaseChanging;
		view_as_skill = new Jinchuvs;
		global = true;
		frequency = Compulsory;
	}
	static qint64 useId(Room *room) { return room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong(); }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging && player) {
			const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from == Player::Play)
				for (int id : player->getSkillInstanceIds(objectName())) player->removeSkillInstanceStateValue(objectName(), id, "extra_uses");
			if (change.to == Player::NotActive)
				for (ServerPlayer *p : room->getAllPlayers(true)) p->removeTag("mobile_jinchu_pending");
		} else if (event == CardFinished) {
			const Card *card = data.value<CardUseStruct>().card;
			if (card) {
				QVariantList keep;
				for (const QVariant &entry : card->getTag("mobile_jinchu_used").toList())
					if (entry.toMap().value("use").toLongLong() != useId(room)) keep << entry;
				card->setTag("mobile_jinchu_used", keep);
			}
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != CardUsed && event != ConfirmDamage) return true;
		const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<DamageStruct>().card;
		if (!card || !card->isKindOf("Slash") || useId(room) <= 0) return true;
		const QVariantList receipts = event == CardUsed && player ? player->getTag("mobile_jinchu_pending").toList() : card->getTag("mobile_jinchu_used").toList();
		for (const QVariant &value : receipts) {
			const QVariantMap receipt = value.toMap();
			if (event == ConfirmDamage && receipt.value("use").toLongLong() != useId(room)) continue;
			ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString());
			if (!target || !target->isAlive() || (event == ConfirmDamage && data.value<DamageStruct>().to != target)) continue;
			SkillContext ctx;
			ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
			if (!ctx.owner) continue;
			ctx.skill_name = objectName();
			ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.amount = receipt.value("amount").toInt();
			ctx.current_event = event;
			ctx.original_data = &data;
			ctx.extra_data = QVariantMap{{"receipt", receipt}, {"used", event == CardUsed}};
			ctx.targets = {target};
			contexts << ctx;
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.targets.value(0); }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		const QVariantMap saved = ctx.extra_data.toMap();
		if (!ctx.owner || !ctx.original_data) return false;
		if (saved.value("used").toBool()) return ctx.owner->getTag("mobile_jinchu_pending").toList().contains(saved.value("receipt"));
		const Card *card = ctx.original_data->value<DamageStruct>().card;
		return card && card->getTag("mobile_jinchu_used").toList().contains(saved.value("receipt"));
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (ctx.extra_data.toMap().value("used").toBool()) {
			QVariantList pending = ctx.owner->getTag("mobile_jinchu_pending").toList();
			pending.removeOne(ctx.extra_data.toMap().value("receipt"));
			ctx.owner->setTag("mobile_jinchu_pending", pending);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.extra_data.toMap().value("used").toBool()) {
			CardUseStruct use = ctx.original_data->value<CardUseStruct>();
			if (!use.card) return false;
			if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
			ctx.original_data->setValue(use);
			QVariantMap receipt = ctx.extra_data.toMap().value("receipt").toMap();
			receipt.insert("use", useId(room));
			receipt.insert("amount", getEffectiveAmount(ctx));
			QVariantList receipts = use.card->getTag("mobile_jinchu_used").toList();
			receipts << receipt;
			use.card->setTag("mobile_jinchu_used", receipts);
			room->setPlayerMark(target, "&jinchu+#" + ctx.owner->objectName() + "-Clear", 0);
		} else {
			DamageStruct damage = ctx.original_data->value<DamageStruct>();
			damage.damage += getEffectiveAmount(ctx);
			ctx.original_data->setValue(damage);
		}
		return false;
	}
};
class MobileAnxianVS : public ViewAsSkillV2
{
public:
	MobileAnxianVS() : ViewAsSkillV2("mobileanxian") { response_pattern = "@@mobileanxian"; }
	static int material(const ActiveSkillRequest &request)
	{
		return request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
			request.activationRef.key.instanceID, "slash_card", -1).toInt();
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& request.pattern == "@@mobileanxian" && request.initiator->handCards().contains(material(request));
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!canActivate(request)) return nullptr;
		const Card *physical = Sanguosha->getCard(material(request));
		if (!physical->isKindOf("Slash")) return nullptr;
		Card *slash = Sanguosha->cloneCard(physical->objectName(), physical->getSuit(), physical->getNumber());
		slash->addSubcard(physical->getEffectiveId());
		slash->setSkillName(objectName());
		return slash;
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
	bool willThrowSelectedCards() const override { return false; }
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		const int id = material(request);
		return id >= 0 && room->isAcceptedViewAsEffect(ctx.activationRef) && room->getCardOwner(id) == ctx.owner
			&& room->getCardPlace(id) == Player::PlaceHand && ctx.use_card && ctx.use_card->getSubcards() == QList<int>{id};
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		if (ctx.use_card) ctx.use_card->setTag("mobile_anxian_receipt", QVariantMap{
			{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
			{"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
			{"activation_instance", ctx.activationRef.key.instanceID}, {"amount", 2 * getEffectiveAmount(ctx)}});
		return ContinueEffects;
	}
};

class MobileAnxian : public TriggerSkillV2
{
public:
	MobileAnxian() : TriggerSkillV2("mobileanxian") { events << CardsMoveOneTime; view_as_skill = new MobileAnxianVS; }
	static QList<int> candidates(Room *room, ServerPlayer *player, const CardsMoveOneTimeStruct &move)
	{
		if (move.from != player || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
			|| !move.from_places.contains(Player::PlaceHand)) return {};
		const QVariantMap current = room->historyParent(room->currentHistoryEventId(), "move_cards", true);
		const qint64 currentId = current.value("id").toLongLong();
		if (currentId <= 0) { qWarning("MobileAnxian: missing move history"); return {}; }
		QVariantMap query{{"turn_id", current.value("turn_id")}, {"from", player->objectName()}};
		qint64 first = 0;
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) { qWarning("MobileAnxian: incomplete move history"); return {}; }
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap(), moved = fact.value("data").toMap();
				if (moved.value("from_place").toInt() == Player::PlaceHand
					&& (moved.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
					first = fact.value("event_id").toLongLong();
					break;
				}
			}
			if (first || !page.value("has_more").toBool()) break;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
		if (first != currentId) return {};
		QList<int> result;
		for (int i = 0; i < move.card_ids.size(); ++i) {
			const int id = move.card_ids.at(i);
			if (move.from_places.value(i) == Player::PlaceHand && Sanguosha->getCard(id)->isKindOf("Slash")
				&& room->getCardPlace(id) == Player::DiscardPile) result << id;
		}
		return result;
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName())
			&& !candidates(room, player, data.value<CardsMoveOneTimeStruct>()).isEmpty()
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const QList<int> ids = candidates(room, player, ctx.original_data->value<CardsMoveOneTimeStruct>());
		if (ids.isEmpty()) return false;
		room->fillAG(ids, player);
		try {
			if (!player->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) { room->clearAG(player); return false; }
			const int id = room->askForAG(player, ids, false, objectName());
			room->clearAG(player);
			if (!ids.contains(id)) return false;
			ctx.extra_data = id;
			ctx.targets = {player};
			return true;
		} catch (...) { room->clearAG(player); throw; }
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const int id = ctx.extra_data.toInt();
		if (room->getCardPlace(id) != Player::DiscardPile) return false;
		// Freeze the accepted continuation before obtaining can remove the original grant.
		Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), ctx);
		if (!prompt.isValid()) return false;
		const SkillInstanceRef ref = prompt.activationRef();
		target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "slash_card", id);
		room->obtainCard(target, id);
		if (target->isAlive() && target->handCards().contains(id))
			room->askForUseCard(target, "@@mobileanxian", "mobileanxian0", -1, Card::MethodUse, false);
		return false;
	}
};

class MobileAnxianEffect : public TriggerSkillV2
{
public:
	MobileAnxianEffect() : TriggerSkillV2("#mobileanxian-effect") { events << CardUsed << Damage << CardFinished; global = true; frequency = Compulsory; }
	static qint64 useId(Room *room) { return room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong(); }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event == Damage) return true;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card) return true;
		QVariantMap receipt = use.card->getTag("mobile_anxian_receipt").toMap();
		if (receipt.isEmpty()) return true;
		if (event == CardUsed && use.sourceRef.isValid()
			&& use.activationRef == SkillInstanceRef(receipt.value("activation_owner").toString(),
				SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()))) {
			receipt.insert("use", useId(room));
			use.card->setTag("mobile_anxian_receipt", receipt);
		} else if (event == CardFinished && receipt.value("use").toLongLong() == useId(room))
			use.card->removeTag("mobile_anxian_receipt");
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != Damage) return true;
		const DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || !player || !player->isAlive()) return true;
		const QVariantMap receipt = damage.card->getTag("mobile_anxian_receipt").toMap();
		if (receipt.isEmpty() || receipt.value("use").toLongLong() <= 0 || receipt.value("use").toLongLong() != useId(room)) return true;
		SkillContext ctx;
		ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
		if (!ctx.owner || ctx.owner != player) return true;
		ctx.skill_name = objectName();
		ctx.invoker = ctx.initiator = player;
		ctx.sourceRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
		ctx.instanceID = ctx.sourceRef.key.instanceID;
		ctx.amount = receipt.value("amount").toInt();
		ctx.current_event = event;
		ctx.original_data = &data;
		ctx.extra_data = receipt;
		ctx.targets = {player};
		contexts << ctx;
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		const Card *card = ctx.original_data ? ctx.original_data->value<DamageStruct>().card : nullptr;
		return ctx.owner && ctx.owner->isAlive() && card && card->getTag("mobile_anxian_receipt") == ctx.extra_data;
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		target->drawCards(getEffectiveAmount(ctx), "mobileanxian");
		return false;
	}
};
class MobileAnxianMod : public TargetModSkillV2
{
public:
	MobileAnxianMod() : TargetModSkillV2("#mobileanxian-mod") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.card || ctx.card->getSkillName() != "mobileanxian") return CorrectSkillResult::noEffect();
		if (ctx.modType == Residue) return CorrectSkillResult::unlimitedResidue();
		if (ctx.modType == DistanceLimit) return CorrectSkillResult::useAmount(1000 * ctx.currentAmount);
		return CorrectSkillResult::noEffect();
	}
};
class Dingfa : public TriggerSkillV2
{
public:
	Dingfa() : TriggerSkillV2("dingfa") { events << RoundStart << RoundEnd; global = true; }
	static int score(Room *room, ServerPlayer *player, const QString &law, const QVariant &round)
	{
		if (law == "dingfa4") return player->getCardCount(true, true);
		if (law == "dingfa3") {
			const QVariantMap first = room->queryHistoryFacts({{"kind", "player_state"}, {"to", player->objectName()}, {"limit", 1}});
			const QVariantList items = first.value("items").toList();
			if (!first.value("complete").toBool() || items.isEmpty()
				|| items.first().toMap().value("data").toMap().value("boundary").toString() != "baseline") return -1;
		}
		QVariantMap query{{"round_id", round}};
		query.insert(law == "dingfa1" ? "from" : "to", player->objectName());
		if (law == "dingfa3") query.insert("kind", "player_state");
		int result = 0;
		for (;;) {
			const QVariantMap page = law == "dingfa1" ? room->queryActualDamage(query)
				: law == "dingfa2" ? room->queryHistoryMoves(query) : room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap().value("data").toMap();
				if (law == "dingfa1") result += fact.value("amount").toInt();
				else if (law == "dingfa2") {
					if (fact.value("to_place").toInt() == Player::PlaceHand && fact.value("reason_skill").toString() != "InitialHandCards") ++result;
				} else if (fact.value("boundary").toString() == "commit") {
					if (!fact.contains("hp_before") || !fact.contains("hp_after")) return -1;
					if (fact.value("hp_before").toInt() != fact.value("hp_after").toInt()) ++result;
				}
			}
			if (!page.value("has_more").toBool()) return result;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (event == RoundStart && player) player->removeTag("mobile_dingfa_laws");
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == RoundStart && player && player->isAlive() && player->hasSkill(objectName())
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != RoundEnd) return false;
		if (!player || !player->isAlive()) return true;
		for (const QVariant &entry : player->getTag("mobile_dingfa_laws").toList()) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("round") != room->historyScopes().value("round_id")) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.amount = receipt.value("amount").toInt();
			ctx.is_forced = true;
			ctx.current_event = event;
			ctx.original_data = &data;
			ctx.extra_data = QVariantMap{{"receipt", receipt}};
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.extra_data.toMap().contains("receipt"))
			return ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("mobile_dingfa_laws").toList().contains(ctx.extra_data.toMap().value("receipt"));
		return TriggerSkillV2::isSourceAvailable(room, ctx);
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == RoundEnd) return true;
		if (!player->askForSkillInvoke(objectName() + "$-1")) return false;
		QStringList choices{"dingfa1", "dingfa2", "dingfa3", "dingfa4"}, chosen;
		for (int i = 0; i < 2; ++i) {
			const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (!choices.removeOne(choice)) return false;
			chosen << choice;
		}
		ctx.extra_data = QVariantMap{{"choices", chosen}, {"votes", QStringList()}};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int effectAmount = getEffectiveAmount(ctx);
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		if (event == RoundStart) {
			const QList<ServerPlayer *> voters = room->getAlivePlayers();
			for (ServerPlayer *voter : voters) if (voter->isAlive() && voter->faceUp()) {
				ctx.modified_amount = amount;
				ctx.modified_amount_set = modified;
				skillEffect(event, room, player, ctx, voter);
			}
			const QStringList votes = ctx.extra_data.toMap().value("votes").toStringList();
			if (votes.isEmpty()) return false;
			QVariantList receipts = player->getTag("mobile_dingfa_laws").toList();
			receipts << QVariantMap{{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
				{"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
				{"activation_instance", ctx.activationRef.key.instanceID}, {"amount", effectAmount}, {"votes", votes},
				{"round", room->historyScopes().value("round_id")}};
			player->setTag("mobile_dingfa_laws", receipts);
			return false;
		}
		const QVariantMap receipt = ctx.extra_data.toMap().value("receipt").toMap();
		QVariantList receipts = player->getTag("mobile_dingfa_laws").toList();
		receipts.removeOne(receipt);
		player->setTag("mobile_dingfa_laws", receipts);
		QMap<QString, int> counts;
		int most = 0;
		for (const QString &vote : receipt.value("votes").toStringList()) most = qMax(most, ++counts[vote]);
		for (const QString &law : QStringList{"dingfa1", "dingfa2", "dingfa3", "dingfa4"}) {
			if (most == 0 || counts.value(law) != most) continue;
			QList<ServerPlayer *> targets;
			int greatest = -1;
			bool complete = true;
			for (ServerPlayer *candidate : room->getAlivePlayers()) {
				const int value = score(room, candidate, law, receipt.value("round"));
				if (value < 0) { complete = false; break; }
				if (value > greatest) { greatest = value; targets.clear(); }
				if (value == greatest) targets << candidate;
			}
			if (!complete) { qWarning("Dingfa: incomplete round score history"); continue; }
			for (ServerPlayer *target : targets) if (target->isAlive()) {
				if (law == "dingfa4") {
					for (ServerPlayer *recipient : room->getOtherPlayers(target)) {
						if (!target->isAlive()) break;
						ctx.modified_amount = amount;
						ctx.modified_amount_set = modified;
						ctx.extra_data = QVariantMap{{"law", law}, {"donor", target->objectName()}};
						skillEffect(event, room, player, ctx, recipient);
					}
				} else {
					ctx.modified_amount = amount;
					ctx.modified_amount_set = modified;
					ctx.extra_data = QVariantMap{{"law", law}};
					skillEffect(event, room, player, ctx, target);
				}
			}
		}
		return false;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		if (event == RoundStart) {
			ServerPlayer *chooser = target;
			QVariantList keep;
			for (const QVariant &entry : target->getTag("mobile_shuxing_votes").toList())
				if (entry.toMap().value("owner").toString() == player->objectName()) chooser = player;
				else keep << entry;
			target->setTag("mobile_shuxing_votes", keep);
			const QStringList choices = saved.value("choices").toStringList();
			const QString vote = room->askForChoice(chooser, objectName(), choices.join("+"), QVariant::fromValue(player));
			QStringList votes = saved.value("votes").toStringList();
			if (choices.contains(vote)) votes << vote;
			saved.insert("votes", votes);
			ctx.extra_data = saved;
			return false;
		}
		const QString law = saved.value("law").toString();
		if (law == "dingfa1") room->damage(DamageStruct(objectName(), nullptr, target, 2 * getEffectiveAmount(ctx)));
		else if (law == "dingfa2") room->addMaxCards(target, -2 * getEffectiveAmount(ctx), false);
		else if (law == "dingfa3") room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
		else {
			ServerPlayer *donor = room->findPlayerByObjectName(saved.value("donor").toString());
			if (!donor || !donor->isAlive() || donor->isNude()) return false;
			const int count = qMin(donor->getCardCount(), getEffectiveAmount(ctx));
			if (count <= 0) return false;
			const Card *gift = room->askForExchange(donor, objectName(), count, count, true, "dingfa40:" + target->objectName());
			if (gift && target->isAlive()) room->giveCard(donor, target, gift, objectName());
		}
		return false;
	}
};
class Shuxing : public TriggerSkillV2
{
public:
	Shuxing() : TriggerSkillV2("shuxing") { events << TargetConfirmed << EventSkillInvoking; }
	LimitScope getLimitScope() const override { return Limit_Custom; }
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.invoker) return false;
		const QVariant turn = ctx.owner->getRoom()->historyScopes().value("turn_id");
		if (turn.toLongLong() <= 0) return false;
		const QVariantMap used = ctx.owner->getSkillInstanceStateValue(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID, "used_targets").toMap();
		return used.value("turn") != turn || !used.value("targets").toStringList().contains(ctx.invoker->objectName());
	}
	void addUsage(const SkillContext &ctx) const override
	{
		const QVariant turn = ctx.owner->getRoom()->historyScopes().value("turn_id");
		QVariantMap used = ctx.owner->getSkillInstanceStateValue(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID, "used_targets").toMap();
		QStringList targets = used.value("turn") == turn ? used.value("targets").toStringList() : QStringList();
		if (!targets.contains(ctx.invoker->objectName())) targets << ctx.invoker->objectName();
		ctx.owner->setSkillInstanceStateValue(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID,
			"used_targets", QVariantMap{{"turn", turn}, {"targets", targets}});
	}
	void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		if (event != TargetConfirmed || !player || !player->isAlive()) return result;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash") || !use.to.contains(player)) return result;
		for (ServerPlayer *owner : room->getOtherPlayers(player))
			if (owner->hasSkill(objectName()) && owner->hasFlag("CurrentPlayer")) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!checkCustomUsage(ctx) || !player->askForSkillInvoke(objectName() + "$-1", ctx.invoker)) return false;
		ctx.targets = {ctx.invoker};
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!checkCustomUsage(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
		ctx.original_data->setValue(use);
		room->showAllCards(target);
		QList<int> ids;
		for (const Card *card : target->getHandcards()) if (card->isKindOf("Jink")) ids << card->getEffectiveId();
		if (ids.isEmpty() || !target->isAlive()) return false;
		if (player->isAlive() && target->askForSkillInvoke("shuxing0", "0:" + player->objectName(), false)) {
			// This is an applied right over the next vote, not a live grant or a name-wide quota.
			QVariantList receipts = target->getTag("mobile_shuxing_votes").toList();
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
				{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
			target->setTag("mobile_shuxing_votes", receipts);
			QList<int> present;
			for (int id : ids) if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand) present << id;
			if (!present.isEmpty()) { DummyCard gift(present); room->giveCard(target, player, &gift, objectName(), true); }
		} else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), player));
		return false;
	}
};
mobilePackage::mobilePackage()
	: Package("mobile")
{
	General *mobile_zhouyu = new General(this, "mobile_zhouyu", "wu", 3);
	mobile_zhouyu->addSkill(new Shouyue);
	mobile_zhouyu->addSkill(new Dieyin);
	addMetaObject<DieyinCard>();

	General *mobile_caoxing = new General(this, "mobile_caoxing", "qun", 4);
	mobile_caoxing->addSkill(new Jinchu);
	mobile_caoxing->addSkill(new MobileAnxian);
	mobile_caoxing->addSkill(new MobileAnxianEffect);
	mobile_caoxing->addSkill(new MobileAnxianMod);
	related_skills.insert("mobileanxian", "#mobileanxian-effect");
	related_skills.insert("mobileanxian", "#mobileanxian-mod");
	addMetaObject<JinchuCard>();




	General *mobile_sunru = new General(this, "mobile_sunru", "wu", 3, false);
	mobile_sunru->addSkill(new Yingjian);
	mobile_sunru->addSkill(new SlashNoDistanceLimitSkill("yingjian"));
	mobile_sunru->addSkill("shixin");
	related_skills.insert("yingjian", "#yingjian-slash-ndl");

	General *liuzan = new General(this, "liuzan", "wu");
	liuzan->addSkill(new Fenyin);

	General *caochun = new General(this, "caochun", "wei", 4);
	caochun->addSkill(new Shanjia("shanjia"));

	skills << new Shanjia("olshanjia") << new SlashNoDistanceLimitSkill("olshanjia");
	addMetaObject<ShanjiaCard>();
	addMetaObject<OLShanjiaCard>();

	General *pangdegong = new General(this, "pangdegong", "qun", 3);
	pangdegong->addSkill(new Pingcai);
	pangdegong->addSkill(new Yinshiy);
	pangdegong->addSkill(new YinshiyPro);
	related_skills.insert("yinshiy", "#yinshiy-pro");
	addMetaObject<PingcaiCard>();

	General *simashi = new General(this, "simashi", "wei", 4);
	simashi->addSkill(new Jinglve);
	simashi->addSkill(new Baiyi);
	simashi->addSkill(new Shanli);
	addMetaObject<BaiyiCard>();
	addMetaObject<JinglveCard>();

	General *mobile_miheng = new General(this, "mobile_miheng", "qun", 3);
	mobile_miheng->addSkill(new MobileKuangcai);
	mobile_miheng->addSkill(new MobileKuangcaiMod);
	mobile_miheng->addSkill(new MobileShejian);

	General *shichangshi = new General(this, "shichangshi", "qun", 1);
	shichangshi->setGender(General::Sexless);
	shichangshi->addSkill(new Danggu);
	shichangshi->addSkill(new Mowang);

	General *cs_zhangrang = new General(this, "cs_zhangrang", "qun", 0, true, true);
	cs_zhangrang->setGender(General::Sexless);
	cs_zhangrang->addSkill(new CsTaoluan);

	General *cs_zhaozhong = new General(this, "cs_zhaozhong", "qun", 0, true, true);
	cs_zhaozhong->setGender(General::Sexless);
	cs_zhaozhong->addSkill(new CsChiyan);

	General *cs_sunzhang = new General(this, "cs_sunzhang", "qun", 0, true, true);
	cs_sunzhang->setGender(General::Sexless);
	cs_sunzhang->addSkill(new CsZimou);

	General *cs_bilan = new General(this, "cs_bilan", "qun", 0, true, true);
	cs_bilan->setGender(General::Sexless);
	cs_bilan->addSkill(new CsPicai);
	addMetaObject<CsPicaiCard>();

	General *cs_xiayun = new General(this, "cs_xiayun", "qun", 0, true, true);
	cs_xiayun->setGender(General::Sexless);
	cs_xiayun->addSkill(new CsYaozhuo);
	addMetaObject<CsYaozhuoCard>();

	General *cs_hanli = new General(this, "cs_hanli", "qun", 0, true, true);
	cs_hanli->setGender(General::Sexless);
	cs_hanli->addSkill(new CsXiaolu);
	addMetaObject<CsXiaoluCard>();
	addMetaObject<CsXiaolu2Card>();

	General *cs_lisong = new General(this, "cs_lisong", "qun", 0, true, true);
	cs_lisong->setGender(General::Sexless);
	cs_lisong->addSkill(new CsKuiji);
	addMetaObject<CsKuijiCard>();
	addMetaObject<CsKuijiDisCard>();

	General *cs_duangui = new General(this, "cs_duangui", "qun", 0, true, true);
	cs_duangui->setGender(General::Sexless);
	cs_duangui->addSkill(new CsChihe);
	cs_duangui->addSkill(new CsChiheEffect);
	cs_duangui->addSkill(new CsChiheLimit);

	General *cs_guosheng = new General(this, "cs_guosheng", "qun", 0, true, true);
	cs_guosheng->setGender(General::Sexless);
	cs_guosheng->addSkill(new CsNiqu);
	addMetaObject<CsNiquCard>();

	General *cs_gaowang = new General(this, "cs_gaowang", "qun", 0, true, true);
	cs_gaowang->setGender(General::Sexless);
	cs_gaowang->addSkill(new CsMiaoyu);

	General *mobileyou_zhugeliang = new General(this, "mobileyou_zhugeliang", "qun", 3);
	mobileyou_zhugeliang->addSkill(new Yance);
	mobileyou_zhugeliang->addSkill(new Fangqiu);
	mobileyou_zhugeliang->addSkill(new ZGGongli);

	General *mobileyou_pangtong = new General(this, "mobileyou_pangtong", "qun", 3);
	mobileyou_pangtong->addSkill(new MobileManjuan);
	mobileyou_pangtong->addSkill(new Yangming);
	mobileyou_pangtong->addSkill(new MobileActiveQuota("yangming"));
	related_skills.insert("yangming", "#yangming-quota");
	mobileyou_pangtong->addSkill(new YangmingMod);
	related_skills.insert("yangming", "#yangming-mod");
	mobileyou_pangtong->addSkill(new PTGongli);

	General *mobileyou_xushu = new General(this, "mobileyou_xushu", "qun", 3);
	mobileyou_xushu->addSkill(new Xiaxing);
	mobileyou_xushu->addSkill(new Qihui);
	mobileyou_xushu->addSkill(new QihuiNext);
	mobileyou_xushu->addSkill(new QihuiMod);
	related_skills.insert("qihui", "#qihui-next");
	related_skills.insert("qihui", "#qihui-mod");
	mobileyou_xushu->addSkill(new XSGongli);

	General *mobileyou_shitao = new General(this, "mobileyou_shitao", "qun", 3);
	mobileyou_shitao->addSkill(new Qinying);
	mobileyou_shitao->addSkill(new Lunxiong);
	mobileyou_shitao->addSkill(new STGongli);
	addMetaObject<QinyingCard>();

	General *mobileyou_cuijun = new General(this, "mobileyou_cuijun", "qun", 3);
	mobileyou_cuijun->addSkill(new Shunyi);
	mobileyou_cuijun->addSkill(new Biwei);
	mobileyou_cuijun->addSkill(new CJGongli);
	addMetaObject<BiweiCard>();

	Card*c = new Xuanjian(Card::Spade,9);
	c->setParent(this);
	skills << new XuanjianVS;

	General *cuifu = new General(this, "cuifu", "wei", 3, false);
	cuifu->addSkill(new Caiqiu);
	cuifu->addSkill(new Xichang);
	skills << new Weizhuang << new WeizhuangTargetMod;
	related_skills.insert("weizhuang", "#weizhuang-target");
	addMetaObject<WeizhuangCard>();

	General *mobile_zhangzhi = new General(this, "mobile_zhangzhi", "qun", 3);
	mobile_zhangzhi->addSkill(new Shiju);
	skills << new Kubai << new KubaiBf << new KubaiLimit;

	General *mobile_liuhui = new General(this, "mobile_liuhui", "qun", 3);
	mobile_liuhui->addSkill(new MobileGeyuan);
	mobile_liuhui->addSkill(new Chongcha);
	mobile_liuhui->addSkill(new ChongchaLimit);
	addMetaObject<ChongchaCard>();

	General *mobile_luyu = new General(this, "mobile_luyu", "wei", 3);
	mobile_luyu->addSkill(new Dingfa);
	mobile_luyu->addSkill(new Shuxing);

	General *luyu = new General(this, "luyu", "wei", 3);
	luyu->addSkill("dingfa");
	luyu->addSkill("shuxing");










}
ADD_PACKAGE(mobile)

class XingWeifeng : public TriggerSkillV2
{
public:
	XingWeifeng() : TriggerSkillV2("xingweifeng") { events << CardFinished; frequency = Compulsory; }
	static bool qualifying(const Card *card)
	{
		return card && (card->isKindOf("Slash") || card->isKindOf("FireAttack") || card->isKindOf("Duel")
			|| card->isKindOf("ArcheryAttack") || card->isKindOf("SavageAssault"));
	}
	static QString name(const Card *card) { return card ? (card->isKindOf("Slash") ? "slash" : card->objectName()) : QString(); }
	static bool hasFear(const Player *player) { return !player->getTag("mobile_xingweifeng_fear").toMap().isEmpty(); }
	static void clearFear(Room *room, ServerPlayer *target)
	{
		target->removeTag("mobile_xingweifeng_fear");
		for (const QString &mark : target->getMarkNames())
			if (mark.startsWith("&xingju") && target->getMark(mark) > 0) room->setPlayerMark(target, mark, 0);
	}
	int firstQualifyingUse(Room *room, ServerPlayer *owner) const
	{
		const QVariantMap current = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		if (current.value("id").toLongLong() <= 0 || current.value("phase_id").toLongLong() <= 0) return -1;
		QVariantMap query{{"kind", "use_card"}, {"phase_id", current.value("phase_id")}, {"from", owner->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &item : page.value("items").toList()) {
				const QVariantMap fact = item.toMap();
				const QVariantMap card = fact.value("data").toMap().value("card").toMap();
				const QVariantList classes = card.value("classes").toList();
				if (classes.isEmpty()) return -1;
				if (classes.contains("Slash") || classes.contains("FireAttack") || classes.contains("Duel")
					|| classes.contains("ArcheryAttack") || classes.contains("SavageAssault"))
					return fact.value("event_id").toString() == current.value("id").toString() ? 1 : 0;
			}
			if (!page.value("has_more").toBool()) return -1;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!qualifying(use.card)) return {};
		const int first = firstQualifyingUse(room, player);
		if (first < 0) qWarning("XingWeifeng use history unavailable");
		if (first != 1) return {};
		for (ServerPlayer *target : use.to)
			if (target != player && target->isAlive() && !hasFear(target)) return TriggerList{{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> candidates;
		for (ServerPlayer *target : ctx.original_data->value<CardUseStruct>().to)
			if (target != ctx.owner && target->isAlive() && !hasFear(target)) candidates << target;
		if (candidates.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@xingweifeng-invoke", false, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (hasFear(target)) return false;
		const QString card = name(ctx.original_data->value<CardUseStruct>().card);
		target->setTag("mobile_xingweifeng_fear", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
			{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
			{"card", card}, {"amount", getEffectiveAmount(ctx)}, {"token", QString::number(room->currentHistoryEventId())}});
		room->setPlayerMark(target, "&xingju+[+" + card + "+]", 1);
		room->broadcastSkillInvoke(objectName());
		return false;
	}
};

class XingWeifengEffect : public TriggerSkillV2
{
public:
	XingWeifengEffect() : TriggerSkillV2("#xingweifeng-effect")
	{
		events << DamageInflicted << EventPhaseStart << Death;
		frequency = Compulsory;
		global = true;
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !((event == EventPhaseStart && player->getPhase() == Player::Start)
			|| (event == Death && data.value<DeathStruct>().who == player))) return true;
		for (ServerPlayer *target : room->getAllPlayers(true))
			if (target->getTag("mobile_xingweifeng_fear").toMap().value("owner").toString() == player->objectName())
				XingWeifeng::clearFear(room, target);
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != DamageInflicted || !player || !player->isAlive() || !XingWeifeng::hasFear(player)) return true;
		const QVariantMap receipt = player->getTag("mobile_xingweifeng_fear").toMap();
		SkillContext ctx;
		ctx.skill_name = objectName();
		ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
		if (!ctx.owner) return true;
		ctx.invoker = ctx.initiator = player;
		ctx.sourceRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
		ctx.instanceID = ctx.sourceRef.key.instanceID;
		ctx.amount = receipt.value("amount").toInt();
		ctx.original_data = &data;
		ctx.current_event = event;
		ctx.extra_data = receipt;
		ctx.targets = {player};
		contexts << ctx;
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.invoker && ctx.sourceRef.isValid()
			&& ctx.invoker->getTag("mobile_xingweifeng_fear").toMap() == ctx.extra_data.toMap();
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		// The mark is consumed by this damage, even when a recipient interceptor cancels the benefit.
		XingWeifeng::clearFear(room, ctx.invoker);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		room->sendCompulsoryTriggerLog(ctx.owner, "xingweifeng");
		if (XingWeifeng::name(damage.card) == ctx.extra_data.toMap().value("card").toString()) {
			const int previous = damage.damage;
			damage.damage += getEffectiveAmount(ctx);
			ctx.original_data->setValue(damage);
			LogMessage log;
			log.type = "#XingWeifeng";
			log.from = target;
			log.arg = QString::number(previous);
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
		} else if (ctx.owner->isAlive() && !target->isNude()) {
			const int id = room->askForCardChosen(ctx.owner, target, "he", "xingweifeng");
			if (id >= 0) room->obtainCard(ctx.owner, id, room->getCardPlace(id) != Player::PlaceHand);
		}
		return false;
	}
};
XingZhilveSlashCard::XingZhilveSlashCard()
{
	mute = true;
	setSkillName("xingzhilve");
}

bool XingZhilveSlashCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("_xingzhilve");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void XingZhilveSlashCard::onUse(Room *room, CardUseStruct &card_use) const
{
	ActiveSkillCard::onUse(room, card_use);
}
XingZhilveCard::XingZhilveCard()
{
	setSkillName("xingzhilve");
	target_fixed = true;
}

void XingZhilveCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}
class XingZhilve : public ViewAsSkillV2
{
public:
	XingZhilve() : ViewAsSkillV2("xingzhilve") { response_pattern = "@@xingzhilve!"; }
	LimitScope getLimitScope() const override { return Limit_Custom; }
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner) return false;
		const SkillInstanceRef ref = getUsageRef(ctx);
		if (!ref.isValid()) return false;
		if (ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "slash_pending").toBool())
			return true;
		const QString phase = ctx.owner->getRoom()->historyScopes().value("phase_id").toString();
		return !phase.isEmpty() && phase != "0"
			&& ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_phase").toString() != phase;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		const SkillInstanceRef ref = getUsageRef(ctx);
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
			"used_phase", ctx.owner->getRoom()->historyScopes().value("phase_id"));
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator) return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
		return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@xingzhilve!"
			&& request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
				request.activationRef.key.instanceID, "slash_pending").toBool();
	}
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{
		return targets.isEmpty();
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (request.pattern == "@@xingzhilve!" || request.userString == "slash") {
			if (!request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
				request.activationRef.key.instanceID, "slash_pending").toBool()) return nullptr;
			Card *slash = Sanguosha->cloneCard("slash");
			slash->setSkillName("_xingzhilve");
			return slash;
		}
		return ViewAsSkillV2::createCard(request);
	}
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		return request.pattern == "@@xingzhilve!" || request.userString == "slash" ? "Slash" : "XingZhilveCard";
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		if (ctx.use_card && ctx.use_card->isKindOf("Slash"))
			return ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
				ctx.activationRef.key.instanceID, "slash_pending").toBool();
		if (!checkCustomUsage(ctx)) return false;
		addUsage(ctx);
		room->loseHp(HpLostStruct(ctx.invoker, getEffectiveAmount(ctx), objectName(), ctx.invoker));
		return ctx.invoker->isAlive();
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		return skillEffect(ctx, ctx.owner);
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *source) const override
	{
		Room *room = source->getRoom();
		Room::AcceptedViewAsEffectScope continuation(room, source, objectName(), ctx);
		if (!continuation.isValid()) return FinishSkill;
		const int amount = getEffectiveAmount(ctx);
		room->addMaxCards(source, amount);
		QStringList choices;
		if (room->canMoveField()) choices << "move";
		choices << "draw";
		if (room->askForChoice(source, objectName(), choices.join("+")) == "move") {
			room->moveField(source, objectName());
			return ContinueEffects;
		}
		source->drawCards(amount, objectName());
		if (source->isDead()) return FinishSkill;
		std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
		slash->setSkillName("_xingzhilve");
		QList<ServerPlayer *> targets;
		for (ServerPlayer *target : room->getOtherPlayers(source))
			if (source->canSlash(target, slash.get(), false)) targets << target;
		if (targets.isEmpty()) return ContinueEffects;
		const SkillInstanceRef ref = continuation.activationRef();
		const QVariant previous = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "slash_pending");
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "slash_pending", true);
		// The accepted scope restores its outer selector and retires this prompt instance.
		try {
			const Card *used = room->askForUseCard(source, "@@xingzhilve!", "@xingzhilve", -1, Card::MethodUse, false);
			if (!used && source->isAlive()) {
				QList<ServerPlayer *> legal;
				for (ServerPlayer *target : targets)
					if (target->isAlive() && source->canSlash(target, slash.get(), false)) legal << target;
				if (!legal.isEmpty()) {
					slash->setActivationSkill(ref.key.skillName, ref.key.instanceID);
					slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
					room->useCardFromSkillEffect(CardUseStruct(slash.get(), source, legal.at(qsanRandomBounded(legal.size()))), ctx, false);
				}
			}
			ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "slash_pending", previous);
		} catch (...) {
			ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "slash_pending", previous);
			throw;
		}
		return ContinueEffects;
	}
};
XingZhiyanCard::XingZhiyanCard()
{
	setSkillName("xingzhiyan");
	handling_method = Card::MethodNone;
	will_throw = false;
}

bool XingZhiyanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(getSubcards().isEmpty()) return false;
	return targets.isEmpty() && to_select != Self;
}

bool XingZhiyanCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return !targets.isEmpty()||getSubcards().isEmpty();
}

void XingZhiyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class XingZhiyan : public ViewAsSkillV2
{
public:
	XingZhiyan() : ViewAsSkillV2("xingzhiyan", -1) { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Custom; }
	static bool used(const Player *player, const SkillInstanceRef &ref, bool give)
	{
		return player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, give ? "gave" : "drew").toBool();
	}
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner) return false;
		const SkillInstanceRef ref = getUsageRef(ctx);
		if (ctx.use_card) return !used(ctx.owner, ref, !ctx.use_card->getSubcards().isEmpty());
		return !used(ctx.owner, ref, false) || !used(ctx.owner, ref, true);
	}
	void addUsage(const SkillContext &ctx) const override
	{
		const SkillInstanceRef ref = getUsageRef(ctx);
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, ctx.use_card && !ctx.use_card->getSubcards().isEmpty() ? "gave" : "drew", true);
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && player->getPhase() == Player::Play
			&& ((!used(player, request.activationRef, false) && player->getMaxHp() > player->getHandcardNum())
				|| (!used(player, request.activationRef, true) && player->getHandcardNum() > player->getHp()));
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		const Player *player = request.initiator;
		return player && card && !used(player, request.activationRef, true)
			&& request.selectedCardIds.size() < player->getHandcardNum() - player->getHp()
			&& player->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player) return false;
		if (request.selectedCardIds.isEmpty()) return !used(player, request.activationRef, false) && player->getMaxHp() > player->getHandcardNum();
		return !used(player, request.activationRef, true) && request.selectedCardIds.size() == player->getHandcardNum() - player->getHp();
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		XingZhiyanCard *card = new XingZhiyanCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
	{
		return !request.selectedCardIds.isEmpty() && targets.isEmpty() && target && target->isAlive() && target != request.initiator;
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
	{
		return request.selectedCardIds.isEmpty() ? targets.isEmpty() : targets.size() == 1;
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "XingZhiyanCard"; }
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!isUsable(ctx) || !cardSelectionFeasible(request)) return false;
		for (int id : request.selectedCardIds)
			if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand) return false;
		ctx.extra_data = !request.selectedCardIds.isEmpty();
		addUsage(ctx);
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		if (ctx.use_card && !ctx.use_card->getSubcards().isEmpty()) return ContinueEffects;
		ctx.manual_effect = true;
		return skillEffect(ctx, ctx.owner);
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		if (!ctx.use_card || ctx.use_card->getSubcards().isEmpty()) {
			// The prohibition is an already applied phase effect, separate from source quota.
			room->addPlayerMark(target, "xingzhiyan_draw-PlayClear");
			const int draw = qMax(0, target->getMaxHp() - target->getHandcardNum());
			if (draw > 0) target->drawCards(draw * getEffectiveAmount(ctx), objectName());
		} else {
			const QList<int> ids = ctx.use_card->getSubcards();
			for (int id : ids) if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
			DummyCard gift(ids);
			room->giveCard(ctx.owner, target, &gift, objectName());
		}
		return ContinueEffects;
	}
};

class XingZhiyanClear : public TriggerSkillV2
{
public:
	XingZhiyanClear() : TriggerSkillV2("#xingzhiyan-clear") { events << EventPhaseChanging; global = true; }
	bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (player && data.value<PhaseChangeStruct>().from == Player::Play)
			for (int id : player->getSkillInstanceIds("xingzhiyan")) {
				player->removeSkillInstanceStateValue("xingzhiyan", id, "gave");
				player->removeSkillInstanceStateValue("xingzhiyan", id, "drew");
			}
		return true;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};
class XingZhiyanPro : public ProhibitSkill
{
public:
	XingZhiyanPro() : ProhibitSkill("#xingzhiyan-pro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return from != to && from->getMark("xingzhiyan_draw-PlayClear") > 0 && !card->isKindOf("SkillCard");
	}
};

XingJinfanCard::XingJinfanCard()
{
	setSkillName("xingjinfan");
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void XingJinfanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class XingJinfanVS : public ViewAsSkillV2
{
public:
	XingJinfanVS(const QString &name) : ViewAsSkillV2(name, -1) { response_pattern = "@@" + name; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == response_pattern;
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		if (!request.initiator || !card || !request.initiator->handCards().contains(card->getEffectiveId())) return false;
		for (int id : request.selectedCardIds + request.initiator->getPile("&xingling"))
			if (Sanguosha->getCard(id)->getSuit() == card->getSuit()) return false;
		return true;
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return !request.selectedCardIds.isEmpty(); }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		XingJinfanCard *card = new XingJinfanCard;
		card->setSkillName(objectName());
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "XingJinfanCard"; }
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return NoTarget; }
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		for (int id : request.selectedCardIds) {
			if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
				|| !canSelectCard(selection, Sanguosha->getCard(id))) return false;
			selection.selectedCardIds << id;
		}
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override { ctx.manual_effect = true; return skillEffect(ctx, ctx.owner); }
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantList receipts = target->getTag("mobile_xingjinfan_cards").toList();
		QList<int> cards;
		Room *room = target->getRoom();
		for (int id : ctx.use_card->getSubcards()) {
			if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand) continue;
			const Card *card = Sanguosha->getCard(id);
			bool duplicate = false;
			for (int present : target->getPile("&xingling")) if (Sanguosha->getCard(present)->getSuit() == card->getSuit()) duplicate = true;
			if (duplicate) continue;
			cards << id;
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"id", id}, {"suit", int(card->getSuit())}};
		}
		// Bind each pile card to its source before movement can re-enter any listener.
		target->setTag("mobile_xingjinfan_cards", receipts);
		if (!cards.isEmpty()) target->addToPile("&xingling", cards);
		return ContinueEffects;
	}
};

class XingJinfan : public TriggerSkillV2
{
public:
	XingJinfan() : TriggerSkillV2("xingjinfan") { events << EventPhaseStart << CardsMoveOneTime; global = true; view_as_skill = new XingJinfanVS(objectName()); }
	bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != CardsMoveOneTime || !player) return true;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player) return true;
		QVariantList retained;
		for (const QVariant &value : player->getTag("mobile_xingjinfan_cards").toList()) {
			const QVariantMap receipt = value.toMap();
			const int index = move.card_ids.indexOf(receipt.value("id").toInt());
			if (index >= 0 && move.from_pile_names.value(index) == "&xingling"
				&& !player->isSkillInstanceEffectAvailable(receipt.value("skill").toString(), receipt.value("instance").toInt())) continue;
			retained << value;
		}
		player->setTag("mobile_xingjinfan_cards", retained);
		return true;
	}
	static QVariantList moved(const ServerPlayer *player, const CardsMoveOneTimeStruct &move, int instance)
	{
		QVariantList result;
		if (move.from != player) return result;
		for (const QVariant &value : player->getTag("mobile_xingjinfan_cards").toList()) {
			const QVariantMap receipt = value.toMap();
			const int index = move.card_ids.indexOf(receipt.value("id").toInt());
			if (receipt.value("skill").toString() == "xingjinfan" && receipt.value("instance").toInt() == instance
				&& index >= 0 && move.from_pile_names.value(index) == "&xingling") result << value;
		}
		return result;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == EventPhaseStart)
			return player->getPhase() == Player::Discard && !player->isKongcheng() ? TriggerList{{player, {objectName()}}} : TriggerList();
		QStringList names;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		for (int id : player->getValidSkillInstanceIds(objectName()))
			if (!moved(player, move, id).isEmpty()) names << SkillInstanceKey(objectName(), id).toString();
		return names.isEmpty() ? TriggerList() : TriggerList{{player, names}};
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == CardsMoveOneTime) {
			ctx.extra_data = moved(player, ctx.original_data->value<CardsMoveOneTimeStruct>(), ctx.sourceRef.key.instanceID);
			ctx.targets = {player};
			return !ctx.extra_data.toList().isEmpty();
		}
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == EventPhaseStart) {
			Room::AcceptedViewAsEffectScope selection(room, player, objectName(), ctx);
			if (!selection.isValid()) return false;
			room->askForUseCard(player, "@@xingjinfan", "@xingjinfan");
		} else {
			QVariantList receipts = player->getTag("mobile_xingjinfan_cards").toList();
			for (const QVariant &receipt : ctx.extra_data.toList()) receipts.removeOne(receipt);
			player->setTag("mobile_xingjinfan_cards", receipts);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		for (const QVariant &receipt : ctx.extra_data.toList())
			for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
				QList<int> candidates;
				for (int id : room->getDrawPile())
					if (Sanguosha->getCard(id)->getSuit() == receipt.toMap().value("suit").toInt()) candidates << id;
				if (candidates.isEmpty()) continue;
				room->sendCompulsoryTriggerLog(ctx.owner, this);
				room->obtainCard(target, candidates.at(qsanRandomBounded(candidates.length())), true);
			}
		return false;
	}
};

class XingJinfanLose : public TriggerSkillV2
{
public:
	XingJinfanLose(const QString &name) : TriggerSkillV2("#" + name + "-lose"), sourceName(name)
	{
		events << EventLoseSkill;
		global = true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		SkillChangeStruct change;
		if (!player || !change.tryParse(data) || change.skillName != sourceName || change.instanceID <= 0) return true;
		QVariantList removed;
		for (const QVariant &value : player->getTag("mobile_xingjinfan_cards").toList())
			if (value.toMap().value("skill").toString() == sourceName && value.toMap().value("instance").toInt() == change.instanceID) removed << value;
		if (removed.isEmpty()) return true;
		SkillContext ctx;
		ctx.skill_name = objectName();
		ctx.owner = ctx.invoker = ctx.initiator = player;
		ctx.sourceRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(sourceName, change.instanceID));
		ctx.instanceID = change.instanceID;
		ctx.current_event = event;
		ctx.original_data = &data;
		ctx.targets = {player};
		ctx.extra_data = removed;
		contexts << ctx;
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && !ctx.extra_data.toList().isEmpty()
			&& ctx.owner->getTag("mobile_xingjinfan_cards").toList().contains(ctx.extra_data.toList().first());
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariantList receipts = player->getTag("mobile_xingjinfan_cards").toList();
		for (const QVariant &receipt : ctx.extra_data.toList()) receipts.removeOne(receipt);
		player->setTag("mobile_xingjinfan_cards", receipts);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
	{
		QList<int> cards;
		for (const QVariant &receipt : ctx.extra_data.toList()) {
			const int id = receipt.toMap().value("id").toInt();
			if (player->getPile("&xingling").contains(id)) cards << id;
		}
		if (!cards.isEmpty()) room->throwCard(cards, sourceName, player);
		return false;
	}
private:
	QString sourceName;
};
class XingSheque : public TriggerSkillV2
{
public:
	XingSheque() : TriggerSkillV2("xingsheque") { events << EventPhaseProceeding; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || player->getPhase() != Player::Start || player->getEquips().isEmpty()) return result;
		for (ServerPlayer *owner : room->getOtherPlayers(player))
			if (owner->hasSkill(objectName()) && owner->canSlash(player, nullptr, false)) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.invoker || ctx.invoker->isDead() || ctx.invoker->getEquips().isEmpty()
			|| !ctx.owner->canSlash(ctx.invoker, nullptr, false)) return false;
		ctx.targets = {ctx.invoker};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (target->isDead() || target->getEquips().isEmpty() || !ctx.owner->canSlash(target, nullptr, false)) return false;
		// The ordinary Slash request owns its material/payment pipeline and its armor flag.
		if (room->askForUseSlashTo(ctx.owner, target, "@xingsheque:" + target->objectName(),
			false, false, false, nullptr, nullptr, "SlashIgnoreArmor")) {
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(ctx.owner, objectName());
		}
		return false;
	}
};
class Gulivs : public ViewAsSkillV2
{
public:
	Gulivs() : ViewAsSkillV2("guli") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->isKongcheng() && Slash::IsAvailable(request.initiator);
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.initiator->isKongcheng()) return false;
		// The button supplies all hand cards; a serialized reply must supply exactly that set.
		if (request.selectedCardIds.isEmpty()) return true;
		QList<int> selected = request.selectedCardIds, hand = request.initiator->handCards();
		std::sort(selected.begin(), selected.end());
		std::sort(hand.begin(), hand.end());
		return selected == hand;
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName(objectName());
		slash->addSubcards(request.initiator->handCards());
		return slash;
	}
};

class Guli : public TriggerSkillV2
{
public:
	Guli() : TriggerSkillV2("guli")
	{
		events << TargetSpecified << CardFinished;
		view_as_skill = new Gulivs;
	}
	static int causedDamage(Room *room, ServerPlayer *player)
	{
		const QVariantMap use = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		const QString useId = use.value("id").toString();
		if (useId.isEmpty() || useId == "0") return -1;
		QVariantMap filter{{"turn_id", use.value("turn_id")}, {"from", player->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryActualDamage(filter);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap();
				const QVariantMap parent = room->historyParent(fact.value("event_id").toLongLong(), "use_card", true);
				if (parent.value("id").toString() == useId) return 1;
			}
			if (!page.value("has_more").toBool()) return 0;
			filter.insert("after", page.value("next_after"));
			filter.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getActivationSkillName() != objectName()
			|| use.card->getActivationSkillInstanceId() <= 0) return {};
		if (event == CardFinished) {
			const int evidence = causedDamage(room, player);
			if (evidence < 0) {
				qWarning("Guli damage history unavailable; settlement cannot be determined");
				return {};
			}
			if (evidence == 0) return {};
		}
		return TriggerList{{player, {SkillInstanceUtils::formatName(objectName(),
			use.card->getActivationSkillInstanceId())}}};
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == CardFinished) return ctx.owner->askForSkillInvoke(this, *ctx.original_data);
		ctx.targets = ctx.original_data->value<CardUseStruct>().to;
		return true;
	}
	bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == CardFinished) room->loseHp(ctx.owner, getEffectiveAmount(ctx), true, ctx.owner, objectName());
		return true;
	}
	bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == CardFinished && ctx.owner->isAlive())
			ctx.owner->drawCards(qMax(0, ctx.owner->getMaxHp() - ctx.owner->getHandcardNum()), objectName());
		return false;
	}
	bool effectTarget(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event == TargetSpecified) target->addQinggangTag(ctx.original_data->value<CardUseStruct>().card);
		return false;
	}
};
class Aoshi : public TriggerSkillV2
{
public:
	Aoshi() : TriggerSkillV2("aoshi") { events << Damage << EventPhaseChanging; frequency = Compulsory; }
	static void project(Room *room, ServerPlayer *target)
	{
		QStringList actors;
		for (const QVariant &entry : target->getTag("mobile_aoshi_receipts").toList()) {
			const QString actor = entry.toMap().value("actor").toString();
			if (!actor.isEmpty() && !actors.contains(actor)) actors << actor;
		}
		for (const QString &mark : target->getMarkNames())
			if (mark.startsWith("&aoshi+#") && !actors.contains(mark.mid(QString("&aoshi+#").length()))) room->setPlayerMark(target, mark, 0);
		for (const QString &actor : actors) room->setPlayerMark(target, "&aoshi+#" + actor, 1);
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventPhaseChanging || !player) return true;
		const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.from != Player::Play && change.to != Player::NotActive) return true;
		const QVariant turn = room->historyScopes().value("turn_id");
		for (ServerPlayer *target : room->getAllPlayers(true)) {
			QVariantList keep;
			for (const QVariant &entry : target->getTag("mobile_aoshi_receipts").toList()) {
				const QVariantMap receipt = entry.toMap();
				bool expired = false;
				if (receipt.value("actor").toString() == player->objectName() && receipt.value("turn") == turn) {
					const QVariantMap previous = room->historyEvent(receipt.value("phase").toLongLong());
					// The current phase scope is already the next phase at EventPhaseChanging.
					expired = change.to == Player::NotActive || (previous.value("status").toString() == "finished"
						&& previous.value("data").toMap().value("phase").toInt() == Player::Play);
				}
				if (!expired) keep << receipt;
			}
			target->setTag("mobile_aoshi_receipts", keep);
			project(room, target);
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != Damage || !player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
		const DamageStruct damage = data.value<DamageStruct>();
		return damage.to && damage.to != player && damage.to->isAlive() && player->inMyAttackRange(damage.to)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.original_data->value<DamageStruct>().to};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const QVariant phase = room->historyScopes().value("phase_id");
		QVariantList receipts = target->getTag("mobile_aoshi_receipts").toList();
		for (const QVariant &entry : receipts) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("phase") == phase && receipt.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
				&& receipt.value("activation_skill").toString() == ctx.activationRef.key.skillName
				&& receipt.value("activation_instance").toInt() == ctx.activationRef.key.instanceID) return false;
		}
		const quint64 serial = room->getTag("mobile_aoshi_serial").toULongLong() + 1;
		room->setTag("mobile_aoshi_serial", serial);
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
			{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
			{"phase", phase}, {"turn", room->historyScopes().value("turn_id")}, {"actor", ctx.owner->objectName()}, {"serial", serial}};
		target->setTag("mobile_aoshi_receipts", receipts);
		// Explicitly expired projection: a nested Play phase must not clear an outer phase's effect.
		project(room, target);
		room->sendCompulsoryTriggerLog(ctx.owner, this);
		return false;
	}
};

class AoshiBf : public TargetModSkillV2
{
public:
	AoshiBf() : TargetModSkillV2("#aoshibf", ".") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == Residue && ctx.primary && ctx.secondary
			&& ctx.secondary->getMark("&aoshi+#" + ctx.primary->objectName()) > 0
			? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
	}
};
class Shidi : public TriggerSkillV2
{
public:
	Shidi() : TriggerSkillV2("shidi")
	{
		events << CardUsed << EventPhaseStart;
		frequency = Compulsory;
		change_skill = true;
		waked_skills = "#shidi";
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		if (!player || !player->isAlive()) return result;
		if (event == EventPhaseStart) {
			if (player->hasSkill(objectName())
				&& (player->getPhase() == Player::Start || player->getPhase() == Player::Finish))
				result[player] << objectName();
		} else {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || !use.card->isKindOf("Slash")) return result;
			QList<ServerPlayer *> holders;
			if (use.card->isBlack()) holders << player;
			else if (use.card->isRed()) holders = use.to;
			for (ServerPlayer *holder : holders) {
				if (!holder->isAlive() || !holder->hasSkill(objectName())) continue;
				for (int id : holder->getSkillInstanceIds(objectName())) {
					const int stance = holder->getSkillInstanceStateValue(objectName(), id, "stance").toInt();
					if (stance == (use.card->isBlack() ? 1 : 2))
						result[holder] << SkillInstanceUtils::formatName(objectName(), id);
				}
			}
		}
		return result;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == EventPhaseStart) {
			const int stance = ctx.owner->getPhase() == Player::Start ? 1 : 2;
			// The UI mark is a projection; each source retains its own stance.
			ctx.owner->setSkillInstanceStateValue(ctx.sourceRef.key.skillName,
				ctx.sourceRef.key.instanceID, "stance", stance);
			room->setSkillInstanceCorrectState(ctx.owner, ctx.sourceRef, "stance", stance);
			room->setChangeSkillState(ctx.owner, objectName(), stance == 1 ? 2 : 1);
			room->setPlayerMark(ctx.owner, "shidiEffect", stance);
			room->sendCompulsoryTriggerLog(ctx.owner, this);
		} else {
			CardUseStruct use = ctx.original_data->value<CardUseStruct>();
			const QString recipient = use.card->isBlack() ? "_ALL_TARGETS" : ctx.owner->objectName();
			if (!use.no_respond_list.contains(recipient)) use.no_respond_list << recipient;
			room->sendCompulsoryTriggerLog(ctx.owner, this, use.card->isBlack() ? 1 : 2);
			ctx.original_data->setValue(use);
		}
		return false;
	}
};

class ShidiDistance : public DistanceSkillV2
{
public:
	ShidiDistance() : DistanceSkillV2("#shidi")
	{
		setHolderSelector(CorrectSkill_Participants);
	}
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.holder) return CorrectSkillResult::noEffect();
		const SkillInstance *helper = ctx.holder->findSkillInstance(ctx.instanceRef.key.skillName,
			ctx.instanceRef.key.instanceID);
		if (!helper || helper->parent.skillName != "shidi") return CorrectSkillResult::noEffect();
		// Related IDs need not equal the parent's ID.
		const int stance = ctx.holder->getSkillInstanceCorrectStateValue(helper->parent.skillName,
			helper->parent.instanceID, "stance").toInt();
		if (ctx.holder == ctx.primary && stance == 1)
			return CorrectSkillResult::useAmount(-ctx.currentAmount);
		if (ctx.holder == ctx.secondary && stance == 2)
			return CorrectSkillResult::useAmount(ctx.currentAmount);
		return CorrectSkillResult::noEffect();
	}
};

class Yishihz : public TriggerSkillV2
{
public:
	Yishihz() : TriggerSkillV2("yishihz") { events << DamageCaused; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		return player && player->isAlive() && player->hasSkill(objectName())
			&& damage.to && damage.to != player && damage.to->hasEquip()
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
		if (!target || !target->hasEquip() || !ctx.owner->askForSkillInvoke(this, target)) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		if (target->hasEquip()) {
			const int id = room->askForCardChosen(ctx.owner, target, "e", objectName());
			if (id >= 0) room->obtainCard(ctx.owner, id);
		}
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		damage.damage -= getEffectiveAmount(ctx);
		ctx.original_data->setValue(damage);
		return damage.damage < 1;
	}
};
class Qishe : public ViewAsSkillV2
{
public:
	Qishe() : ViewAsSkillV2("qishe", 1)
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && ((request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& Analeptic::IsAvailable(request.initiator))
			|| (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
				&& request.pattern.contains("analeptic") && request.initiator->getCardCount() > 0));
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty()
			&& card->isKindOf("EquipCard") && (request.initiator->getEquipsId().contains(card->getEffectiveId())
				|| request.initiator->handCards().contains(card->getEffectiveId()));
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
	}

	QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		Card *analeptic = Sanguosha->cloneCard("analeptic");
		analeptic->setSkillName(objectName());
		analeptic->addSubcard(request.selectedCardIds.first());
		return analeptic;
	}
};

class QisheMax : public MaxCardsSkillV2
{
public:
	QisheMax() : MaxCardsSkillV2("#qishe") {}
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.holder ? CorrectSkillResult::useAmount(ctx.holder->getEquips().size() * ctx.currentAmount)
			: CorrectSkillResult::noEffect();
	}
};

class ZhenbianMax : public MaxCardsSkillV2
{
public:
	ZhenbianMax() : MaxCardsSkillV2("#zhenbian-max") {}
	CorrectSkillResult getCorrection(const CorrectSkillContext &) const override
	{
		return CorrectSkillResult::noEffect();
	}
	CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
	{
		return ctx.holder ? CorrectSkillResult::useAmount(ctx.holder->getMaxHp())
			: CorrectSkillResult::noEffect();
	}
};
class Xiongjin : public TriggerSkillV2
{
public:
	Xiongjin() : TriggerSkillV2("xiongjin") { events << EventPhaseStart << EventSkillInvoking; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Round; }
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == EventSkillInvoking) return {};
		TriggerList result;
		if (!player || !player->isAlive() || player->getPhase() != Player::Play) return result;
		for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !ctx.invoker || !ctx.invoker->isAlive()) return false;
		if (!ctx.owner->askForSkillInvoke(objectName() + "$-1", ctx.owner == ctx.invoker ? QVariant() : QVariant::fromValue(ctx.invoker))) return false;
		ctx.targets = {ctx.invoker};
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const quint64 serial = room->getTag("mobile_xiongjin_serial").toULongLong() + 1;
		room->setTag("mobile_xiongjin_serial", serial);
		QVariantList receipts = target->getTag("mobile_xiongjin_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
			{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
			{"issuer", ctx.owner->objectName()}, {"turn", room->historyScopes().value("turn_id")},
			{"self", target == ctx.owner}, {"serial", serial}};
		target->setTag("mobile_xiongjin_receipts", receipts);
		target->drawCards(qBound(1, ctx.owner->getLostHp(), 3) * getEffectiveAmount(ctx), objectName());
		return false;
	}
};

class XiongjinDiscard : public TriggerSkillV2
{
public:
	XiongjinDiscard() : TriggerSkillV2("#xiongjin-discard")
	{
		events << EventPhaseStart << EventPhaseChanging;
		frequency = Compulsory;
		global = true;
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
	{
		if (event == EventPhaseStart && actor && actor->getPhase() == Player::Discard) {
			QVariantList receipts = actor->getTag("mobile_xiongjin_receipts").toList();
			int ordinal = 0;
			for (QVariant &entry : receipts) {
				QVariantMap receipt = entry.toMap();
				if (receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
				receipt["ready_phase"] = room->historyScopes().value("phase_id");
				receipt["ordinal"] = ++ordinal; entry = receipt;
			}
			actor->setTag("mobile_xiongjin_receipts", receipts);
			return true;
		}
		if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
		const QVariant turn = room->historyScopes().value("turn_id");
		for (ServerPlayer *player : room->getAllPlayers(true)) {
			QVariantList retained;
			for (const QVariant &receipt : player->getTag("mobile_xiongjin_receipts").toList())
				if (receipt.toMap().value("turn") != turn) retained << receipt;
			player->setTag("mobile_xiongjin_receipts", retained);
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Discard) return true;
		const QVariant turn = room->historyScopes().value("turn_id");
		for (const QVariant &receipt : player->getTag("mobile_xiongjin_receipts").toList()) {
			const QVariantMap saved = receipt.toMap();
			if (saved.value("turn") != turn || saved.value("ready_phase") != room->historyScopes().value("phase_id")) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(saved.value("issuer").toString(), true);
			if (!ctx.owner) continue;
			ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(saved.value("owner").toString(), SkillInstanceKey(saved.value("skill").toString(), saved.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.trigger_count = saved.value("ordinal").toInt();
			ctx.preferredTarget = player; ctx.preferredTargetSeat = player->getSeat();
			ctx.targets = {player};
			ctx.current_event = event;
			ctx.original_data = &data;
			ctx.extra_data = receipt;
			contexts << ctx;
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.invoker && ctx.sourceRef.isValid()
			&& ctx.invoker->getTag("mobile_xiongjin_receipts").toList().contains(ctx.extra_data);
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantList receipts = ctx.invoker->getTag("mobile_xiongjin_receipts").toList();
		if (!receipts.removeOne(ctx.extra_data)) return false;
		ctx.invoker->setTag("mobile_xiongjin_receipts", receipts);
		const bool self = ctx.extra_data.toMap().value("self").toBool();
		room->sendCompulsoryTriggerLog(target, "xiongjin");
		QList<int> ids;
		for (const Card *card : target->getCards(self ? "he" : "h"))
			if (card->isKindOf("BasicCard") != self && target->canDiscard(target, card->getEffectiveId())) ids << card->getEffectiveId();
		if (!ids.isEmpty()) room->throwCard(ids, "xiongjin", target);
		return false;
	}
};
class Zhenbian : public TriggerSkillV2
{
public:
	Zhenbian() : TriggerSkillV2("zhenbian")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		TriggerList result;
		if (move.to_place != Player::DiscardPile
			|| (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE)
			return result;
		if (player && player->isAlive() && player->hasSkill(objectName())) result[player] << objectName();
		return result;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const SkillInstanceRef ref = ctx.sourceRef;
		QStringList suits = ctx.owner->getSkillInstanceStateValue(ref.key.skillName,
			ref.key.instanceID, "suits").toStringList();
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		for (int id : move.card_ids) {
			const Card *card = Sanguosha->getCard(id);
			if (card->getSuit() < Card::Spade || card->getSuit() > Card::Diamond) continue;
			const QString suit = card->getSuitString() + "_char";
			if (!suits.contains(suit)) suits << suit;
			if (suits.size() == 4 && ctx.owner->getMaxHp() < 9) {
				suits.clear();
				// Persist progress before gainMaxHp can emit nested events.
				ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "suits", suits);
				room->sendCompulsoryTriggerLog(ctx.owner, this);
				room->gainMaxHp(ctx.owner, qMin(getEffectiveAmount(ctx), 9 - ctx.owner->getMaxHp()), objectName());
			}
		}
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "suits", suits);
		// Existing public marks are only a display projection of instance progress.
		for (const QString &mark : ctx.owner->getMarkNames())
			if (mark.startsWith("&zhenbian+:+")) room->setPlayerMark(ctx.owner, mark, 0);
		if (!suits.isEmpty()) room->setPlayerMark(ctx.owner, "&zhenbian+:+" + suits.join("+"), 1);
		return false;
	}
};
class Baoxivs : public ViewAsSkillV2
{
public:
	Baoxivs() : ViewAsSkillV2("baoxi", 1)
	{
		response_pattern = "@@baoxi";
	}
	LimitScope getLimitScope() const override { return Limit_Custom; }
	static bool available(ServerPlayer *owner, const SkillInstanceRef &ref, bool duel)
	{
		if (!owner || !ref.isValid()) return false;
		const QString round = owner->getRoom()->historyScopes().value("round_id").toString();
		return !round.isEmpty() && round != "0"
			&& owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
				duel ? "duel_round" : "slash_round").toString() != round;
	}
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		// The pre-create usage gate has no card yet; the exact branch is checked at payment.
		return ctx.use_card ? available(ctx.owner, getUsageRef(ctx), ctx.use_card->isKindOf("Duel"))
			: available(ctx.owner, getUsageRef(ctx), true) || available(ctx.owner, getUsageRef(ctx), false);
	}
	void addUsage(const SkillContext &ctx) const override
	{
		const SkillInstanceRef ref = getUsageRef(ctx);
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
			ctx.use_card->isKindOf("Duel") ? "duel_round" : "slash_round",
			ctx.owner->getRoom()->historyScopes().value("round_id"));
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && !request.initiator->isKongcheng()
			&& request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& (request.pattern == "@@baoxi1" || request.pattern == "@@baoxi2");
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty()
			&& request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.selectedCardIds.size() == 1
			&& request.initiator->handCards().contains(request.selectedCardIds.first());
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
		Card *card = Sanguosha->cloneCard(request.pattern == "@@baoxi1" ? "duel" : "slash");
		card->setSkillName(objectName());
		card->addSubcard(request.selectedCardIds.first());
		return card;
	}
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		return request.pattern == "@@baoxi1" ? "Duel" : "Slash";
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		if (!checkCustomUsage(ctx)) return false;
		// Commit before the max-HP loss can dispatch a nested discard batch.
		addUsage(ctx);
		room->loseMaxHp(ctx.invoker, getEffectiveAmount(ctx), objectName());
		return ctx.invoker->isAlive();
	}
};

class Baoxi : public TriggerSkillV2
{
public:
	Baoxi() : TriggerSkillV2("baoxi")
	{
		events << CardsMoveOneTime;
		view_as_skill = new Baoxivs;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->isKongcheng()
			|| move.to_place != Player::DiscardPile) return result;
		int basic = 0;
		for (int id : move.card_ids) if (Sanguosha->getCard(id)->isKindOf("BasicCard")) ++basic;
		for (int id : player->getSkillInstanceIds(objectName())) {
			const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
			if ((basic > 1 && Baoxivs::available(player, ref, true))
				|| (move.card_ids.size() - basic > 1 && Baoxivs::available(player, ref, false)))
				result[player] << SkillInstanceUtils::formatName(objectName(), id);
		}
		return result;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		int basic = 0;
		for (int id : move.card_ids) if (Sanguosha->getCard(id)->isKindOf("BasicCard")) ++basic;
		MobileResponseInstanceScope selection(room, ctx.owner, ctx.sourceRef);
		if (basic > 1 && Baoxivs::available(ctx.owner, ctx.sourceRef, true))
			room->askForUseCard(ctx.owner, "@@baoxi1", "baoxi1");
		if (ctx.owner->isAlive() && !ctx.owner->isKongcheng() && move.card_ids.size() - basic > 1
			&& Baoxivs::available(ctx.owner, ctx.sourceRef, false))
			room->askForUseCard(ctx.owner, "@@baoxi2", "baoxi2", -1, Card::MethodUse, false);
		return false;
	}
};
class Cangjia : public TriggerSkillV2
{
public:
	Cangjia() : TriggerSkillV2("cangjia") { events << CardsMoveOneTime << TargetConfirming; frequency = Compulsory; }
	static QStringList suits(const Player *owner, int id)
	{
		return owner->getSkillInstanceStateValue("cangjia", id, "suits").toStringList();
	}
	static void setSuits(Room *room, ServerPlayer *owner, int id, const QStringList &values)
	{
		owner->setSkillInstanceStateValue("cangjia", id, "suits", values);
		room->setSkillInstanceCorrectState(owner, SkillInstanceRef(owner->objectName(), SkillInstanceKey("cangjia", id)),
			"suits", values);
		QStringList display;
		for (int sourceId : owner->getSkillInstanceIds("cangjia"))
			for (const QString &suit : suits(owner, sourceId))
				if (!display.contains(suit + "_char")) display << suit + "_char";
		for (const QString &mark : owner->getMarkNames())
			if (mark.startsWith("&cangjia+")) room->setPlayerMark(owner, mark, 0);
		if (!display.isEmpty()) room->setPlayerMark(owner, "&cangjia+" + display.join("+"), 1);
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == CardsMoveOneTime) {
			const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			return move.to == player && move.to != move.from && move.to_place == Player::PlaceHand
				&& move.reason.m_skillName != "InitialHandCards" && player->getPhase() != Player::Play
				? TriggerList{{player, {objectName()}}} : TriggerList();
		}
		const CardUseStruct use = data.value<CardUseStruct>();
		TriggerList result;
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.from || use.from == player) return result;
		for (int id : player->getSkillInstanceIds(objectName()))
			if (!suits(player, id).contains(use.card->getSuitString()))
				result[player] << SkillInstanceUtils::formatName(objectName(), id);
		return result;
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == TargetConfirming) ctx.targets = {ctx.original_data->value<CardUseStruct>().from};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != CardsMoveOneTime) return false;
		QStringList values = suits(ctx.owner, ctx.sourceRef.key.instanceID);
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		bool changed = false;
		for (int id : move.card_ids) {
			const QString suit = Sanguosha->getCard(id)->getSuitString();
			if (!values.contains(suit)) { values << suit; changed = true; }
		}
		if (changed) {
			setSuits(room, ctx.owner, ctx.sourceRef.key.instanceID, values);
			room->sendCompulsoryTriggerLog(ctx.owner, this);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->sendCompulsoryTriggerLog(ctx.owner, this);
		if (!room->askForCard(target, "..", "cangjia0:" + ctx.owner->objectName(), *ctx.original_data)) {
			CardUseStruct use = ctx.original_data->value<CardUseStruct>();
			if (!use.nullified_list.contains(ctx.owner->objectName())) use.nullified_list << ctx.owner->objectName();
			ctx.original_data->setValue(use);
		}
		return false;
	}
};

class CangjiaLimit : public CardLimitSkill
{
public:
	CangjiaLimit() : CardLimitSkill("#CangjiaLimit") {}
	QString limitList(const Player *) const override { return "use"; }
	QString limitReason(const Player *, const Card *) const override { return "cangjia"; }
	QString limitPattern(const Player *target, const Card *card) const override
	{
		if (!target || !card || target->getPhase() != Player::Play) return QString();
		// Every effective source imposes its own restriction; display marks are not authority.
		for (int id : target->getSkillInstanceIds("cangjia"))
			if (target->isSkillInstanceEffectAvailable("cangjia", id)
				&& !target->getSkillInstanceCorrectStateValue("cangjia", id, "suits").toStringList().contains(card->getSuitString()))
				return card->toString();
		return QString();
	}
};
class Duohui : public TriggerSkillV2
{
public:
	Duohui() : TriggerSkillV2("duohui") { events << EventPhaseStart; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || player->getPhase() != Player::Start || player->getCardCount() == 0)
			return result;
		for (ServerPlayer *holder : room->getOtherPlayers(player))
			if (holder->hasSkill(objectName())) result[holder] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		if (!player || player->isDead() || ctx.owner->isDead()) return false;
		const Card *card = room->askForCard(player, "..", "duohui0:" + ctx.owner->objectName(),
			QVariant::fromValue(ctx.owner), Card::MethodNone);
		if (!card) return false;
		// Only immutable identities cross the choice/payment boundary.
		ctx.extra_data = QVariantMap{{"id", card->getEffectiveId()}, {"card", card->toString()},
			{"suit", card->getSuitString()}};
		ctx.targets = {player};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		const int id = ctx.extra_data.toMap().value("id", -1).toInt();
		if (id < 0 || !player || room->getCardOwner(id) != player || ctx.owner->isDead()) return false;
		player->skillInvoked(objectName(), -1, ctx.owner);
		room->giveCard(player, ctx.owner, Sanguosha->getCard(id), objectName());
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.owner->isDead()) return false;
		const QVariantMap gift = ctx.extra_data.toMap();
		const Card *returned = room->askForCard(ctx.owner,
			"^" + gift.value("card").toString() + "|" + gift.value("suit").toString(),
			"duohui1:" + target->objectName(), QVariant::fromValue(target), Card::MethodNone);
		if (returned) room->giveCard(ctx.owner, target, returned, objectName());
		else target->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};
YueyuanCard::YueyuanCard() { target_fixed = true; setSkillName("yueyuan"); }

void YueyuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class Yueyuan : public ViewAsSkillV2
{
public:
	Yueyuan() : ViewAsSkillV2("yueyuan") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	QString historyKey(const ActiveSkillRequest &) const override { return "YueyuanCard"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
		for (int id : request.initiator->getSkillInstanceIds("cangjia"))
			if (!Cangjia::suits(request.initiator, id).isEmpty()) return true;
		return false;
	}
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		QStringList sources;
		for (int id : ctx.invoker->getSkillInstanceIds("cangjia"))
			if (!Cangjia::suits(ctx.invoker, id).isEmpty()) sources << SkillInstanceUtils::formatName("cangjia", id);
		if (sources.isEmpty()) return false;
		const QString selected = sources.size() == 1 ? sources.first()
			: room->askForChoice(ctx.invoker, objectName(), sources.join("+"));
		if (!sources.contains(selected)) return false;
		ctx.extra_data = SkillInstanceUtils::parseInstanceId(selected);
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		const int id = ctx.extra_data.toInt();
		QStringList values = Cangjia::suits(ctx.invoker, id);
		if (values.isEmpty()) return FinishSkill;
		ctx.invoker->drawCards(values.size() * getEffectiveAmount(ctx), objectName());
		if (!ctx.invoker->isAlive() || !ctx.invoker->hasSkillInstance("cangjia", id)) return FinishSkill;
		// Re-read after drawing: nested effects may have changed this exact record.
		values = Cangjia::suits(ctx.invoker, id);
		if (values.isEmpty()) return FinishSkill;
		Room *room = ctx.invoker->getRoom();
		const QString choice = room->askForChoice(ctx.invoker, objectName(), values.join("+"));
		if (values.removeOne(choice)) Cangjia::setSuits(room, ctx.invoker, id, values);
		return ContinueEffects;
	}
};
class Fuyu : public TriggerSkillV2
{
public:
	Fuyu() : TriggerSkillV2("fuyu")
	{
		events << TargetConfirmed << TargetSpecified << EventSkillInvoking;
		m_baseAmount = 2;
	}
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Custom; }
	QString usageKey(const SkillContext &ctx) const
	{
		const QString saved = ctx.extra_data.toMap().value("usage_key").toString();
		return !saved.isEmpty() ? saved
			: (ctx.current_event == TargetSpecified ? "outgoing_turn" : "incoming_turn");
	}
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner || !getUsageRef(ctx).isValid()) return false;
		const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
		return !turn.isEmpty() && turn != "0"
			&& ctx.owner->getSkillInstanceStateValue(getUsageRef(ctx).key.skillName,
				getUsageRef(ctx).key.instanceID, usageKey(ctx)).toString() != turn;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		ctx.owner->setSkillInstanceStateValue(getUsageRef(ctx).key.skillName, getUsageRef(ctx).key.instanceID,
			usageKey(ctx), ctx.owner->getRoom()->historyScopes().value("turn_id"));
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
			|| (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) || use.to.size() != 1)
			return {};
		ServerPlayer *other = event == TargetSpecified ? use.to.first() : use.from;
		if (!other || other == player || (event == TargetConfirmed && !use.to.contains(player))
			|| !player->canPindian(other)) return {};
		return TriggerList{{player, {objectName()}}};
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.extra_data = QVariantMap{{"usage_key", event == TargetSpecified ? "outgoing_turn" : "incoming_turn"}};
		if (!checkCustomUsage(ctx)) return false;
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		ServerPlayer *other = event == TargetSpecified ? use.to.first() : use.from;
		if (!other || !ctx.owner->canPindian(other)
			|| !ctx.owner->askForSkillInvoke(objectName() + "$-1", other)) return false;
		ctx.targets = {other};
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		// Reserve the branch before pindian emits nested trigger events.
		if (!checkCustomUsage(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	QVariant previousResult(Room *room, const SkillContext &ctx) const
	{
		QVariant result;
		QVariantMap query{{"kind", "pindian_result"}, {"from", ctx.owner->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) { qWarning("Fuyu: incomplete previous pindian history"); return {}; }
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
				if (value.value("reason").toString() != objectName()) continue;
				const QVariantMap skill = room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("data").toMap();
				if (!skill.value("attribution_complete").toBool()) return {};
				if (skill.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
					&& skill.value("activation_skill").toString() == ctx.activationRef.key.skillName
					&& skill.value("activation_instance_id").toInt() == ctx.activationRef.key.instanceID) result = value.value("success");
			}
			if (!page.value("has_more").toBool()) return result;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ServerPlayer *other = ctx.targets.value(0);
		if (!other) return false;
		skillEffect(event, room, player, ctx, other);
		if (ctx.extra_data.toMap().value("draw").toBool() && player->isAlive()) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			ctx.choice = "draw";
			skillEffect(event, room, player, ctx, player);
		}
		return false;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
		const QVariant previous = previousResult(room, ctx);
		if (!ctx.owner->canPindian(target)) return false;
		const int result = ctx.owner->pindianInt(target, objectName());
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		const bool userWon = event == TargetSpecified ? result > 0 : result < 0;
		if (userWon) ++use.extra_use;
		else if (!use.nullified_list.contains("_ALL_TARGETS")) use.nullified_list << "_ALL_TARGETS";
		ctx.original_data->setValue(use);
		QVariantMap saved = ctx.extra_data.toMap();
		saved.insert("draw", previous.isValid() && previous.toBool() == (result > 0));
		ctx.extra_data = saved;
		return false;
	}
};
ZhanshiCard::ZhanshiCard() { setSkillName("zhanshi"); }

bool ZhanshiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return ActiveSkillCard::targetFilter(targets, to_select, Self);
}

void ZhanshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class Zhanshivs : public ViewAsSkillV2
{
public:
	Zhanshivs() : ViewAsSkillV2("zhanshi") { response_pattern = "@@zhanshi"; setN(2); }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zhanshi"
			&& request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName, request.activationRef.key.instanceID, "pindian").toMap().value("id").toLongLong() > 0;
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.length() < 2 && !request.selectedCardIds.contains(card->getEffectiveId())
			&& (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()))
			&& !request.initiator->isJilei(card);
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return !request.selectedCardIds.isEmpty() && request.selectedCardIds.length() <= 2; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
	{
		if (!request.initiator || !target || !target->isAlive() || targets.contains(target) || targets.length() >= request.selectedCardIds.length()) return false;
		const QVariantMap pending = request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName, request.activationRef.key.instanceID, "pindian").toMap();
		return pending.value("participants").toStringList().contains(target->objectName());
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
	{
		return !targets.isEmpty() && targets.length() == request.selectedCardIds.length();
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "ZhanshiCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		ZhanshiCard *card = new ZhanshiCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		// Freeze the parent resolution before discarding can cause a nested pindian.
		ctx.extra_data = request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName, request.activationRef.key.instanceID, "pindian");
		return ctx.extra_data.toMap().value("id").toLongLong() > 0;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantList receipts = ctx.owner->getTag("mobile_zhanshi_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"pindian", ctx.extra_data.toMap().value("id")},
			{"predicted", target->objectName()}, {"amount", 3 * getEffectiveAmount(ctx)},
			{"token", QString::number(ctx.owner->getRoom()->currentHistoryEventId())}};
		ctx.owner->setTag("mobile_zhanshi_receipts", receipts);
		return ContinueEffects;
	}
};

class Zhanshi : public TriggerSkillV2
{
public:
	Zhanshi() : TriggerSkillV2("zhanshi") { events << PindianVerifying; view_as_skill = new Zhanshivs; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		TriggerList result;
		const PindianStruct *pindian = data.value<PindianStruct *>();
		if (!pindian || !pindian->from || !pindian->to) return result;
		for (ServerPlayer *owner : room->getAlivePlayers())
			if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "he")) result[owner] << objectName();
		return result;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
		const QVariant id = room->historyParent(room->currentHistoryEventId(), "pindian", true).value("id");
		if (!pindian || id.toLongLong() <= 0) { qWarning("Zhanshi: missing pindian resolution identity"); return false; }
		Room::AcceptedViewAsEffectScope selection(room, ctx.owner, objectName(), ctx);
		if (!selection.isValid()) return false;
		const SkillInstanceRef ref = selection.activationRef();
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pindian",
			QVariantMap{{"id", id}, {"participants", QStringList{pindian->from->objectName(), pindian->to->objectName()}}});
		room->askForUseCard(ctx.owner, "@@zhanshi", "zhanshi0", -1, Card::MethodUse, false);
		return false;
	}
};

class ZhanshiResult : public TriggerSkillV2
{
public:
	ZhanshiResult() : TriggerSkillV2("#zhanshi-result") { events << Pindian; global = true; frequency = Compulsory; }
	static QString winner(Room *room, bool &known)
	{
		const QVariant id = room->historyParent(room->currentHistoryEventId(), "pindian", true).value("id");
		const QVariantMap page = room->queryHistoryFacts(QVariantMap{{"kind", "pindian_result"}, {"event_id", id}});
		const QVariantList facts = page.value("items").toList();
		known = page.value("complete").toBool() && facts.length() == 1;
		if (!known) { qWarning("Zhanshi: incomplete pindian result history"); return QString(); }
		const QVariantMap result = facts.first().toMap().value("data").toMap();
		const int from = result.value("from_number").toInt(), to = result.value("to_number").toInt();
		return from == to ? QString() : result.value(from > to ? "from" : "to").toString();
	}
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		const QString id = room->historyParent(room->currentHistoryEventId(), "pindian", true).value("id").toString();
		bool known = false;
		const QString won = winner(room, known);
		if (!known) return true;
		for (ServerPlayer *owner : room->getAllPlayers(true)) {
			QVariantList keep;
			for (const QVariant &value : owner->getTag("mobile_zhanshi_receipts").toList()) {
				const QVariantMap receipt = value.toMap();
				if (receipt.value("pindian").toString() != id || (!won.isEmpty() && receipt.value("predicted").toString() == won)) keep << value;
			}
			owner->setTag("mobile_zhanshi_receipts", keep);
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		const QString id = room->historyParent(room->currentHistoryEventId(), "pindian", true).value("id").toString();
		bool known = false;
		const QString won = winner(room, known);
		if (!known) return true;
		if (id.toLongLong() <= 0 || won.isEmpty()) return true;
		for (ServerPlayer *owner : room->getAllPlayers(true)) {
			for (const QVariant &value : owner->getTag("mobile_zhanshi_receipts").toList()) {
				const QVariantMap receipt = value.toMap();
				if (receipt.value("pindian").toString() != id || receipt.value("predicted").toString() != won) continue;
				SkillContext ctx;
				ctx.skill_name = objectName();
				ctx.owner = owner;
				ctx.invoker = ctx.initiator = player;
				ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
				ctx.instanceID = ctx.sourceRef.key.instanceID;
				ctx.amount = receipt.value("amount").toInt();
				ctx.original_data = &data;
				ctx.current_event = event;
				ctx.extra_data = receipt;
				ctx.targets = {owner};
				contexts << ctx;
			}
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.owner->getTag("mobile_zhanshi_receipts").toList().contains(ctx.extra_data);
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		QVariantList receipts = ctx.owner->getTag("mobile_zhanshi_receipts").toList();
		receipts.removeOne(ctx.extra_data);
		ctx.owner->setTag("mobile_zhanshi_receipts", receipts);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		target->drawCards(getEffectiveAmount(ctx), "zhanshi");
		return false;
	}
};
mobileStarPackage::mobileStarPackage()
	: Package("mobile_star")
{
	General *xing_zhangliao = new General(this, "xing_zhangliao", "qun", 4);
	xing_zhangliao->addSkill(new XingWeifeng);
	xing_zhangliao->addSkill(new XingWeifengEffect);
	related_skills.insert("xingweifeng", "#xingweifeng-effect");

	General *xing_zhanghe = new General(this, "xing_zhanghe", "qun", 4);
	xing_zhanghe->addSkill(new XingZhilve);
	xing_zhanghe->addSkill(new MobileActiveQuota("xingzhilve"));
	related_skills.insert("xingzhilve", "#xingzhilve-quota");
	xing_zhanghe->addSkill(new SlashNoDistanceLimitSkill("xingzhilve"));
	related_skills.insert("xingzhilve", "#xingzhilve-slash-ndl");
	addMetaObject<XingZhilveCard>();
	addMetaObject<XingZhilveSlashCard>();

	General *xing_xuhuang = new General(this, "xing_xuhuang", "qun", 4);
	xing_xuhuang->addSkill(new XingZhiyan);
	xing_xuhuang->addSkill(new MobileActiveQuota("xingzhiyan"));
	related_skills.insert("xingzhiyan", "#xingzhiyan-quota");
	xing_xuhuang->addSkill(new XingZhiyanClear);
	related_skills.insert("xingzhiyan", "#xingzhiyan-clear");
	xing_xuhuang->addSkill(new XingZhiyanPro);
	related_skills.insert("xingzhiyan", "#xingzhiyan-pro");
	addMetaObject<XingZhiyanCard>();

	General *xing_ganning = new General(this, "xing_ganning", "qun", 4);
	xing_ganning->addSkill(new XingJinfan);
	xing_ganning->addSkill(new XingJinfanLose("xingjinfan"));
	xing_ganning->addSkill(new XingSheque);
	related_skills.insert("xingjinfan", "#xingjinfan-lose");
	addMetaObject<XingJinfanCard>();

	General *xing_weiyan = new General(this, "xing_weiyan", "qun", 4);
	xing_weiyan->addSkill(new Guli);
	xing_weiyan->addSkill(new Aoshi);
	xing_weiyan->addSkill(new AoshiBf);
	related_skills.insert("aoshi", "#aoshibf");

	General *xing_huangzhong = new General(this, "xing_huangzhong", "qun", 4);
	xing_huangzhong->addSkill(new Shidi);
	xing_huangzhong->addSkill(new ShidiDistance);
    related_skills.insert("shidi", "#shidi");
	xing_huangzhong->addSkill(new Yishihz);
	xing_huangzhong->addSkill(new Qishe);
	xing_huangzhong->addSkill(new QisheMax);
    related_skills.insert("qishe", "#qishe");

	General *xing_dongzuo = new General(this, "xing_dongzuo", "qun", 4,true,false,false,3);
	xing_dongzuo->addSkill(new Xiongjin);
	xing_dongzuo->addSkill(new XiongjinDiscard);
	related_skills.insert("xiongjin", "#xiongjin-discard");
	xing_dongzuo->addSkill(new Zhenbian);
    xing_dongzuo->addSkill(new ZhenbianMax);
    related_skills.insert("zhenbian", "#zhenbian-max");
	xing_dongzuo->addSkill(new Baoxi);
	xing_dongzuo->addSkill(new MobileActiveQuota("baoxi"));
	related_skills.insert("baoxi", "#baoxi-quota");

	General *xing_fazheng = new General(this, "xing_fazheng", "qun", 4);
	xing_fazheng->addSkill(new Cangjia);
	xing_fazheng->addSkill(new CangjiaLimit);
	related_skills.insert("cangjia", "#CangjiaLimit");
	xing_fazheng->addSkill(new Duohui);
	xing_fazheng->addSkill(new Yueyuan);
	addMetaObject<YueyuanCard>();

	General *xing_wanglang = new General(this, "xing_wanglang", "qun", 4);
	xing_wanglang->addSkill(new Fuyu);
	xing_wanglang->addSkill(new Zhanshi);
	xing_wanglang->addSkill(new ZhanshiResult);
	related_skills.insert("zhanshi", "#zhanshi-result");
	addMetaObject<ZhanshiCard>();





}
ADD_PACKAGE(mobileStar)



class MobileQizhou : public TriggerSkillV2
{
public:
	MobileQizhou() : TriggerSkillV2("mobileqizhou")
	{
		events << GameStart << CardsMoveOneTime << EventAcquireSkill;
		frequency = Compulsory;
		waked_skills = "nosyingzi,qixi,xuanfeng";
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == EventAcquireSkill) {
			SkillChangeStruct change;
			if (!change.tryParse(data) || change.skillName != objectName()) return {};
			return {{player, {SkillInstanceKey(objectName(), change.instanceID).toString()}}};
		}
		if (event == CardsMoveOneTime) {
			const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (!(move.from == player && move.from_places.contains(Player::PlaceEquip))
				&& !(move.to == player && move.to_place == Player::PlaceEquip)) return {};
		}
		return {{player, {objectName()}}};
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return true; }
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QStringList suits;
		for (const Card *card : target->getEquips())
			if (!suits.contains(card->getSuitString())) suits << card->getSuitString();
		const QStringList granted{"nosyingzi", "qixi", "xuanfeng"};
		bool changed = false;
		for (int threshold = 0; threshold < granted.length(); ++threshold) {
			QList<SkillInstanceRef> children;
			for (const SkillInstance &instance : target->getSkillInstances())
				if (instance.skillName == granted.at(threshold) && instance.parentRef == ctx.sourceRef)
					children << SkillInstanceRef(target->objectName(), instance.key());
			if (suits.length() > threshold) {
				if (children.isEmpty()) { room->attachSkillToPlayer(target, granted.at(threshold), ctx.sourceRef); changed = true; }
			} else {
				for (const SkillInstanceRef &child : children) { room->detachAttachedSkill(child); changed = true; }
			}
		}
		// Parent removal cascades only these attached instances, preserving unrelated copies.
		if (changed) room->sendCompulsoryTriggerLog(target, objectName(), true, true);
		return false;
	}
};
MobileShanxiCard::MobileShanxiCard()
{
	setSkillName("mobileshanxi");
	handling_method = Card::MethodDiscard;
}

void MobileShanxiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class MobileShanxiVS : public ViewAsSkillV2
{
public:
	MobileShanxiVS() : ViewAsSkillV2("mobileshanxi") { response_pattern = "@@mobileshanxi"; setN(1); }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@mobileshanxi";
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty() && card->isRed() && card->isKindOf("BasicCard")
			&& request.initiator->handCards().contains(card->getEffectiveId()) && !request.initiator->isJilei(card);
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.length() == 1; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		return target && target->isAlive() && target != request.initiator && selected.isEmpty() && !target->isNude();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "MobileShanxiCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		MobileShanxiCard *card = new MobileShanxiCard;
		card->addSubcards(request.selectedCardIds);
		card->setActiveSkill(this); card->setSkillName(objectName());
		return card;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		const int count = qMin(target->getCards("he").length(), qMax(0, ctx.owner->getHp()) * getEffectiveAmount(ctx));
		QList<int> cards;
		for (int i = 0; i < count && target->isAlive() && ctx.owner->isAlive(); ++i) {
			const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodNone, cards, i > 0);
			if (id < 0) break;
			if (room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip) && !cards.contains(id)) cards << id;
		}
		if (cards.isEmpty()) return ContinueEffects;
		QVariantList ids;
		for (int id : cards) ids << id;
		const quint64 serial = room->getTag("mobile_shanxi_serial").toULongLong() + 1;
		room->setTag("mobile_shanxi_serial", serial);
		QVariantList receipts = target->getTag("mobile_shanxi_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
			{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
			{"issuer", ctx.owner->objectName()}, {"cards", ids}, {"serial", serial}, {"turn", room->historyScopes().value("turn_id")}};
		target->setTag("mobile_shanxi_receipts", receipts);
		DummyCard material(cards);
		target->addToPile("mobileshanxi", &material, false);
		return ContinueEffects;
	}
};

class MobileShanxi : public TriggerSkillV2
{
public:
	MobileShanxi() : TriggerSkillV2("mobileshanxi") { events << EventPhaseStart; view_as_skill = new MobileShanxiVS; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play && player->canDiscard(player, "h")
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		Room::AcceptedViewAsEffectScope selection(room, ctx.owner, objectName(), ctx);
		if (!selection.isValid()) return false;
		room->askForUseCard(ctx.owner, "@@mobileshanxi", "@mobileshanxi", -1, Card::MethodDiscard);
		return false;
	}
};

class MobileShanxiGet : public TriggerSkillV2
{
public:
	MobileShanxiGet() : TriggerSkillV2("#mobileshanxi-get") { events << EventPhaseChanging << Death; frequency = Compulsory; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
			|| (event == Death && data.value<DeathStruct>().who != player)) return true;
		int ordinal = 0;
		for (ServerPlayer *target : room->getAllPlayers(true)) {
			QVariantList receipts = target->getTag("mobile_shanxi_receipts").toList();
			for (QVariant &entry : receipts) {
				QVariantMap receipt = entry.toMap();
				if (receipt.value("issuer").toString() != player->objectName()
					|| (event != Death && receipt.value("turn") != room->historyScopes().value("turn_id"))) continue;
				receipt["ordinal"] = ++ordinal; entry = receipt;
			}
			target->setTag("mobile_shanxi_receipts", receipts);
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (!player || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
			|| (event == Death && data.value<DeathStruct>().who != player)) return true;
		for (ServerPlayer *target : room->getAllPlayers(true)) {
			for (const QVariant &value : target->getTag("mobile_shanxi_receipts").toList()) {
				const QVariantMap receipt = value.toMap();
				if (receipt.value("issuer").toString() != player->objectName()
					|| (event != Death && receipt.value("turn") != room->historyScopes().value("turn_id"))) continue;
				SkillContext ctx;
				ctx.skill_name = objectName();
				ctx.owner = player;
				ctx.invoker = ctx.initiator = target;
				ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
				ctx.instanceID = ctx.sourceRef.key.instanceID;
				ctx.trigger_count = receipt.value("ordinal").toInt();
				ctx.preferredTarget = target; ctx.preferredTargetSeat = target->getSeat();
				ctx.original_data = &data;
				ctx.current_event = event;
				ctx.extra_data = receipt;
				ctx.targets = {target};
				contexts << ctx;
			}
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.invoker && ctx.invoker->getTag("mobile_shanxi_receipts").toList().contains(ctx.extra_data);
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		QVariantList receipts = ctx.invoker->getTag("mobile_shanxi_receipts").toList();
		receipts.removeOne(ctx.extra_data);
		ctx.invoker->setTag("mobile_shanxi_receipts", receipts);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QList<int> cards;
		const QList<int> remaining = target->getPile("mobileshanxi");
		for (const QVariant &id : ctx.extra_data.toMap().value("cards").toList())
			if (remaining.contains(id.toInt())) cards << id.toInt();
		// Only return this source's still-present material, never cards already moved elsewhere.
		if (!cards.isEmpty()) { DummyCard material(cards); room->obtainCard(target, &material, false); }
		return false;
	}
};
LuanzhanCard::LuanzhanCard()
{
	mute = true;
}

bool LuanzhanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	int n = Self->getMark("luanzhan_target_num-Clear");
	return targets.length() < n && to_select->hasFlag("luanzhan_canchoose");
}

void LuanzhanCard::onUse(Room *room, CardUseStruct &card_use) const
{
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "luanzhan_extratarget");
}

class LuanzhanVS : public ZeroCardViewAsSkill
{
public:
	LuanzhanVS() : ZeroCardViewAsSkill("luanzhan")
	{
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@luanzhan");
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern=="@@luanzhan1")
			return new ExtraCollateralCard;
		return new LuanzhanCard;
	}
};

class Luanzhan : public TriggerSkill
{
public:
	Luanzhan() : TriggerSkill("luanzhan")
	{
		events << TargetSpecified << CardUsed;
		view_as_skill = new LuanzhanVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (event == CardUsed) {
			int n = player->getMark("&luanzhanMark") + player->getMark("luanzhanMark");
			if (use.to.length()>=n) return false;
			if (!(use.card->isBlack() && use.card->isNDTrick())) return false;
			if(use.card->targetFixed()){
				bool canextra = false;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (use.to.contains(p) || player->isProhibited(p, use.card)) continue;
					room->setPlayerFlag(p, "luanzhan_canchoose");
					canextra = true;
				}
				if (!canextra) return false;
				player->setTag("luanzhanData", data);
				room->setPlayerMark(player, "luanzhan_target_num-Clear", n);
				if (!room->askForUseCard(player, "@@luanzhan", QString("@luanzhan:%1::%2").arg(use.card->objectName()).arg(n)))
					return false;
				LogMessage log;
				foreach(ServerPlayer *p, room->getAlivePlayers()) {
					room->setPlayerFlag(p, "-luanzhan_canchoose");
					if (p->hasFlag("luanzhan_extratarget")) {
						room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
						room->setPlayerFlag(p,"-luanzhan_extratarget");
						use.to.append(p);
						log.to << p;
					}
				}
				if (log.to.isEmpty()) return false;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "luanzhan";
				room->sendLog(log);
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
		} else {
			if (!use.card->isKindOf("Slash") && !(use.card->isBlack() && use.card->isNDTrick())) return false;
			int n = player->getMark("&luanzhanMark") + player->getMark("luanzhanMark");
			if (use.to.length() < n) {
				room->setPlayerMark(player, "&luanzhanMark", 0);
				room->setPlayerMark(player, "luanzhanMark", 0);
			}
		}
		return false;
	}
};

class LuanzhanTargetMod : public TargetModSkill
{
public:
	LuanzhanTargetMod() : TargetModSkill("#luanzhan-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getExtraTargetNum(const Player *from, const Card *card) const
	{
		if ((card->isKindOf("Slash") || (card->isBlack() && card->isNDTrick()))&&from->hasSkill("luanzhan"))
			return from->getMark("&luanzhanMark") + from->getMark("luanzhanMark");
		return 0;
	}
};


class NewFengpo : public TriggerSkillV2
{
public:
	NewFengpo() : TriggerSkillV2("newfengpo") { events << TargetSpecified; frequency = Frequent; }
	static qint64 useId(Room *room) { return room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong(); }
	static int firstUse(Room *room, ServerPlayer *owner)
	{
		const qint64 current = useId(room);
		if (current <= 0) return -1;
		QVariantMap filter{{"kind", "use_card"}, {"turn_id", room->historyScopes().value("turn_id")}, {"from", owner->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(filter);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap();
				const QVariantList classes = fact.value("data").toMap().value("card").toMap().value("classes").toList();
				if (classes.isEmpty()) return -1;
				if (classes.contains("Slash") || classes.contains("Duel")) return fact.value("event_id").toLongLong() == current ? 1 : 0;
			}
			if (!page.value("has_more").toBool()) return 0;
			filter.insert("after", page.value("next_after"));
			filter.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasFlag("CurrentPlayer")) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.to.length() != 1 || (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel"))) return {};
		const int first = firstUse(room, player);
		if (first < 0) qWarning("NewFengpo: incomplete turn use history");
		return first > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
		ctx.targets = ctx.original_data->value<CardUseStruct>().to;
		ctx.manual_effect = true;
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.targets.value(0);
		if (!target) return false;
		const int modified = ctx.modified_amount;
		const bool modifiedSet = ctx.modified_amount_set;
		ctx.choice = "inspect";
		skillEffect(event, room, ctx.owner, ctx, target);
		if (ctx.extra_data.toMap().value("choice").toString() == "drawCards" && ctx.owner->isAlive()) {
			ctx.choice = "draw"; ctx.modified_amount = modified; ctx.modified_amount_set = modifiedSet;
			skillEffect(event, room, ctx.owner, ctx, ctx.owner);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.choice == "draw") {
			target->drawCards(ctx.extra_data.toMap().value("diamonds").toInt() * getEffectiveAmount(ctx), objectName());
			return false;
		}
		room->broadcastSkillInvoke(objectName());
		int diamonds = 0;
		for (const Card *card : target->getHandcards()) if (card->getSuit() == Card::Diamond) ++diamonds;
		const QString choice = room->askForChoice(ctx.owner, objectName(), "drawCards+addDamage", *ctx.original_data);
		ctx.extra_data = QVariantMap{{"choice", choice}, {"diamonds", diamonds}};
		const int amount = diamonds * getEffectiveAmount(ctx);
		if (choice == "addDamage" && amount > 0) {
			const Card *card = ctx.original_data->value<CardUseStruct>().card;
			const quint64 serial = room->getTag("mobile_newfengpo_serial").toULongLong() + 1;
			room->setTag("mobile_newfengpo_serial", serial);
			QVariantList receipts = card->getTag("mobile_newfengpo_receipts").toList();
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
				{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
				{"issuer", ctx.owner->objectName()}, {"serial", serial}, {"use", QString::number(useId(room))}, {"amount", amount}};
			card->setTag("mobile_newfengpo_receipts", receipts);
		}
		return false;
	}
};

class NewFengpoEffect : public TriggerSkillV2
{
public:
	NewFengpoEffect() : TriggerSkillV2("#newfengpo-effect") { events << DamageCaused << CardFinished; frequency = Compulsory; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event == DamageCaused) {
			const Card *card = data.value<DamageStruct>().card;
			if (!card) return true;
			QVariantList receipts = card->getTag("mobile_newfengpo_receipts").toList();
			int ordinal = 0;
			for (QVariant &entry : receipts) {
				QVariantMap receipt = entry.toMap();
				if (receipt.value("use").toLongLong() == NewFengpo::useId(room)) { receipt["ordinal"] = ++ordinal; entry = receipt; }
			}
			card->setTag("mobile_newfengpo_receipts", receipts);
			return true;
		}
		if (event != CardFinished) return true;
		const Card *card = data.value<CardUseStruct>().card;
		if (!card) return true;
		QVariantList keep;
		for (const QVariant &receipt : card->getTag("mobile_newfengpo_receipts").toList())
			if (receipt.toMap().value("use").toLongLong() != NewFengpo::useId(room)) keep << receipt;
		card->setTag("mobile_newfengpo_receipts", keep);
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != DamageCaused) return true;
		const DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || !damage.to || !damage.to->isAlive()) return true;
		for (const QVariant &value : damage.card->getTag("mobile_newfengpo_receipts").toList()) {
			const QVariantMap receipt = value.toMap();
			if (receipt.value("use").toLongLong() != NewFengpo::useId(room)) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
			if (!ctx.owner) continue;
			ctx.invoker = ctx.initiator = damage.from;
			ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.trigger_count = receipt.value("ordinal").toInt();
			ctx.preferredTarget = damage.to; ctx.preferredTargetSeat = damage.to->getSeat();
			ctx.amount = receipt.value("amount").toInt();
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.extra_data = receipt;
			ctx.targets = {damage.to};
			contexts << ctx;
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.targets.value(0); }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		const Card *card = ctx.original_data ? ctx.original_data->value<DamageStruct>().card : nullptr;
		return ctx.owner && card && card->getTag("mobile_newfengpo_receipts").toList().contains(ctx.extra_data);
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		// A receipt belongs to one use event, so reuse of a physical card cannot inherit the bonus.
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		damage.damage += getEffectiveAmount(ctx);
		ctx.original_data->setValue(damage);
		return false;
	}
};
FumanCard::FumanCard()
{
	setSkillName("fuman");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool FumanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return ActiveSkillCard::targetFilter(targets, to_select, Self);
}

void FumanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

static QString fumanPhaseId(const Room *room)
{
	if (!room) return QString();
	const QString phase = room->historyScopes().value("phase_id").toString();
	return phase.isEmpty() || phase == "0" ? QString() : phase;
}

static QStringList fumanPhaseTargets(const Player *owner, const SkillInstanceRef &ref, const QString &phase)
{
	if (!owner || !ref.isValid() || phase.isEmpty()) return QStringList();
	return owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase_targets").toMap().value(phase).toStringList();
}

class FumanVS : public ViewAsSkillV2
{
public:
	FumanVS() : ViewAsSkillV2("fuman") { setN(1); }
	LimitScope getLimitScope() const override { return Limit_Custom; }
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner) return false;
		const QString phase = fumanPhaseId(ctx.owner->getRoom());
		if (phase.isEmpty()) return false;
		const QStringList used = fumanPhaseTargets(ctx.owner, getUsageRef(ctx), phase);
		for (const Player *target : ctx.owner->getAliveSiblings()) if (!used.contains(target->objectName())) return true;
		return false;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner) return;
		const SkillInstanceRef ref = getUsageRef(ctx);
		const QString phase = fumanPhaseId(ctx.owner->getRoom());
		if (phase.isEmpty()) return;
		QVariantMap phases = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase_targets").toMap();
		QStringList used = phases.value(phase).toStringList();
		for (ServerPlayer *target : ctx.targets) if (target && !used.contains(target->objectName())) used << target->objectName();
		phases.insert(phase, used);
		// "targets" is the owner-visible projection of this phase only. Nested play phases keep their own keys.
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "phase_targets", phases);
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "targets", used);
		ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "active_phase", phase);
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty() && card->isKindOf("Slash")
			&& request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.length() == 1; }
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		if (!request.initiator || !target || !target->isAlive() || !selected.isEmpty() || target == request.initiator) return false;
		QStringList used;
		if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator)) {
			const QString phase = fumanPhaseId(server->getRoom());
			if (phase.isEmpty()) return false;
			used = fumanPhaseTargets(server, request.activationRef, phase);
		} else {
			used = request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
				request.activationRef.key.instanceID, "targets").toStringList();
		}
		return !used.contains(target->objectName());
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "FumanCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		FumanCard *card = new FumanCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		ctx.extra_data = request.selectedTargetNames;
		return request.selectedTargetNames.length() == 1;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.length() != 1 || request.selectedTargetNames.length() != 1) return false;
		const int id = request.selectedCardIds.first();
		ServerPlayer *target = room->findPlayerByObjectName(request.selectedTargetNames.first());
		if (!target || !canSelectTarget(request, {}, target) || room->getCardOwner(id) != ctx.owner
			|| room->getCardPlace(id) != Player::PlaceHand || !Sanguosha->getCard(id)->isKindOf("Slash")) return false;
		addUsage(ctx);
		return true;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		const int id = ctx.use_card->getSubcards().value(0, -1);
		if (id < 0 || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
		QVariantList receipts = target->getTag("mobile_fuman_receipts").toList();
		// The gift's draw belongs to the giver. The deadline is the recipient's next turn, not this play phase.
		receipts << QVariantMap{{"giver", ctx.owner->objectName()}, {"owner", ctx.owner->objectName()},
			{"source_owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"card", id}, {"amount", getEffectiveAmount(ctx)},
			{"beneficiary", target->objectName()}, {"granted_turn", room->historyScopes().value("turn_id")},
			{"armed", false}, {"token", QString::number(room->currentHistoryEventId())}};
		target->setTag("mobile_fuman_receipts", receipts);
		room->giveCard(ctx.owner, target, Sanguosha->getCard(id), objectName());
		return ContinueEffects;
	}
};

class Fuman : public TriggerSkillV2
{
public:
	Fuman() : TriggerSkillV2("fuman")
	{ events << CardUsed << EventPhaseChanging << EventPhaseEnd << TurnStart << Death; global = true; view_as_skill = new FumanVS; }
	static void projectQuotas(Room *room, ServerPlayer *player, TriggerEvent event)
	{
		QString projectId = fumanPhaseId(room);
		if (event == EventPhaseEnd && !projectId.isEmpty()) {
			const QVariantMap current = room->historyEvent(projectId.toLongLong());
			const QVariantMap parent = room->historyEvent(current.value("parent_id").toLongLong());
			// Ending a nested play phase resumes the still-active outer play phase, whose quota must stay.
			if (parent.value("kind").toString() == "phase" && parent.value("status").toString() == "active"
				&& parent.value("data").toMap().value("player").toString() == player->objectName()
				&& parent.value("data").toMap().value("phase").toInt() == int(Player::Play))
				projectId = parent.value("id").toString();
		}
		for (int id : player->getSkillInstanceIds("fuman")) {
			QVariantMap phases = player->getSkillInstanceStateValue("fuman", id, "phase_targets").toMap();
			QVariantMap kept;
			for (auto it = phases.cbegin(); it != phases.cend(); ++it) {
				const QVariantMap phase = room->historyEvent(it.key().toLongLong());
				if (phase.value("status").toString() == "active") kept.insert(it.key(), it.value());
			}
			player->setSkillInstanceStateValue("fuman", id, "phase_targets", kept);
			player->setSkillInstanceStateValue("fuman", id, "targets", kept.value(projectId).toStringList());
			player->setSkillInstanceStateValue("fuman", id, "active_phase", projectId);
		}
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!room) return true;
		if ((event == EventPhaseChanging || event == EventPhaseEnd) && player) projectQuotas(room, player, event);
		if (event == TurnStart && player) {
			const QVariant turn = room->historyScopes().value("turn_id");
			if (turn.toLongLong() > 0) {
				QVariantList receipts;
				for (const QVariant &value : player->getTag("mobile_fuman_receipts").toList()) {
					QVariantMap receipt = value.toMap();
					// Arm on the recipient's next turn start. A gift during their current turn waits for the turn after.
					if (!receipt.value("armed").toBool() && receipt.value("granted_turn").toLongLong() > 0
						&& receipt.value("granted_turn") != turn) {
						receipt.insert("armed", true);
						receipt.insert("expire_turn", turn);
					}
					receipts << receipt;
				}
				player->setTag("mobile_fuman_receipts", receipts);
			}
		}
		if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive) {
			const QVariant turn = room->historyScopes().value("turn_id");
			QVariantList kept;
			for (const QVariant &value : player->getTag("mobile_fuman_receipts").toList()) {
				const QVariantMap receipt = value.toMap();
				const bool known = receipt.value("granted_turn").toLongLong() > 0 && turn.toLongLong() > 0;
				const bool due = known && receipt.value("armed").toBool() && receipt.value("expire_turn") == turn;
				if (known && !due) kept << receipt;
			}
			player->setTag("mobile_fuman_receipts", kept);
		}
		if (event == Death) {
			ServerPlayer *dead = data.value<DeathStruct>().who;
			if (dead) dead->removeTag("mobile_fuman_receipts");
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != CardUsed || !player) return true;
		const Card *card = data.value<CardUseStruct>().card;
		if (!card || card->isKindOf("SkillCard")) return true;
		const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
		for (const QVariant &value : player->getTag("mobile_fuman_receipts").toList()) {
			const QVariantMap receipt = value.toMap();
			if (!ids.contains(receipt.value("card").toInt())) continue;
			const QString beneficiary = receipt.value("beneficiary").toString();
			if (!beneficiary.isEmpty() && beneficiary != player->objectName()) continue;
			const QString giverName = receipt.value("giver").toString().isEmpty()
				? receipt.value("owner").toString() : receipt.value("giver").toString();
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(giverName, true);
			if (!ctx.owner) continue;
			ctx.invoker = ctx.initiator = player;
			const QString sourceOwner = receipt.value("source_owner").toString().isEmpty()
				? giverName : receipt.value("source_owner").toString();
			ctx.sourceRef = SkillInstanceRef(sourceOwner, SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.amount = receipt.value("amount").toInt();
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.extra_data = receipt;
			ctx.targets = {ctx.owner};
			contexts << ctx;
		}
		return true;
	}
	// The draw belongs to the giver. Order follows that player, not the recipient who used the gifted slash.
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.owner; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.invoker && ctx.invoker->getTag("mobile_fuman_receipts").toList().contains(ctx.extra_data);
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		// Consume before drawing: nested uses cannot redeem this gift twice.
		QVariantList receipts = ctx.invoker->getTag("mobile_fuman_receipts").toList();
		receipts.removeOne(ctx.extra_data);
		ctx.invoker->setTag("mobile_fuman_receipts", receipts);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target->isAlive()) return false;
		room->sendCompulsoryTriggerLog(target, objectName(), true, true);
		target->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};
class MobileFuhan : public PhaseChangeSkill
{
public:
	MobileFuhan() : PhaseChangeSkill("mobilefuhan")
	{
		frequency = Limited;
		limit_mark = "@mobilefuhanMark";
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::RoundStart) return false;
		if (player->getMark("@mobilefuhanMark") <= 0) return false;

		int nn = player->getMark("meiying") + player->getMark("&meiying");
		if (nn <= 0) return false;
		nn = qMin(room->getPlayers().length(), nn);
		QString num = QString::number(nn);
		if (!player->askForSkillInvoke("mobilefuhan", QString("mobilefuhan_invoke:%1").arg(num))) return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "mobilefuhan");
		room->removePlayerMark(player, "@mobilefuhanMark");
		player->loseAllMarks("&meiying");

		QStringList five_shus, shus = Sanguosha->getLimitedGeneralNames("shu");
		foreach (QString name, shus) {
			if (hasshu(name, room))
				shus.removeOne(name);
		}
		for (int i = 1; i < 6; i++) {
			if (shus.isEmpty()) break;
			QString name = shus.at((qsanRandomBounded(shus.length())));
			five_shus << name;
			shus.removeOne(name);
		}
		if (five_shus.isEmpty()) return false;
		QString shu_general = room->askForGeneral(player, five_shus);
		room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "mobile_zhaoxiang" && player->getGeneral2Name() == "mobile_zhaoxiang"));
		int n = player->getMark("meiying");
		n = qMin(room->getPlayers().length(), n);
		room->setPlayerProperty(player, "maxhp", n);
		if (!player->isLowestHpPlayer()) return false;
		room->recover(player, RecoverStruct("mobilefuhan", player));
		return false;
	}

	bool hasshu(const QString name, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getGeneralName() == name || p->getGeneral2Name() == name)
				return true;
		}
		return false;
	}
};

MobileFuhaiCard::MobileFuhaiCard()
{
	setSkillName("mobilefuhai");
	target_fixed = true;
}

void MobileFuhaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class MobileFuhai : public ViewAsSkillV2
{
public:
	MobileFuhai() : ViewAsSkillV2("mobilefuhai") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "MobileFuhaiCard"; }
	const Card *createCard(const ActiveSkillRequest &) const override { return new MobileFuhaiCard; }
	EffectFlow effect(SkillContext &ctx) const override
	{
		Room *room = ctx.owner->getRoom();
		ctx.manual_effect = true;
		ctx.extra_data = QVariantList();
		ctx.choice = "choose";
		const int modifiedAmount = ctx.modified_amount;
		const bool hasModifiedAmount = ctx.modified_amount_set;
		const QList<ServerPlayer *> players = room->getOtherPlayers(ctx.owner);
		for (ServerPlayer *target : players) {
			if (ctx.owner->isDead()) return FinishSkill;
			ctx.modified_amount = modifiedAmount;
			ctx.modified_amount_set = hasModifiedAmount;
			if (target->isAlive()) skillEffect(ctx, target);
		}
		const QVariantList answers = ctx.extra_data.toList();
		QStringList choices;
		for (const QVariant &answer : answers) {
			const QVariantMap value = answer.toMap();
			ServerPlayer *target = room->findPlayerByObjectName(value.value("player").toString());
			if (!target || target->isDead()) continue;
			choices << value.value("choice").toString();
			LogMessage log;
			log.type = "#ShouxiChoice";
			log.from = target;
			log.arg = objectName() + ":" + choices.last();
			room->sendLog(log);
		}
		int count = choices.isEmpty() ? 0 : 1;
		while (count < choices.length() && choices.at(count) == choices.at(count - 1)) ++count;
		if (count > 1 && ctx.owner->isAlive()) {
			ctx.extra_data = count;
			ctx.choice = "draw";
			ctx.modified_amount = modifiedAmount;
			ctx.modified_amount_set = hasModifiedAmount;
			skillEffect(ctx, ctx.owner);
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.choice == "draw") target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
		else {
			const QString choice = target->getRoom()->askForChoice(target, objectName(), "up+down");
			QVariantList answers = ctx.extra_data.toList();
			// Bind each answer to its player: deaths during another prompt cannot shift seat answers.
			answers << QVariantMap{{"player", target->objectName()}, {"choice", choice}};
			ctx.extra_data = answers;
		}
		return ContinueEffects;
	}
};
JixuCard::JixuCard()
{
}

bool JixuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.isEmpty())
		return to_select != Self;
	else {
		int hp = targets.first()->getHp();
		return to_select != Self && to_select->getHp() == hp;
	}
}

void JixuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	bool has_slash = false;
	foreach (const Card *c, source->getCards("h")) {
		if (c->isKindOf("Slash")) {
			has_slash = true;
			break;
		}
	}

	QStringList choices;
	QList<ServerPlayer *> players;
	foreach (ServerPlayer *p, targets) {
		if (source->isDead()) return;
		if (p->isDead()) continue;
		QString choice = room->askForChoice(p, "jixu", "has+not", QVariant::fromValue(source));
		if (p->isAlive()) {
			choices << choice;
			players << p;
		}
	}
	if (choices.isEmpty() || players.isEmpty()) return;

	QList<ServerPlayer *> nots, hass;

	int i = -1;
	foreach (ServerPlayer *p, players) {
		i++;
		if (i > choices.length()) break;
		if (p->isDead()) continue;

		QString str = choices.at(i);
		QString result = "wrong";
		if ((str == "has" && has_slash) || (str == "not" && !has_slash))
			result = "correct";

		LogMessage log;
		log.type = "#JixuChoice";
		log.from = p;
		log.arg = "jixu:" + str;
		log.arg2 = "jixu:" + result;
		room->sendLog(log);
		if (str == "has")
			hass << p;
		else
			nots << p;
	}

	if (has_slash) {
		if (nots.isEmpty()) {
			LogMessage log;
			log.type = "#JixuStop";
			log.from = source;
			room->sendLog(log);

			source->endPlayPhase(false);
			return;
		}
		foreach (ServerPlayer *p, nots) {
			room->addPlayerMark(p, "jixu_choose_not" + source->objectName() + "-PlayClear");
			room->addPlayerMark(p, "&jixu_wrong-PlayClear");
		}
		source->drawCards(nots.length(), "jixu");
	} else {
		if (hass.isEmpty()) {
			LogMessage log;
			log.type = "#JixuStop";
			log.from = source;
			room->sendLog(log);

			source->endPlayPhase(false);
			return;
		}
		foreach (ServerPlayer *p, hass) {
			if (source->isDead()) return;
			if (p->isDead() || !source->canDiscard(p, "he")) continue;
			int id = room->askForCardChosen(source, p, "he", "jixu", false, Card::MethodDiscard);
			room->throwCard(id, p, source);
		}
		source->drawCards(hass.length(), "jixu");
	}
}

class JixuVS : public ZeroCardViewAsSkill
{
public:
	JixuVS() : ZeroCardViewAsSkill("jixu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("JixuCard");
	}

	const Card *viewAs() const
	{
		return new JixuCard;
	}
};

class Jixu : public TriggerSkill
{
public:
	Jixu() :TriggerSkill("jixu")
	{
		//events << TargetSpecified;
		events << CardUsed;
		view_as_skill = new JixuVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;

		LogMessage log;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->getMark("jixu_choose_not" + player->objectName() + "-PlayClear") > 0
					&& !use.to.contains(p) && player->canSlash(p, use.card, false)) {
				room->doAnimate(1, player->objectName(), p->objectName());
				use.to.append(p);
				log.to << p;
			}
		}

		if (!log.to.isEmpty()) {
			log.type = "#JixuSlash";
			log.from = player;
			log.arg = objectName();
			log.card_str = use.card->toString();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Chengzhao : public TriggerSkillV2
{
public:
	Chengzhao() : TriggerSkillV2("chengzhao") { events << EventPhaseStart; }
	static int gained(Room *room, ServerPlayer *owner)
	{
		QVariantMap filter{{"turn_id", room->historyScopes().value("turn_id")}, {"to", owner->objectName()}};
		int count = 0;
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(filter);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList())
				if (entry.toMap().value("data").toMap().value("to_place").toInt() == Player::PlaceHand && ++count >= 2) return count;
			if (!page.value("has_more").toBool()) return count;
			filter.insert("after", page.value("next_after"));
			filter.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
		for (ServerPlayer *owner : room->getAlivePlayers()) {
			if (!owner->hasSkill(objectName())) continue;
			const int count = gained(room, owner);
			if (count < 0) qWarning("Chengzhao: incomplete turn move history");
			else if (count >= 2) result[owner] << objectName();
		}
		return result;
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> candidates;
		for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) if (ctx.owner->canPindian(target)) candidates << target;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@chengzhao-invoke", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!ctx.owner->canPindian(target)) return false;
		room->broadcastSkillInvoke(objectName());
		if (!ctx.owner->pindian(target, objectName()) || ctx.owner->isDead() || target->isDead()) return false;
		std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
		slash->setSkillName("_chengzhao");
		slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
		slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
		// Armor bypass travels with the ordinary card rather than a borrowed player flag.
		slash->setFlags("SlashIgnoreArmor");
		if (ctx.owner->canSlash(target, slash.get(), false)) room->useCardFromSkillEffect(CardUseStruct(slash.get(), ctx.owner, target), ctx, true);
		return false;
	}
};
class Xiefang : public DistanceSkillV2
{
public:
	Xiefang() : DistanceSkillV2("xiefang") {}
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.holder) return CorrectSkillResult::noEffect();
		int women = 0;
		for (const Player *player : ctx.holder->getAliveSiblings(true))
			if (player->isFemale()) ++women;
		return CorrectSkillResult::useAmount(-women * ctx.currentAmount);
	}
};
class Zhengnan : public TriggerSkillV2
{
public:
	Zhengnan() : TriggerSkillV2("zhengnan")
	{
		events << Death;
		frequency = Frequent;
		waked_skills = "wusheng,dangxian,zhiman";
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		// Death is dispatched to each survivor; each dispatch belongs to that holder only.
		return player && player->isAlive() && player != data.value<DeathStruct>().who && player->hasSkill(objectName())
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
		QStringList choices{"draw"};
		for (const QString &name : QStringList{"wusheng", "dangxian", "zhiman"})
			if (!ctx.owner->hasSkill(name, true)) choices << name;
		ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
		ctx.targets = {ctx.owner};
		return choices.contains(ctx.choice);
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		if (ctx.choice == "draw") target->drawCards(3 * getEffectiveAmount(ctx), objectName());
		// This is a permanent reward; losing Zhengnan must not revoke it.
		else if (!target->hasSkill(ctx.choice, true)) room->acquireSkill(target, ctx.choice);
		return false;
	}
};
class Duoduan : public TriggerSkillV2
{
public:
	Duoduan() : TriggerSkillV2("duoduan") { events << TargetConfirmed << EventSkillInvoking; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Turn; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasTurn() || player->isNude()) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return use.card && use.card->isKindOf("Slash") && use.to.contains(player) && use.from
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		const Card *card = room->askForCard(ctx.owner, "..", "@duoduan-card", *ctx.original_data,
			Card::MethodNone, nullptr, false, objectName());
		if (!card || card->getEffectiveId() < 0 || ctx.owner->isCardLimited(card, Card::MethodRecast)) return false;
		ctx.extra_data = card->getEffectiveId();
		ctx.targets = {ctx.original_data->value<CardUseStruct>().from};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const int id = ctx.extra_data.toInt();
		const Card *card = Sanguosha->getCard(id);
		if (!isUsable(ctx) || room->getCardOwner(id) != ctx.owner
			|| (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
			|| ctx.owner->isCardLimited(card, Card::MethodRecast)) return false;
		// Commit the exact source quota before the recast can trigger nested card moves.
		addUsage(ctx);
		LogMessage log;
		log.type = "$DuoduanRecast";
		log.from = ctx.owner;
		log.arg = objectName();
		log.card_str = card->toString();
		room->sendLog(log);
		CardMoveReason reason(CardMoveReason::S_REASON_RECAST, ctx.owner->objectName());
		reason.m_skillName = objectName();
		room->moveCardTo(card, ctx.owner, nullptr, Player::DiscardPile, reason);
		return true;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		if (ctx.owner->isAlive()) ctx.owner->drawCards(getEffectiveAmount(ctx), "recast");
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const bool discarded = target->canDiscard(target, "he")
			&& room->askForDiscard(target, objectName(), 1, 1, true, true, "@duoduan-discard");
		if (!discarded) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		QStringList &affected = discarded ? use.no_respond_list : use.nullified_list;
		if (!affected.contains("_ALL_TARGETS")) affected << "_ALL_TARGETS";
		ctx.original_data->setValue(use);
		return false;
	}
};
GongsunCard::GongsunCard()
{
	handling_method = Card::MethodDiscard;
}

void GongsunCard::onEffect(CardEffectStruct &effect) const
{
	QList<int> ids;
	QStringList names;
	for (int id = 0; id < Sanguosha->getCardCount(); id++) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (c->isKindOf("DelayedTrick") || c->isKindOf("EquipCard")) continue;
		if (c->isKindOf("Slash") && c->objectName() != "slash") continue;
		if (names.contains(c->objectName())||Config.BanPackages.contains(c->getPackage())) continue;
		names << c->objectName();
		ids << id;
	}
	if (ids.isEmpty()) return;

	ServerPlayer *player = effect.from, *target = effect.to;
	Room *room = player->getRoom();

	room->fillAG(ids, player);
	int id = room->askForAG(player, ids, false, objectName());
	room->clearAG(player);

	LogMessage log;
	log.type = "#GongsunLimit";
	log.from = player;
	log.to << target;
	log.arg = Sanguosha->getEngineCard(id)->objectName();
	room->sendLog(log);

	names = player->getTag("GongsunLimited" + target->objectName()).toStringList();
	names << Sanguosha->getEngineCard(id)->getClassName();
	player->setTag("GongsunLimited" + target->objectName(), names);
	room->setPlayerCardLimitation(player, "use,response,discard", names.last()+"|.|.|hand", false);
	room->setPlayerCardLimitation(target, "use,response,discard", names.last()+"|.|.|hand", false);
	room->setPlayerMark(player, "&gongsun+" + log.arg,1);
	room->setPlayerMark(target, "&gongsun+" + log.arg,1);
}

class GongsunVS : public ViewAsSkill
{
public:
	GongsunVS() : ViewAsSkill("gongsun")
	{
		response_pattern = "@@gongsun";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return !Self->isJilei(to_select) && selected.length() < 2;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 2)
			return nullptr;

		GongsunCard *c = new GongsunCard;
		c->addSubcards(cards);
		return c;
	}
};

class Gongsun : public TriggerSkill
{
public:
	Gongsun() : TriggerSkill("gongsun")
	{
		events << EventPhaseStart << EventPhaseChanging << Death;
		view_as_skill = new GongsunVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return false;
			if (player->canDiscard(player, "he"))
				room->askForUseCard(player, "@@gongsun", "@gongsun");
		} else {
			if (event == EventPhaseChanging) {
				if (data.value<PhaseChangeStruct>().to != Player::RoundStart) return false;
			} else {
				DeathStruct death = data.value<DeathStruct>();
				if (player != death.who) return false;
			}
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				QStringList limit_names = player->getTag("GongsunLimited" + p->objectName()).toStringList();
				if (limit_names.isEmpty()) continue;
				player->removeTag("GongsunLimited" + p->objectName());
				foreach (QString classname, limit_names) {
					room->removePlayerCardLimitation(player, "use,response,discard", classname + "|.|.|hand");
					room->removePlayerCardLimitation(p, "use,response,discard", classname + "|.|.|hand");
					for (int id = 0; id < Sanguosha->getCardCount(); id++) {
						const Card *c = Sanguosha->getEngineCard(id);
						if (c->getClassName() == classname) {
							room->removePlayerMark(player, "&gongsun+" + c->objectName());
							room->removePlayerMark(p, "&gongsun+" + c->objectName());
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class Andong : public TriggerSkillV2
{
public:
	Andong() : TriggerSkillV2("andong") { events << DamageInflicted; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && damage.from
			&& damage.from->isAlive() && damage.from != player ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *source = ctx.original_data->value<DamageStruct>().from;
		if (!ctx.owner->askForSkillInvoke(this, QVariant::fromValue(source))) return false;
		ctx.targets = {source};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		const QString choice = room->askForChoice(target, objectName(), "prevent+get", *ctx.original_data);
		LogMessage log;
		log.type = "#FumianFirstChoice";
		log.from = target;
		log.arg = "andong:" + choice;
		room->sendLog(log);
		if (choice == "prevent") {
			QVariantList receipts = target->getTag("mobile_andong_receipts").toList();
			receipts << QVariantMap{{"owner", ctx.owner->objectName()}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"turn", room->historyScopes().value("turn_id")}};
			target->setTag("mobile_andong_receipts", receipts);
			room->setPlayerMark(target, "andong_heart-Clear", 1);
			return true;
		}
		room->doGongxin(ctx.owner, target, QList<int>(), objectName());
		QList<int> hearts;
		for (int id : target->handCards())
			if (Sanguosha->getCard(id)->getSuit() == Card::Heart) hearts << id;
		if (!hearts.isEmpty() && ctx.owner->isAlive()) {
			DummyCard cards(hearts);
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, ctx.owner->objectName());
			room->obtainCard(ctx.owner, &cards, reason, false);
		}
		return false;
	}
};

class AndongIgnore : public TriggerSkillV2
{
public:
	AndongIgnore() : TriggerSkillV2("#andong-ignore")
	{
		events << EventPhaseProceeding << EventPhaseChanging;
		frequency = Compulsory;
		global = true;
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
			const QString turn = room->historyScopes().value("turn_id").toString();
			for (ServerPlayer *target : room->getAllPlayers(true)) {
				QVariantList keep;
				for (const QVariant &receipt : target->getTag("mobile_andong_receipts").toList())
					if (receipt.toMap().value("turn").toString() != turn) keep << receipt;
				target->setTag("mobile_andong_receipts", keep);
			}
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != EventPhaseProceeding || !player || !player->isAlive() || player->getPhase() != Player::Discard) return true;
		for (const QVariant &value : player->getTag("mobile_andong_receipts").toList()) {
			const QVariantMap receipt = value.toMap();
			if (receipt.value("turn").toString() != room->historyScopes().value("turn_id").toString()) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
			if (!ctx.owner) continue;
			ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.extra_data = receipt;
			ctx.targets = {player};
			contexts << ctx;
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.invoker && ctx.invoker->getTag("mobile_andong_receipts").toList().contains(ctx.extra_data);
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
	{
		// The accepted prevention grants this turn's exemption even after Andong is lost.
		QList<int> hearts;
		for (int id : target->handCards())
			if (Sanguosha->getCard(id)->getSuit() == Card::Heart) hearts << id;
		room->ignoreCards(target, hearts);
		return false;
	}
};
YingshiCard::YingshiCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void YingshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->setPlayerProperty(source, "yingshi_name", "");
	LogMessage log;
	log.type = "$KuangbiGet";
	log.from = source;
	log.arg = "yschou";
	log.card_str = QString::number(getEffectiveId());
	room->sendLog(log);
	room->obtainCard(source, this, true);
}

class YingshiVS : public OneCardViewAsSkill
{
public:
	YingshiVS() : OneCardViewAsSkill("yingshi")
	{
		expand_pile = "%yschou";
		response_pattern = "@@yingshi";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (selected.length() >= 2)
			return false;
		QString name = Self->property("yingshi_name").toString();
		foreach (const Player *p, Self->getAliveSiblings(true)) {
			if (p->objectName() == name)
				return p->getPile("yschou").contains(to_select->getId());
		}
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		YingshiCard *card = new YingshiCard;
		card->addSubcard(originalCard);
		return card;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}
};

class Yingshi : public TriggerSkill
{
public:
	Yingshi() : TriggerSkill("yingshi")
	{
		events << EventPhaseStart << Damage;
		view_as_skill = new YingshiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Play || !player->hasSkill(objectName())) return false;
			bool has_chou = false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (!p->getPile("yschou").isEmpty()) {
					has_chou = true;
					break;
				}
			}
			if (has_chou) return false;
			QList<int> hearts;
			foreach (const Card *c, player->getCards("he")) {
				if (c->getSuit() == Card::Heart)
					hearts << c->getEffectiveId();
			}
			if (hearts.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@yingshi-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			target->addToPile("yschou", hearts);
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			if (damage.to->isDead() || damage.to->getPile("yschou").isEmpty()) return false;
			room->setPlayerProperty(player, "yingshi_name", damage.to->objectName());
			if (!room->askForUseCard(player, "@@yingshi", "@yingshi:" + damage.to->objectName()))
				room->setPlayerProperty(player, "yingshi_name", "");
		}
		return false;
	}
};

class YingshiDeath : public TriggerSkill
{
public:
	YingshiDeath() : TriggerSkill("#yingshi-death")
	{
		events << Death;
		view_as_skill = new YingshiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who == player || death.who->getPile("yschou").isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, "yingshi", true, true);
		DummyCard get(death.who->getPile("yschou"));
		room->obtainCard(player, &get, true);
		return false;
	}
};

QinguoCard::QinguoCard() { setSkillName("qinguo"); mute = true; }

bool QinguoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
	slash->setSkillName("qinguo");
	return slash->targetFilter(targets, to_select, Self);
}

void QinguoCard::onUse(Room *room, CardUseStruct &use) const
{
	ActiveSkillCard::onUse(room, use);
}

class QinguoVS : public ViewAsSkillV2
{
public:
	QinguoVS() : ViewAsSkillV2("qinguo") { response_pattern = "@@qinguo"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@qinguo";
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("qinguo");
		return slash;
	}
};

class Qinguo : public TriggerSkillV2
{
public:
	Qinguo() : TriggerSkillV2("qinguo") { events << CardFinished << CardsMoveOneTime; view_as_skill = new QinguoVS; }
	static int equipmentDelta(Room *room, ServerPlayer *owner)
	{
		const qint64 event = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
		if (event <= 0) return INT_MIN;
		QVariantMap filter{{"event_id", QString::number(event)}};
		int delta = 0;
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(filter);
			if (!page.value("complete").toBool()) return INT_MIN;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap move = entry.toMap().value("data").toMap();
				if (move.value("from").toString() == owner->objectName() && move.value("from_place").toInt() == Player::PlaceEquip) --delta;
				if (move.value("to").toString() == owner->objectName() && move.value("to_place").toInt() == Player::PlaceEquip) ++delta;
			}
			if (!page.value("has_more").toBool()) return delta;
			filter.insert("after", page.value("next_after"));
			filter.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == CardFinished) {
			const Card *card = data.value<CardUseStruct>().card;
			return card && card->isKindOf("EquipCard") && player->hasFlag("CurrentPlayer") ? TriggerList{{player, {objectName()}}} : TriggerList();
		}
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!(move.to == player && move.to_place == Player::PlaceEquip)
			&& !(move.from == player && move.from_places.contains(Player::PlaceEquip))) return {};
		if (!player->isWounded() || player->getEquips().length() != player->getHp()) return {};
		const int delta = equipmentDelta(room, player);
		if (delta == INT_MIN) { qWarning("Qinguo: incomplete equipment move history"); return {}; }
		return delta != 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == CardsMoveOneTime) ctx.targets = {ctx.owner};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != CardFinished) return false;
		std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
		slash->setSkillName(objectName());
		for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) {
			if (!ctx.owner->canSlash(target, slash.get(), true)) continue;
			Room::AcceptedViewAsEffectScope selection(room, ctx.owner, objectName(), ctx);
			if (!selection.isValid()) return false;
			room->askForUseCard(ctx.owner, "@@qinguo", "@qinguo", -1, Card::MethodUse, false);
			break;
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (target->isWounded() && target->getEquips().length() == target->getHp()) {
			room->sendCompulsoryTriggerLog(target, objectName(), true, true);
			room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
		}
		return false;
	}
};
KannanCard::KannanCard()
{
}

bool KannanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return Self->canPindian(to_select) && targets.isEmpty() && to_select->getMark("kannan_target-PlayClear") <= 0;
}

void KannanCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.from->canPindian(effect.to, false)) return;

	Room *room = effect.from->getRoom();
	int n = effect.from->pindianInt(effect.to, "kannan");
	if (n == 1) {
		room->addPlayerMark(effect.from, "kannan-PlayClear");
		room->addPlayerMark(effect.from, "&kannan");
	} else if (n == -1) {
		room->addPlayerMark(effect.to, "kannan_target-PlayClear");
		room->addPlayerMark(effect.to, "&kannan");
	}
}

class KannanVS : public ZeroCardViewAsSkill
{
public:
	KannanVS() : ZeroCardViewAsSkill("kannan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getMark("kannan-PlayClear") > 0) return false;
		if (player->usedTimes("KannanCard") >= player->getHp()) return false;
		foreach (const Player *p, player->getAliveSiblings()) {
			if (player->canPindian(p) && p->getMark("kannan_target-PlayClear") <= 0)
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new KannanCard;
	}
};

class Kannan : public TriggerSkill
{
public:
	Kannan() :TriggerSkill("kannan")
	{
		events << CardUsed << ConfirmDamage << CardFinished;
		view_as_skill = new KannanVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.from->isDead() || use.from->getMark("&kannan") <= 0) return false;
			int n = use.from->getMark("&kannan");
			room->setPlayerMark(use.from, "&kannan", 0);
			room->setTag("kannan_damage" + use.card->toString() + use.from->objectName(), n);
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card) return false;
			int n = room->getTag("kannan_damage" + damage.card->toString() + damage.from->objectName()).toInt();
			if (n <= 0 || damage.from->isDead() || damage.to->isDead()) return false;
			LogMessage log;
			log.type = "#KannanDamage";
			log.from = damage.from;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(damage.damage += n);
			log.to << damage.to;
			room->sendLog(log);
			data = QVariant::fromValue(damage);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			room->removeTag("kannan_damage" + use.card->toString() + use.from->objectName());
		}
		return false;
	}
};

class Zhaohuo : public TriggerSkillV2
{
public:
	Zhaohuo() : TriggerSkillV2("zhaohuo")
	{
		events << Dying;
		frequency = Compulsory;
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		// Dying is dispatched once per potential saver; only this dispatch's holder participates.
		ServerPlayer *dying = data.value<DyingStruct>().who;
		return player && player != dying && player->isAlive() && player->hasSkill(objectName()) && player->getMaxHp() != 1
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *owner = ctx.owner;
		const int difference = owner->getMaxHp() - 1;
		room->sendCompulsoryTriggerLog(owner, objectName(), true, true,
			qsanRandomBounded(2) + 1 + (owner->isJieGeneral() ? 2 : 0));
		if (difference < 0) room->gainMaxHp(owner, -difference, objectName());
		else if (difference > 0) {
			room->loseMaxHp(owner, difference, objectName());
			if (owner->isAlive()) owner->drawCards(difference * getEffectiveAmount(ctx), objectName());
		}
		return false;
	}
};

class Yixiang : public TriggerSkillV2
{
public:
	Yixiang() : TriggerSkillV2("yixiang") { events << TargetConfirmed << EventSkillInvoking; frequency = Frequent; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Turn; }
	QList<int> candidates(Room *room, ServerPlayer *owner) const
	{
		QList<int> result;
		for (int id : room->getDrawPile()) {
			const Card *candidate = Sanguosha->getCard(id);
			if (!candidate->isKindOf("BasicCard")) continue;
			bool owned = false;
			for (const Card *card : owner->getCards("he"))
				if (card->sameNameWith(candidate)) { owned = true; break; }
			if (!owned) result << id;
		}
		return result;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !room->hasCurrent()) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return use.card && !use.card->isKindOf("SkillCard") && use.from && use.from->isAlive()
			&& use.from->getHp() > player->getHp() && use.to.contains(player) && !candidates(room, player).isEmpty()
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return isUsable(ctx) && ctx.owner->askForSkillInvoke(this);
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive(); ++i) {
			const QList<int> ids = candidates(room, ctx.owner);
			if (ids.isEmpty()) break;
			room->obtainCard(ctx.owner, ids.at(qsanRandomBounded(ids.size())), false);
		}
		return false;
	}
};
class Yirang : public TriggerSkillV2
{
public:
	Yirang() : TriggerSkillV2("yirang") { events << EventPhaseStart; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
		bool material = false;
		for (const Card *card : player->getCards("he")) if (!card->isKindOf("BasicCard")) { material = true; break; }
		if (!material) return {};
		for (ServerPlayer *target : room->getOtherPlayers(player)) if (target->getMaxHp() > player->getMaxHp()) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> candidates;
		for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) if (target->getMaxHp() > ctx.owner->getMaxHp()) candidates << target;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@yirang-invoke", true, true);
		if (!target) return false;
		ctx.targets = {target};
		ctx.manual_effect = true;
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *recipient = ctx.targets.value(0);
		if (!recipient || recipient->isDead()) return false;
		const int modified = ctx.modified_amount;
		const bool hasModified = ctx.modified_amount_set;
		ctx.choice = "give";
		skillEffect(event, room, ctx.owner, ctx, recipient);
		if (ctx.extra_data.toMap().value("given").toBool() && ctx.owner->isAlive()) {
			ctx.choice = "recover";
			ctx.modified_amount = modified;
			ctx.modified_amount_set = hasModified;
			skillEffect(event, room, ctx.owner, ctx, ctx.owner);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.choice == "give") {
			QList<int> cards, types;
			for (const Card *card : ctx.owner->getCards("he")) {
				if (card->isKindOf("BasicCard")) continue;
				cards << card->getEffectiveId();
				if (!types.contains(card->getTypeId())) types << card->getTypeId();
			}
			if (cards.isEmpty()) return false;
			room->broadcastSkillInvoke(objectName());
			DummyCard material(cards);
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), "");
			room->obtainCard(target, &material, reason, false);
			ctx.extra_data = QVariantMap{{"given", true}, {"maxhp", target->getMaxHp()}, {"types", types.length()}};
		} else {
			const QVariantMap result = ctx.extra_data.toMap();
			const int delta = result.value("maxhp").toInt() - target->getMaxHp();
			if (delta > 0) room->gainMaxHp(target, delta, objectName());
			else if (delta < 0) room->loseMaxHp(target, -delta, objectName());
			if (target->isAlive() && target->isWounded()) room->recover(target,
				RecoverStruct(ctx.owner, nullptr, result.value("types").toInt() * getEffectiveAmount(ctx), objectName()));
		}
		return false;
	}
};
class Tushe : public TriggerSkillV2
{
public:
	Tushe() : TriggerSkillV2("tushe") { events << TargetSpecified; frequency = Frequent; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->isKindOf("SkillCard") || use.card->isKindOf("EquipCard") || use.to.isEmpty()) return {};
		for (const Card *card : player->getCards("he")) if (card->isKindOf("BasicCard")) return {};
		return TriggerList{{player, {objectName()}}};
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner->askForSkillInvoke(this);
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		ctx.owner->drawCards(ctx.original_data->value<CardUseStruct>().to.size() * getEffectiveAmount(ctx), objectName());
		return false;
	}
};
LimuCard::LimuCard()
{
	setSkillName("limu");
	mute = true;
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void LimuCard::onUse(Room *room, CardUseStruct &use) const { ActiveSkillCard::onUse(room, use); }

class Limu : public ViewAsSkillV2
{
public:
	Limu() : ViewAsSkillV2("limu") { setN(1); response_or_use = true; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->hasJudgeArea() && !request.initiator->containsTrick("indulgence");
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		if (!request.initiator || !card || !request.selectedCardIds.isEmpty() || card->getSuit() != Card::Diamond) return false;
		const int id = card->getEffectiveId();
		if (!request.initiator->handCards().contains(id) && !request.initiator->getEquipsId().contains(id)
			&& !request.initiator->getHandPile().contains(id)) return false;
		std::unique_ptr<Card> indulgence(Sanguosha->cloneCard("indulgence", card->getSuit(), card->getNumber()));
		indulgence->addSubcard(id);
		indulgence->setSkillName(objectName());
		return !request.initiator->isLocked(indulgence.get()) && !request.initiator->isProhibited(request.initiator, indulgence.get());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.length() == 1; }
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &request) const override { return request.userString == "indulgence" ? "Indulgence" : "LimuCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		LimuCard *card = new LimuCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.length() != 1 || !canActivate(request)) return false;
		ActiveSkillRequest empty = request;
		empty.selectedCardIds.clear();
		return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int modified = ctx.modified_amount;
		const bool hasModified = ctx.modified_amount_set;
		ctx.choice = "use";
		skillEffect(ctx, ctx.owner);
		if (ctx.extra_data.toBool() && ctx.owner->isAlive()) {
			ctx.choice = "recover";
			ctx.modified_amount = modified;
			ctx.modified_amount_set = hasModified;
			skillEffect(ctx, ctx.owner);
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		if (ctx.choice == "recover") {
			if (target->isWounded()) room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
			return ContinueEffects;
		}
		const int id = ctx.use_card->getSubcards().value(0, -1);
		if (id < 0 || room->getCardOwner(id) != ctx.owner || !target->hasJudgeArea() || target->containsTrick("indulgence")) return ContinueEffects;
		const Card *material = Sanguosha->getCard(id);
		ActiveSkillRequest request;
		request.initiator = ctx.owner;
		if (!canSelectCard(request, material)) return ContinueEffects;
		std::unique_ptr<Card> indulgence(Sanguosha->cloneCard("indulgence", material->getSuit(), material->getNumber()));
		indulgence->addSubcard(id);
		indulgence->setSkillName(objectName());
		indulgence->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
		indulgence->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
		// Limu explicitly uses Indulgence on self. Its ordinary use commits the material and settles the trick.
		ctx.extra_data = room->useCardFromSkillEffect(CardUseStruct(indulgence.get(), ctx.owner, target), ctx, true);
		return ContinueEffects;
	}
};

class LimuTargetMod : public TargetModSkillV2
{
public:
	LimuTargetMod() : TargetModSkillV2("#limu-target") { pattern = "."; }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.holder || !ctx.secondary || ctx.holder->getJudgingArea().isEmpty() || !ctx.holder->inMyAttackRange(ctx.secondary))
			return CorrectSkillResult::noEffect();
		if (ctx.modType == Residue) return CorrectSkillResult::unlimitedResidue();
		if (ctx.modType == DistanceLimit) return CorrectSkillResult::useAmount(1000 * ctx.currentAmount);
		return CorrectSkillResult::noEffect();
	}
};
class MobileShouye : public TriggerSkillV2
{
public:
	MobileShouye() : TriggerSkillV2("mobileshouye") { events << TargetConfirmed << EventSkillInvoking; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Turn; }
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && room->hasCurrent()
			&& use.card && !use.card->isKindOf("SkillCard") && use.to.length() == 1 && use.to.contains(player)
			&& use.from && use.from->isAlive() && use.from != player ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(ctx.original_data->value<CardUseStruct>().from))) return false;
		ctx.targets = {ctx.owner};
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.from || use.from->isDead()) return false;
		room->broadcastSkillInvoke(objectName());
		const QString defense = room->askForChoice(target, objectName(), "kaicheng+qixi");
		const QString attack = room->askForChoice(use.from, objectName(), "quanjun+fenbing");
		LogMessage log;
		log.type = "#MobileshouyeDuice";
		log.from = target;
		log.to << use.from;
		log.arg = "mobileshouye:" + defense;
		log.arg2 = "mobileshouye:" + attack;
		room->sendLog(log);
		const bool success = (defense == "kaicheng" && attack == "fenbing") || (defense == "qixi" && attack == "quanjun");
		log.type = success ? "#MobileshouyeDuiceSucceed" : "#MobileshouyeDuiceFail";
		room->sendLog(log);
		if (success) {
			use = ctx.original_data->value<CardUseStruct>();
			if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
			ctx.original_data->setValue(use);
			const QVariant useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id");
			const quint64 serial = room->getTag("mobile_shouye_serial").toULongLong() + 1;
			room->setTag("mobile_shouye_serial", serial);
			QVariantList receipts = use.card->getTag("mobile_shouye_receipts").toList();
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
				{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
				{"issuer", ctx.owner->objectName()}, {"serial", serial}, {"ordinal", int(serial)}, {"recipient", target->objectName()}, {"use", useId}};
			use.card->setTag("mobile_shouye_receipts", receipts);
		}
		return false;
	}
};

class MobileShouyeGet : public TriggerSkillV2
{
public:
	MobileShouyeGet() : TriggerSkillV2("#mobileshouye-get") { events << BeforeCardsMove << CardFinished; frequency = Compulsory; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event != CardFinished) return true;
		const Card *card = data.value<CardUseStruct>().card;
		if (!card) return true;
		const QString id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toString();
		QVariantList keep;
		for (const QVariant &receipt : card->getTag("mobile_shouye_receipts").toList())
			if (receipt.toMap().value("use").toString() != id) keep << receipt;
		card->setTag("mobile_shouye_receipts", keep);
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != BeforeCardsMove || !player || player->isDead()) return true;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile || !move.from_places.contains(Player::PlaceTable)
			|| (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE) return true;
		const Card *card = move.reason.m_extraData.value<const Card *>();
		if (!card) return true;
		const QString use = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toString();
		for (const QVariant &value : card->getTag("mobile_shouye_receipts").toList()) {
			const QVariantMap receipt = value.toMap();
			// BeforeCardsMove is dispatched per player; settle only this recipient's receipts here.
			if (receipt.value("recipient").toString() != player->objectName() || receipt.value("use").toString() != use) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
			if (!ctx.owner) continue;
			ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.trigger_count = receipt.value("ordinal").toInt();
			ctx.preferredTarget = player; ctx.preferredTargetSeat = player->getSeat();
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.extra_data = receipt;
			ctx.targets = {player};
			contexts << ctx;
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		const Card *card = ctx.original_data ? ctx.original_data->value<CardsMoveOneTimeStruct>().reason.m_extraData.value<const Card *>() : nullptr;
		return ctx.owner && card && card->getTag("mobile_shouye_receipts").toList().contains(ctx.extra_data);
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		const Card *card = ctx.original_data->value<CardsMoveOneTimeStruct>().reason.m_extraData.value<const Card *>();
		QVariantList receipts = card->getTag("mobile_shouye_receipts").toList();
		receipts.removeOne(ctx.extra_data);
		card->setTag("mobile_shouye_receipts", receipts);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		QList<int> ids;
		for (int i = 0; i < move.card_ids.length(); ++i)
			if (move.from_places.value(i) == Player::PlaceTable && room->getCardPlace(move.card_ids.at(i)) == Player::PlaceTable) ids << move.card_ids.at(i);
		if (ids.isEmpty()) return false;
		DummyCard material(ids);
		room->obtainCard(target, &material, true);
		move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		move.removeCardIds(ids);
		ctx.original_data->setValue(move);
		return false;
	}
};
MobileLiezhiCard::MobileLiezhiCard() { setSkillName("mobileliezhi"); }

bool MobileLiezhiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return ActiveSkillCard::targetFilter(targets, to_select, Self);
}

void MobileLiezhiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class MobileLiezhiVS : public ViewAsSkillV2
{
public:
	MobileLiezhiVS() : ViewAsSkillV2("mobileliezhi") { response_pattern = "@@mobileliezhi"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@mobileliezhi"
			&& !request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName, request.activationRef.key.instanceID, "disabled").toBool();
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
	{
		return target && target->isAlive() && targets.length() < 2 && target != request.initiator && request.initiator->canDiscard(target, "hej");
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty() && targets.length() <= 2; }
	QString historyKey(const ActiveSkillRequest &) const override { return "MobileLiezhiCard"; }
	const Card *createCard(const ActiveSkillRequest &) const override { return new MobileLiezhiCard; }
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive() && ctx.owner->canDiscard(target, "hej"); ++i) {
			const int id = room->askForCardChosen(ctx.owner, target, "hej", objectName(), false, Card::MethodDiscard);
			if (id >= 0) room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, ctx.owner);
		}
		return ContinueEffects;
	}
};

class MobileLiezhi : public TriggerSkillV2
{
public:
	MobileLiezhi() : TriggerSkillV2("mobileliezhi")
	{
		events << EventPhaseStart << Death << EventLoseSkill << Damaged;
		global = true;
		view_as_skill = new MobileLiezhiVS;
	}
	static void project(Room *room, ServerPlayer *owner)
	{
		int disabled = 0;
		for (int id : owner->getSkillInstanceIds("mobileliezhi"))
			if (owner->getSkillInstanceStateValue("mobileliezhi", id, "disabled").toBool()) ++disabled;
		room->setPlayerMark(owner, "mobileliezhi_disabled", disabled);
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		if ((event == EventPhaseStart && player->getPhase() == Player::Finish)
			|| (event == Death && data.value<DeathStruct>().who == player)) {
			// Reset every actual source, including a temporarily invalid copy.
			for (int id : player->getSkillInstanceIds(objectName())) player->removeSkillInstanceStateValue(objectName(), id, "disabled");
			project(room, player);
		} else if (event == EventLoseSkill) {
			SkillChangeStruct change;
			if (change.tryParse(data) && change.skillName == objectName()) project(room, player);
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == Damaged) return {{player, {objectName()}}};
		if (event != EventPhaseStart || player->getPhase() != Player::Start) return {};
		QStringList names;
		for (int id : player->getValidSkillInstanceIds(objectName()))
			if (!player->getSkillInstanceStateValue(objectName(), id, "disabled").toBool()) names << SkillInstanceKey(objectName(), id).toString();
		return names.isEmpty() ? TriggerList() : TriggerList{{player, names}};
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == Damaged) ctx.targets = {ctx.owner};
		else if (ctx.owner->getSkillInstanceStateValue(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID, "disabled").toBool()) return false;
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == EventPhaseStart) {
			bool canDiscard = false;
			for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) if (ctx.owner->canDiscard(target, "hej")) { canDiscard = true; break; }
			if (canDiscard) {
				Room::AcceptedViewAsEffectScope selection(room, ctx.owner, objectName(), ctx);
				if (!selection.isValid()) return false;
				room->askForUseCard(ctx.owner, "@@mobileliezhi", "@mobileliezhi");
			}
		}
		return false;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		if (event != Damaged) return false;
		ctx.owner->setSkillInstanceStateValue(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID, "disabled", true);
		project(room, ctx.owner);
		LogMessage log;
		log.type = "#MobileliezhiDisabled";
		log.from = ctx.owner;
		log.arg = objectName();
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(ctx.owner, objectName());
		return false;
	}
};
class Jijun : public TriggerSkillV2
{
public:
	Jijun() : TriggerSkillV2("jijun") { events << TargetSpecified; frequency = Frequent; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return use.card && !use.card->isKindOf("SkillCard") && use.to.contains(player)
			&& (use.card->isKindOf("Weapon") || !use.card->isKindOf("EquipCard")) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner->askForSkillInvoke(this)) return false;
		ctx.targets = {ctx.owner};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
			JudgeStruct judge;
			judge.pattern = ".";
			judge.play_animation = false;
			judge.who = target;
			judge.reason = objectName();
			room->judge(judge);
			// Claim only this completed judgment's remaining card, never another holder's judgment move.
			if (target->isAlive() && judge.card && room->getCardPlace(judge.card->getEffectiveId()) == Player::DiscardPile)
				target->addToPile("jjfang", judge.card->getEffectiveId());
		}
		return false;
	}
};
FangtongCard::FangtongCard()
{
	setSkillName("fangtong");
	will_throw = false;
	target_fixed = true;
	mute = true;
	handling_method = Card::MethodNone;
}

void FangtongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class FangtongVS : public ViewAsSkillV2
{
public:
	FangtongVS() : ViewAsSkillV2("fangtong") { expand_pile = "jjfang"; response_pattern = "@@fangtong!"; setN(-1); }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@fangtong!"
			&& request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName, request.activationRef.key.instanceID, "selecting").toBool();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.initiator->getPile("jjfang").contains(card->getEffectiveId())
			&& !request.selectedCardIds.contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return !request.selectedCardIds.isEmpty(); }
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "FangtongCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		FangtongCard *card = new FangtongCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!canActivate(request)) return false;
		for (int id : request.selectedCardIds) if (!ctx.owner->getPile("jjfang").contains(id)) return false;
		return !request.selectedCardIds.isEmpty();
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		QVariantList ids;
		for (int id : ctx.use_card->getSubcards()) ids << id;
		// This response only chooses the second cost; the outer activation owns payment and damage.
		ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "selected", ids);
		return ContinueEffects;
	}
};

class Fangtong : public TriggerSkillV2
{
public:
	Fangtong() : TriggerSkillV2("fangtong") { events << EventPhaseStart; view_as_skill = new FangtongVS; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
			&& !player->getPile("jjfang").isEmpty() && player->canDiscard(player, "he") ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const Card *card = room->askForCard(ctx.owner, "..", "fangtong-invoke", QVariant(), Card::MethodNone, nullptr, false, objectName());
		if (!card || card->getEffectiveId() < 0 || ctx.owner->isJilei(card)) return false;
		ctx.extra_data = QVariantMap{{"material", card->getEffectiveId()}, {"number", card->getNumber()}};
		ctx.manual_effect = true;
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const int id = ctx.extra_data.toMap().value("material", -1).toInt();
		if (id < 0 || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)
			|| ctx.owner->getPile("jjfang").isEmpty()) return false;
		room->throwCard(id, ctx.owner, ctx.owner);
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *owner = ctx.owner;
		if (owner->isDead() || owner->getPile("jjfang").isEmpty()) return false;
		room->broadcastSkillInvoke(objectName());
		const SkillInstanceRef ref = ctx.activationRef;
		const QVariant previousSelecting = owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "selecting");
		const QVariant previousSelected = owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "selected");
		auto restore = [&]() {
			owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "selecting", previousSelecting);
			owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "selected", previousSelected);
		};
		QList<int> chosen;
		owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "selecting", true);
		owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "selected", QVariantList());
		try {
			MobileResponseInstanceScope selection(room, owner, ref);
			if (owner->getPile("jjfang").length() > 1) room->askForUseCard(owner, "@@fangtong!", "@fangtong", -1, Card::MethodUse, false);
			for (const QVariant &id : owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "selected").toList())
				if (owner->getPile("jjfang").contains(id.toInt())) chosen << id.toInt();
			restore();
		} catch (...) { restore(); throw; }
		const QList<int> pile = owner->getPile("jjfang");
		if (chosen.isEmpty() && !pile.isEmpty()) chosen << pile.at(qsanRandomBounded(pile.length()));
		if (chosen.isEmpty() || owner->isDead()) return false;
		int sum = ctx.extra_data.toMap().value("number").toInt();
		for (int id : chosen) sum += Sanguosha->getCard(id)->getNumber();
		DummyCard material(chosen);
		CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, owner->objectName(), objectName(), "");
		room->throwCard(&material, reason, nullptr);
		if (sum != 36 || owner->isDead() || room->getOtherPlayers(owner).isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(owner, room->getOtherPlayers(owner), objectName(), "@fangtong-damage");
		if (target) skillEffect(event, room, owner, ctx, target);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->doAnimate(1, ctx.owner->objectName(), target->objectName());
		room->damage(DamageStruct(objectName(), ctx.owner, target, 3 * getEffectiveAmount(ctx), DamageStruct::Thunder));
		return false;
	}
};
class Shuyong : public TriggerSkillV2
{
public:
	Shuyong() : TriggerSkillV2("shuyong") { events << CardUsed << CardResponded; }
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
		if (!card || !card->isKindOf("Slash")) return {};
		for (ServerPlayer *target : room->getOtherPlayers(player))
			if (!target->isAllNude()) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> candidates;
		for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
			if (!target->isAllNude()) candidates << target;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@shuyong-invoke", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive() && !target->isAllNude(); ++i) {
			const int id = room->askForCardChosen(ctx.owner, target, "hej", objectName());
			if (id >= 0) room->obtainCard(ctx.owner, id, false);
		}
		if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};
MobileXushenCard::MobileXushenCard() { setSkillName("mobilexushen"); target_fixed = true; }

void MobileXushenCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class MobileXushenVS : public ViewAsSkillV2
{
public:
	MobileXushenVS() : ViewAsSkillV2("mobilexushen") { frequency = Limited; limit_mark = "@mobilexushenMark"; }
	LimitScope getLimitScope() const override { return Limit_Game; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
		for (const Player *player : request.initiator->getAliveSiblings(true)) if (player->isMale()) return true;
		return false;
	}
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "MobileXushenCard"; }
	const Card *createCard(const ActiveSkillRequest &) const override { return new MobileXushenCard; }
	bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		const QVariant id = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id");
		if (id.toLongLong() <= 0) { qWarning("MobileXushen: missing activation use identity"); return false; }
		ctx.extra_data = id;
		return true;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		room->removePlayerMark(ctx.owner, limit_mark);
		room->doSuperLightbox(ctx.owner, objectName());
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		return skillEffect(ctx, ctx.owner);
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		int men = 0;
		for (ServerPlayer *player : room->getAlivePlayers()) if (player->isMale()) ++men;
		if (men <= 0) return ContinueEffects;
		const QVariant previous = target->getTag("mobile_xushen_pending");
		target->setTag("mobile_xushen_pending", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
			{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}, {"use", ctx.extra_data}});
		try {
			room->loseHp(HpLostStruct(target, men * getEffectiveAmount(ctx), objectName(), ctx.owner));
			target->setTag("mobile_xushen_pending", previous);
		} catch (...) { target->setTag("mobile_xushen_pending", previous); throw; }
		return ContinueEffects;
	}
};

class MobileXushen : public TriggerSkillV2
{
public:
	MobileXushen() : TriggerSkillV2("mobilexushen")
	{
		events << QuitDying;
		frequency = Limited;
		limit_mark = "@mobilexushenMark";
		global = true;
		view_as_skill = new MobileXushenVS;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (!player || player->isDead()) return true;
		const DyingStruct dying = data.value<DyingStruct>();
		if (dying.who != player || !dying.hplost || dying.hplost->reason != objectName() || dying.hplost->from != player) return true;
		const QVariantMap pending = player->getTag("mobile_xushen_pending").toMap();
		const qint64 use = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
		const QVariant dyingId = room->historyParent(room->currentHistoryEventId(), "dying", true).value("id");
		ServerPlayer *saver = player->getSaver();
		if (pending.isEmpty() || use <= 0 || pending.value("use").toLongLong() != use || dyingId.toLongLong() <= 0 || !saver || saver->isDead()) return true;
		SkillContext ctx;
		ctx.skill_name = objectName();
		ctx.owner = ctx.invoker = ctx.initiator = player;
		ctx.sourceRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(pending.value("skill").toString(), pending.value("instance").toInt()));
		ctx.instanceID = ctx.sourceRef.key.instanceID;
		ctx.original_data = &data;
		ctx.current_event = event;
		ctx.extra_data = QVariantMap{{"pending", pending}, {"dying", dyingId}};
		ctx.targets = {saver};
		contexts << ctx;
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("mobile_xushen_pending").toMap() == ctx.extra_data.toMap().value("pending").toMap();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *saver = ctx.targets.value(0);
		return saver && saver->isAlive() && ctx.owner->askForSkillInvoke(objectName(), QString("mobilexushen:%1").arg(saver->objectName()));
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
	{
		// Rescue rewards are permanent grants, independent of the already-spent source skill.
		for (const QString &name : QStringList{"wusheng", "dangxian"})
			if (!target->hasSkill(name, true)) room->acquireSkill(target, name);
		return false;
	}
};
class MoboleZhennan : public TriggerSkillV2
{
public:
	MoboleZhennan() : TriggerSkillV2("mobolezhennan") { events << TargetSpecified; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!player || !player->isAlive() || !use.card || use.card->isKindOf("SkillCard")
			|| use.to.length() <= 1 || use.to.length() <= player->getHp()) return result;
		for (ServerPlayer *owner : use.to)
			if (owner->isAlive() && owner->hasSkill(objectName()) && owner->canDiscard(owner, "he")) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.from || use.from->isDead() || use.to.length() <= use.from->getHp()) return false;
		const Card *card = room->askForCard(ctx.owner, "..", "@mobolezhennan-discard:" + use.from->objectName(),
			*ctx.original_data, Card::MethodNone, nullptr, false, objectName());
		if (!card || card->getEffectiveId() < 0 || ctx.owner->isJilei(card)) return false;
		ctx.extra_data = card->getEffectiveId();
		ctx.targets = {use.from};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const int id = ctx.extra_data.toInt();
		if (room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)) return false;
		room->throwCard(id, ctx.owner, ctx.owner);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
		return false;
	}
};
MobileSpQianxinCard::MobileSpQianxinCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void MobileSpQianxinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<int> ids = getSubcards();
	QList<ServerPlayer *> players = room->getOtherPlayers(source);

	int n = 0;
	QList<CardsMoveStruct> moves;
	while (n < 2) {
		if (ids.isEmpty() || players.isEmpty()) break;

		int id = ids.at(qsanRandomBounded(ids.length()));
		ids.removeOne(id);

		ServerPlayer *to = players.at(qsanRandomBounded(players.length()));
		players.removeOne(to);

		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), to->objectName(), "mobilespqianxin", "");
		CardsMoveStruct move(QList<int>() << id, to, Player::PlaceHand, reason);
		moves << move;
	}
	if (moves.isEmpty()) return;
	room->moveCardsAtomic(moves, false);
}

class MobileSpQianxinVS : public ViewAsSkill
{
public:
	MobileSpQianxinVS() : ViewAsSkill("mobilespqianxin")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (to_select->isEquipped()) return false;
		int n = Self->getAliveSiblings().length();
		n = qMin(2, n);
		return selected.length() < n;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileSpQianxinCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		MobileSpQianxinCard *card = new MobileSpQianxinCard;
		card->addSubcards(cards);
		return card;
	}
};

class MobileSpQianxin : public PhaseChangeSkill
{
public:
	MobileSpQianxin() : PhaseChangeSkill("mobilespqianxin")
	{
		view_as_skill = new MobileSpQianxinVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getPhase() == Player::RoundStart && !target->getTag("mobilespqianxin_xin").toList().isEmpty();
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		player->removeTag("mobilespqianxin_xin");
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			QStringList choices;
			choices << "draw";
			if (player->getMaxCards() > 0)
				choices << "maxcards";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(p));
			LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = player;
			log.arg = "mobilespqianxin:" + choice;
			room->sendLog(log);

			if (choice == "draw")
				p->drawCards(2, objectName());
			else
				room->addMaxCards(player, -2);
		}
		return false;
	}
};

class MobileSpQianxinMove : public TriggerSkill
{
public:
	MobileSpQianxinMove() : TriggerSkill("#mobilespqianxin-move")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from && move.from_places.contains(Player::PlaceHand) && move.to && move.to_place == Player::PlaceHand
				&& move.reason.m_skillName == "mobilespqianxin") {
			QVariantList xin = move.to->getTag("mobilespqianxin_xin").toList();
			foreach (int id, move.card_ids) {
				if (xin.contains(id)) continue;
				xin << id;
			}
			move.to->setTag("mobilespqianxin_xin", xin);
		} else if (move.from && move.from_places.contains(Player::PlaceHand)) {
			QVariantList xin = move.from->getTag("mobilespqianxin_xin").toList();
			for (int i = 0; i < move.card_ids.length(); i++) {
				if (move.from_places.at(i) == Player::PlaceHand) {
					if (!xin.contains(move.card_ids.at(i))) continue;
					xin.removeOne(move.card_ids.at(i));
				}
			}
			move.from->setTag("mobilespqianxin_xin", xin);
		}
		return false;
	}
};

class MobileZhenxing : public TriggerSkillV2
{
public:
	MobileZhenxing() : TriggerSkillV2("mobilezhenxing") { events << EventPhaseStart << Damaged; frequency = Frequent; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName())
			&& (event == Damaged || player->getPhase() == Player::Finish) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner->askForSkillInvoke(this)) return false;
		ctx.targets = {ctx.owner};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		const QList<int> views = room->getNCards(3, false);
		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = target;
		log.arg = QString::number(views.length());
		log.card_str = ListI2S(views).join("+");
		room->sendLog(log, target);
		log.type = "#ViewDrawPile";
		room->sendLog(log, room->getOtherPlayers(target, true));
		QList<int> enabled, disabled;
		for (int id : views) {
			int matches = 0;
			for (int other : views) if (Sanguosha->getCard(other)->getSuit() == Sanguosha->getCard(id)->getSuit()) ++matches;
			(matches == 1 ? enabled : disabled) << id;
		}
		QList<int> chosen;
		try {
			for (int i = 0; i < getEffectiveAmount(ctx) && !enabled.isEmpty(); ++i) {
				room->fillAG(views, target, disabled);
				int id = room->askForAG(target, enabled, enabled.length() <= 1, objectName());
				room->clearAG(target);
				if (!enabled.contains(id)) id = enabled.first();
				chosen << id;
				enabled.removeOne(id);
				disabled << id;
			}
		} catch (...) {
			room->clearAG(target);
			room->returnToTopDrawPile(views);
			throw;
		}
		// Restore the inspected pile before moving chosen cards through the normal obtain pipeline.
		room->returnToTopDrawPile(views);
		if (!chosen.isEmpty()) { DummyCard cards(chosen); room->obtainCard(target, &cards, false); }
		return false;
	}
};
class Zhongzuo : public TriggerSkillV2
{
public:
	Zhongzuo() : TriggerSkillV2("zhongzuo") { events << EventPhaseChanging; }
	static int involvedInDamage(Room *room, ServerPlayer *owner)
	{
		const QVariant turn = room->historyScopes().value("turn_id");
		if (turn.toLongLong() <= 0) return -1;
		bool unknown = false;
		for (const QString &role : QStringList{"from", "to"}) {
			const QVariantMap page = room->queryActualDamage(QVariantMap{{"turn_id", turn}, {role, owner->objectName()}, {"limit", 1}});
			if (!page.value("complete").toBool()) { unknown = true; continue; }
			if (!page.value("items").toList().isEmpty()) return 1;
		}
		return unknown ? -1 : 0;
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		TriggerList result;
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
		for (ServerPlayer *owner : room->getAlivePlayers()) {
			if (!owner->hasSkill(objectName())) continue;
			const int resultForOwner = involvedInDamage(room, owner);
			if (resultForOwner < 0) qWarning("Zhongzuo: incomplete turn damage history");
			else if (resultForOwner > 0) result[owner] << objectName();
		}
		return result;
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@zhongzuo-invoke", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		target->drawCards(2 * getEffectiveAmount(ctx), objectName());
		if (target->isWounded() && ctx.owner->isAlive()) ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};
class Wanlan : public TriggerSkillV2
{
public:
	Wanlan() : TriggerSkillV2("wanlan") { events << Dying << EventSkillInvoking; frequency = Limited; limit_mark = "@wanlanMark"; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Game; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		ServerPlayer *dying = data.value<DyingStruct>().who;
		return player && player->isAlive() && player->hasSkill(objectName()) && player->canDiscard(player, "h")
			&& dying && dying->isAlive() && dying->getHp() < 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *dying = ctx.original_data->value<DyingStruct>().who;
		const QVariant id = room->historyParent(room->currentHistoryEventId(), "dying", true).value("id");
		if (!dying || dying->isDead() || dying->getHp() > 0 || !isUsable(ctx) || !ctx.owner->canDiscard(ctx.owner, "h")) return false;
		if (id.toLongLong() <= 0) { qWarning("Wanlan: missing dying resolution identity"); return false; }
		if (!ctx.owner->askForSkillInvoke(this, QVariant::fromValue(dying))) return false;
		ctx.targets = {dying};
		ctx.extra_data = id;
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !ctx.owner->canDiscard(ctx.owner, "h")) return false;
		addUsage(ctx);
		room->removePlayerMark(ctx.owner, limit_mark);
		room->doSuperLightbox(ctx.owner, objectName());
		ctx.owner->throwAllHandCards(objectName());
		return true;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		ServerPlayer *current = room->getCurrent();
		ServerPlayer *dying = ctx.original_data->value<DyingStruct>().who;
		if (current && current->isAlive() && current->hasFlag("CurrentPlayer") && dying) {
			QVariantList receipts = dying->getTag("mobile_wanlan_receipts").toList();
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"dying", ctx.extra_data}, {"target", current->objectName()},
				{"amount", getEffectiveAmount(ctx)}, {"token", QString::number(room->currentHistoryEventId())}};
			dying->setTag("mobile_wanlan_receipts", receipts);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const int amount = qMin(1 - target->getHp(), target->getLostHp());
		if (amount > 0) room->recover(target, RecoverStruct(ctx.owner, nullptr, amount, objectName()));
		return false;
	}
};

class WanlanDamage : public TriggerSkillV2
{
public:
	WanlanDamage() : TriggerSkillV2("#wanlan-damage") { events << QuitDying; frequency = Compulsory; global = true; }
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (!player || data.value<DyingStruct>().who != player) return true;
		const QString id = room->historyParent(room->currentHistoryEventId(), "dying", true).value("id").toString();
		if (id.toLongLong() <= 0) { qWarning("Wanlan: missing completed dying resolution identity"); return true; }
		for (const QVariant &value : player->getTag("mobile_wanlan_receipts").toList()) {
			const QVariantMap receipt = value.toMap();
			if (receipt.value("dying").toString() != id) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
			if (!ctx.owner) continue;
			ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.amount = receipt.value("amount").toInt();
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.extra_data = receipt;
			ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
			if (target) ctx.targets = {target};
			contexts << ctx;
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.targets.isEmpty() ? ctx.invoker : ctx.targets.first(); }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.invoker && ctx.invoker->getTag("mobile_wanlan_receipts").toList().contains(ctx.extra_data);
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		// Consume by exact dying event before damage can open another dying sequence.
		QVariantList receipts = ctx.invoker->getTag("mobile_wanlan_receipts").toList();
		receipts.removeOne(ctx.extra_data);
		ctx.invoker->setTag("mobile_wanlan_receipts", receipts);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->damage(DamageStruct("wanlan", ctx.owner, target, getEffectiveAmount(ctx)));
		return false;
	}
};
TongquCard::TongquCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TongquCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->getMark("&tqqu") > 0 && to_select != Self;
}

bool TongquCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	int id = getSubcards().first();
	if (Self->canDiscard(Self, id))
		return true;
	return !targets.isEmpty();
}

void TongquCard::onUse(Room *room, CardUseStruct &card_use) const
{
	int id = getSubcards().first();
	const Card *c = Sanguosha->getCard(id);
	if (card_use.to.isEmpty()) {
		if (card_use.from->canDiscard(card_use.from, id)) {
			CardMoveReason reason(CardMoveReason::S_REASON_THROW, card_use.from->objectName(), "tongqu", "");
			room->throwCard(this, reason, card_use.from, nullptr);
		} else {
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getOtherPlayers(card_use.from)) {
				if (p->getMark("&tqqu") <= 0) continue;
				targets << p;
			}
			if (targets.isEmpty()) return;
			ServerPlayer *target = targets.at(qsanRandomBounded(targets.length()));
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), target->objectName(), "tongqu", "");
			room->obtainCard(target, this, reason, false);
			if (target->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(target) && !target->isProhibited(target, c))
				room->useCard(CardUseStruct(c, target));
		}
	} else {
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), card_use.to.first()->objectName(), "tongqu", "");
		room->obtainCard(card_use.to.first(), this, reason, false);
		if (card_use.to.first()->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(card_use.to.first())
				&& !card_use.to.first()->isProhibited(card_use.to.first(), c))
			room->useCard(CardUseStruct(c, card_use.to.first()));
	}
}

class TongquVS : public OneCardViewAsSkill
{
public:
	TongquVS() : OneCardViewAsSkill("tongqu")
	{
		response_pattern = "@@tongqu!";
	}

	const Card *viewAs(const Card *originalcard) const
	{
		TongquCard *c = new TongquCard;
		c->addSubcard(originalcard->getId());
		return c;
	}
};

class Tongqu : public TriggerSkill
{
public:
	Tongqu() : TriggerSkill("tongqu")
	{
		events << DrawNCards << AfterDrawNCards;
		view_as_skill = new TongquVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase"||player->getMark("&tqqu") <= 0) return false;
			int length = 0;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead() || !p->hasSkill("tongqu")) continue;
				room->broadcastSkillInvoke("tongqu");
				room->notifySkillInvoked(p, "tongqu");
				length++;
			}
			if (length <= 0) return false;
			room->setPlayerFlag(player, "tongqu");
			room->addPlayerMark(player, "tongqu-Clear", length);
			draw.num += length;
			LogMessage log;
			log.type = "#HuaijuDraw";
			log.from = player;
			log.arg = "tongqu";
			log.arg2 = QString::number(length);
			room->sendLog(log);
			data = QVariant::fromValue(draw);
		} else {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase"||!player->hasFlag("tongqu")) return false;
			room->setPlayerFlag(player, "-tongqu");
			int n = player->getMark("tongqu-Clear");
			room->addPlayerMark(player, "tongqu-Clear", 0);
			for (int i = 0; i < n; i++) {
				if (player->isDead() || player->isNude()) return false;
				if (!room->askForUseCard(player, "@@tongqu!", "@tongqu")) {
					QList<int> dis;
					foreach (const Card *c, player->getCards("he")) {
						if (!player->canDiscard(player, c->getEffectiveId())) continue;
						dis << c->getEffectiveId();
					}
					if (!dis.isEmpty()) {
						int id = dis.at(qsanRandomBounded(dis.length()));
						room->throwCard(id, player);
					} else {
						const Card *c = player->getCards("he").at(qsanRandomBounded(player->getCards("he").length()));
						QList<ServerPlayer *> targets;
						foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
							if (p->getMark("&tqqu") <= 0) continue;
							targets << p;
						}
						if (targets.isEmpty()) return false;
						ServerPlayer *target = targets.at(qsanRandomBounded(targets.length()));
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "tongqu", "");
						room->obtainCard(target, c, reason, false);
						if (target->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(target) && !target->isProhibited(target, c))
							room->useCard(CardUseStruct(c, target));
					}
				}
			}
		}
		return false;
	}
};

class TongquTrigger : public TriggerSkill
{
public:
	TongquTrigger() : TriggerSkill("#tongqu-trigger")
	{
		events << GameStart << EventPhaseStart << Dying;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == GameStart) {
			room->sendCompulsoryTriggerLog(player, "tongqu", true, true);
			player->gainMark("&tqqu");
		} else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Start) return false;
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getMark("&tqqu") > 0) continue;
				targets << p;
			}
			if (targets.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, targets, "tongqu", "@tongqu-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke("tongqu");
			room->loseHp(HpLostStruct(player, 1, "tongqu", player));
			target->gainMark("&tqqu");
		} else {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who->getMark("&tqqu") <= 0) return false;
			room->sendCompulsoryTriggerLog(player, "tongqu", true, true);
			dying.who->loseAllMarks("&tqqu");
		}
		return false;
	}
};

class NewWanlan : public TriggerSkillV2
{
public:
	NewWanlan() : TriggerSkillV2("newwanlan") { events << DamageInflicted; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || data.value<DamageStruct>().damage < player->getHp()) return result;
		for (ServerPlayer *owner : room->getAlivePlayers())
			if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "e")) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
		if (!target || target->isDead() || !ctx.owner->canDiscard(ctx.owner, "e")
			|| !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
		ctx.targets = {target};
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.owner->canDiscard(ctx.owner, "e")) return false;
		// Discardability is rechecked at payment; equipment loss may trigger nested effects.
		ctx.owner->throwAllEquips(objectName());
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *) const override
	{
		room->broadcastSkillInvoke(objectName());
		return true;
	}
};
class Biaozhao : public TriggerSkillV2
{
public:
	Biaozhao() : TriggerSkillV2("biaozhao") { events << EventPhaseStart << CardsMoveOneTime; }
	static int biao(const Player *owner, int instance)
	{
		const QVariant value = owner->getSkillInstanceStateValue("biaozhao", instance, "card");
		return value.isValid() && owner->getPile("bzbiao").contains(value.toInt()) ? value.toInt() : -1;
	}
	static QList<int> matching(Room *room, ServerPlayer *owner, int instance, const CardsMoveOneTimeStruct &move)
	{
		const int id = biao(owner, instance);
		if (id < 0 || move.to_place != Player::DiscardPile) return {};
		const Card *card = Sanguosha->getCard(id);
		const qint64 moveId = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
		if (moveId <= 0) return {};
		QVariantMap query{{"event_id", moveId}};
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) { qWarning("Biaozhao: incomplete movement snapshots"); return {}; }
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap().value("data").toMap();
				if (!move.card_ids.contains(fact.value("card_id").toInt()) || fact.value("to_place").toInt() != Player::DiscardPile) continue;
				const QVariantMap snapshot = fact.value("card_before").toMap();
				if (!snapshot.contains("suit") || !snapshot.contains("number")) return {};
				if (snapshot.value("suit").toInt() == card->getSuit() && snapshot.value("number").toInt() == card->getNumber()) return {id};
			}
			if (!page.value("has_more").toBool()) return {};
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return result;
		for (int id : player->getSkillInstanceIds(objectName())) {
			bool ready = false;
			if (event == EventPhaseStart) ready = (player->getPhase() == Player::Finish && !player->isNude() && biao(player, id) < 0)
				|| (player->getPhase() == Player::Start && biao(player, id) >= 0);
			else ready = !matching(room, player, id, data.value<CardsMoveOneTimeStruct>()).isEmpty();
			// CardsMoveOneTime already invokes this selector separately for each player.
			if (ready) result[player] << SkillInstanceUtils::formatName(objectName(), id);
		}
		return result;
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = {player};
		const SkillInstanceRef ref = getUsageRef(ctx);
		if (ref.ownerObjectName != player->objectName()) return false;
		if (event == EventPhaseStart && player->getPhase() == Player::Finish) {
			if (biao(player, ref.key.instanceID) >= 0) return false;
			const Card *card = room->askForCard(player, "..", "biaozhao-put", QVariant(), Card::MethodNone);
			if (!card || (!player->handCards().contains(card->getEffectiveId()) && !player->getEquipsId().contains(card->getEffectiveId()))) return false;
			ctx.extra_data = QVariantMap{{"action", "put"}, {"card", card->getEffectiveId()}};
			return true;
		}
		const int id = biao(player, ref.key.instanceID);
		if (id < 0) return false;
		ctx.is_forced = true;
		QVariantMap saved{{"action", event == EventPhaseStart ? "start" : "match"}, {"card", id}};
		if (event == CardsMoveOneTime) {
			const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
			if (matching(room, player, ref.key.instanceID, move).isEmpty()) return false;
			if (move.from && move.from != player && move.from->isAlive()
				&& (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
				saved.insert("recipient", move.from->objectName());
		}
		ctx.extra_data = saved;
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const QVariantMap initial = ctx.extra_data.toMap();
		if (initial.value("action").toString() == "put") return false;
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		const auto run = [&](const QString &action, ServerPlayer *target) {
			QVariantMap saved = ctx.extra_data.toMap();
			saved.insert("action", action);
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			if (target && target->isAlive()) skillEffect(event, room, player, ctx, target);
		};
		ServerPlayer *recipient = room->findPlayerByObjectName(initial.value("recipient").toString());
		run(recipient && recipient->isAlive() ? "give" : "remove", recipient && recipient->isAlive() ? recipient : player);
		if (initial.value("action").toString() == "match") {
			run("lose", player);
			return false;
		}
		if (!player->isAlive() || !ctx.extra_data.toMap().value("removed").toBool()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@biaozhao-invoke");
		run("recover", target);
		run("draw", target);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		const QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		const int id = saved.value("card").toInt();
		if (action == "put") {
			const SkillInstanceRef ref = getUsageRef(ctx);
			if (biao(player, ref.key.instanceID) >= 0 || room->getCardOwner(id) != player
				|| (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
			// Store ownership before movement callbacks; another instance cannot spend this pile card.
			player->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "card", id);
			player->addToPile("bzbiao", id);
		} else if (action == "remove" || action == "give") {
			if (!player->getPile("bzbiao").contains(id)) return false;
			if (action == "give") room->obtainCard(target, id, true);
			else {
				const CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", player->objectName(), objectName(), "");
				room->throwCard(Sanguosha->getCard(id), reason, nullptr);
			}
			QVariantMap result = ctx.extra_data.toMap();
			result.insert("removed", !player->getPile("bzbiao").contains(id));
			ctx.extra_data = result;
		} else if (action == "lose") room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), player));
		else if (action == "recover") room->recover(target, RecoverStruct(objectName(), player, getEffectiveAmount(ctx)));
		else if (action == "draw") {
			int highest = target->getHandcardNum();
			for (ServerPlayer *other : room->getAlivePlayers()) highest = qMax(highest, other->getHandcardNum());
			const int count = qMin(5, highest - target->getHandcardNum()) * getEffectiveAmount(ctx);
			if (count > 0) target->drawCards(count, objectName());
		}
		return false;
	}
};

class Yechou : public TriggerSkillV2
{
public:
	Yechou() : TriggerSkillV2("yechou") { events << Death; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || data.value<DeathStruct>().who != player || !player->hasSkill(objectName())) return {};
		for (ServerPlayer *target : room->getAlivePlayers()) if (target->getLostHp() > 1) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> candidates;
		for (ServerPlayer *target : room->getAlivePlayers()) if (target->getLostHp() > 1) candidates << target;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@yechou-invoke", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		QVariantList receipts = target->getTag("mobile_yechou_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
			{"token", QString::number(room->currentHistoryEventId())}};
		target->setTag("mobile_yechou_receipts", receipts);
		room->setPlayerMark(target, "&yechou", receipts.length());
		return false;
	}
};

class YechouEffect : public TriggerSkillV2
{
public:
	YechouEffect() : TriggerSkillV2("#yechou-effect") { events << EventPhaseChanging << EventPhaseStart; frequency = Compulsory; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
			player->removeTag("mobile_yechou_receipts");
			room->setPlayerMark(player, "&yechou", 0);
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
		for (ServerPlayer *target : room->getAlivePlayers()) {
			for (const QVariant &value : target->getTag("mobile_yechou_receipts").toList()) {
				const QVariantMap receipt = value.toMap();
				SkillContext ctx;
				ctx.skill_name = objectName();
				ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
				if (!ctx.owner) continue;
				ctx.invoker = player;
				ctx.initiator = target;
				ctx.sourceRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
				ctx.instanceID = ctx.sourceRef.key.instanceID;
				ctx.amount = receipt.value("amount").toInt();
				ctx.original_data = &data;
				ctx.current_event = event;
				ctx.extra_data = receipt;
				ctx.targets = {target};
				contexts << ctx;
			}
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.initiator; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.initiator && ctx.initiator->getTag("mobile_yechou_receipts").toList().contains(ctx.extra_data);
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		// The dead source remains the immutable cause until the recipient's next turn starts.
		LogMessage log;
		log.type = "#YechouEffect";
		log.from = target;
		log.arg = "yechou";
		room->sendLog(log);
		room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), "yechou", ctx.owner));
		return false;
	}
};
class Zhengjian : public TriggerSkill
{
public:
	Zhengjian() : TriggerSkill("zhengjian")
	{
		events << EventPhaseStart << EventLoseSkill << PreCardUsed << PreCardResponded << Death;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead() || !player->hasSkill(objectName())) return false;
			if (player->getPhase() == Player::Finish) {
				ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@zhengjian-invoke", false, true);
				room->broadcastSkillInvoke(objectName());
				QStringList names = player->property("ZhengjianTargets").toStringList();
				if (names.contains(target->objectName())) return false;
				names << target->objectName();
				room->setPlayerProperty(player, "ZhengjianTargets", names);
				target->gainMark("&zhengjian");
			} else if (player->getPhase() == Player::RoundStart) {
				QStringList names = player->property("ZhengjianTargets").toStringList();
				if (names.isEmpty()) return false;
				room->setPlayerProperty(player, "ZhengjianTargets", QStringList());

				QList<ServerPlayer *> sp;
				foreach (QString name, names) {
					ServerPlayer *target = room->findPlayerByObjectName(name);
					if (target && target->isAlive() && !sp.contains(target))
						sp << target;
				}
				if (sp.isEmpty()) return false;

				bool peiyin = false;
				room->sortByActionOrder(sp);
				foreach (ServerPlayer *p, sp) {
					int mark = p->getMark("&zhengjiandraw");
					room->setPlayerMark(p, "&zhengjiandraw", 0);
					mark = qMin(5, mark);
					mark = qMin(mark, p->getMaxHp());
					if (mark > 0) {
						if (!peiyin) {
							peiyin = true;
							room->sendCompulsoryTriggerLog(player, objectName(), true, true);
						}
						p->drawCards(mark, objectName());
					}
					if (p->getMark("&zhengjian") > 0) {
						if (!peiyin) {
							peiyin = true;
							room->sendCompulsoryTriggerLog(player, objectName(), true, true);
						}
						p->loseAllMarks("&zhengjian");
					}
				}
			}
		} else if (event == PreCardUsed) {
			if (player->getMark("&zhengjian") <= 0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			room->addPlayerMark(player, "&zhengjiandraw");
		} else if (event == PreCardResponded) {
			if (player->getMark("&zhengjian") <= 0) return false;
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (res.m_card->isKindOf("SkillCard")) return false;
			room->addPlayerMark(player, "&zhengjiandraw");
		} else {
			if (event == EventLoseSkill) {
				if (player->isDead() || data.toString() != objectName()) return false;
			} else if (event == Death) {
				if (data.value<DeathStruct>().who != player) return false;
			}

			QStringList names = player->property("ZhengjianTargets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "ZhengjianTargets", QStringList());

			QList<ServerPlayer *> sp;
			foreach (QString name, names) {
				ServerPlayer *target = room->findPlayerByObjectName(name);
				if (target && target->isAlive() && !sp.contains(target))
					sp << target;
			}
			if (sp.isEmpty()) return false;

			room->sortByActionOrder(sp);
			foreach (ServerPlayer *p, sp) {
				room->setPlayerMark(p, "&zhengjian", 0);
				room->setPlayerMark(p, "&zhengjiandraw", 0);
			}
		}
		return false;
	}
};

GaoyuanCard::GaoyuanCard()
{
}

bool GaoyuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (!targets.isEmpty()||to_select == Self||to_select->getMark("&zhengjian")<1) return false;
	const Card *slash = Card::Parse(Self->property("gaoyuanData").toString());
	foreach (const Player *p, Self->getAliveSiblings()) {
		if(p->hasFlag("GaoyuanFrom")&&p->canSlash(to_select, slash, false))
			return true;
	}
	return false;
}

void GaoyuanCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->setFlags("GaoyuanTarget");
}

class GaoyuanVS : public OneCardViewAsSkill
{
public:
	GaoyuanVS() : OneCardViewAsSkill("gaoyuan")
	{
		response_pattern = "@@gaoyuan";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		GaoyuanCard *c = new GaoyuanCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Gaoyuan : public TriggerSkill
{
public:
	Gaoyuan() : TriggerSkill("gaoyuan")
	{
		events << TargetConfirming;
		view_as_skill = new GaoyuanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();

		if (use.card->isKindOf("Slash") && player->canDiscard(player, "he")) {
			QList<ServerPlayer *> players = room->getOtherPlayers(player);
			players.removeOne(use.from);

			bool can_invoke = false;
			foreach (ServerPlayer *p, players) {
				if (p->getMark("&zhengjian") > 0 && use.from->canSlash(p, use.card, false)) {
					can_invoke = true;
					break;
				}
			}

			if (can_invoke) {
				room->setPlayerFlag(use.from,"GaoyuanFrom");
				QString prompt = "@gaoyuan:" + use.from->objectName();
				room->setPlayerProperty(player, "gaoyuanData", use.card->toString());
				if (room->askForUseCard(player, "@@gaoyuan", prompt, -1, Card::MethodDiscard)) {
					foreach (ServerPlayer *p, players) {
						if (p->hasFlag("GaoyuanTarget")) {
							p->setFlags("-GaoyuanTarget");
							use.to.removeOne(player);
							use.to.append(p);
							room->sortByActionOrder(use.to);
							data = QVariant::fromValue(use);
							break;
						}
					}
				}
				room->setPlayerFlag(use.from,"-GaoyuanFrom");
			}
		}
		return false;
	}
};

class Xuewei : public TriggerSkillV2
{
public:
	Xuewei() : TriggerSkillV2("xuewei") { events << EventPhaseStart << EventPhaseChanging << DamageInflicted << Death; global = true; }
	static void project(Room *room, ServerPlayer *owner)
	{
		const QVariantList pending = owner->getTag("mobile_xuewei_pending").toList();
		for (ServerPlayer *target : room->getAllPlayers(true)) {
			int count = 0;
			for (const QVariant &entry : pending) if (entry.toMap().value("target").toString() == target->objectName()) ++count;
			room->setPlayerMark(target, "&xuewei+#" + owner->objectName(), count, QList<ServerPlayer *>{owner});
		}
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		const bool expires = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::RoundStart;
		const bool died = event == Death && data.value<DeathStruct>().who == player;
		if (expires || died) {
			player->removeTag("mobile_xuewei_pending");
			project(room, player);
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
			&& player->getPhase() == Player::Start && !room->getOtherPlayers(player).isEmpty()
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != DamageInflicted) return false;
		if (!player || !player->isAlive() || data.value<DamageStruct>().damage <= 0) return true;
		for (ServerPlayer *owner : room->getOtherPlayers(player)) {
			for (const QVariant &entry : owner->getTag("mobile_xuewei_pending").toList()) {
				const QVariantMap receipt = entry.toMap();
				if (receipt.value("target").toString() != player->objectName()) continue;
				SkillContext ctx;
				ctx.skill_name = objectName();
				ctx.owner = owner;
				ctx.invoker = ctx.initiator = player;
				ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
				ctx.instanceID = ctx.sourceRef.key.instanceID;
				ctx.amount = receipt.value("amount").toInt();
				ctx.is_forced = true;
				ctx.current_event = event;
				ctx.original_data = &data;
				ctx.extra_data = QVariantMap{{"receipt", receipt}};
				contexts << ctx;
			}
		}
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.owner; }
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.extra_data.toMap().contains("receipt"))
			return ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("mobile_xuewei_pending").toList().contains(ctx.extra_data.toMap().value("receipt"));
		return TriggerSkillV2::isSourceAvailable(room, ctx);
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == DamageInflicted) return true;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@xuewei-invoke", true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event != DamageInflicted) return false;
		ctx.manual_effect = true;
		QVariantMap saved = ctx.extra_data.toMap();
		QVariantList pending = player->getTag("mobile_xuewei_pending").toList();
		pending.removeOne(saved.value("receipt"));
		player->setTag("mobile_xuewei_pending", pending);
		project(room, player);
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		saved.insert("step", "prevent");
		saved.insert("damage", damage.damage);
		saved.insert("nature", static_cast<int>(damage.nature));
		saved.insert("from", damage.from ? damage.from->objectName() : QString());
		ctx.extra_data = saved;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		skillEffect(event, room, player, ctx, ctx.invoker);
		saved = ctx.extra_data.toMap();
		if (!saved.value("prevented").toBool()) return false;
		ctx.modified_amount = amount;
		ctx.modified_amount_set = modified;
		saved.insert("step", "take");
		ctx.extra_data = saved;
		skillEffect(event, room, player, ctx, player);
		ServerPlayer *attacker = room->findPlayerByObjectName(saved.value("from").toString());
		if (player->isAlive() && attacker && attacker->isAlive()) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			saved.insert("step", "retaliate");
			ctx.extra_data = saved;
			skillEffect(event, room, player, ctx, attacker);
		}
		return true;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event == EventPhaseStart) {
			QVariantList pending = player->getTag("mobile_xuewei_pending").toList();
			pending << QVariantMap{{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
				{"target", target->objectName()}, {"amount", getEffectiveAmount(ctx)}};
			player->setTag("mobile_xuewei_pending", pending);
			project(room, player);
			LogMessage log;
			log.type = "#ChoosePlayerWithSkill";
			log.from = player;
			log.to << target;
			log.arg = objectName();
			room->sendLog(log, player);
			log.type = "#InvokeSkill";
			log.to.clear();
			room->sendLog(log, room->getOtherPlayers(player, true));
			room->broadcastSkillInvoke(objectName());
			return false;
		}
		QVariantMap saved = ctx.extra_data.toMap();
		const QString step = saved.value("step").toString();
		if (step == "prevent") {
			saved.insert("prevented", true);
			ctx.extra_data = saved;
			LogMessage log;
			log.type = "#XueweiPrevent";
			log.from = player;
			log.to << target;
			log.arg = objectName();
			log.arg2 = QString::number(saved.value("damage").toInt());
			room->sendLog(log);
		} else if (step == "take")
			room->damage(DamageStruct(objectName(), nullptr, target, saved.value("damage").toInt() * getEffectiveAmount(ctx)));
		else
			room->damage(DamageStruct(objectName(), player, target, saved.value("damage").toInt() * getEffectiveAmount(ctx),
				static_cast<DamageStruct::Nature>(saved.value("nature").toInt())));
		return false;
	}
};
class Liechi : public TriggerSkillV2
{
public:
	Liechi() : TriggerSkillV2("liechi") { events << EnterDying; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const DyingStruct dying = data.value<DyingStruct>();
		ServerPlayer *source = dying.damage ? dying.damage->from : nullptr;
		return player && player->isAlive() && player->hasSkill(objectName()) && dying.who == player
			&& source && source->isAlive() && source->canDiscard(source, "he") ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.original_data->value<DyingStruct>().damage->from};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target->canDiscard(target, "he")) return false;
		room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
		room->askForDiscard(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx), false, true);
		return false;
	}
};
TiansuanCard::TiansuanCard()
{
	//target_fixed = true;
}

bool TiansuanCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
	return false;
}

bool TiansuanCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
	return true;
}

void TiansuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->addPlayerMark(source, "tiansuan_lun");
	room->setEmotion(source, "chouqian");

	QList<int> mingyunqians;
	int num = -1;
	if (!user_string.isEmpty())
		num = user_string.split(":").last().toInt();
	if (num > 0)
		mingyunqians << num;
	mingyunqians << 1 << 2 << 2 << 3 << 3 << 3 << 4 << 4 << 5;

	int mingyunqian = mingyunqians.at(qsanRandomBounded(mingyunqians.length()));

	LogMessage log;
	log.from = source;
	log.type = "#TiansuanMingyunqian";
	log.arg = "tiansuan" + QString::number(mingyunqian);
	room->sendLog(log);

	ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), "tiansuan_"  + QString::number(mingyunqian), "@tiansuan-mingyunqian:" + log.arg);
	room->doAnimate(1, source->objectName(), target->objectName());

	log.type = "#TiansuanMingyunqianTarget";
	log.to << target;
	room->sendLog(log);

	room->addPlayerMark(target, "&" + log.arg + "+#" + source->objectName());

	if (mingyunqian == 1) {
		room->doGongxin(source, target, QList<int>(), "tiansuan");
		QString flags = "hej";
		if (source == target)
			flags = "ej";
		if (target->getCards(flags).isEmpty()) return;
		int id = room->askForCardChosen(source, target, flags, "tiansuan", true);
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
		room->obtainCard(source, Sanguosha->getCard(id), reason);
	} else if (mingyunqian == 2) {
		QString flags = "he";
		if (source == target)
			flags = "e";
		if (target->getCards(flags).isEmpty()) return;
		int id = room->askForCardChosen(source, target, flags, "tiansuan");
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
		room->obtainCard(source, Sanguosha->getCard(id), reason);
	} else if (mingyunqian == 5)
		room->setPlayerCardLimitation(target, "use", "Peach,Analeptic", false);

}

class TiansuanVS : public ZeroCardViewAsSkill
{
public:
	TiansuanVS() : ZeroCardViewAsSkill("tiansuan")
	{
	}

	const Card *viewAs() const
	{
		QString choice = Self->getTag("tiansuan").toString();
		if (choice.isEmpty()) return nullptr;
		TiansuanCard *card = new TiansuanCard;
		card->setUserString(choice);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("tiansuan_lun") <= 0;
	}
};

class Tiansuan : public TriggerSkill
{
public:
	Tiansuan() : TriggerSkill("tiansuan")
	{
		events << Death << EventPhaseStart;
		view_as_skill = new TiansuanVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	void removeMingyunqian(ServerPlayer *player, ServerPlayer *player2) const
	{
		Room *room = player->getRoom();
		foreach (QString mark, player->getMarkNames()) {
			if (player->getMark(mark) <= 0) continue;
			if (mark.startsWith("&tiansuan") && mark.endsWith("+#" + player2->objectName()))
				room->setPlayerMark(player, mark, 0);
		}

		bool limit = false;
		foreach (QString mark, player->getMarkNames()) {
			if (player->getMark(mark) <= 0) continue;
			if (mark.startsWith("&tiansuan5")) {
				limit = true;
				break;
			}
		}
		if (!limit)
			room->removePlayerCardLimitation(player, "use", "Peach,Analeptic");
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::tiansuan(objectName());
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Death) {
			ServerPlayer *who = data.value<DeathStruct>().who;
			foreach (ServerPlayer *p, room->getAllPlayers())
				removeMingyunqian(p, who);

			foreach (QString mark, who->getMarkNames()) {
				if (who->getMark(mark) <= 0) continue;
				if (mark.startsWith("&tiansuan5"))
					room->removePlayerCardLimitation(who, "use", "Peach,Analeptic");
			}
		} else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			foreach (ServerPlayer *p, room->getAllPlayers())
				removeMingyunqian(p, player);
		}
		return false;
	}
};

class TiansuanEffect : public TriggerSkill
{
public:
	TiansuanEffect() : TriggerSkill("#tiansuan")
	{
		events << DamageInflicted << Damaged;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	int MingyunqianNum(ServerPlayer *player, int i) const
	{
		int num = 0;
		foreach (QString mark, player->getMarkNames()) {
			if (player->getMark(mark) <= 0) continue;
			if (mark.startsWith("&tiansuan" + QString::number(i)))
				num++;
		}
		return num;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();

		LogMessage log;
		log.from = player;

		if (event == DamageInflicted) {
			log.arg2 = QString::number(damage.damage);
			if (MingyunqianNum(player, 1) > 0) {
				log.type = "#TiansuanMingyunqianEffect1";
				log.arg = "tiansuan1";
				room->sendLog(log);
				return true;
			}

			if (MingyunqianNum(player, 2) > 0) {
				log.type = "#TiansuanMingyunqianEffect2";
				log.arg = "tiansuan2";
				room->sendLog(log);
				damage.damage = 1;
			}

			if (MingyunqianNum(player, 3) > 0) {
				log.type = "#TiansuanMingyunqianEffect3";
				log.arg = "tiansuan3";
				room->sendLog(log);
				damage.nature = DamageStruct::Fire;
				if (damage.damage > 1)
					damage.damage = 1;
			}

			for (int i = 0; i < MingyunqianNum(player, 4); i++) {
				log.type = "#TiansuanMingyunqianEffect4";
				log.arg = "tiansuan4";
				room->sendLog(log);
				++damage.damage;
			}

			for (int i = 0; i < MingyunqianNum(player, 5); i++) {
				log.type = "#TiansuanMingyunqianEffect4";
				log.arg = "tiansuan4";
				room->sendLog(log);
				++damage.damage;
			}

			data = QVariant::fromValue(damage);
		} else {
			for (int i = 0; i < damage.damage * MingyunqianNum(player, 2); i ++)
				player->drawCards(1, "tiansuan");
		}
		return false;
	}
};

class ZhiyiVS : public ZeroCardViewAsSkill
{
public:
	ZhiyiVS() : ZeroCardViewAsSkill("zhiyi")
	{
		response_pattern = "@@zhiyi!";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		QString name = Self->property("zhiyi_card_name").toString();
		Card *c = Sanguosha->cloneCard(name);
		c->setSkillName("_zhiyi");
		return c;
	}
};

class Zhiyi : public TriggerSkill
{
public:
	Zhiyi() : TriggerSkill("zhiyi")
	{
		events << CardUsed << CardResponded << CardFinished;
		frequency = Compulsory;
		view_as_skill = new ZhiyiVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (!room->hasCurrent()) return false;
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->hasFlag("zhiyi_card")) return false;
			Card *c = Sanguosha->cloneCard(use.card->objectName());
			c->setSkillName("_" + objectName());
			c->deleteLater();
			if (player->isLocked(c)) return false;
			room->setPlayerProperty(player, "zhiyi_card_name", c->objectName());

			if (room->askForUseCard(player, "@@zhiyi!", "@zhiyi:" + c->objectName(), -1, Card::MethodUse, false)) return false;

			QList<ServerPlayer *> targets = room->getCardTargets(player,c);
			if (targets.isEmpty()) return false;
			room->useCard(CardUseStruct(c, player, targets.at(qsanRandomBounded(targets.length()))), false);
		} else if (event == CardUsed) {
			if (player->getMark("zhiyi-Clear") > 0) return false;
			const Card *card = data.value<CardUseStruct>().card;
			if (!card || !card->isKindOf("BasicCard")) return false;

			room->sendCompulsoryTriggerLog(player, this);
			room->addPlayerMark(player, "zhiyi-Clear");

			QStringList choices;
			Card *c = Sanguosha->cloneCard(card);
			c->setSkillName("_" + objectName());
			int n = player->usedTimes(c->getClassName());
			player->clearHistory(c->getClassName());
			if (c->isAvailable(player))
				choices << "use";
			choices << "draw";

			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(card));
			player->addHistory(c->getClassName(),n);
			if (choice == "use")
				room->setCardFlag(card, "zhiyi_card");
			else
				player->drawCards(1, objectName());
			c->deleteLater();
		} else {
			if (player->getMark("zhiyi-Clear") > 0) return false;
			const Card *card = data.value<CardResponseStruct>().m_card;
			if (!card || !card->isKindOf("BasicCard")) return false;

			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->addPlayerMark(player, "zhiyi-Clear");

			QStringList choices;
			Card *c = Sanguosha->cloneCard(card->objectName());
			c->setSkillName("_" + objectName());
			if (c->isAvailable(player))
				choices << "use";
			choices << "draw";

			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(card));
			if (choice == "use") {
				room->setPlayerProperty(player, "zhiyi_card_name", c->objectName());

				if (room->askForUseCard(player, "@@zhiyi!", "@zhiyi:" + c->objectName(), -1, Card::MethodUse, false)) return false;

				QList<ServerPlayer *> targets = room->getCardTargets(player,c);
				if(!targets.isEmpty())
					room->useCard(CardUseStruct(c, player, targets.at(qsanRandomBounded(targets.length()))), false);
			}else
				player->drawCards(1, objectName());
			c->deleteLater();
		}
		return false;
	}
};

class SecondZhiyiVS : public ZeroCardViewAsSkill
{
public:
	SecondZhiyiVS() : ZeroCardViewAsSkill("secondzhiyi")
	{
		response_pattern = "@@secondzhiyi!";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		QString name = Self->property("secondzhiyi_card_name").toString();

		Card *c = Sanguosha->cloneCard(name);
		c->setSkillName("_secondzhiyi");
		return c;
	}
};

class SecondZhiyi : public PhaseChangeSkill
{
public:
	SecondZhiyi() : PhaseChangeSkill("secondzhiyi")
	{
		frequency = Compulsory;
		view_as_skill = new SecondZhiyiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer *, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			QStringList used_cards = p->getTag("SecondZhiyiUsedCard").toStringList();
			if (used_cards.isEmpty()) continue;

			room->sendCompulsoryTriggerLog(p, objectName(), true, true);
			QStringList choices;

			foreach (QString card_name, used_cards) {
				Card *c = Sanguosha->cloneCard(card_name);
				if (!c) continue;
				c->setSkillName("_secondzhiyi");
				if (c->isAvailable(p))
					choices << card_name;
				c->deleteLater();
			}
			choices << "draw";
			QString choice = room->askForChoice(p, objectName(), choices.join("+"));
			if (choice == "draw")
				p->drawCards(1, objectName());
			else {
				Card *c = Sanguosha->cloneCard(choice);
				c->setSkillName("_secondzhiyi");
				if (c->targetFixed())
					room->useCard(CardUseStruct(c, p), false);
				else {
					room->setPlayerProperty(p, "secondzhiyi_card_name", choice);
					if (!room->askForUseCard(p, "@@secondzhiyi!", "@secondzhiyi:" + choice)) {
						foreach (ServerPlayer *t, room->getAlivePlayers()) {
							if (c->targetFilter(QList<const Player *>(), t, p)){
								room->useCard(CardUseStruct(c, p, t), false);
								break;
							}
						}
					}
				}
				c->deleteLater();
			}
		}
		return false;
	}
};

class SecondZhiyiRecord : public TriggerSkill
{
public:
	SecondZhiyiRecord() : TriggerSkill("#secondzhiyi-record")
	{
		events << CardUsed << CardResponded << EventPhaseChanging;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers())
				p->removeTag("SecondZhiyiUsedCard");
		} else {
			if (player->isDead()) return false;
			const Card *c = nullptr;
			if (event == CardResponded)
				c = data.value<CardResponseStruct>().m_card;
			else
				c = data.value<CardUseStruct>().card;
			if (!c || !c->isKindOf("BasicCard")) return false;
			QStringList used_cards = player->getTag("SecondZhiyiUsedCard").toStringList();
			if (used_cards.contains(c->objectName())) return false;
			used_cards << c->objectName();
			player->setTag("SecondZhiyiUsedCard", used_cards);
		}
		return false;
	}
};

class Jimeng : public TriggerSkillV2
{
public:
	Jimeng() : TriggerSkillV2("jimeng")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
		for (ServerPlayer *target : room->getOtherPlayers(player))
			if (!target->isNude()) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isNude())
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@jimeng-invoke", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		for (int i = 0; i < getEffectiveAmount(ctx) && player->isAlive() && target->isAlive() && !target->isNude(); ++i) {
			const int id = room->askForCardChosen(player, target, "he", objectName());
			if (room->getCardOwner(id) == target) room->obtainCard(player, id, false);
		}
		// Movement triggers can change HP, cards and either participant's survival.
		if (!player->isAlive() || !target->isAlive() || player->isNude() || player->getHp() <= 0) return false;

		int n = qMin(player->getCardCount(), player->getHp());
		QString prompt = QString("@jimeng-give:%1::%2").arg(target->objectName()).arg(QString::number(n));
		const Card *c = room->askForExchange(player, objectName(), n, n, true, prompt);
		if (c && target->isAlive()) room->giveCard(player, target, c, objectName());
		return false;
	}
};

class Shuaiyan : public TriggerSkillV2
{
public:
	Shuaiyan() : TriggerSkillV2("shuaiyan")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Discard
			&& player->getHandcardNum() > 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isNude())
				targets << p;
		}

		if (targets.isEmpty()) {
			if (!player->askForSkillInvoke(this)) return false;
			return true;
		}

		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@shuaiyan-invoke", true, true);
		if (!target) return false;
		ctx.extra_data = target->objectName();
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		ctx.manual_effect = true;
		const QString chosen = ctx.extra_data.toString();
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.extra_data = QStringLiteral("reveal");
		skillEffect(event, room, player, ctx, player);
		ctx.modified_amount = amount;
		ctx.modified_amount_set = modified;
		ctx.extra_data = QStringLiteral("receive");
		ServerPlayer *target = room->findPlayerByObjectName(chosen);
		if (player->isAlive() && target && target->isAlive()) skillEffect(event, room, player, ctx, target);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.extra_data.toString() == "reveal") room->showAllCards(target);
		else if (!target->isNude() && player->isAlive()) {
			const int count = qMin(target->getCardCount(), getEffectiveAmount(ctx));
			if (count <= 0) return false;
			const Card *gift = room->askForExchange(target, objectName(), count, count, true, "@shuaiyan-give:" + player->objectName());
			if (gift && player->isAlive()) room->giveCard(target, player, gift, objectName());
		}
		return false;
	}
};

BeizhuCard::BeizhuCard()
{
}

bool BeizhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void BeizhuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const { ActiveSkillCard::use(room, source, targets); }

class Beizhu : public ViewAsSkillV2
{
public:
	Beizhu() : ViewAsSkillV2("beizhu") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
	{
		return targets.isEmpty() && target && target->isAlive() && target != request.initiator && !target->isKongcheng();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		BeizhuCard *card = new BeizhuCard;
		card->setActiveSkill(this);
		card->setSkillName(objectName());
		return card;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.targets.value(0);
		if (!target) return FinishSkill;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		const auto run = [&](const QString &action) {
			QVariantMap saved = ctx.extra_data.toMap(); saved["action"] = action; ctx.extra_data = saved;
			ctx.modified_amount = amount; ctx.modified_amount_set = modified;
			if (target->isAlive() && ctx.owner->isAlive()) skillEffect(ctx, target);
		};
		run("reveal");
		if (!ctx.extra_data.toMap().value("revealed").toBool()) return FinishSkill;
		const QVariantList slashes = ctx.extra_data.toMap().value("slashes").toList();
		if (slashes.isEmpty()) {
			run("discard");
			if (ctx.extra_data.toMap().value("discarded").toBool()) run("obtain");
		} else {
			for (const QVariant &id : slashes) {
				QVariantMap saved = ctx.extra_data.toMap(); saved["slash"] = id; ctx.extra_data = saved;
				run("slash");
			}
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = ctx.owner->getRoom();
		QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		if (action == "reveal") {
			if (target->isKongcheng()) return ContinueEffects;
			room->doGongxin(ctx.owner, target, {}, objectName());
			QVariantList slashes;
			for (const Card *card : target->getCards("h")) if (card->isKindOf("Slash")) slashes << card->getEffectiveId();
			saved["slashes"] = slashes; saved["revealed"] = true;
		} else if (action == "discard") {
			if (!ctx.owner->canDiscard(target, "he")) return ContinueEffects;
			const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), true, Card::MethodDiscard);
			if (!ctx.owner->canDiscard(target, id)) return ContinueEffects;
			room->throwCard(id, target, ctx.owner);
			// Grant the follow-up only after this effect actually discarded the chosen card.
			const qint64 skill = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
			QVariantMap query{{"from", target->objectName()}};
			bool discarded = false;
			for (;;) {
				const QVariantMap page = room->queryHistoryMoves(query);
				if (!page.value("complete").toBool()) { discarded = false; break; }
				for (const QVariant &entry : page.value("items").toList()) {
					const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
					if (move.value("card_id").toInt() == id && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
						&& skill > 0 && room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == skill) discarded = true;
				}
				if (!page.value("has_more").toBool()) break;
				query["after"] = page.value("next_after"); query["watermark"] = page.value("watermark");
			}
			saved["discarded"] = discarded;
		} else if (action == "obtain") {
			QList<int> slashes;
			for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf("Slash")) slashes << id;
			if (slashes.isEmpty() || !ctx.owner->askForSkillInvoke(objectName(), "beizhu:" + target->objectName(), false)) return ContinueEffects;
			room->obtainCard(target, slashes.at(qsanRandomBounded(slashes.length())), true);
		} else if (action == "slash") {
			const int id = saved.value("slash", -1).toInt();
			const Card *slash = id >= 0 ? Sanguosha->getCard(id) : nullptr;
			if (!slash || !slash->isKindOf("Slash") || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
				|| !target->canSlash(ctx.owner, slash, false)) return ContinueEffects;
			const quint64 serial = room->getTag("mobile_beizhu_serial").toULongLong() + 1;
			room->setTag("mobile_beizhu_serial", serial);
			QVariantMap receipt{{"serial", serial}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
				{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
				{"execution", ctx.executionID}, {"from", target->objectName()}, {"card", id}, {"amount", getEffectiveAmount(ctx)}};
			QVariantList receipts = ctx.owner->getTag("mobile_beizhu_receipts").toList(); receipts << receipt;
			ctx.owner->setTag("mobile_beizhu_receipts", receipts);
			const auto release = [&]() {
				QVariantList keep;
				for (const QVariant &entry : ctx.owner->getTag("mobile_beizhu_receipts").toList())
					if (entry.toMap().value("serial").toULongLong() != serial) keep << entry;
				ctx.owner->setTag("mobile_beizhu_receipts", keep);
			};
			try { room->useCardFromSkillEffect(CardUseStruct(slash, target, ctx.owner), ctx, true); }
			catch (...) { release(); throw; }
			release();
		}
		ctx.extra_data = saved;
		return ContinueEffects;
	}
};

class BeizhuDraw : public TriggerSkillV2
{
public:
	BeizhuDraw() : TriggerSkillV2("#beizhu-draw") { events << PreCardUsed << Damaged; global = true; frequency = Compulsory; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event != PreCardUsed) return true;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.from || !use.isAcceptedSkillEffectCard() || !use.card->isKindOf("Slash")) return true;
		const qint64 useId = use.targetModReveal.useHistoryEventId;
		if (useId <= 0) return true;
		for (ServerPlayer *owner : room->getAllPlayers(true)) {
			QVariantList receipts = owner->getTag("mobile_beizhu_receipts").toList();
			for (QVariant &entry : receipts) {
				QVariantMap receipt = entry.toMap();
				if (receipt.value("use").toLongLong() > 0 || receipt.value("execution").toLongLong() != use.skillExecutionID
					|| receipt.value("from").toString() != use.from->objectName() || receipt.value("card").toInt() != use.card->getEffectiveId()) continue;
				const SkillInstanceRef source(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
				const SkillInstanceRef activation(receipt.value("activation_owner").toString(), SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()));
				if (use.sourceRef != source || use.activationRef != activation) continue;
				receipt["use"] = useId; entry = receipt;
			}
			owner->setTag("mobile_beizhu_receipts", receipts);
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != Damaged || !player || !player->isAlive()) return true;
		const DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || !damage.card->isKindOf("Slash") || damage.damage <= 0) return true;
		const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
		for (const QVariant &entry : player->getTag("mobile_beizhu_receipts").toList()) {
			const QVariantMap receipt = entry.toMap();
			if (useId <= 0 || receipt.value("use").toLongLong() != useId || receipt.value("card").toInt() != damage.card->getEffectiveId()) continue;
			SkillContext ctx;
			ctx.skill_name = objectName(); ctx.owner = ctx.invoker = player;
			ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.amount = receipt.value("amount").toInt();
			ctx.targets = {player}; ctx.preferredTarget = player; ctx.preferredTargetSeat = player->getSeat();
			ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = receipt;
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.sourceRef.isValid() && ctx.owner->getTag("mobile_beizhu_receipts").toList().contains(ctx.extra_data);
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->sendCompulsoryTriggerLog(ctx.owner, "beizhu", true, true);
		target->drawCards(ctx.original_data->value<DamageStruct>().damage * getEffectiveAmount(ctx), "beizhu");
		return false;
	}
};
class Juliao : public DistanceSkillV2
{
public:
	Juliao() : DistanceSkillV2("juliao") { setHolderSelector(CorrectSkill_Secondary); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.holder) return CorrectSkillResult::noEffect();
		QSet<QString> kingdoms;
		for (const Player *player : ctx.holder->getAliveSiblings(true)) kingdoms << player->getKingdom();
		return CorrectSkillResult::useAmount(qMax(0, int(kingdoms.size()) - 1) * ctx.currentAmount);
	}
};
class Taomie : public TriggerSkill
{
public:
	Taomie() : TriggerSkill("taomie")
	{
		events << Damage << Damaged << DamageCaused;
	}

	bool transferMark(ServerPlayer *to, Room *room) const
	{
		int n = 0;
		foreach (ServerPlayer *p, room->getOtherPlayers(to)) {
			if (to->isDead()) break;
			if (p->isAlive() && p->getMark("&taomie") > 0) {
				n++;
				int mark = p->getMark("&taomie");
				p->loseAllMarks("&taomie");
				to->gainMark("&taomie", mark);
			}
		}
		return n > 0;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (event == Damage) {
			if (damage.to->isDead() || damage.to->getMark("&taomie") > 0 || !player->askForSkillInvoke(this, damage.to)) return false;
			room->broadcastSkillInvoke(objectName());
			if (transferMark(damage.to, room)) return false;
			damage.to->gainMark("&taomie", 1);
		} else if (event == Damaged) {
			if (!damage.from || damage.from->isDead() || damage.from->getMark("&taomie") > 0 ||
					!player->askForSkillInvoke(this, damage.from)) return false;
			room->broadcastSkillInvoke(objectName());
			if (transferMark(damage.from, room)) return false;
			damage.from->gainMark("&taomie", 1);
		} else {
			if (damage.to->isDead() || damage.to->getMark("&taomie") <= 0) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			QStringList choices;
			choices << "damage=" + damage.to->objectName();
			if (!damage.to->isAllNude())
				choices << "get=" + damage.to->objectName();
			choices << "all=" + damage.to->objectName();
			QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);

			/*LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = player;
			log.arg = "taomie:" + choice.split("=").first();
			room->sendLog(log);*/

			if (choice.startsWith("damage")) {
				++damage.damage;
				data = QVariant::fromValue(damage);
			} else if (choice.startsWith("get")) {
				if (damage.to->isAllNude()) return false;
				int id = room->askForCardChosen(player, damage.to, "hej", objectName());
				room->obtainCard(player, id, false);
				if (player->isDead() || room->getCardPlace(id) != Player::PlaceHand || room->getCardOwner(id) != player) return false;
				QList<int> list;
				list << id;
				room->askForYiji(player, list, objectName());
			} else {
				damage.tips << "taomie_throwmark_" + damage.to->objectName();
				++damage.damage;
				data = QVariant::fromValue(damage);
				if (damage.to->isAllNude()) return false;
				int id = room->askForCardChosen(player, damage.to, "hej", objectName());
				room->obtainCard(player, id, false);
				if (player->isDead() || room->getCardPlace(id) != Player::PlaceHand || room->getCardOwner(id) != player) return false;
				QList<int> list;
				list << id;
				room->askForYiji(player, list, objectName());
			}
		}
		return false;
	}
};

class TaomieMark : public TriggerSkill
{
public:
	TaomieMark() : TriggerSkill("#taomie-mark")
	{
		events << DamageComplete;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getMark("&taomie") > 0;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.tips.contains("taomie_throwmark_" + player->objectName())) return false;
		player->loseAllMarks("&taomie");
		return false;
	}
};

DaojiCard::DaojiCard()
{
	setSkillName("daoji");
	handling_method = Card::MethodNone;
	will_throw = false;
}
bool DaojiCard::targetFilter(const QList<const Player *> &targets, const Player *candidate, const Player *self) const
{
	return ActiveSkillCard::targetFilter(targets, candidate, self);
}
void DaojiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}
class Daoji : public ViewAsSkillV2
{
public:
	Daoji() : ViewAsSkillV2("daoji", 1) { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play && request.initiator->canDiscard("he");
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty() && !card->isKindOf("BasicCard")
			&& !request.initiator->isJilei(card) && (request.initiator->handCards().contains(card->getEffectiveId())
				|| request.initiator->getEquipsId().contains(card->getEffectiveId()));
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		DaojiCard *card = new DaojiCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
	{
		return targets.isEmpty() && target && target->isAlive() && target != request.initiator && !target->getEquips().isEmpty();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "DaojiCard"; }
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return false;
		const int id = request.selectedCardIds.first();
		if (room->getCardOwner(id) != ctx.owner || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
		room->throwCard(id, objectName(), ctx.owner, ctx.owner);
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		ServerPlayer *target = ctx.targets.value(0);
		if (!target || !target->isAlive() || !ctx.owner->isAlive()) return FinishSkill;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.extra_data = QVariantMap{{"step", "obtain"}};
		skillEffect(ctx, target);
		QVariantMap saved = ctx.extra_data.toMap();
		if (!saved.contains("equipment") || !ctx.owner->isAlive()) return FinishSkill;
		ctx.modified_amount = amount;
		ctx.modified_amount_set = modified;
		saved.insert("step", "use");
		ctx.extra_data = saved;
		skillEffect(ctx, ctx.owner);
		saved = ctx.extra_data.toMap();
		if (saved.value("weapon_used").toBool() && ctx.owner->isAlive() && target->isAlive()) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			saved.insert("step", "damage");
			ctx.extra_data = saved;
			skillEffect(ctx, target);
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		QVariantMap saved = ctx.extra_data.toMap();
		const QString step = saved.value("step").toString();
		if (step == "obtain") {
			if (target->getEquips().isEmpty() || !ctx.owner->isAlive()) return ContinueEffects;
			const int id = room->askForCardChosen(ctx.owner, target, "e", objectName());
			if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip) return ContinueEffects;
			saved.insert("equipment", id);
			ctx.extra_data = saved;
			room->obtainCard(ctx.owner, id);
		} else if (step == "use") {
			const int id = saved.value("equipment").toInt();
			const Card *card = Sanguosha->getCard(id);
			if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand || !card->isAvailable(target)) return ContinueEffects;
			// Complete this exact ordinary use before the separate retaliation effect.
			const bool used = room->useCardFromSkillEffect(CardUseStruct(card, target), ctx, true);
			saved.insert("weapon_used", used && card->isKindOf("Weapon"));
			ctx.extra_data = saved;
		} else room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
		return ContinueEffects;
	}
};
ZhouxuanCard::ZhouxuanCard()
{
	target_fixed = true;
}

void ZhouxuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isDead()) return;
	ServerPlayer *target = room->askForPlayerChosen(source, room->getOtherPlayers(source), "zhouxuan", "@zhouxuan-invoke");
	room->doAnimate(1, source->objectName(), target->objectName());
	QStringList names;
	names << "EquipCard" << "TrickCard";
	foreach (int id, Sanguosha->getRandomCards()) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (!c->isKindOf("BasicCard") || c->isKindOf("FireSlash") || c->isKindOf("ThunderSlash")) continue;
		QString name = c->objectName();
		if (names.contains(name)) continue;
		names << name;
	}
	if (names.isEmpty()) return;

	QString name = room->askForChoice(source, "zhouxuan", names.join("+"), QVariant::fromValue(target));

	/*LogMessage log;
	log.type = "#ZhouxuanChoice";
	log.from = source;
	log.to << target;
	log.arg = name;
	room->sendLog(log);*/

	/*QStringList zhouxuan = target->getTag("Zhouxuan" + source->objectName()).toStringList();
	if (!zhouxuan.contains(name)) {
		zhouxuan << name;
		target->setTag("Zhouxuan" + source->objectName(), zhouxuan);
		room->addPlayerMark(target, "&zhouxuan+" + name);
	}*/
	if (target->getTag("Zhouxuan" + source->objectName()).toString() == name) return;
	target->setTag("Zhouxuan" + source->objectName(), name);
	foreach (QString mark, target->getMarkNames()) {
		if (!mark.startsWith("&zhouxuan+") && !mark.endsWith("+#" + source->objectName())) continue;
		if (target->getMark(mark) <= 0) continue;
		room->setPlayerMark(target, mark, 0);
	}
	room->setPlayerMark(target, "&zhouxuan+" + name + "+#" + source->objectName(), 1, QList<ServerPlayer *>() << source);
}

class ZhouxuanVS : public OneCardViewAsSkill
{
public:
	ZhouxuanVS() :OneCardViewAsSkill("zhouxuan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("ZhouxuanCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		ZhouxuanCard *c = new ZhouxuanCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Zhouxuan : public TriggerSkill
{
public:
	Zhouxuan() : TriggerSkill("zhouxuan")
	{
		events << CardUsed << CardResponded << Death;
		view_as_skill = new ZhouxuanVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Death) {
			if (player != data.value<DeathStruct>().who) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player))
				player->removeTag("Zhouxuan" + p->objectName());
		} else {
			const Card *card = nullptr;
			if (event == CardUsed)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;
			if (card == nullptr || card->isKindOf("SkillCard")) return false;

			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->isDead() || !p->hasSkill(objectName(), true)) continue;
				QString zhouxuan = player->getTag("Zhouxuan" + p->objectName()).toString();
				if (zhouxuan.isEmpty()) continue;
				player->removeTag("Zhouxuan" + p->objectName());
				room->setPlayerMark(player, "&zhouxuan+" + zhouxuan + "+#" + p->objectName(), 0);

				if (p->isDead() || !p->hasSkill(objectName())) continue;
				bool same = false;
				if (card->isKindOf("EquipCard")) {
					if (zhouxuan != "EquipCard")
						continue;
					else
						same = true;
				}
				if (card->isKindOf("TrickCard")) {
					if (zhouxuan != "TrickCard")
						continue;
					else
						same = true;
				}
				if (!same) {
					if (card->sameNameWith(zhouxuan))
						same = true;
				}
				if (!same) continue;
				room->sendCompulsoryTriggerLog(p, objectName(), true, true);

				QList<ServerPlayer *> _player;
				_player.append(p);
				QList<int> yiji_cards = room->getNCards(3, false);

				CardsMoveStruct move(yiji_cards, nullptr, p, Player::PlaceTable, Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
				QList<CardsMoveStruct> moves;
				moves.append(move);
				room->notifyMoveCards(true, moves, false, _player);
				room->notifyMoveCards(false, moves, false, _player);

				QList<int> origin_yiji = yiji_cards;
				while (room->askForYiji(p, yiji_cards, objectName(), true, false, true, -1, room->getAlivePlayers())) {
					CardsMoveStruct move(QList<int>(), p, nullptr, Player::PlaceHand, Player::PlaceTable,
						CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
					foreach (int id, origin_yiji) {
						if (room->getCardPlace(id) != Player::DrawPile) {
							move.card_ids << id;
							yiji_cards.removeOne(id);
						}
					}
					origin_yiji = yiji_cards;
					QList<CardsMoveStruct> moves;
					moves.append(move);
					room->notifyMoveCards(true, moves, false, _player);
					room->notifyMoveCards(false, moves, false, _player);
					if (!p->isAlive())
						return false;
				}

				if (!yiji_cards.isEmpty()) {
					CardsMoveStruct move(yiji_cards, p, nullptr, Player::PlaceHand, Player::PlaceTable,
										CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
					QList<CardsMoveStruct> moves;
					moves.append(move);
					room->notifyMoveCards(true, moves, false, _player);
					room->notifyMoveCards(false, moves, false, _player);

					DummyCard *dummy = new DummyCard(yiji_cards);
					p->obtainCard(dummy, false);
					dummy->deleteLater();
				}
			}
		}
		return false;
	}
};

class Fengji : public TriggerSkillV2
{
public:
	Fengji() : TriggerSkillV2("fengji")
	{
		events << EventPhaseStart << EventPhaseChanging << TurnStart;
		frequency = Compulsory;
		m_baseAmount = 2;
	}
	static int previousHand(Room *room, const ServerPlayer *player)
	{
		const qint64 current = room->historyScopes().value("turn_id").toLongLong();
		if (current <= 0) return -1;
		qint64 previous = 0;
		QVariantMap query{{"kind", "turn"}, {"player", player->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryEvents(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const qint64 id = entry.toMap().value("id").toLongLong();
				if (id < current) previous = qMax(previous, id);
			}
			if (!page.value("has_more").toBool()) break;
			query["after"] = page.value("next_after"); query["watermark"] = page.value("watermark");
		}
		if (previous <= 0) return -1;
		qint64 end = 0;
		query = {{"kind", "turn_hp_snapshot"}, {"event_id", previous}, {"player", player->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap();
				if (fact.value("data").toMap().value("boundary").toString() == "end") end = fact.value("sequence").toLongLong();
			}
			if (!page.value("has_more").toBool()) break;
			query["after"] = page.value("next_after"); query["watermark"] = page.value("watermark");
		}
		if (end <= 0) return -1;
		// Reconstruct the hand at that exact boundary, including changes made before this grant existed.
		query = {{"kind", "player_state"}, {"player", player->objectName()}, {"watermark", end}};
		bool baseline = false;
		int hand = -1;
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap state = entry.toMap().value("data").toMap();
				if (!state.contains("hand_count_after")) return -1;
				baseline = baseline || state.value("boundary").toString() == "baseline";
				hand = state.value("hand_count_after").toInt();
			}
			if (!page.value("has_more").toBool()) break;
			query["after"] = page.value("next_after"); query["watermark"] = page.value("watermark");
		}
		return baseline ? hand : -1;
	}
	static bool applied(Room *room, const Player *player)
	{
		const QVariant turn = room->historyScopes().value("turn_id");
		for (const QVariant &entry : player->getTag("mobile_fengji_receipts").toList())
			if (entry.toMap().value("turn") == turn) return true;
		return false;
	}
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
	{
		for (ServerPlayer *player : room->getAllPlayers(true)) {
			QVariantList keep;
			for (const QVariant &entry : player->getTag("mobile_fengji_receipts").toList())
				if (room->historyEvent(entry.toMap().value("turn").toLongLong()).value("status").toString() != "finished") keep << entry;
			player->setTag("mobile_fengji_receipts", keep);
			room->setPlayerMark(player, "fengji_applied", applied(room, player) ? 1 : 0);
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
			|| player->getPhase() != Player::RoundStart) return {};
		const int previous = previousHand(room, player);
		return previous >= 0 && player->getHandcardNum() >= previous ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.owner};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantList receipts = target->getTag("mobile_fengji_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
			{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
			{"turn", room->historyScopes().value("turn_id")}};
		target->setTag("mobile_fengji_receipts", receipts);
		room->setPlayerMark(target, "fengji_applied", 1);
		room->sendCompulsoryTriggerLog(ctx.owner, this);
		target->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};

class FengjiMax : public MaxCardsSkillV2
{
public:
	FengjiMax() : MaxCardsSkillV2("#fengji-max") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &) const override
	{
		return CorrectSkillResult::noEffect();
	}
	CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
	{
		const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(ctx.primary);
		return ctx.primary && (server ? Fengji::applied(server->getRoom(), server) : ctx.primary->getMark("fengji_applied") > 0)
			? CorrectSkillResult::useAmount(ctx.primary->getMaxHp()) : CorrectSkillResult::noEffect();
	}
};
class Bingqing : public TriggerSkillV2
{
public:
	Bingqing() : TriggerSkillV2("bingqing") { events << CardFinished; }
	static int suitOrdinal(Room *room, ServerPlayer *owner)
	{
		const QVariantMap use = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		const qint64 current = use.value("id").toLongLong();
		if (current <= 0) return -1;
		QVariantMap query{{"kind", "use_card"}, {"phase_id", use.value("phase_id")}, {"from", owner->objectName()}};
		QMap<int, int> suits;
		int currentSuit = -1;
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap();
				const QVariantMap card = fact.value("data").toMap().value("card").toMap();
				if (!card.contains("type") || !card.contains("suit")) return -1;
				const int suit = card.value("suit").toInt();
				if (card.value("type").toInt() == Card::TypeSkill || suit < Card::Spade || suit > Card::Diamond) continue;
				++suits[suit];
				if (fact.value("event_id").toLongLong() == current) currentSuit = suit;
			}
			// The printed rule checks all other cards at resolution, including nested uses.
			if (!page.value("has_more").toBool()) return currentSuit < 0 ? -1 : suits.value(currentSuit) == 1 ? suits.size() : 0;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
		TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
		const Card *card = data.value<CardUseStruct>().card;
		if (!card || card->isKindOf("SkillCard") || !card->hasSuit()) return {};
		const int ordinal = suitOrdinal(room, player);
		if (ordinal < 0) qWarning("Bingqing: incomplete phase card-suit history");
		return ordinal >= 2 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const int ordinal = suitOrdinal(room, player);
		if (ordinal < 2 || ordinal > 4) return false;
		QList<ServerPlayer *> targets;
		for (ServerPlayer *target : room->getAlivePlayers()) {
			if (ordinal == 2 || (ordinal == 3 && player->canDiscard(target, "hej")) || (ordinal == 4 && target != player))
				targets << target;
		}
		if (targets.isEmpty()) return false;
		const QString prompt = ordinal == 2 ? "@bingqing-draw" : ordinal == 3 ? "@bingqing-discard" : "@bingqing-damage";
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), prompt, true, true);
		if (!target) return false;
		ctx.targets = {target};
		ctx.extra_data = ordinal;
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		const int ordinal = ctx.extra_data.toInt();
		if (ordinal == 2) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
		else if (ordinal == 4) room->damage(DamageStruct(objectName(), player, target, getEffectiveAmount(ctx)));
		else for (int i = 0; i < getEffectiveAmount(ctx) && player->isAlive() && target->isAlive() && player->canDiscard(target, "hej"); ++i) {
			const int id = room->askForCardChosen(player, target, "hej", objectName(), false, Card::MethodDiscard);
			if (player->canDiscard(target, id)) room->throwCard(id, target, player);
		}
		return false;
	}
};
class Yingfeng : public TriggerSkillV2
{
public:
	Yingfeng() : TriggerSkillV2("yingfeng") { events << EventPhaseStart; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return {};
		for (ServerPlayer *target : room->getAlivePlayers()) if (target->getMark("&mjyffeng") == 0) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		bool transfer = false;
		QList<ServerPlayer *> targets;
		for (ServerPlayer *target : room->getAlivePlayers()) {
			if (target->getMark("&mjyffeng") > 0) transfer = true;
			else targets << target;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(),
			transfer ? "@yingfeng-transfer" : "@yingfeng-gain", true, true);
		if (!target) return false;
		ctx.targets = {target};
		ctx.extra_data = transfer;
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		QVariantList receipts;
		if (ctx.extra_data.toBool()) {
			// Wind is a transferable public token; retain every contributing source.
			for (ServerPlayer *holder : room->getAllPlayers(true)) {
				receipts.append(holder->getTag("mobile_yingfeng_receipts").toList());
				holder->removeTag("mobile_yingfeng_receipts");
				room->setPlayerMark(holder, "&mjyffeng", 0);
			}
		} else {
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
		}
		target->setTag("mobile_yingfeng_receipts", receipts);
		int count = 0;
		for (const QVariant &receipt : receipts) count += receipt.toMap().value("amount").toInt();
		room->setPlayerMark(target, "&mjyffeng", count);
		return false;
	}
};

class YingfengTarget : public TargetModSkillV2
{
public:
	YingfengTarget() : TargetModSkillV2("#yingfeng")
	{
		pattern = ".";
		setHolderSelector(CorrectSkill_System);
	}
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == DistanceLimit && ctx.primary && ctx.primary->getMark("&mjyffeng") > 0
			? CorrectSkillResult::useAmount(1000 * ctx.currentAmount) : CorrectSkillResult::noEffect();
	}
};
class Huantu : public TriggerSkillV2
{
public:
	Huantu() : TriggerSkillV2("huantu") { events << EventPhaseChanging << EventSkillInvoking; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Round; }
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		TriggerList result;
		if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().to != Player::Draw || player->isSkipped(Player::Draw)) return result;
		for (ServerPlayer *owner : room->getOtherPlayers(player))
			if (owner->hasSkill(objectName()) && !owner->isNude() && owner->inMyAttackRange(player)) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !ctx.invoker || !ctx.invoker->isAlive()) return false;
		const Card *card = room->askForCard(player, "..", "@huantu-invoke:" + ctx.invoker->objectName(), *ctx.original_data, Card::MethodNone);
		if (!card) return false;
		ctx.extra_data = card->getEffectiveId();
		ctx.targets = {ctx.invoker};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const int id = ctx.extra_data.toInt();
		if (!isUsable(ctx) || room->getCardOwner(id) != player
			|| (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		const int id = ctx.extra_data.toInt();
		if (room->getCardOwner(id) != player) return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->giveCard(player, target, Sanguosha->getCard(id), objectName());
		if (!target->isAlive()) return false;
		target->skip(Player::Draw);
		QVariantList receipts = target->getTag("mobile_huantu_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
			{"turn", room->historyScopes().value("turn_id")}, {"token", QString::number(room->currentHistoryEventId())}};
		target->setTag("mobile_huantu_receipts", receipts);
		return false;
	}
};

class HuantuFinish : public TriggerSkillV2
{
public:
	HuantuFinish() : TriggerSkillV2("#huantu-finish") { events << EventPhaseEnd << EventPhaseChanging; global = true; }
	bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
			player->removeTag("mobile_huantu_receipts");
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != EventPhaseEnd || !player || player->getPhase() != Player::Finish) return true;
		for (const QVariant &value : player->getTag("mobile_huantu_receipts").toList()) {
			const QVariantMap receipt = value.toMap();
			if (receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString());
			if (!ctx.owner || !ctx.owner->isAlive()) continue;
			ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.amount = receipt.value("amount").toInt();
			ctx.current_event = event;
			ctx.original_data = &data;
			ctx.extra_data = QVariantMap{{"receipt", receipt}};
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.owner->isAlive() && ctx.invoker
			&& ctx.invoker->getTag("mobile_huantu_receipts").toList().contains(ctx.extra_data.toMap().value("receipt"));
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player->askForSkillInvoke("huantu", ctx.invoker)) return false;
		QStringList choices;
		if (ctx.invoker->isAlive()) choices << "recover=" + ctx.invoker->objectName();
		choices << "draw=" + ctx.invoker->objectName();
		QVariantMap saved = ctx.extra_data.toMap();
		saved.insert("choice", room->askForChoice(player, "huantu", choices.join("+"), QVariant::fromValue(ctx.invoker)));
		ctx.extra_data = saved;
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariantList receipts = ctx.invoker->getTag("mobile_huantu_receipts").toList();
		receipts.removeOne(ctx.extra_data.toMap().value("receipt"));
		ctx.invoker->setTag("mobile_huantu_receipts", receipts);
		ctx.manual_effect = true;
		room->broadcastSkillInvoke("huantu");
		QVariantMap saved = ctx.extra_data.toMap();
		if (saved.value("choice").toString().startsWith("recover")) {
			saved.insert("step", "recover");
			ctx.extra_data = saved;
			skillEffect(event, room, player, ctx, ctx.invoker);
		} else {
			const int amount = ctx.modified_amount;
			const bool modified = ctx.modified_amount_set;
			saved.insert("step", "draw");
			ctx.extra_data = saved;
			skillEffect(event, room, player, ctx, player);
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			saved.insert("step", "give");
			ctx.extra_data = saved;
			if (player->isAlive() && ctx.invoker->isAlive()) skillEffect(event, room, player, ctx, ctx.invoker);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		const QString step = ctx.extra_data.toMap().value("step").toString();
		const int amount = getEffectiveAmount(ctx);
		if (step == "recover") {
			room->recover(target, RecoverStruct(player, nullptr, amount, "huantu"));
			if (target->isAlive()) target->drawCards(2 * amount, "huantu");
		} else if (step == "draw") target->drawCards(3 * amount, "huantu");
		else if (!player->isKongcheng()) {
			const int count = qMin(player->getHandcardNum(), 2 * amount);
			if (count <= 0) return false;
			const Card *gift = room->askForExchange(player, "huantu", count, count, false, "@huantu-give:" + target->objectName());
			if (gift && target->isAlive()) room->giveCard(player, target, gift, "huantu");
		}
		return false;
	}
};
class Bihuo : public TriggerSkillV2
{
public:
	Bihuo() : TriggerSkillV2("bihuo")
	{
		events << QuitDying << RoundStart << EventSkillInvoking;
		frequency = Limited;
		limit_mark = "@bihuoMark";
		global = true;
	}
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Game; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == EventSkillInvoking) return false;
		if (event == RoundStart && player) {
			player->removeTag("mobile_bihuo_receipts");
			room->setPlayerMark(player, "&bihuo_lun", 0);
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == EventSkillInvoking) return {};
		TriggerList result;
		if (event != QuitDying || !player || !player->isAlive()) return result;
		for (ServerPlayer *owner : room->getAlivePlayers())
			if (owner->hasSkill(objectName())) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !ctx.invoker || !ctx.invoker->isAlive() || !ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
		ctx.targets = {ctx.invoker};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		room->removePlayerMark(ctx.owner, limit_mark);
		room->doSuperLightbox(ctx.owner, objectName());
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		target->drawCards(3 * getEffectiveAmount(ctx), objectName());
		if (!target->isAlive()) return false;
		QVariantList receipts = target->getTag("mobile_bihuo_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
			{"round", room->historyScopes().value("round_id")}};
		target->setTag("mobile_bihuo_receipts", receipts);
		// This public round token is an applied effect, independent of source availability.
		room->addPlayerMark(target, "&bihuo_lun", getEffectiveAmount(ctx));
		return false;
	}
};

class BihuoDistance : public DistanceSkillV2
{
public:
	BihuoDistance() : DistanceSkillV2("#bihuo") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.secondary || ctx.secondary->getMark("&bihuo_lun") <= 0) return CorrectSkillResult::noEffect();
		QSet<QString> kingdoms;
		for (const Player *player : ctx.secondary->getAliveSiblings(true)) kingdoms << player->getKingdom();
		return CorrectSkillResult::useAmount(ctx.secondary->getMark("&bihuo_lun") * kingdoms.size() * ctx.currentAmount);
	}
};
class JibingVS : public ViewAsSkillV2
{
public:
	JibingVS() : ViewAsSkillV2("jibing", 1) { expand_pile = "jbbing"; response_or_use = true; }
	bool willThrowSelectedCards() const override { return false; }
	QString cardName(const ActiveSkillRequest &request) const
	{
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return "slash";
		if (request.pattern == "jink") return "jink";
		if (request.pattern.contains("slash", Qt::CaseInsensitive)) return "slash";
		return QString();
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.initiator->getPile("jbbing").isEmpty() || cardName(request).isEmpty()) return false;
		std::unique_ptr<Card> card(Sanguosha->cloneCard(cardName(request)));
		card->setSkillName(objectName());
		return request.reason != CardUseStruct::CARD_USE_REASON_PLAY || card->isAvailable(request.initiator);
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && request.selectedCardIds.isEmpty() && card
			&& request.initiator->getPile("jbbing").contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.length() == 1; }
	QString historyKey(const ActiveSkillRequest &request) const override { return cardName(request) == "jink" ? "Jink" : "Slash"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request) || cardName(request).isEmpty()) return nullptr;
		Card *card = Sanguosha->cloneCard(cardName(request));
		card->setSkillName(objectName());
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		// The ordinary card pipeline consumes the pile material after authoritative validation.
		return ctx.owner && request.selectedCardIds.length() == 1 && ctx.owner->getPile("jbbing").contains(request.selectedCardIds.first());
	}
};

class Jibing : public TriggerSkillV2
{
public:
	Jibing() : TriggerSkillV2("jibing") { events << EventPhaseStart; view_as_skill = new JibingVS; }
	static int getKingdoms(const ServerPlayer *player)
	{
		QSet<QString> kingdoms;
		for (ServerPlayer *other : player->getRoom()->getAlivePlayers()) kingdoms << other->getKingdom();
		return kingdoms.size();
	}
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw
			&& player->getPile("jbbing").length() < getKingdoms(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player->askForSkillInvoke(this)) return false;
		ctx.targets = {player};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		const int count = 2 * getEffectiveAmount(ctx);
		if (count > 0) target->addToPile("jbbing", room->getNCards(count, false));
		return true;
	}
};

class Wangjing : public TriggerSkillV2
{
public:
	Wangjing() : TriggerSkillV2("wangjing") { events << CardUsed << CardResponded; frequency = Compulsory; }
	static bool highestHp(ServerPlayer *player)
	{
		if (!player || !player->isAlive()) return false;
		for (ServerPlayer *other : player->getRoom()->getAlivePlayers()) if (other->getHp() > player->getHp()) return false;
		return true;
	}
	static int count(TriggerEvent event, const QVariant &data)
	{
		if (event == CardResponded) {
			const CardResponseStruct response = data.value<CardResponseStruct>();
			return response.m_card && !response.m_card->isKindOf("SkillCard")
				&& response.m_card->getSkillNames().contains("jibing") && highestHp(response.m_who) ? 1 : 0;
		}
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->isKindOf("SkillCard") || !use.card->getSkillNames().contains("jibing")) return 0;
		int result = 0;
		for (ServerPlayer *target : use.to) if (highestHp(target)) ++result;
		return result;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && count(event, data) > 0
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.extra_data = count(event, *ctx.original_data);
		ctx.targets = {player};
		return ctx.extra_data.toInt() > 0;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->sendCompulsoryTriggerLog(ctx.owner, this);
		for (int i = 0; i < ctx.extra_data.toInt() && target->isAlive(); ++i) target->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};
class Moucuan : public TriggerSkillV2
{
public:
	Moucuan() : TriggerSkillV2("moucuan") { events << EventPhaseStart << EventSkillInvoking; frequency = Wake; waked_skills = "binghuo"; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Game; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (event == EventSkillInvoking) return {};
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
			&& (player->getPile("jbbing").length() >= Jibing::getKingdoms(player) || player->canWake(objectName()))
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		ctx.targets = {player};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		room->setPlayerMark(player, objectName(), 1);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (target->getPile("jbbing").length() >= Jibing::getKingdoms(target)) {
			LogMessage log;
			log.type = "#ZaoxianWake";
			log.from = target;
			log.arg = QString::number(target->getPile("jbbing").length());
			log.arg2 = objectName();
			log.arg3 = "jbbing";
			room->sendLog(log);
		}
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(ctx.owner, objectName());
		room->doSuperLightbox(ctx.owner, objectName());
		// Awakening grants a permanent skill, rather than a temporary child of the wake trigger.
		if (room->changeMaxHpForAwakenSkill(target, -getEffectiveAmount(ctx), objectName())) room->acquireSkill(target, "binghuo");
		return false;
	}
};
class Binghuo : public TriggerSkill
{
public:
	Binghuo() : TriggerSkill("binghuo")
	{
		events << PreCardUsed << PreCardResponded << EventPhaseStart;
		global = true;//
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == PreCardResponded) {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (res.m_card->getSkillNames().contains("jibing"))
				player->addMark("jibing_used-Clear");
			if (res.m_who&&res.m_toCard)
				res.m_who->addMark("tenyearsigong_xiangying-Clear");
			if (res.m_isUse&&player->getPhase()<=Player::Play){
				player->addMark("jingce-Clear");
			}
			if (player->getPhase() == Player::Play)
				player->addMark("dev_zhuaji_use-Clear");
		} else if (event == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (use.card->getSkillNames().contains("jibing"))
				player->addMark("jibing_used-Clear");
			if (use.whocard&&use.who)
				use.who->addMark("tenyearsigong_xiangying-Clear");
			foreach(ServerPlayer *p, use.to)
				p->addMark("secondsouying_num_" + use.from->objectName() + p->objectName() + "-Clear");
			if (use.card->isKindOf("Analeptic"))
				player->addMark("mtchuanjiu_Analeptic-Clear");
			if (use.card->getHandlingMethod() == Card::MethodUse) {
				int n = player->getMark("xingwu");
				if (use.card->isBlack())
					n |= 1;
				else if (use.card->isRed())
					n |= 2;
				player->setMark("xingwu", n);
			}
			if (player->getPhase() <= Player::Play) 
				player->addMark("jingce-Clear");
			if (player->getPhase() == Player::Play){
				player->addMark("dev_zhuaji_use-Clear");
				player->addMark("secondmobilexinzifu-PlayClear");
				if(player->getMark("fengporec-PlayClear")<1){
					player->removeTag("fengpoaddDamage" + use.card->toString());
					room->setCardFlag(use.card, "fengporecc");
					player->addMark("fengporec-PlayClear");
				}
			}
			if (use.card->isKindOf("TrickCard"))
				player->addMark("olcangzhuo_usedtrick-Clear");
			if (use.card->isKindOf("Slash")){
				if(player->getPhase() == Player::Play){
					room->setPlayerFlag(player, "ForbidFuluan");
					foreach(ServerPlayer *p, use.to)
						room->addPlayerMark(p,"chixin-PlayClear");
				}
				if (!room->getTag("XinghanRecord").toBool()) {
					room->setCardFlag(use.card, "xinghan_first_slash");
					room->setTag("XinghanRecord", true);
				}
			}
			player->addMark("tenyearjingce-Clear");
			QString s = use.card->getSuitString();
			if (s == "no_suit_black" || s == "no_suit_red")
				s = "no_suit";
			player->setMark("secondtenyearjingce" + s + "-Clear", 1);
			if (player->getMark("secondtenyearlihuo-Clear")<1){
				room->setCardFlag(use.card, "first_card_in_one_turn");
				player->addMark("secondtenyearlihuo-Clear");
			}
		} else {
			if (player->getPhase() == Player::Finish){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if (p->getMark("jibing_used-Clear")<1||!p->hasSkill(objectName())) continue;
					ServerPlayer *target = room->askForPlayerChosen(p, room->getAlivePlayers(), objectName(), "@binghuo-invoke", true, true);
					if (!target) continue;
					p->peiyin(this);
					JudgeStruct judge;
					judge.who = target;
					judge.reason = objectName();
					judge.play_animation = true;
					judge.pattern = ".|black";
					judge.good = false;
					judge.negative = true;
					room->judge(judge);
					if (judge.isBad())
						room->damage(DamageStruct("binghuo", p, target, 1, DamageStruct::Thunder));
				}
			}else if (player->getPhase() == Player::NotActive){
				room->setTag("XinghanRecord", false);
			}else if(player->getPhase() == Player::RoundStart){
				foreach (ServerPlayer *p, room->getAlivePlayers())
				p->setMark("mtzhongyi_hp-Keep", p->getHp());
			}
		}
		return false;
	}
};

class Renshi : public TriggerSkillV2
{
public:
	Renshi(const QString &name) : TriggerSkillV2(name) { events << DamageInflicted; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && player->isWounded()
			&& damage.card && damage.card->isKindOf("Slash") && damage.damage > 0
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.owner};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		target->damageRevises(*ctx.original_data, -damage.damage);
		LogMessage log;
		log.type = "#RenshiPrevent";
		log.from = ctx.owner;
		if (damage.from) log.to << damage.from;
		log.arg = objectName();
		log.arg2 = QString::number(damage.damage);
		room->sendLog(log);
		ctx.owner->peiyin(this);
		room->notifySkillInvoked(ctx.owner, objectName());
		if (objectName() == "renshi" && room->CardInTable(damage.card))
			room->obtainCard(ctx.owner, damage.card, true);
		else if (objectName() == "tenyeardeshi") {
			QList<int> slashes;
			for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf("Slash")) slashes << id;
			if (!slashes.isEmpty()) room->obtainCard(ctx.owner, slashes.at(qsanRandomBounded(slashes.size())));
		}
		room->loseMaxHp(ctx.owner, getEffectiveAmount(ctx), objectName());
		return true;
	}
};
class Huaizi : public MaxCardsSkillV2
{
public:
	Huaizi() : MaxCardsSkillV2("huaizi") {}
	CorrectSkillResult getCorrection(const CorrectSkillContext &) const override
	{
		return CorrectSkillResult::noEffect();
	}
	CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
	{
		return ctx.holder ? CorrectSkillResult::useAmount(ctx.holder->getMaxHp())
			: CorrectSkillResult::noEffect();
	}
};
WuyuanCard::WuyuanCard(const QString &name)
{
	setSkillName(name);
	will_throw = false;
	handling_method = Card::MethodNone;
}

void WuyuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

TenyearWuyuanCard::TenyearWuyuanCard() : WuyuanCard("tenyearwuyuan") {}

class Wuyuan : public ViewAsSkillV2
{
public:
	Wuyuan(const QString &name) : ViewAsSkillV2(name, 1) { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play;
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty() && card->isKindOf("Slash")
			&& request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.length() == 1; }
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		return target && target->isAlive() && target != request.initiator && selected.isEmpty();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return objectName() == "wuyuan" ? "WuyuanCard" : "TenyearWuyuanCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		WuyuanCard *card = objectName() == "wuyuan" ? new WuyuanCard : new TenyearWuyuanCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.length() != 1 || request.selectedTargetNames.length() != 1) return false;
		const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
		ctx.extra_data = QVariantMap{{"id", card->getEffectiveId()}, {"red", card->isRed()},
			{"nature", card->isKindOf("NatureSlash")}, {"target", request.selectedTargetNames.first()}};
		return true;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		const int id = ctx.extra_data.toMap().value("id", -1).toInt();
		return id >= 0 && room->getCardOwner(id) == ctx.owner && room->getCardPlace(id) == Player::PlaceHand
			&& Sanguosha->getCard(id)->isKindOf("Slash");
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		Room *room = ctx.owner->getRoom();
		QVariantMap saved = ctx.extra_data.toMap();
		ServerPlayer *target = room->findPlayerByObjectName(saved.value("target").toString());
		if (!target || !target->isAlive()) return FinishSkill;
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		auto apply = [&](const QString &step, ServerPlayer *recipient) {
			if (!recipient->isAlive()) return;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			QVariantMap current = saved;
			current.insert("step", step);
			ctx.extra_data = current;
			skillEffect(ctx, recipient);
		};
		apply("give", target);
		if (!ctx.extra_data.toMap().value("given").toBool()) return FinishSkill;
		apply("recover", ctx.owner);
		QList<ServerPlayer *> drawers{target};
		if (objectName() == "tenyearwuyuan") drawers << ctx.owner;
		room->sortByActionOrder(drawers);
		for (ServerPlayer *drawer : drawers) apply("draw", drawer);
		if (saved.value("red").toBool()) apply("recover", target);
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		QVariantMap saved = ctx.extra_data.toMap();
		const QString step = saved.value("step").toString();
		if (step == "give") {
			const int id = saved.value("id", -1).toInt();
			if (id < 0 || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
			// Record material identity before the give triggers any nested effect.
			room->giveCard(ctx.owner, target, Sanguosha->getCard(id), objectName());
			saved.insert("given", true);
			ctx.extra_data = saved;
		} else if (step == "recover") room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
		else target->drawCards((target != ctx.owner && saved.value("nature").toBool() ? 2 : 1) * getEffectiveAmount(ctx), objectName());
		return ContinueEffects;
	}
};
class NewTunchu : public TriggerSkillV2
{
public:
	NewTunchu() : TriggerSkillV2("newtunchu") { events << DrawNCards; m_baseAmount = 2; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const DrawStruct draw = data.value<DrawStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && draw.who == player
			&& draw.reason == "draw_phase" && draw.historyEventId > 0 && player->getPile("food").isEmpty()
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player->askForSkillInvoke(this)) return false;
		ctx.targets = {player};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		DrawStruct draw = ctx.original_data->value<DrawStruct>();
		if (draw.historyEventId <= 0 || draw.who != target) return false;
		draw.num += getEffectiveAmount(ctx);
		ctx.original_data->setValue(draw);
		const quint64 serial = room->getTag("mobile_newtunchu_serial").toULongLong() + 1;
		room->setTag("mobile_newtunchu_serial", serial);
		QVariantList pending = target->getTag("mobile_newtunchu_draws").toList();
		pending << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
			{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
			{"serial", serial}, {"draw", QVariant::fromValue(draw.historyEventId)}};
		target->setTag("mobile_newtunchu_draws", pending);
		room->broadcastSkillInvoke(objectName());
		return false;
	}
};

class NewTunchuPut : public TriggerSkillV2
{
public:
	NewTunchuPut() : TriggerSkillV2("#newtunchu-put")
	{
		events << AfterDrawNCards << DrawNCards << EventPhaseChanging;
		global = true;
		frequency = Compulsory;
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		if (event == AfterDrawNCards) {
			const qint64 draw = data.value<DrawStruct>().historyEventId;
			QVariantList pending = player->getTag("mobile_newtunchu_draws").toList();
			int ordinal = 0;
			for (QVariant &entry : pending) {
				QVariantMap receipt = entry.toMap();
				if (draw > 0 && receipt.value("draw").toLongLong() == draw) {
					receipt["ordinal"] = ++ordinal; entry = receipt;
				}
			}
			player->setTag("mobile_newtunchu_draws", pending);
			return true;
		}
		QVariantList keep;
		for (const QVariant &entry : player->getTag("mobile_newtunchu_draws").toList()) {
			// Keep an outer draw until its AfterDraw callbacks finish, even inside a nested draw.
			const QVariantMap result = room->historyEvent(entry.toMap().value("draw").toLongLong());
			if (result.value("status").toString() != "finished") keep << entry;
		}
		player->setTag("mobile_newtunchu_draws", keep);
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != AfterDrawNCards || !player || !player->isAlive()) return true;
		const DrawStruct draw = data.value<DrawStruct>();
		if (draw.who != player || draw.historyEventId <= 0) return true;
		for (const QVariant &entry : player->getTag("mobile_newtunchu_draws").toList()) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("draw").toLongLong() != draw.historyEventId) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = ctx.invoker = ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.trigger_count = receipt.value("ordinal").toInt();
			ctx.preferredTarget = player; ctx.preferredTargetSeat = player->getSeat();
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.extra_data = receipt;
			ctx.targets = {player};
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("mobile_newtunchu_draws").toList().contains(ctx.extra_data);
	}
	bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariantList pending = player->getTag("mobile_newtunchu_draws").toList();
		pending.removeOne(ctx.extra_data);
		player->setTag("mobile_newtunchu_draws", pending);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
	{
		if (!target->isAlive() || target->isKongcheng()) return false;
		const Card *selection = room->askForExchange(target, "newtunchu", target->getHandcardNum(), 1, false, "@newtunchu-put", true);
		if (!selection) return false;
		const QList<int> ids = selection->getSubcards();
		for (int id : ids)
			if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) return false;
		if (!ids.isEmpty()) target->addToPile("food", ids);
		return false;
	}
};
class NewTunchuLimit : public CardLimitSkill
{
public:
	NewTunchuLimit() : CardLimitSkill("#newtunchu-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->getPile("food").length()>0&&target->hasSkill("newtunchu"))
			return "Slash";
		return "";
	}
};

NewShuliangCard::NewShuliangCard()
{
	setSkillName("newshuliang");
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void NewShuliangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class NewShuliangVS : public ViewAsSkillV2
{
public:
	NewShuliangVS() : ViewAsSkillV2("newshuliang", 1) { response_pattern = "@@newshuliang"; expand_pile = "food"; }
	TargetMode targetMode() const override { return NoTarget; }
	bool willThrowSelectedCards() const override { return false; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.pattern == "@@newshuliang"
			&& request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
			&& !request.initiator->getPile("food").isEmpty()
			&& !request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
				request.activationRef.key.instanceID, "pending_target").toString().isEmpty();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty()
			&& request.initiator->getPile("food").contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.length() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "NewShuliangCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		NewShuliangCard *card = new NewShuliangCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		ctx.extra_data = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "pending_target");
		return !ctx.extra_data.toString().isEmpty();
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.length() != 1 || !ctx.owner->getPile("food").contains(request.selectedCardIds.first())) return false;
		CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), "");
		room->moveCardTo(Sanguosha->getCard(request.selectedCardIds.first()), nullptr, Player::DiscardPile, reason, true);
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		ServerPlayer *target = ctx.owner->getRoom()->findPlayerByObjectName(ctx.extra_data.toString());
		if (target && target->isAlive()) skillEffect(ctx, target);
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		target->drawCards(2 * getEffectiveAmount(ctx), objectName());
		return ContinueEffects;
	}
};

class NewShuliang : public TriggerSkillV2
{
public:
	NewShuliang() : TriggerSkillV2("newshuliang") { events << EventPhaseStart; view_as_skill = new NewShuliangVS; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || player->getPhase() != Player::Finish || player->getHandcardNum() >= player->getHp()) return result;
		for (ServerPlayer *owner : room->getAlivePlayers())
			if (owner->hasSkill(objectName()) && !owner->getPile("food").isEmpty()) result[owner] << objectName();
		return result;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!ctx.invoker || !ctx.invoker->isAlive()) return false;
		Room::AcceptedViewAsEffectScope selection(room, player, objectName(), ctx);
		if (!selection.isValid()) return false;
		const SkillInstanceRef ref = selection.activationRef();
		player->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pending_target", ctx.invoker->objectName());
		room->askForUseCard(player, "@@newshuliang", "@newshuliang:" + ctx.invoker->objectName(), -1, Card::MethodNone);
		return false;
	}
};
YizanCard::YizanCard()
{
	target_fixed = true;
	will_throw = false;
	mute = true;
	handling_method = Card::MethodNone;
}

bool YizanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("yizan");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}

	const Card *dc = Self->getTag("yizan").value<const Card *>();
	return dc && dc->targetFilter(targets, to_select, Self);
}

bool YizanCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}

	const Card *dc = Self ? Self->getTag("yizan").value<const Card *>() : nullptr;
	return dc && dc->targetFixed();
}

bool YizanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("yizan");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}

	const Card *dc = Self->getTag("yizan").value<const Card *>();
	return dc && dc->targetsFeasible(targets, Self);
}

const Card *YizanCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *player = card_use.from;
	Room *room = player->getRoom();

	QString to_yizan = user_string;
	if ((user_string.contains("slash") || user_string.contains("Slash")) && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_yizan = room->askForChoice(player, "yizan_slash", guhuo_list.join("+"));
	}

	Card *use_card = Sanguosha->cloneCard(to_yizan);
	use_card->addSubcards(getSubcards());
	use_card->setSkillName("yizan");
	use_card->deleteLater();
	return use_card;
}

const Card *YizanCard::validateInResponse(ServerPlayer *player) const
{
	Room *room = player->getRoom();

	QString to_yizan = user_string;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		guhuo_list << "peach";
		if (Sanguosha->hasCard("analeptic")) guhuo_list << "analeptic";
		to_yizan = room->askForChoice(player, "yizan_saveself", guhuo_list.join("+"));
	} else if (user_string.contains("slash") || user_string.contains("Slash")) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty()) guhuo_list << "slash";
		to_yizan = room->askForChoice(player, "yizan_slash", guhuo_list.join("+"));
	}

	Card *use_card = Sanguosha->cloneCard(to_yizan);
	use_card->addSubcards(getSubcards());
	use_card->setSkillName("yizan");
	use_card->deleteLater();
	return use_card;
}

class YizanVS : public ViewAsSkill
{
public:
	YizanVS() : ViewAsSkill("yizan")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return true;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		foreach (QString name, pattern.split("+")) {
			Card *card = Sanguosha->cloneCard(name.toLower());
			if (!card) continue;
			card->deleteLater();
			if (card->isKindOf("BasicCard"))
				return true;
		}
		foreach (QString name, pattern.split(",")) {
			Card *card = Sanguosha->cloneCard(name.toLower());
			if (!card) continue;
			card->deleteLater();
			if (card->isKindOf("BasicCard"))
				return true;
		}
		return false;
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		int level = Self->property("yizan_level").toInt();
		if (level < 1) {
			level = selected.length();
			if (level<1) return true;
			else if (level >= 2) return false;
			else if (selected.first()->isKindOf("BasicCard"))
				return true;
			return to_select->isKindOf("BasicCard");
		}
		return selected.isEmpty() && to_select->isKindOf("BasicCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		int level = Self->property("yizan_level").toInt();
		if (level < 1) level = 2;
		if (cards.length() != level) return nullptr;
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
			|| Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			YizanCard *card = new YizanCard;
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			card->addSubcards(cards);
			return card;
		}

		const Card *c = Self->getTag("yizan").value<const Card *>();
		if (c && c->isAvailable(Self)) {
			YizanCard *card = new YizanCard;
			card->setUserString(c->objectName());
			card->addSubcards(cards);
			return card;
		}
		return nullptr;
	}
};

class Yizan : public TriggerSkill
{
public:
	Yizan() : TriggerSkill("yizan")
	{
		events << PreCardResponded << PreCardUsed;
		view_as_skill = new YizanVS;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::guhuo(objectName(), true, false);
	}

	int getPriority(TriggerEvent) const
	{
		return 5;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark("longyuan") > 0 || player->getMark("tenyearlongyuan") > 0) return false;
		const Card *card = nullptr;
		if (event == PreCardResponded)
			card = data.value<CardResponseStruct>().m_card;
		else
			card = data.value<CardUseStruct>().card;
		if (card == nullptr || card->isKindOf("SkillCard") || !card->getSkillNames().contains("yizan")) return false;
		room->addPlayerMark(player, "&yizan");
		return false;
	}
};

class Longyuan : public PhaseChangeSkill
{
public:
	Longyuan() : PhaseChangeSkill("longyuan")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getMark("&yizan")>2){
			LogMessage log;
			log.type = "#LongyuanWake";
			log.from = player;
			log.arg = QString::number(player->getMark("&yizan"));
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, "longyuan");
		room->setPlayerMark(player, "longyuan", 1);
		room->setPlayerProperty(player, "yizan_level", 1);
		if (room->changeMaxHpForAwakenSkill(player, 0, objectName())) {
			QString translate = Sanguosha->translate(":yizan2");
			room->changeTranslation(player, "yizan", translate);
		}
		room->setPlayerMark(player, "&yizan", 0);
		return false;
	}
};

class Qianchong : public TriggerSkillV2
{
public:
	Qianchong() : TriggerSkillV2("qianchong")
	{
		events << CardsMoveOneTime << EventPhaseStart << EventAcquireSkill << EventPhaseChanging;
		frequency = Compulsory;
		global = true;
	}
	static QString granted(const Player *player)
	{
		const QList<const Card *> equips = player->getEquips();
		if (equips.isEmpty()) return QString();
		bool red = true, black = true;
		for (const Card *card : equips) { red = red && card->isRed(); black = black && card->isBlack(); }
		return black ? QStringLiteral("weimu") : red ? QStringLiteral("mingzhe") : QString();
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
			for (ServerPlayer *player : room->getAllPlayers(true)) {
				player->removeTag("mobile_qianchong_types");
				for (const QString &type : QStringList{"basic", "trick", "equip"}) room->setPlayerMark(player, "qianchong_" + type + "-Clear", 0);
			}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == EventPhaseStart)
			return player->getPhase() == Player::Play && granted(player).isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
		if (event == EventAcquireSkill) {
			SkillChangeStruct change;
			if (!change.tryParse(data) || change.skillName != objectName()) return {};
			return {{player, {SkillInstanceKey(objectName(), change.instanceID).toString()}}};
		}
		if (event != CardsMoveOneTime) return {};
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!(move.from == player && move.from_places.contains(Player::PlaceEquip)) && !(move.to == player && move.to_place == Player::PlaceEquip)) return {};
		return {{player, {objectName()}}};
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = {player};
		if (event == EventPhaseStart) {
			const QString choice = room->askForChoice(player, objectName(), "basic+trick+equip");
			if (!QStringList{"basic", "trick", "equip"}.contains(choice)) return false;
			ctx.extra_data = choice;
		}
		return true;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event == EventPhaseStart) {
			const QString type = ctx.extra_data.toString();
			QVariantList receipts = target->getTag("mobile_qianchong_types").toList();
			receipts << QVariantMap{{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}, {"type", type}};
			target->setTag("mobile_qianchong_types", receipts);
			room->setPlayerMark(target, "qianchong_" + type + "-Clear", 1);
			LogMessage log;
			log.type = "#QianchongChoice";
			log.from = target;
			log.arg = objectName();
			log.arg2 = type;
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName(), 3);
			return false;
		}
		const QString desired = granted(target);
		bool changed = false;
		for (const QString &name : QStringList{"weimu", "mingzhe"}) {
			QList<SkillInstanceRef> children;
			for (const SkillInstance &instance : target->getSkillInstances())
				if (instance.skillName == name && instance.parentRef == ctx.sourceRef)
					children << SkillInstanceRef(target->objectName(), instance.key());
			if (name == desired) {
				if (children.isEmpty()) { room->attachSkillToPlayer(target, name, ctx.sourceRef); changed = true; }
			} else for (const SkillInstanceRef &child : children) { room->detachAttachedSkill(child); changed = true; }
		}
		if (changed) room->sendCompulsoryTriggerLog(target, objectName(), true, true, desired == "mingzhe" ? 2 : 1);
		return false;
	}
};
class QianchongTargetMod : public TargetModSkillV2
{
public:
	QianchongTargetMod() : TargetModSkillV2("#qianchong-target", ".") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.primary || !ctx.card || ctx.primary->getMark("qianchong_" + ctx.card->getType() + "-Clear") <= 0) return CorrectSkillResult::noEffect();
		if (ctx.modType == Residue) return CorrectSkillResult::unlimitedResidue();
		if (ctx.modType == DistanceLimit) return CorrectSkillResult::useAmount(1000 * ctx.currentAmount);
		return CorrectSkillResult::noEffect();
	}
};
// These independent printed effects belong beside their generals, not inside Qianchong.
class MobileAppliedDistance : public TargetModSkillV2
{
public:
	explicit MobileAppliedDistance(const QString &skill) : TargetModSkillV2("#" + skill + "-distance", "."), source(skill)
	{
		setHolderSelector(CorrectSkill_System);
	}
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.primary || !ctx.card || ctx.modType != DistanceLimit) return CorrectSkillResult::noEffect();
		const bool active = source == "bswanglie" ? ctx.card->hasTip("bswanglie")
			: source == "zhuangshi" ? ctx.primary->getPhase() == Player::Play && ctx.primary->getMark("&zhuangshi+1-PlayClear") > 0
			: ctx.primary->getPile("bstun_tian").contains(ctx.card->getId());
		return active ? CorrectSkillResult::useAmount(1000 * ctx.currentAmount) : CorrectSkillResult::noEffect();
	}
private:
	QString source;
};
class ChizhangDistance : public TargetModSkillV2
{
public:
	ChizhangDistance() : TargetModSkillV2("#chizhang-distance", ".") { }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.card && ctx.card->isDamageCard() && ctx.modType == DistanceLimit
			? CorrectSkillResult::useAmount(1000 * ctx.currentAmount) : CorrectSkillResult::noEffect();
	}
};
class Shangjian : public TriggerSkillV2
{
public:
	Shangjian() : TriggerSkillV2("shangjian") { events << EventPhaseStart; frequency = Frequent; }
	static int lostCards(Room *room, ServerPlayer *owner)
	{
		QVariantMap query{{"turn_id", room->historyScopes().value("turn_id")}, {"from", owner->objectName()}};
		int count = 0;
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap move = entry.toMap().value("data").toMap();
				const int from = move.value("from_place", -1).toInt();
				const int to = move.value("to_place", -1).toInt();
				if (from != Player::PlaceHand && from != Player::PlaceEquip) continue;
				// Moving between this player's hand and equipment is not losing a card.
				if (move.value("to").toString() == owner->objectName() && (to == Player::PlaceHand || to == Player::PlaceEquip)) continue;
				++count;
			}
			if (!page.value("has_more").toBool()) return count;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || player->getPhase() != Player::Finish) return result;
		for (ServerPlayer *owner : room->getAlivePlayers()) {
			if (!owner->hasSkill(objectName())) continue;
			const int count = lostCards(room, owner);
			if (count < 0) qWarning("Shangjian: incomplete turn movement history");
			else if (count > 0 && count <= owner->getHp()) result[owner] << objectName();
		}
		return result;
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const int count = lostCards(room, player);
		if (count <= 0 || count > player->getHp() || !player->askForSkillInvoke(this)) return false;
		ctx.extra_data = count;
		ctx.targets = {player};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
		return false;
	}
};

HongyiCard::HongyiCard()
{
}

void HongyiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QStringList names = effect.from->property("hongyi_targets").toStringList();
	names << effect.to->objectName();
	room->setPlayerProperty(effect.from, "hongyi_targets", names);
	room->addPlayerMark(effect.to, "&hongyi");
}

class HongyiVS : public ZeroCardViewAsSkill
{
public:
	HongyiVS() : ZeroCardViewAsSkill("hongyi")
	{
	}

	const Card *viewAs() const
	{
		return new HongyiCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("HongyiCard");
	}
};

class Hongyi : public TriggerSkill
{
public:
	Hongyi() : TriggerSkill("hongyi")
	{
		events << DamageCaused << EventPhaseStart << Death << EventLoseSkill;
		view_as_skill = new HongyiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == EventPhaseStart)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			QStringList names = player->property("hongyi_targets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "hongyi_targets", QStringList());
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p) room->removePlayerMark(p, "&hongyi");
			}
		} else if (event == DamageCaused) {
			if (player->isDead() || player->getMark("&hongyi") <= 0) return false;
			DamageStruct damage = data.value<DamageStruct>();
			int n = 0;
			for (int i = 1; i <= player->getMark("&hongyi"); i++) {
				if (player->isDead()) break;
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);

				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".";
				judge.play_animation = false;
				room->judge(judge);

				if (judge.card->getColor() == Card::Red)
					room->drawCards(damage.to, 1, objectName());
				else if (judge.card->getColor() == Card::Black)
					n--;
			}
			if (n<0) player->damageRevises(data,n);
		} else if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (player != death.who) return false;
			QStringList names = player->property("hongyi_targets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "hongyi_targets", QStringList());
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p) room->removePlayerMark(p, "&hongyi");
			}
		} else if (event == EventLoseSkill) {
			if (data.toString() != objectName()) return false;
			QStringList names = player->property("hongyi_targets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "hongyi_targets", QStringList());
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p) room->removePlayerMark(p, "&hongyi");
			}
		}
		return false;
	}
};

class Quanfeng : public TriggerSkill
{
public:
	Quanfeng() : TriggerSkill("quanfeng")
	{
		events << Death << AskForPeaches;
		frequency = Limited;
		limit_mark = "@quanfengMark";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark("@quanfengMark") <= 0) return false;
		if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (!player->askForSkillInvoke(this, death.who)) return false;
			room->broadcastSkillInvoke(objectName());
			room->doSuperLightbox(player, "quanfeng");
			room->removePlayerMark(player, "@quanfengMark");
			room->handleAcquireDetachSkills(player, "-hongyi");

			QStringList skills;
			const General *general = Sanguosha->getGeneral(death.who->getGeneralName());
			foreach (const Skill *sk, general->getSkillList()) {
				if (!sk->isVisible() || sk->isLordSkill()) continue;
				if (skills.contains(sk->objectName())) continue;
				skills << sk->objectName();
			}
			if (death.who->getGeneral2()) {
				const General *general2 = Sanguosha->getGeneral(death.who->getGeneral2Name());
				foreach (const Skill *sk, general2->getSkillList()) {
					if (!sk->isVisible() || sk->isLordSkill()) continue;
					if (skills.contains(sk->objectName())) continue;
					skills << sk->objectName();
				}
			}
			if (!skills.isEmpty())
				room->handleAcquireDetachSkills(player, skills);
			room->gainMaxHp(player, 1, objectName());
			room->recover(player, RecoverStruct("quanfeng", player));
		} else {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who != player) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			room->doSuperLightbox(player, "quanfeng");
			room->removePlayerMark(player, "@quanfengMark");
			room->gainMaxHp(player, 2, objectName());
			int num = qMin(4, player->getMaxHp() - player->getHp());
			room->recover(player, RecoverStruct(player, nullptr, num, "quanfeng"));
		}
		return false;
	}
};

class Polu : public TriggerSkillV2
{
public:
	Polu(const QString &name) : TriggerSkillV2(name)
	{
		events << EventPhaseStart << Damaged;
		frequency = Compulsory;
		waked_skills = name == "secondpolu" ? "_secondpiliche" : "_piliche";
	}
	QString weaponName() const { return objectName() == "secondpolu" ? "_secondpiliche" : "_piliche"; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == EventPhaseStart)
			return player->getPhase() == Player::RoundStart ? TriggerList{{player, {objectName()}}} : TriggerList();
		if (player->getWeapon() && player->getWeapon()->objectName() == weaponName()) return {};
		QStringList triggers;
		for (int i = 0; i < data.value<DamageStruct>().damage; ++i) triggers << objectName();
		return {{player, triggers}};
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.extra_data = event == Damaged;
		ctx.targets = {player};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const bool second = objectName() == "secondpolu";
		if (ctx.extra_data.toBool()) {
			room->sendCompulsoryTriggerLog(ctx.owner, this);
			target->drawCards(getEffectiveAmount(ctx), objectName());
			if (!second || !target->isAlive()) return false;
			QList<int> weapons;
			for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf("Weapon")) weapons << id;
			if (weapons.isEmpty()) return false;
			const int id = weapons.at(qsanRandomBounded(weapons.length()));
			room->obtainCard(target, id, true);
			const Card *weapon = Sanguosha->getCard(id);
			if (target->isAlive() && target->handCards().contains(id) && weapon->isAvailable(target))
				room->useCardFromSkillEffect(CardUseStruct(weapon, target), ctx, true);
			return false;
		}
		const QString name = weaponName();
		for (ServerPlayer *other : room->getAlivePlayers())
			for (const Card *card : other->getCards(second ? "hej" : "ej"))
				if (Sanguosha->getEngineCard(card->getEffectiveId())->objectName() == name) return false;
		if (second) for (int id : room->getDrawPile() + room->getDiscardPile())
			if (Sanguosha->getEngineCard(id)->objectName() == name) return false;
		int id = target->getDerivativeCard(name, Player::PlaceTable);
		if (id < 0) {
			for (ServerPlayer *other : room->getAlivePlayers()) {
				for (const QString &pile : other->getPileNames()) {
					if (pile == "wooden_ox") continue;
					for (int candidate : other->getPile(pile))
						if (Sanguosha->getEngineCard(candidate)->objectName() == name) { id = candidate; break; }
					if (id >= 0) break;
				}
				if (id >= 0) break;
			}
		}
		if (id < 0) return false;
		const Card *weapon = Sanguosha->getCard(id);
		if (!second && !weapon->isAvailable(target)) return false;
		room->sendCompulsoryTriggerLog(ctx.owner, this);
		if (second) {
			const CardMoveReason reason = room->getCardPlace(id) == Player::PlaceTable
				? CardMoveReason(CardMoveReason::S_REASON_EXCLUSIVE, target->objectName()) : CardMoveReason();
			room->obtainCard(target, weapon, reason, false);
			if (!target->isAlive() || !target->handCards().contains(id) || !weapon->isAvailable(target)) return false;
		}
		room->useCardFromSkillEffect(CardUseStruct(weapon, target), ctx, true);
		return false;
	}
};
class ChoulveVS : public ZeroCardViewAsSkill
{
public:
	ChoulveVS() :ZeroCardViewAsSkill("choulve")
	{
		response_pattern = "@@choulve!";
	}

	const Card *viewAs() const
	{
		QString name = Self->property("choulve_damage_card").toString();
		if (name.isEmpty()) return nullptr;
		Card *use_card = Sanguosha->cloneCard(name);
		if (!use_card) return nullptr;
		use_card->setSkillName("_choulve");
		return use_card;
	}
};

class Choulve : public PhaseChangeSkill
{
public:
	Choulve() : PhaseChangeSkill("choulve")
	{
		view_as_skill = new ChoulveVS;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		QString name = player->property("choulve_damage_card").toString();
		Card *use_card = Sanguosha->cloneCard(name);
		if (!use_card) return false;
		QList<ServerPlayer *> tos;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isNude())
				tos << p;
		}
		if (tos.isEmpty()) return false;
		ServerPlayer *to = room->askForPlayerChosen(player, tos, objectName(), "@choulve-invoke", true, true);
		if (!to) return false;
		room->broadcastSkillInvoke(objectName());
		const Card *card = room->askForExchange(to, objectName(), 1, 1, true, "@choulve-give:" + player->objectName(), true);
		if (!card) return false;
		room->giveCard(to, player, card, objectName());
		use_card->setSkillName("_choulve");
		use_card->deleteLater();
		if (!player->canUse(use_card)) return false;

		if (use_card->targetFixed())
			room->useCard(CardUseStruct(use_card, player), true);
		else {
			if (room->askForUseCard(player, "@@choulve!", "@choulve:" + name)) return false;
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (player->canUse(use_card, p))
					targets << p;
			}
			if (targets.isEmpty()) return false;
			ServerPlayer *target = targets.at(qsanRandomBounded(targets.length()));
			room->useCard(CardUseStruct(use_card, player, target), true);
		}
		return false;
	}
};

class ChoulveRecord : public TriggerSkill
{
public:
	ChoulveRecord() : TriggerSkill("#choulve-record")
	{
		events << DamageDone << EventLoseSkill;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || damage.card->isKindOf("SkillCard")) return false;
			QString name = damage.card->objectName();
			if (!damage.card->isKindOf("DelayedTrick")) {
				room->setPlayerProperty(player, "choulve_damage_card", name);
	
				foreach (QString mark, player->getMarkNames()) {
					if (mark.startsWith("&choulve+"))
						room->setPlayerMark(player, mark, 0);
				}
	
				if (player->hasSkill("choulve", true))
					room->setPlayerMark(damage.to, "&choulve+" + name, 1);
			}
	
			QList<ServerPlayer *> players;
			players << player;
			if (damage.from && damage.from->isAlive())
				players << damage.from;
	
			foreach (ServerPlayer *p, players) {
				room->setPlayerProperty(p, "yhduwei_damage_card", name);
	
				foreach (QString mark, p->getMarkNames()) {
					if (mark.startsWith("&yhduwei+"))
						room->setPlayerMark(p, mark, 0);
				}
	
				if (p->hasSkill("yhduwei", true))
					room->setPlayerMark(p, "&yhduwei+" + name, 1);
			}
		}else {
			if(data.toString()=="choulve"){
				foreach (QString mark, player->getMarkNames()) {
					if (mark.startsWith("&choulve+"))
						room->setPlayerMark(player, mark, 0);
				}
			}else if(data.toString()=="yhduwei"){
				foreach (QString mark, player->getMarkNames()) {
					if (mark.startsWith("&yhduwei+"))
						room->setPlayerMark(player, mark, 0);
				}
			}
		}
		return false;
	}
};

class Daigong : public TriggerSkillV2
{
public:
	Daigong() : TriggerSkillV2("daigong") { events << DamageInflicted << EventSkillInvoking; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Turn; }
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		return player && player->isAlive() && player->hasSkill(objectName()) && !player->isKongcheng() && room->hasCurrent()
			&& data.value<DamageStruct>().damage > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
		return player->askForSkillInvoke(this, from && from->isAlive() ? QVariant::fromValue(from) : QVariant());
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.extra_data = QStringLiteral("reveal");
		skillEffect(event, room, player, ctx, player);
		ctx.modified_amount = amount;
		ctx.modified_amount_set = modified;
		ctx.extra_data = QStringLiteral("give");
		ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
		return from && from->isAlive() && skillEffect(event, room, player, ctx, from);
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.extra_data.toString() == "reveal") { room->showAllCards(target); return false; }
		QStringList suits{"spade", "club", "heart", "diamond", "no_suit_black", "no_suit_red", "no_suit"};
		for (const Card *card : player->getHandcards()) suits.removeAll(card->getSuitString());
		QList<int> allowed;
		for (const Card *card : target->getCards("he"))
			if (suits.contains(card->getSuitString())) allowed << card->getEffectiveId();
		const Card *gift = allowed.isEmpty() ? nullptr : room->askForCard(target, ".|" + suits.join(",") + "|.|.",
			"daigong-give:" + player->objectName(), QStringList{player->objectName(), suits.join(",")}, Card::MethodNone);
		if (gift && allowed.contains(gift->getEffectiveId()) && room->getCardOwner(gift->getEffectiveId()) == target) {
			room->giveCard(target, player, gift, objectName());
			return false;
		}
		LogMessage log;
		log.type = "#Daigong";
		log.from = target;
		log.to << player;
		log.arg = QString::number(ctx.original_data->value<DamageStruct>().damage);
		room->sendLog(log);
		return true;
	}
};
SpZhaoxinCard::SpZhaoxinCard()
{
	target_fixed= true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void SpZhaoxinCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->addToPile("zxwang", this);
	source->drawCards(getSubcards().length(), "spzhaoxin");
}

SpZhaoxinChooseCard::SpZhaoxinChooseCard()
{
	m_skillName = "spzhaoxin";
	target_fixed= true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

class SpZhaoxinVS : public ViewAsSkill
{
public:
	SpZhaoxinVS() : ViewAsSkill("spzhaoxin")
	{
	expand_pile = "zxwang";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			return selected.length() < 3 - Self->getPile("zxwang").length() && Self->hasCard(to_select);
		}
		return Self->getPile("zxwang").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			if (cards.isEmpty())
				return nullptr;
			SpZhaoxinCard *c = new SpZhaoxinCard;
			c->addSubcards(cards);
			return c;
		}
		if (cards.length() != 1)
			return nullptr;
		SpZhaoxinChooseCard *c = new SpZhaoxinChooseCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("SpZhaoxinCard") && player->getPile("zxwang").length() < 3;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern == "@@spzhaoxin" && !player->getPile("zxwang").isEmpty();
	}
};

class SpZhaoxin : public TriggerSkill
{
public:
	SpZhaoxin() : TriggerSkill("spzhaoxin")
	{
		events << EventPhaseEnd;
		view_as_skill = new SpZhaoxinVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;
		foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
			if (player->isDead() || !p->hasSkill(objectName())) return false;
			if (p->isDead() || p->getPile("zxwang").isEmpty()) continue;
			if (p != player && !p->inMyAttackRange(player)) continue;
			const Card *card = room->askForUseCard(p, "@@spzhaoxin", "@spzhaoxin:" + player->objectName());
			if (!card) continue;
			room->fillAG(card->getSubcards(), player);
			if (!player->askForSkillInvoke(this, QString("spzhaoxin_get:%1::%2").arg(card->getSubcards().first()).arg(p->objectName()), false)) {
				room->clearAG(player);
				continue;
			}
			room->clearAG(player);
			if (p == player) {
				LogMessage log;
				log.type = "$KuangbiGet";
				log.from = player;
				log.arg = "zxwang";
				log.card_str = ListI2S(card->getSubcards()).join("+");
				room->sendLog(log);
			}
			player->obtainCard(card, true);
			if (!p->askForSkillInvoke(this, "spzhaoxin_damage:"+player->objectName(), false)) continue;
			room->damage(DamageStruct("spzhaoxin", p, player));
		}
		return false;
	}
};

SecondZhanyiViewAsBasicCard::SecondZhanyiViewAsBasicCard()
{
	m_skillName = "secondzhanyi";
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool SecondZhanyiViewAsBasicCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->addSubcard(getEffectiveId());
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool SecondZhanyiViewAsBasicCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->addSubcard(getEffectiveId());
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool SecondZhanyiViewAsBasicCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->addSubcard(getEffectiveId());
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card *SecondZhanyiViewAsBasicCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *zhuling = card_use.from;
	Room *room = zhuling->getRoom();

	QString to_zhanyi = user_string;
	if (user_string == "slash" && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list;
		guhuo_list << "slash";
		if (!Config.BanPackages.contains("maneuvering"))
			guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
		to_zhanyi = room->askForChoice(zhuling, "secondzhanyi_slash", guhuo_list.join("+"));
	}

	if (to_zhanyi == "slash") {
		const Card *card = Sanguosha->getCard(subcards.first());
		if (card->isKindOf("Slash"))
			to_zhanyi = card->objectName();
	} else if (to_zhanyi == "normal_slash")
		to_zhanyi = "slash";
	Card *use_card = Sanguosha->cloneCard(to_zhanyi);
	use_card->setSkillName("secondzhanyi");
	use_card->addSubcard(subcards.first());
	use_card->deleteLater();
	return use_card;
}

const Card *SecondZhanyiViewAsBasicCard::validateInResponse(ServerPlayer *zhuling) const
{
	Room *room = zhuling->getRoom();

	QString to_zhanyi = user_string;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		guhuo_list << "peach";
		if (!Config.BanPackages.contains("maneuvering"))
			guhuo_list << "analeptic";
		to_zhanyi = room->askForChoice(zhuling, "secondzhanyi_saveself", guhuo_list.join("+"));
	} else if (user_string == "slash") {
		QStringList guhuo_list;
		guhuo_list << "slash";
		if (!Config.BanPackages.contains("maneuvering"))
			guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
		to_zhanyi = room->askForChoice(zhuling, "secondzhanyi_slash", guhuo_list.join("+"));
	}
	if (to_zhanyi == "slash") {
		const Card *card = Sanguosha->getCard(subcards.first());
		if (card->isKindOf("Slash"))
			to_zhanyi = card->objectName();
	} else if (to_zhanyi == "normal_slash")
		to_zhanyi = "slash";
	Card *use_card = Sanguosha->cloneCard(to_zhanyi);
	use_card->setSkillName("secondzhanyi");
	use_card->addSubcard(subcards.first());
	use_card->deleteLater();
	return use_card;
}

SecondZhanyiCard::SecondZhanyiCard()
{
	target_fixed = true;
}

void SecondZhanyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->loseHp(HpLostStruct(source, 1, "secondzhanyi", source));
	if (source->isAlive()) {
		const Card *c = Sanguosha->getCard(subcards.first());
		if (c->getTypeId() == Card::TypeBasic) {
			room->setPlayerMark(source, "ViewAsSkill_secondzhanyiEffect", 1);
			room->setPlayerMark(source, "Secondzhanyieffect-PlayClear", 1);
		} else if (c->getTypeId() == Card::TypeEquip)
			room->setPlayerMark(source, "secondzhanyiEquip-PlayClear", 1);
		else if (c->getTypeId() == Card::TypeTrick) {
			source->drawCards(3, "secondzhanyi");
			room->setPlayerMark(source, "secondzhanyiTrick-PlayClear", 1);
		}
	}
}

class SecondZhanyiVS : public OneCardViewAsSkill
{
public:
	SecondZhanyiVS() : OneCardViewAsSkill("secondzhanyi")
	{
	}

	bool isResponseOrUse() const
	{
		return Self->getMark("ViewAsSkill_secondzhanyiEffect") > 0;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (!player->hasUsed("SecondZhanyiCard"))
			return true;
		return player->getMark("ViewAsSkill_secondzhanyiEffect") > 0;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		if (player->getMark("ViewAsSkill_secondzhanyiEffect")<1) return false;
		if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
		foreach (QString pn, pattern.split("+")) {
			Card*dc = Sanguosha->cloneCard(pn);
			if(dc){
				dc->deleteLater();
				if(dc->isKindOf("BasicCard"))
					return true;
			}
		}
		return false;
	}

	bool viewFilter(const Card *to_select) const
	{
		return Self->getMark("ViewAsSkill_secondzhanyiEffect")<1||to_select->isKindOf("BasicCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Self->getMark("ViewAsSkill_secondzhanyiEffect") <1) {
			SecondZhanyiCard *zy = new SecondZhanyiCard;
			zy->addSubcard(originalCard);
			return zy;
		}
		QString pattern = Sanguosha->getCurrentCardUsePattern();

		if (pattern.isEmpty()) {
			const Card *c = Self->getTag("secondzhanyi").value<const Card *>();
			if(c) pattern = c->objectName();
			else return nullptr;
		}
		SecondZhanyiViewAsBasicCard *card = new SecondZhanyiViewAsBasicCard;
		card->addSubcard(originalCard);
		card->setUserString(pattern);
		return card;
	}
};

class SecondZhanyi : public TriggerSkill
{
public:
	SecondZhanyi() : TriggerSkill("secondzhanyi")
	{
		events << PreHpRecover << ConfirmDamage << PreCardUsed << EventPhaseChanging
			<< PreCardResponded << TrickCardCanceling << TargetSpecified;
		view_as_skill = new SecondZhanyiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::guhuo("secondzhanyi", true, false);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TrickCardCanceling) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->isKindOf("TrickCard")) return false;
			return effect.from && effect.from->getMark("secondzhanyiTrick-PlayClear")>0;
		}else if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().from != Player::Play) return false;
			room->setPlayerMark(player, "ViewAsSkill_secondzhanyiEffect", 0);
		} else if (event == PreHpRecover) {
			RecoverStruct recover = data.value<RecoverStruct>();
			if (!recover.card || !recover.card->hasFlag("secondzhanyi_effect")) return false;
			int old = recover.recover;
			++recover.recover;
			int now = qMin(recover.recover, player->getMaxHp() - player->getHp());
			if (now <= 0) return true;
			if (recover.who && now > old) {
				LogMessage log;
				log.type = "#NewlonghunRecover";
				log.from = recover.who;
				log.to << player;
				log.arg = objectName();
				log.arg2 = QString::number(now);
				room->sendLog(log);
			}
			recover.recover = now;
			data = QVariant::fromValue(recover);
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("secondzhanyi_effect") || !damage.by_user) return false;

			LogMessage log;
			log.type = "#NewlonghunDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);

			data = QVariant::fromValue(damage);
		} else if (event == TargetSpecified) {
			if (player->getMark("secondzhanyiEquip-PlayClear") <= 0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.to.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player, objectName());
			foreach (ServerPlayer *p, use.to) {
				if (p->isDead() || !p->canDiscard(p, "he")) continue;
				const Card *c = room->askForDiscard(p, "secondzhanyi", 2, 2, false, true);
				if (!c || player->isDead()) continue;
				room->fillAG(c->getSubcards(), player);
				int id = room->askForAG(player, c->getSubcards(), false, "secondzhanyi");
				room->clearAG(player);
				room->obtainCard(player, id);
			}
		} else {
			if (player->getMark("Secondzhanyieffect-PlayClear") <= 0) return false;
			const Card *card = nullptr;
			if (event == PreCardUsed)
				card = data.value<CardUseStruct>().card;
			else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if (!res.m_isUse) return false;
				card = res.m_card;
			}
			if (!card || !card->isKindOf("BasicCard")) return false;
			room->setPlayerMark(player, "Secondzhanyieffect-PlayClear", 0);
			room->setCardFlag(card, "secondzhanyi_effect");
		}
		return false;
	}
};

class Zhaohan : public TriggerSkillV2
{
public:
	Zhaohan() : TriggerSkillV2("zhaohan") { events << EventPhaseStart; frequency = Compulsory; }
	static int startCount(Room *room, const ServerPlayer *player)
	{
		QVariantMap query{{"kind", "phase"}, {"player", player->objectName()}};
		int count = 0;
		for (;;) {
			const QVariantMap page = room->queryHistoryEvents(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap phase = entry.toMap().value("data").toMap();
				if (phase.value("phase").toInt() != Player::Start) continue;
				// A skipped phase is not a preparation phase entered; old snapshots cannot infer this fact.
				if (!phase.contains("entered")) return -1;
				if (phase.value("entered").toBool()) ++count;
			}
			if (!page.value("has_more").toBool()) return count;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return {};
		const int count = startCount(room, player);
		if (count < 0) qWarning("Zhaohan: incomplete entered-phase history");
		return count > 0 && count <= 7 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const int count = startCount(room, player);
		if (count <= 0 || count > 7) return false;
		ctx.extra_data = count <= 4 ? QStringLiteral("gain") : QStringLiteral("lose");
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const bool gain = ctx.extra_data.toString() == "gain";
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		skillEffect(event, room, player, ctx, player);
		if (gain && player->isAlive()) {
			ctx.extra_data = QStringLiteral("recover");
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, player);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const QString action = ctx.extra_data.toString();
		if (action == "gain") room->gainMaxHp(target, getEffectiveAmount(ctx), objectName());
		else if (action == "lose") room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
		else room->recover(target, RecoverStruct(objectName(), target, getEffectiveAmount(ctx)));
		return false;
	}
};

class Rangjie : public TriggerSkillV2
{
public:
	Rangjie() : TriggerSkillV2("rangjie") { events << Damaged; frequency = Frequent; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		QStringList triggers;
		for (int i = 0; i < data.value<DamageStruct>().damage; ++i) triggers << objectName();
		return {{player, triggers}};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player->askForSkillInvoke(this)) return false;
		QStringList choices;
		if (room->canMoveField("ej")) choices << "move";
		choices << "BasicCard" << "TrickCard" << "EquipCard";
		ctx.extra_data = room->askForChoice(player, objectName(), choices.join("+"));
		ctx.targets = {player};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		const QString choice = ctx.extra_data.toString();
		const int amount = getEffectiveAmount(ctx);
		for (int i = 0; i < amount && target->isAlive(); ++i) {
			if (choice == "move") room->moveField(target, objectName(), false, "ej");
			else {
				QList<int> candidates;
				const QByteArray type = choice.toLatin1();
				for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf(type.constData())) candidates << id;
				if (candidates.isEmpty()) continue;
				room->obtainCard(target, candidates.at(qsanRandomBounded(candidates.length())), true);
			}
			if (target->isAlive()) target->drawCards(1, objectName());
		}
		return false;
	}
};
YizhengCard::YizhengCard() { setSkillName("yizheng"); }
bool YizhengCard::targetFilter(const QList<const Player *> &targets, const Player *candidate, const Player *self) const
{
	return ActiveSkillCard::targetFilter(targets, candidate, self);
}
void YizhengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class YizhengVS : public ViewAsSkillV2
{
public:
	YizhengVS() : ViewAsSkillV2("yizheng") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play && request.initiator->canPindian();
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		return request.initiator && target && selected.isEmpty() && request.initiator->canPindian(target)
			&& target->getHp() <= request.initiator->getHp();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "YizhengCard"; }
	const Card *createCard(const ActiveSkillRequest &) const override { return new YizhengCard; }
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		ctx.extra_data = QStringLiteral("pindian");
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (ServerPlayer *target : ctx.targets) {
			if (target->isAlive()) skillEffect(ctx, target);
			if (ctx.extra_data.toString() == "lost" && ctx.owner->isAlive()) {
				ctx.modified_amount = amount;
				ctx.modified_amount_set = modified;
				skillEffect(ctx, ctx.owner);
			}
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		if (ctx.extra_data.toString() == "lost") {
			room->loseMaxHp(target, getEffectiveAmount(ctx), objectName());
			return ContinueEffects;
		}
		if (!ctx.owner->canPindian(target, false)) return ContinueEffects;
		if (!ctx.owner->pindian(target, objectName())) { ctx.extra_data = QStringLiteral("lost"); return ContinueEffects; }
		QVariantList receipts = target->getTag("mobile_yizheng_receipts").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"token", QString::number(room->currentHistoryEventId())}};
		target->setTag("mobile_yizheng_receipts", receipts);
		room->setPlayerMark(target, "&yizheng", receipts.length());
		return ContinueEffects;
	}
};

class Yizheng : public TriggerSkillV2
{
public:
	Yizheng() : TriggerSkillV2("yizheng") { events << EventPhaseChanging; global = true; view_as_skill = new YizhengVS; }
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (!player || !player->isAlive() || data.value<PhaseChangeStruct>().to != Player::Draw || player->isSkipped(Player::Draw)) return true;
		const QVariantList receipts = player->getTag("mobile_yizheng_receipts").toList();
		if (receipts.isEmpty()) return true;
		// Multiple outstanding wins jointly skip the next unskipped draw phase once.
		const QVariantMap receipt = receipts.first().toMap();
		SkillContext ctx;
		ctx.skill_name = objectName();
		ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
		if (!ctx.owner) return true;
		ctx.invoker = ctx.initiator = player;
		ctx.sourceRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
		ctx.instanceID = ctx.sourceRef.key.instanceID;
		ctx.current_event = event;
		ctx.original_data = &data;
		ctx.extra_data = receipts;
		ctx.targets = {player};
		contexts << ctx;
		return true;
	}
	ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.owner && ctx.invoker && !ctx.extra_data.toList().isEmpty()
			&& ctx.invoker->getTag("mobile_yizheng_receipts").toList().contains(ctx.extra_data.toList().first());
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantList receipts = ctx.invoker->getTag("mobile_yizheng_receipts").toList();
		for (const QVariant &receipt : ctx.extra_data.toList()) receipts.removeOne(receipt);
		ctx.invoker->setTag("mobile_yizheng_receipts", receipts);
		room->setPlayerMark(ctx.invoker, "&yizheng", receipts.length());
		LogMessage log;
		log.type = "#YizhengEffect";
		log.from = target;
		log.arg = objectName();
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		target->skip(Player::Draw);
		return false;
	}
};
class MobileNiluan : public TriggerSkillV2
{
public:
	MobileNiluan() : TriggerSkillV2("mobileniluan") { events << EventPhaseStart; }
	static int usedOnOthers(Room *room, ServerPlayer *player)
	{
		const QVariant turn = room->historyScopes().value("turn_id");
		if (turn.toLongLong() <= 0) return -1;
		QVariantMap query{{"kind", "use_card_targets"}, {"turn_id", turn}, {"from", player->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap().value("data").toMap();
				const QVariantMap card = fact.value("card").toMap();
				if (!card.contains("type")) return -1;
				if (card.value("type").toInt() == Card::TypeSkill) continue;
				for (const QVariant &target : fact.value("targets").toList()) if (target.toString() != player->objectName()) return 1;
			}
			if (!page.value("has_more").toBool()) return 0;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
		const int used = usedOnOthers(room, player);
		if (used < 0) qWarning("MobileNiluan: incomplete card-target history");
		if (used != 1) return result;
		for (ServerPlayer *owner : room->getOtherPlayers(player))
			if (owner->hasSkill(objectName()) && owner->canSlash(player, false)) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!ctx.invoker || !ctx.invoker->isAlive() || !player->canSlash(ctx.invoker, false)) return false;
		ctx.targets = {ctx.invoker};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		const CardUseStruct use = room->askForUseSlashToStruct(player, target, "@mobileniluan:" + target->objectName(), false, false, false);
		if (!use.card || !player->isAlive() || !target->isAlive() || !player->canDiscard(target, "he")) return false;
		const qint64 useId = use.targetModReveal.useHistoryEventId;
		if (useId <= 0) { qWarning("MobileNiluan: missing exact Slash use identity"); return false; }
		QVariantMap query{{"turn_id", room->historyEvent(useId).value("turn_id")}};
		bool damaged = false;
		for (;;) {
			const QVariantMap page = room->queryActualDamage(query);
			if (!page.value("complete").toBool()) { qWarning("MobileNiluan: incomplete damage history"); return false; }
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap();
				if (!fact.value("data").toMap().value("card").toMap().value("classes").toStringList().contains("Slash")) continue;
				// A nested Slash has a different nearest use ancestor even when it reuses physical material.
				if (room->historyParent(fact.value("event_id").toLongLong(), "use_card", true).value("id").toLongLong() == useId) damaged = true;
			}
			if (damaged || !page.value("has_more").toBool()) break;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
		if (!damaged) return false;
		for (int i = 0; i < getEffectiveAmount(ctx) && player->isAlive() && target->isAlive() && player->canDiscard(target, "he"); ++i) {
			const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
			if (room->getCardOwner(id) == target && player->canDiscard(target, id)) room->throwCard(id, target, player);
		}
		return false;
	}
};

class MobileXiaoxi : public ViewAsSkillV2
{
public:
	MobileXiaoxi() : ViewAsSkillV2("mobilexiaoxi", 1) { response_or_use = true; }
	bool willThrowSelectedCards() const override { return false; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator) return false;
		if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return request.pattern.contains("slash", Qt::CaseInsensitive);
		std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
		slash->setSkillName(objectName());
		return slash->isAvailable(request.initiator);
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		if (!request.initiator || !card || !card->isBlack() || !request.selectedCardIds.isEmpty()) return false;
		const int id = card->getEffectiveId();
		if (!request.initiator->handCards().contains(id) && !request.initiator->getEquipsId().contains(id)
			&& !request.initiator->getHandPile().contains(id)) return false;
		std::unique_ptr<Card> slash(Sanguosha->cloneCard("slash"));
		slash->setSkillName(objectName());
		slash->addSubcard(id);
		return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? slash->isAvailable(request.initiator)
			: !request.initiator->isLocked(slash.get());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.length() != 1) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName(objectName());
		slash->addSubcard(request.selectedCardIds.first());
		return slash;
	}
};
MobileLianjiCard::MobileLianjiCard()
{
}

bool MobileLianjiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < 2 && to_select != Self;
}

bool MobileLianjiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void MobileLianjiCard::onUse(Room *room, CardUseStruct &card_use) const
{
	QVariant data = QVariant::fromValue(card_use);
	RoomThread *thread = room->getThread();

	thread->trigger(PreCardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();

	LogMessage log;
	log.from = card_use.from;
	log.to << card_use.to;
	log.type = "#UseCard";
	log.card_str = toString();
	room->sendLog(log);

	thread->trigger(CardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
	thread->trigger(CardFinished, room, card_use.from, data);
}

void MobileLianjiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *first = targets.first(), *second = targets.last();
	QList<const Card *> weapons;
	foreach (int id, room->getDrawPile()) {
		const Card *c = Sanguosha->getCard(id);
		if (c->isKindOf("Weapon")&&first->canUse(c,first))
			weapons << c;
	}
	if (weapons.isEmpty()) return;

	const Card *weapon = weapons.at(qsanRandomBounded(weapons.length()));
	if(weapon->isKindOf("QinggangSword")){
		for (int i = 0; i < Sanguosha->getCardCount(); i++) {
			const Card *c = Sanguosha->getEngineCard(i);
			if(c->objectName().endsWith("qibaodao")&&!room->getCardOwner(i)&&first->canUse(c,first)){
				room->setCardMapping(weapon->getId(), nullptr, Player::PlaceTable);
				room->getDrawPile().removeOne(weapon->getId());
				weapon = c;
				break;
			}
		}
	}
	room->useCard(CardUseStruct(weapon, first));

	if (first->isDead() || second->isDead()) return;

	QStringList names;
	names << "duel" << "savage_assault" << "archery_attack" << "slash";
	if (!Config.BanPackages.contains("maneuvering"))
		names << "fire_attack";

	QList<Card *> cards;
	foreach (QString name, names) {
		Card *card = Sanguosha->cloneCard(name);
		if (!card) continue;
		card->setSkillName("_mobilelianji");
		card->deleteLater();
		if (!first->canUse(card, second, true)) continue;
		cards << card;
	}
	if (cards.isEmpty()) return;

	Card *card = cards.at(qsanRandomBounded(cards.length()));
	room->setCardFlag(card, "mobilelianji_card_" + source->objectName());
	room->useCard(CardUseStruct(card, first, second));
}

class MobileLianjiVS : public ZeroCardViewAsSkill
{
public:
	MobileLianjiVS() : ZeroCardViewAsSkill("mobilelianji")
	{
	}

	const Card *viewAs() const
	{
		return new MobileLianjiCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileLianjiCard");
	}
};

class MobileLianji : public TriggerSkill
{
public:
	MobileLianji() : TriggerSkill("mobilelianji")
	{
		events << CardFinished << DamageDone;
		view_as_skill = new MobileLianjiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card) return false;
			foreach (QString flag, damage.card->getFlags()) {
				if (!flag.startsWith("mobilelianji_card_")) continue;
				int n = room->getTag("mobilelianji_card_damage_point_" + damage.card->toString()).toInt();
				n += damage.damage;
				room->setTag("mobilelianji_card_damage_point_" + damage.card->toString(), n);
				break;
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			foreach (QString flag, use.card->getFlags()) {
				if (!flag.startsWith("mobilelianji_card_")) continue;

				int n = room->getTag("mobilelianji_card_damage_point_" + use.card->toString()).toInt();
				room->removeTag("mobilelianji_card_damage_point_" + use.card->toString());
				if (n <= 0) break;

				QString name = flag.split("_").last();
				ServerPlayer *source = room->findChild<ServerPlayer *>(name);
				if (!source || source->isDead()) break;

				source->gainMark("&mobilelianji", n);
				break;
			}
		}
		return false;
	}
};

class MobileMoucheng : public TriggerSkill
{
public:
	MobileMoucheng() : TriggerSkill("mobilemoucheng")
	{
		events << Damage;
		frequency = Wake;
		waked_skills = "jingong";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName()) || p->getMark(objectName()) > 0) continue;

			if(p->getMark("&mobilelianji")>2){
				LogMessage log;
				log.type = "#MobileMouchengWake";
				log.from = p;
				log.arg = QString::number(p->getMark("&mobilelianji"));
				log.arg2 = objectName();
				room->sendLog(log);
			}else if(!p->canWake(objectName()))
				continue;
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(p, objectName());

			room->setPlayerMark(p, "&mobilelianji", 0);

			room->doSuperLightbox(p, "mobilemoucheng");
			room->setPlayerMark(p, "mobilemoucheng", 1);

			if (room->changeMaxHpForAwakenSkill(p, 1, objectName())){
				room->recover(p, RecoverStruct(p, nullptr, 1, "mengqing"));
				room->handleAcquireDetachSkills(p, "-mobilelianji|jingong");
			}
		}
		return false;
	}
};

class Xunde : public TriggerSkillV2
{
public:
	Xunde() : TriggerSkillV2("xunde") { events << Damaged; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || !player->isAlive()) return result;
		for (ServerPlayer *holder : room->getAlivePlayers())
			if (holder->hasSkill(objectName()) && holder->distanceTo(player) <= 1) result[holder] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.invoker && ctx.invoker->isAlive() && ctx.owner->askForSkillInvoke(this, ctx.invoker);
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(this);
		JudgeStruct judge;
		judge.who = ctx.owner;
		judge.reason = objectName();
		judge.play_animation = false;
		judge.pattern = ".";
		room->judge(judge);
		if (!judge.card) return false;
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		QStringList choices;
		if (judge.card->getNumber() >= 6 && room->CardInPlace(judge.card, Player::DiscardPile)
			&& ctx.invoker && ctx.invoker->isAlive())
			choices << "obtain=" + ctx.invoker->objectName() + "=" + judge.card->objectName();
		if (judge.card->getNumber() <= 6 && damage.from && damage.from->isAlive() && damage.from->canDiscard(damage.from, "h"))
			choices << "discard=" + damage.from->objectName();
		if (choices.isEmpty()) return false;
		ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), *ctx.original_data);
		ctx.extra_data = judge.card->getEffectiveId();
		ctx.manual_effect = true;
		ServerPlayer *target = ctx.choice.startsWith("obtain=") ? ctx.invoker : damage.from;
		return target && target->isAlive() ? skillEffect(event, room, ctx.invoker, ctx, target) : false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.choice.startsWith("obtain=")) {
			const int id = ctx.extra_data.toInt();
			if (room->getCardPlace(id) == Player::DiscardPile) room->obtainCard(target, id);
		} else if (target->canDiscard(target, "h")) {
			const int amount = getEffectiveAmount(ctx);
			room->askForDiscard(target, objectName(), amount, amount);
		}
		return false;
	}
};
class Chenjie : public TriggerSkillV2
{
public:
	Chenjie() : TriggerSkillV2("chenjie") { events << AskForRetrial; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const JudgeStruct *judge = data.value<JudgeStruct *>();
		return player && player->isAlive() && player->hasSkill(objectName()) && !player->isNude() && judge && judge->card
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
		if (!judge || !judge->card || !judge->who) return false;
		const QString prompt = QStringList{"@chenjie-card", judge->who->objectName(), objectName(), judge->reason,
			QString::number(judge->card->getEffectiveId())}.join(":");
		const Card *card = room->askForCard(player, ".|" + judge->card->getSuitString(), prompt,
			*ctx.original_data, Card::MethodNone, judge->who, true, objectName());
		if (!card) return false;
		ctx.extra_data = QVariantMap{{"card", card->getEffectiveId()}, {"judge", judge->card->getEffectiveId()}};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
		const QVariantMap saved = ctx.extra_data.toMap();
		const int id = saved.value("card", -1).toInt();
		const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
		return judge && judge->card && judge->card->getEffectiveId() == saved.value("judge").toInt()
			&& card && room->getCardOwner(id) == player
			&& (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
			&& card->getSuit() == judge->card->getSuit() && !player->isCardLimited(card, Card::MethodResponse);
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
		if (!judge || !judge->who) return false;
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		skillEffect(event, room, player, ctx, judge->who);
		if (ctx.extra_data.toMap().value("retried").toBool() && player->isAlive()) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, player);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		if (saved.value("retried").toBool()) {
			target->drawCards(2 * getEffectiveAmount(ctx), objectName());
			return false;
		}
		JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
		const int id = saved.value("card", -1).toInt();
		if (!judge || !judge->card || judge->who != target || id < 0 || room->getCardOwner(id) != player
			|| judge->card->getEffectiveId() != saved.value("judge").toInt()) return false;
		room->broadcastSkillInvoke(objectName());
		// Retrial owns material submission and response events; no earlier discard occurs.
		room->retrial(Sanguosha->getCard(id), player, judge, objectName());
		saved.insert("retried", true);
		ctx.extra_data = saved;
		return false;
	}
};
class MobileYingyuan : public TriggerSkillV2
{
public:
	MobileYingyuan() : TriggerSkillV2("mobileyingyuan") { events << CardsMoveOneTime << EventSkillInvoking; }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Custom; }
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner || !getUsageRef(ctx).isValid()) return false;
		const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
		if (turn.isEmpty() || turn == "0") return false;
		const QVariantMap used = ctx.owner->getSkillInstanceStateValue(getUsageRef(ctx).key.skillName,
			getUsageRef(ctx).key.instanceID, "used_names").toMap();
		return used.value("turn").toString() != turn
			|| !used.value("names").toStringList().contains(ctx.extra_data.toMap().value("name").toString());
	}
	void addUsage(const SkillContext &ctx) const override
	{
		const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
		QVariantMap used = ctx.owner->getSkillInstanceStateValue(getUsageRef(ctx).key.skillName,
			getUsageRef(ctx).key.instanceID, "used_names").toMap();
		QStringList names = used.value("turn").toString() == turn ? used.value("names").toStringList() : QStringList();
		names << ctx.extra_data.toMap().value("name").toString();
		ctx.owner->setSkillInstanceStateValue(getUsageRef(ctx).key.skillName, getUsageRef(ctx).key.instanceID,
			"used_names", QVariantMap{{"turn", turn}, {"names", names}});
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasFlag("CurrentPlayer")) return {};
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile || move.reason.m_playerId != player->objectName()
			|| (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE) return {};
		const Card *card = move.reason.m_extraData.value<const Card *>();
		return card && !card->isKindOf("SkillCard") && room->CardInPlace(card, Player::DiscardPile)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		const Card *card = move.reason.m_extraData.value<const Card *>();
		if (!card || !room->CardInPlace(card, Player::DiscardPile)) return false;
		QVariantList ids;
		if (card->isVirtualCard()) for (int id : card->getSubcards()) ids << id;
		else ids << card->getEffectiveId();
		if (ids.isEmpty()) return false;
		ctx.extra_data = QVariantMap{{"ids", ids}, {"name", card->isKindOf("Slash") ? "slash" : card->objectName()}};
		if (!checkCustomUsage(ctx)) return false;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
			"@mobileyingyuan:" + card->objectName(), true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!checkCustomUsage(ctx)) return false;
		for (const QVariant &id : ctx.extra_data.toMap().value("ids").toList())
			if (room->getCardPlace(id.toInt()) != Player::DiscardPile) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QList<int> ids;
		for (const QVariant &id : ctx.extra_data.toMap().value("ids").toList()) {
			if (room->getCardPlace(id.toInt()) != Player::DiscardPile) return false;
			ids << id.toInt();
		}
		if (ids.isEmpty()) return false;
		room->broadcastSkillInvoke(objectName());
		DummyCard cards(ids);
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), "");
		room->obtainCard(target, &cards, reason, true);
		return false;
	}
};
class MobileYongdi : public TriggerSkillV2
{
public:
	MobileYongdi() : TriggerSkillV2("mobileyongdi")
	{
		events << Damaged << EventSkillInvoking;
		frequency = Limited;
		limit_mark = "@mobileyongdiMark";
	}
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Game; }
	void addUsage(const SkillContext &ctx) const override
	{
		TriggerSkillV2::addUsage(ctx);
		const SkillInstanceRef ref = getUsageRef(ctx);
		ServerPlayer *holder = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
		if (holder) holder->getRoom()->removePlayerMark(holder, limit_mark);
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == EventSkillInvoking) return {};
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		for (ServerPlayer *target : room->getOtherPlayers(player))
			if (target->isMale()) return TriggerList{{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		QList<ServerPlayer *> males;
		for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) if (target->isMale()) males << target;
		if (males.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, males, objectName(), "@mobileyongdi-invoke", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(ctx.owner, objectName());
		room->gainMaxHp(target, getEffectiveAmount(ctx), objectName());
		if (target->isDead() || target->isLord()) return false;
		QStringList skills;
		const General *generals[] = {target->getGeneral(), target->getGeneral2()};
		for (const General *general : generals) {
			if (!general) continue;
			for (const Skill *skill : general->getVisibleSkillList())
				if (skill->isLordSkill() && !target->hasLordSkill(skill, true) && !skills.contains(skill->objectName()))
					skills << skill->objectName();
		}
		// These are permanent grants, so they deliberately do not cascade with the consumed source.
		for (const QString &name : skills) room->acquireSkillFromEffect(target, name, ctx);
		return false;
	}
};
class Chijiec : public TriggerSkillV2
{
public:
	Chijiec() : TriggerSkillV2("chijiec") { events << GameStart; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player->askForSkillInvoke(this)) return false;
		QStringList kingdoms;
		for (ServerPlayer *other : room->getAlivePlayers()) if (!kingdoms.contains(other->getKingdom())) kingdoms << other->getKingdom();
		if (kingdoms.isEmpty()) return false;
		ctx.extra_data = room->askForChoice(player, objectName(), kingdoms.join("+"));
		ctx.targets = {player};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		LogMessage log;
		log.type = "#ChijieKingdom";
		log.from = target;
		log.arg = ctx.extra_data.toString();
		room->sendLog(log);
		room->setPlayerProperty(target, "kingdom", ctx.extra_data);
		return false;
	}
};
WaishiCard::WaishiCard() { setSkillName("waishi"); will_throw = false; handling_method = Card::MethodNone; }
bool WaishiCard::targetFilter(const QList<const Player *> &targets, const Player *candidate, const Player *self) const
{
	return ActiveSkillCard::targetFilter(targets, candidate, self);
}
void WaishiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}

class Waishi : public ViewAsSkillV2
{
public:
	Waishi() : ViewAsSkillV2("waishi", -1) { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	int getMaxUsageLimit(const SkillContext &ctx) const override
	{
		return ctx.owner ? 1 + ctx.owner->getMark("mobile_waishi_extra_active") : 1;
	}
	static int kingdoms(const Player *player)
	{
		QSet<QString> names;
		for (const Player *other : player->getAliveSiblings(true)) names << other->getKingdom();
		return names.size();
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getPhase() == Player::Play;
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.length() < kingdoms(request.initiator)
			&& (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.initiator && !request.selectedCardIds.isEmpty() && request.selectedCardIds.length() <= kingdoms(request.initiator);
	}
	bool willThrowSelectedCards() const override { return false; }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		return target && target->isAlive() && target != request.initiator && selected.isEmpty()
			&& target->getHandcardNum() >= request.selectedCardIds.length();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "WaishiCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		WaishiCard *card = new WaishiCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return false;
		for (int id : request.selectedCardIds)
			if (room->getCardOwner(id) != ctx.owner || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
		return true;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		ctx.extra_data = false;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (ServerPlayer *target : ctx.targets) if (target->isAlive()) skillEffect(ctx, target);
		if (ctx.extra_data.toBool() && ctx.owner->isAlive()) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(ctx, ctx.owner);
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.extra_data.toBool()) { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
		Room *room = target->getRoom();
		const QList<int> materials = ctx.use_card->getSubcards();
		if (!ctx.owner->isAlive() || materials.isEmpty() || target->getHandcardNum() < materials.length()) return ContinueEffects;
		for (int id : materials) if (room->getCardOwner(id) != ctx.owner) return ContinueEffects;
		QList<int> pool = target->handCards(), selected;
		for (int i = 0; i < materials.length(); ++i) {
			const int index = qsanRandomBounded(pool.length());
			selected << pool.takeAt(index);
		}
		// Both directions are one atomic swap; no intermediate give can consume the other half.
		QList<CardsMoveStruct> moves;
		moves << CardsMoveStruct(materials, target, Player::PlaceHand, CardMoveReason(CardMoveReason::S_REASON_SWAP, ctx.owner->objectName(), target->objectName(), objectName(), ""));
		moves << CardsMoveStruct(selected, ctx.owner, Player::PlaceHand, CardMoveReason(CardMoveReason::S_REASON_SWAP, target->objectName(), ctx.owner->objectName(), objectName(), ""));
		room->moveCardsAtomic(moves, false);
		ctx.extra_data = ctx.owner->isAlive() && target->isAlive()
			&& (ctx.owner->getKingdom() == target->getKingdom() || target->getHandcardNum() > ctx.owner->getHandcardNum());
		return ContinueEffects;
	}
};

class Renshe : public TriggerSkillV2
{
public:
	Renshe() : TriggerSkillV2("renshe") { events << Damaged << EventPhaseChanging << EventPhaseStart; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		const bool start = event == EventPhaseStart && player->getPhase() == Player::Play;
		const bool end = event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play;
		if (!start && !end) return true;
		QVariantList receipts;
		int active = 0, total = 0;
		for (const QVariant &value : player->getTag("mobile_renshe_receipts").toList()) {
			QVariantMap receipt = value.toMap();
			if (end && receipt.value("active").toBool()) continue;
			if (start) receipt.insert("active", true);
			const int amount = receipt.value("amount").toInt();
			if (receipt.value("active").toBool()) active += amount;
			total += amount;
			receipts << receipt;
		}
		player->setTag("mobile_renshe_receipts", receipts);
		room->setPlayerMark(player, "mobile_waishi_extra_active", active);
		room->setPlayerMark(player, "&waishi_extra-SelfPlayClear", total);
		return true;
	}	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == Damaged && player && player->isAlive() && player->hasSkill(objectName())
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player->askForSkillInvoke(this)) return false;
		QStringList kingdoms, choices;
		for (ServerPlayer *other : room->getAlivePlayers())
			if (other->getKingdom() != player->getKingdom() && !kingdoms.contains(other->getKingdom())) kingdoms << other->getKingdom();
		if (!kingdoms.isEmpty()) choices << "change";
		choices << "extra";
		if (!room->getOtherPlayers(player).isEmpty()) choices << "draw";
		const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		ctx.extra_data = QVariantMap{{"choice", choice}};
		ctx.targets = {player};
		if (choice == "change") ctx.extra_data = QVariantMap{{"choice", choice}, {"kingdom", room->askForChoice(player, "renshe_change", kingdoms.join("+"))}};
		else if (choice == "draw") {
			const QList<ServerPlayer *> others = room->getOtherPlayers(player);
			if (!others.isEmpty()) {
				ServerPlayer *target = room->askForPlayerChosen(player, others, objectName(), "@renshe-target");
				if (target) ctx.targets << target;
			}
			room->sortByActionOrder(ctx.targets);
		}
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const QVariantMap saved = ctx.extra_data.toMap();
		const QString choice = saved.value("choice").toString();
		room->broadcastSkillInvoke(objectName());
		if (choice == "change") room->changeKingdom(target, saved.value("kingdom").toString());
		else if (choice == "extra") {
			QVariantList receipts = target->getTag("mobile_renshe_receipts").toList();
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
			target->setTag("mobile_renshe_receipts", receipts);
			LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = target;
			log.arg = "renshe:extra";
			room->sendLog(log);
			room->addPlayerMark(target, "&waishi_extra-SelfPlayClear", getEffectiveAmount(ctx));
		} else target->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};
class Xingluan : public TriggerSkillV2
{
public:
	Xingluan(const QString &name) : TriggerSkillV2(name) { events << CardFinished << EventSkillInvoking; frequency = Frequent; setPhaseName("Play"); }
		void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		// A cost bypass still consumes the exact accepted activation before effects can re-enter.
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	LimitScope getLimitScope() const override { return Limit_Phase; }
	static bool hasSix(const ServerPlayer *player)
	{
		for (const Card *card : player->getCards("ej")) if (card->getNumber() == 6) return true;
		return false;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
			&& use.card && !use.card->isKindOf("SkillCard") && (objectName() == "olxingluan" || use.to.length() == 1)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !player->askForSkillInvoke(this)) return false;
		ctx.targets = {player};
		ctx.extra_data = QStringLiteral("draw");
		if (objectName() != "olxingluan") return true;
		QList<ServerPlayer *> fields, others;
		for (ServerPlayer *target : room->getAlivePlayers()) {
			if (hasSix(target)) fields << target;
			if (target != player && !target->isNude()) others << target;
		}
		QStringList choices{"draw"};
		if (!fields.isEmpty()) choices.prepend("get");
		if (!others.isEmpty()) choices << "player";
		const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		if (choice != "draw") {
			ServerPlayer *target = room->askForPlayerChosen(player, choice == "get" ? fields : others, objectName(), "olxingluan0:");
			if (!target) return false;
			ctx.targets = {target};
		}
		ctx.extra_data = choice;
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		const QString choice = ctx.extra_data.toString();
		for (int i = 0; i < getEffectiveAmount(ctx) && player->isAlive() && target->isAlive(); ++i) {
			if (choice == "get") {
				if (!hasSix(target)) break;
				QList<int> disabled;
				for (const Card *card : target->getCards("ej")) if (card->getNumber() != 6) disabled << card->getEffectiveId();
				const int id = room->askForCardChosen(player, target, "ej", objectName(), false, Card::MethodNone, disabled);
				if (id >= 0 && room->getCardOwner(id) == target && Sanguosha->getCard(id)->getNumber() == 6)
					room->obtainCard(player, id, true);
			} else if (choice == "player") {
				if (target->isNude()) break;
				if (!room->askForDiscard(target, objectName(), 1, 1, true, true, "olxingluan01:", ".|.|6")) {
					const Card *gift = room->askForExchange(target, objectName(), 1, 1, true);
					if (gift && player->isAlive()) room->giveCard(target, player, gift, objectName());
				}
			} else {
				QList<int> candidates;
				for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->getNumber() == 6) candidates << id;
				if (candidates.isEmpty()) {
					if (objectName() == "xingluan") {
						LogMessage log;
						log.type = "#XingluanNoSix";
						log.arg = "6";
						room->sendLog(log);
					} else target->drawCards(objectName() == "tenyearxingluan" ? 6 : 1, objectName());
					continue;
				}
				int id = candidates.takeAt(qsanRandomBounded(candidates.length()));
				if (objectName() == "olxingluan") {
					QList<int> offered{id};
					if (!candidates.isEmpty()) offered << candidates.at(qsanRandomBounded(candidates.length()));
					room->fillAG(offered);
					try { id = room->askForAG(target, offered, false, objectName()); }
					catch (...) { room->clearAG(); throw; }
					room->clearAG();
					if (!offered.contains(id)) continue;
				}
				if (room->getCardPlace(id) == Player::DrawPile) room->obtainCard(target, id, true);
			}
		}
		return false;
	}
};
JiaohuaCard::JiaohuaCard()
{
}

bool JiaohuaCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void JiaohuaCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	room->addPlayerMark(from, "jiaohua_tiansuan_remove_" + user_string);
	QStringList ts = from->getTag("jiaohuaType").toStringList();
	ts << user_string;
	from->setTag("jiaohuaType", ts);

	foreach (int id, room->getDrawPile()) {
		if (Sanguosha->getCard(id)->getType()==user_string){
			room->obtainCard(to,id);
			break;
		}
	}
	if(ts.length()>2){
		foreach (QString t, ts) {
			room->removePlayerMark(from, "jiaohua_tiansuan_remove_" + t);
		}
		from->setTag("jiaohuaType", QStringList());
	}
}

class jiaohua : public ZeroCardViewAsSkill
{
public:
	jiaohua() : ZeroCardViewAsSkill("jiaohua")
	{
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::tiansuan("jiaohua", "basic,trick,equip");
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JiaohuaCard") < 2;
	}

	const Card *viewAs() const
	{
		JiaohuaCard *c = new JiaohuaCard;
		c->setUserString(Self->getTag("jiaohua").toString());
		return c;
	}
};

class Yichong : public TriggerSkill
{
public:
	Yichong() : TriggerSkill("yichong")
	{
		events << EventPhaseStart << BeforeCardsMove << EventLoseSkill << Death;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==BeforeCardsMove){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&player->hasSkill(objectName())&&player->getMark("que_ycNum")<1){
				QList<int> ids;
				foreach (int id, move.card_ids) {
					const Card *c = Sanguosha->getCard(id);
					if(move.to->getMark("&que_yc+-+"+c->getSuitString()+"_char+#"+player->objectName())>0){
						player->addMark("que_ycNum");
						ids << id;
						break;
					}
				}
				if(ids.isEmpty()) return false;
				move.removeCardIds(ids);
				data = QVariant::fromValue(move);
				CardsMoveStruct move1 = CardsMoveStruct(ids,move.from,player,room->getCardPlace(ids.last()),move.to_place,move.reason);
				room->moveCardsAtomic(move1,move.open.last());
			}
		}else if(event==EventPhaseStart&&player->getPhase()==Player::Start){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName()))
						p->loseMark(m);
				}
			}
			if(!player->hasSkill(objectName())) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "yichong0", true, true);
			if(target){
				room->broadcastSkillInvoke(objectName());
				Card::Suit suit = room->askForSuit(player,objectName());
				QString st = "no_suit";
				if(suit==Card::Spade)
					st = "spade";
				else if(suit==Card::Club)
					st = "club";
				else if(suit==Card::Heart)
					st = "heart";
				else if(suit==Card::Diamond)
					st = "diamond";
				DummyCard *dummy = new DummyCard();
				foreach (const Card *c, target->getEquips()) {
					if (c->getSuit()==suit)
						dummy->addSubcard(c);
				}
				QList<const Card *> cards = target->getHandcards();
				qsanShuffle(cards);
				foreach (const Card *c, cards) {
					if (c->getSuit()==suit){
						dummy->addSubcard(c);
						break;
					}
				}
				player->obtainCard(dummy, false);
				dummy->deleteLater();
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					foreach (QString m, p->getMarkNames()) {
						if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName()))
							p->loseMark(m);
					}
				}
				target->gainMark("&que_yc+-+"+st+"_char+#"+player->objectName());
				player->setMark("que_ycNum",0);
			}
		}else if(event==EventLoseSkill&&data.value<SkillChangeStruct>().skillName==objectName()){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName()))
						room->setPlayerMark(p,m,0);
				}
			}
		}else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(death.who)) {
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(death.who->objectName()))
						room->setPlayerMark(p,m,0);
				}
			}
		}
		return false;
	}
};

class Wufei : public TriggerSkill
{
public:
	Wufei() : TriggerSkill("wufei")
	{
		events << DamageCaused << Damaged;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName())&&p->getMark(m)>0&&p->getHp()>3&&player->askForSkillInvoke(this,p)){
						room->broadcastSkillInvoke(objectName());
						room->damage(DamageStruct(objectName(),nullptr,p));
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isDamageCard()){
				CardUseStruct card_use = room->getTag("UseHistory"+damage.card->toString()).value<CardUseStruct>();
				if(card_use.from!=player) return false;
				if(damage.card->isKindOf("Slash")){
					if(card_use.to.length()!=1)
						return false;
				}else if(card_use.to.length()<2)
					return false;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					foreach (QString m, p->getMarkNames()) {
						if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName())&&p->getMark(m)>0){
							room->sendCompulsoryTriggerLog(player,this);
							damage.from = p;
							data.setValue(damage);
						}
					}
				}
			}
		}
		return false;
	}
};

ShiheCard::ShiheCard() { setSkillName("shihe"); }
bool ShiheCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *self) const
{
	return ActiveSkillCard::targetFilter(targets, target, self);
}
void ShiheCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ActiveSkillCard::use(room, source, targets);
}
class Shihevs : public ViewAsSkillV2
{
public:
	Shihevs() : ViewAsSkillV2("shihe") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play && request.initiator->canPindian();
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		return request.initiator && target && selected.isEmpty() && target != request.initiator && request.initiator->canPindian(target);
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "ShiheCard"; }
	const Card *createCard(const ActiveSkillRequest &) const override { return new ShiheCard; }
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (ServerPlayer *target : ctx.targets) {
			ctx.extra_data = QStringLiteral("pindian");
			skillEffect(ctx, target);
			if (ctx.extra_data.toString() == "lost" && ctx.owner->isAlive()) {
				ctx.modified_amount = amount;
				ctx.modified_amount_set = modified;
				skillEffect(ctx, ctx.owner);
			}
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = target->getRoom();
		if (ctx.extra_data.toString() == "lost") {
			for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
				QList<int> ids;
				for (const Card *card : target->getCards("he")) if (target->canDiscard(target, card->getEffectiveId())) ids << card->getEffectiveId();
				if (ids.isEmpty()) break;
				room->throwCard(ids.at(qsanRandomBounded(ids.length())), target);
			}
			return ContinueEffects;
		}
		if (!ctx.owner->canPindian(target, false)) return ContinueEffects;
		if (!ctx.owner->pindian(target, objectName())) { ctx.extra_data = QStringLiteral("lost"); return ContinueEffects; }
		QVariantList receipts = target->getTag("mobile_shihe_protection").toList();
		const quint64 serial = room->getTag("mobile_shihe_serial").toULongLong() + 1;
		room->setTag("mobile_shihe_serial", serial);
		receipts << QVariantMap{{"serial", serial}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
			{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
			{"protected", ctx.owner->objectName()}, {"amount", getEffectiveAmount(ctx)}, {"expiry_turn", QStringLiteral("0")}};
		target->setTag("mobile_shihe_protection", receipts);
		room->setPlayerMark(target, "&shihe+#" + ctx.owner->objectName(), 1);
		return ContinueEffects;
	}
};
class Shihe : public TriggerSkillV2
{
public:
	Shihe() : TriggerSkillV2("shihe") { events << TurnStart << EventPhaseStart << Predamage; global = true; view_as_skill = new Shihevs; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || (event != TurnStart && (event != EventPhaseStart || player->getPhase() != Player::NotActive))) return true;
		const QString turn = room->historyScopes().value("turn_id").toString();
		QVariantList keep;
		QStringList names;
		for (const QVariant &entry : player->getTag("mobile_shihe_protection").toList()) {
			QVariantMap receipt = entry.toMap();
			names << receipt.value("protected").toString();
			if (event == TurnStart && receipt.value("expiry_turn").toString() == "0") receipt.insert("expiry_turn", turn);
			if (event != EventPhaseStart || receipt.value("expiry_turn").toString() != turn) keep << receipt;
		}
		player->setTag("mobile_shihe_protection", keep);
		names.removeDuplicates();
		for (const QString &name : names) {
			bool active = false;
			for (const QVariant &entry : keep) if (entry.toMap().value("protected").toString() == name) active = true;
			room->setPlayerMark(player, "&shihe+#" + name, active ? 1 : 0);
		}
		return true;
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != Predamage || !player) return true;
		const DamageStruct damage = data.value<DamageStruct>();
		if (!damage.to || !damage.to->isAlive() || damage.damage <= 0) return true;
		for (const QVariant &entry : player->getTag("mobile_shihe_protection").toList()) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("protected").toString() != damage.to->objectName()) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
			if (!ctx.owner) continue;
			ctx.invoker = player;
			ctx.initiator = ctx.owner;
			ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.amount = receipt.value("amount").toInt();
			ctx.extra_data = receipt;
			ctx.targets = {damage.to};
			ctx.current_event = event;
			ctx.original_data = &data;
			ctx.is_forced = true;
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *, const SkillContext &ctx) const override
	{
		return ctx.invoker && ctx.invoker->getTag("mobile_shihe_protection").toList().contains(ctx.extra_data);
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		return target->damageRevises(*ctx.original_data, -ctx.original_data->value<DamageStruct>().damage);
	}
};
class Zhenfu : public TriggerSkillV2
{
public:
	Zhenfu() : TriggerSkillV2("zhenfu") { events << EventPhaseStart; }
	static int discarded(Room *room, ServerPlayer *player)
	{
		const QVariant turn = room->historyScopes().value("turn_id");
		if (turn.toLongLong() <= 0) return -1;
		QVariantMap query{{"turn_id", turn}, {"from", player->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList())
				if ((entry.toMap().value("data").toMap().value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) return 1;
			if (!page.value("has_more").toBool()) return 0;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
		const int result = discarded(room, player);
		if (result < 0) qWarning("Zhenfu: incomplete discard history");
		return result == 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "zhenfu0:", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		target->gainHujia(getEffectiveAmount(ctx), 5);
		return false;
	}
};

class Guimou : public TriggerSkill
{
public:
	Guimou() : TriggerSkill("guimou")
	{
		events << CardsMoveOneTime << EventPhaseStart << GameStart << CardUsed << Death << EventLoseSkill;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (player==move.from&&(move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou+-+discard")>0){
						room->addPlayerMark(player,"&guimou_discard",move.card_ids.length());
						break;
					}
				}
			}
			if (player==move.to&&move.to_place == Player::PlaceHand) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou+-+gain")>0){
						room->addPlayerMark(player,"&guimou_gain",move.card_ids.length());
						break;
					}
				}
			}
		}else if(triggerEvent==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("SkillCard")){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou+-+use")>0){
						room->addPlayerMark(player,"&guimou_use");
						break;
					}
				}
			}
		}else if(triggerEvent==Death||triggerEvent==EventLoseSkill){
			if(room->findPlayerBySkillName(objectName(),true)) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&guimou"))
						room->setPlayerMark(player,m,0);
				}
			}
		} else{
			if(!player->hasSkill(objectName())) return false;
			QString choice,choices = "use+discard+gain";
			if(triggerEvent == GameStart){
				room->sendCompulsoryTriggerLog(player,this);
				choice = choices.split("+").at(qsanRandomBounded(3));
				room->setPlayerMark(player,"&guimou+-+"+choice,1);
			}else if(player->getPhase()==Player::NotActive){
				room->sendCompulsoryTriggerLog(player,this);
				choice = room->askForChoice(player,objectName(),choices);
				room->setPlayerMark(player,"&guimou+-+"+choice,1);
			}else if(player->getPhase()==Player::Start){
				foreach (QString m, player->getMarkNames()) {
					if(m.startsWith("&guimou+-+")&&player->getMark(m)>0){
						room->setPlayerMark(player,m,0);
						choice = m.split("+").last();
					}
				}
				if(choice.isEmpty()) return false;
				room->sendCompulsoryTriggerLog(player,this);
				int n = 998;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					n = qMin(n,p->getMark("&guimou_"+choice));
				}
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou_"+choice)<=n)
						tos << p;
					room->setPlayerMark(p,"&guimou_"+choice,0);
				}
				ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"guimou0:",false,true);
				if(to){
					int id = room->doGongxin(player,to,to->handCards(),objectName());
					if(id>=0){
						tos = room->getOtherPlayers(player);
						tos.removeOne(to);
						ServerPlayer *to1 = room->askForPlayerChosen(player,tos,"guimou1","guimou1:",player->canDiscard(to,id));
						if(to1){
							room->giveCard(player,to1,Sanguosha->getCard(id),objectName());
							n = -1;
						}else if(player->canDiscard(to,id)){
							room->throwCard(id,objectName(),to,player);
							n = -1;
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhouxian : public TriggerSkillV2
{
public:
	Zhouxian() : TriggerSkillV2("zhouxian") { events << TargetConfirming; frequency = Compulsory; m_baseAmount = 3; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && use.from && use.from != player
			&& use.card && use.card->isDamageCard() && use.to.contains(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.extra_data = QVariantMap{{"step", "reveal"}};
		skillEffect(event, room, player, ctx, player);
		QVariantMap saved = ctx.extra_data.toMap();
		if (!saved.contains("cards")) return false;
		const QVariantList revealed = saved.value("cards").toList();
		auto cleanup = [&]() {
			QList<int> ids;
			for (const QVariant &id : revealed) if (room->getCardPlace(id.toInt()) == Player::PlaceTable) ids << id.toInt();
			if (!ids.isEmpty()) room->throwCard(ids, objectName(), nullptr);
		};
		try {
			ServerPlayer *user = ctx.original_data->value<CardUseStruct>().from;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			saved.insert("step", "discard");
			ctx.extra_data = saved;
			if (user && user->isAlive()) skillEffect(event, room, player, ctx, user);
			saved = ctx.extra_data.toMap();
			if (!saved.value("paid").toBool() && player->isAlive()) {
				ctx.modified_amount = amount;
				ctx.modified_amount_set = modified;
				saved.insert("step", "cancel_target");
				ctx.extra_data = saved;
				skillEffect(event, room, player, ctx, player);
			}
			cleanup();
		} catch (...) { try { cleanup(); } catch (...) {} throw; }
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		const QString step = saved.value("step").toString();
		if (step == "reveal") {
			room->sendCompulsoryTriggerLog(player, this);
			QVariantList cards;
			QStringList types;
			for (int id : room->showDrawPile(target, getEffectiveAmount(ctx), objectName(), true)) {
				cards << id;
				const QString type = Sanguosha->getCard(id)->getType();
				if (!types.contains(type)) types << type;
			}
			saved.insert("cards", cards);
			saved.insert("types", types);
		} else if (step == "discard") {
			const QStringList types = saved.value("types").toStringList();
			if (!types.isEmpty()) {
				// The AI prompt payload is scoped; a nested prompt restores its outer value.
				const QVariant previous = target->getTag("zhouxianUse");
				target->setTag("zhouxianUse", *ctx.original_data);
				try {
					saved.insert("paid", room->askForDiscard(target, objectName(), 1, 1, true, true,
						"zhouxian0:" + player->objectName(), types.join(",")) != nullptr);
					target->setTag("zhouxianUse", previous);
				} catch (...) { target->setTag("zhouxianUse", previous); throw; }
			}
		} else {
			// Payment may have entered other callbacks; preserve their updates to this use.
			CardUseStruct use = ctx.original_data->value<CardUseStruct>();
			use.to.removeOne(target);
			ctx.original_data->setValue(use);
		}
		ctx.extra_data = saved;
		return false;
	}
};
class Shoufa : public TriggerSkill
{
public:
	Shoufa() : TriggerSkill("shoufa")
	{
		events << Damage << Damaged;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (triggerEvent == Damage) {
			player->addMark("shoufaDamage-Clear");
			if (player->getMark("shoufaDamage-Clear")==1){
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(player->distanceTo(p)<=2)
						tos << p;
				}
				ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"shoufa0:",true,true);
				if(to){
					room->broadcastSkillInvoke(objectName());
					QString yeshou = player->getTag("zhoulin_yeshou").toString();
					if(yeshou.isEmpty()){
						yeshou = "yeshou_bao+yeshou_ying+yeshou_xiong+yeshou_tu";
						yeshou = yeshou.split("+").at(qsanRandomBounded(4));
					}
					LogMessage log;
					log.type = "#shoufa";
					log.from = player;
					log.to << to;
					log.arg = yeshou;
					log.arg2 = "shoufa";
					room->sendLog(log);
					if(yeshou=="yeshou_bao"){
						room->damage(DamageStruct(yeshou,nullptr,to));
					}else if(yeshou=="yeshou_ying"){
						QList<const Card *> cs = to->getCards("he");
						qsanShuffle(cs);
						foreach (const Card *c, cs){
							room->obtainCard(player,c,false);
							break;
						}
					}else if(yeshou=="yeshou_xiong"){
						QList<const Card *> cs = to->getCards("e");
						qsanShuffle(cs);
						foreach (const Card *c, cs){
							if(player->canDiscard(to,c->getId())){
								room->throwCard(c,yeshou,to,player);
								break;
							}
						}
					}else{
						to->drawCards(1,yeshou);
					}
				}
			}
		}else if(player->getMark("shoufaDamaged-Clear")<=5){
			QList<ServerPlayer *> tos;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->distanceTo(player)>2)
					tos << p;
			}
			ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"shoufa0:",true,true);
			if(to){
				room->broadcastSkillInvoke(objectName());
				player->addMark("shoufaDamaged-Clear");
				QString yeshou = player->getTag("zhoulin_yeshou").toString();
				if(yeshou.isEmpty()){
					yeshou = "yeshou_bao+yeshou_ying+yeshou_xiong+yeshou_tu";
					yeshou = yeshou.split("+").at(qsanRandomBounded(4));
				}
				LogMessage log;
				log.type = "#shoufa";
				log.from = player;
				log.to << to;
				log.arg = yeshou;
				log.arg2 = "shoufa";
				room->sendLog(log);
				if(yeshou=="yeshou_bao"){
					room->damage(DamageStruct(yeshou,nullptr,to));
				}else if(yeshou=="yeshou_ying"){
					QList<const Card *> cs = to->getCards("he");
					qsanShuffle(cs);
					foreach (const Card *c, cs){
						room->obtainCard(player,c,false);
						break;
					}
				}else if(yeshou=="yeshou_xiong"){
					QList<const Card *> cs = to->getCards("e");
					qsanShuffle(cs);
					foreach (const Card *c, cs){
						if(player->canDiscard(to,c->getId())){
							room->throwCard(c,yeshou,to,player);
							break;
						}
					}
				}else{
					to->drawCards(1,yeshou);
				}
			}
		}
		return false;
	}
};

ZhoulinCard::ZhoulinCard()
{
	target_fixed = true;
}

void ZhoulinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source,"@zhoulin");
	room->doSuperLightbox(source,getSkillName());
	source->gainHujia(2,5);
	QString choices = "yeshou_bao+yeshou_ying+yeshou_xiong+yeshou_tu";
	choices = room->askForChoice(source,getSkillName(),choices);
	source->setTag("zhoulin_yeshou", choices);
	
}

class Zhoulinvs : public ZeroCardViewAsSkill
{
public:
	Zhoulinvs() : ZeroCardViewAsSkill("zhoulin")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@zhoulin")>0;
	}

	const Card *viewAs() const
	{
		return new ZhoulinCard;
	}
};

class Zhoulin : public TriggerSkill
{
public:
	Zhoulin() : TriggerSkill("zhoulin")
	{
		events << EventPhaseStart;
		view_as_skill = new Zhoulinvs;
		frequency = Limited;
		limit_mark = "@zhoulin";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent , Room *, ServerPlayer *player, QVariant &) const
	{
		if(player->getPhase()<Player::Start){
			player->setTag("zhoulin_yeshou", "");
		}
		return false;
	}
};

class Yuxiang : public TriggerSkillV2
{
public:
	Yuxiang() : TriggerSkillV2("yuxiang") { events << DamageInflicted; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && player->getHujia() > 0
			&& data.value<DamageStruct>().nature == DamageStruct::Fire ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override { ctx.targets = {player}; return true; }
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
	{
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		damage.damage += getEffectiveAmount(ctx);
		ctx.original_data->setValue(damage);
		return false;
	}
};

class YuxiangDistance : public DistanceSkillV2
{
public:
	YuxiangDistance() : DistanceSkillV2("#yuxiang") { setHolderSelector(CorrectSkill_Participants); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.holder || ctx.holder->getHujia() <= 0) return CorrectSkillResult::noEffect();
		// Each participant contributes only its own live related instance.
		if (ctx.holder == ctx.primary) return CorrectSkillResult::useAmount(-ctx.currentAmount);
		if (ctx.holder == ctx.secondary) return CorrectSkillResult::useAmount(ctx.currentAmount);
		return CorrectSkillResult::noEffect();
	}
};

class spYilie : public TriggerSkill
{
public:
	spYilie() : TriggerSkill("spyilie")
	{
		events << DamageInflicted << GameStart << EventPhaseStart << Damage;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageInflicted) {
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(player->getMark("&spyilie+#"+p->objectName())>0&&p->hasSkill(objectName())&&p->getMark("&yi_lie")<1){
					room->sendCompulsoryTriggerLog(p,this,qsanRandomBounded(2)+1);
					p->gainMark("&yi_lie",damage.damage);
					return true;
				}
			}
		}else if(triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(damage.to!=p&&player->getMark("&spyilie+#"+p->objectName())>0&&p->hasSkill(objectName())&&p->isWounded()){
					room->sendCompulsoryTriggerLog(p,this,qsanRandomBounded(2)+1);
					room->recover(p,RecoverStruct(objectName()));
				}
			}
		}else if(triggerEvent == GameStart) {
			if(player->hasSkill(objectName())){
				ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"spyilie0:",false,true);
				if(to){
					room->broadcastSkillInvoke(objectName(),player,qsanRandomBounded(2)+1);
					room->setPlayerMark(to,"&spyilie+#"+player->objectName(),1);
				}
			}
		}else if(player->getPhase()==Player::Finish){
			int n = player->getMark("&yi_lie");
			if(n>0){
				room->sendCompulsoryTriggerLog(player,this,3);
				player->drawCards(1,objectName());
				room->loseHp(player,n,true,player,objectName());
				player->loseAllMarks("&yi_lie");
			}
		}
		return false;
	}
};

class Laishou : public TriggerSkillV2
{
public:
	Laishou() : TriggerSkillV2("laishou") { events << DamageInflicted << EventPhaseStart; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		const bool eligible = event == DamageInflicted
			? player->getMaxHp() < 9 && data.value<DamageStruct>().damage >= player->getHp() + player->getHujia()
			: player->getPhase() == Player::Start && player->getMaxHp() >= 9;
		return eligible ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override { ctx.targets = {player}; return true; }
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->sendCompulsoryTriggerLog(player, this, event == DamageInflicted ? qsanRandomBounded(2) + 1 : 3);
		if (event == DamageInflicted) {
			room->gainMaxHp(target, ctx.original_data->value<DamageStruct>().damage * getEffectiveAmount(ctx), objectName());
			return true;
		}
		room->killPlayer(target);
		return false;
	}
};

LuanqunCard::LuanqunCard()
{
	target_fixed = true;
}

void LuanqunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->doAnimate(1,source->objectName(),p->objectName());
	}
	QHash<ServerPlayer *, const Card* > pc;
	foreach (ServerPlayer *p, room->getAllPlayers()){
		if(p->getHandcardNum()>0)
			pc[p] = room->askForCardShow(p,source,getSkillName());
	}
	QList<int> ids;
	foreach (ServerPlayer *p, pc.keys()){
		const Card*c = pc[p];
		if(c) room->showCard(p,c->getEffectiveId());
		if(source!=p&&pc.contains(source)){
			if(c->getColor()==pc[source]->getColor())
				ids << c->getEffectiveId();
			else{
				room->setPlayerMark(p,"&luanqun+#"+source->objectName(),1);
			}
		}
	}
	if(ids.length()>0){
		room->fillAG(ids,source);
		int id = room->askForAG(source,ids,true,getSkillName());
		room->clearAG(source);
		if(id>=0){
			room->obtainCard(source,id);
		}
	}
}

class Luanqunvs : public ZeroCardViewAsSkill
{
public:
	Luanqunvs() : ZeroCardViewAsSkill("luanqun")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("LuanqunCard")<1&&player->getHandcardNum()>0;
	}

	const Card *viewAs() const
	{
		return new LuanqunCard;
	}
};

class Luanqun : public TriggerSkill
{
public:
	Luanqun() : TriggerSkill("luanqun")
	{
		events << EventPhaseEnd << CardEffect << CardUsed;
		view_as_skill = new Luanqunvs;
		waked_skills = "#luanqun-pro";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardEffect){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->isKindOf("Slash")&&effect.from->hasFlag("CurrentPlayer")&&effect.from->getMark("&luanqun+#"+player->objectName())>0){
				effect.no_respond = true;
				data.setValue(effect);
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&player->hasFlag("CurrentPlayer")&&player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, use.to)
					room->setPlayerMark(player,"&luanqun+#"+p->objectName(),0);
			}
		}else if(player->getPhase()==Player::Play&&player->hasFlag("CurrentPlayer")){
			foreach (ServerPlayer *p, room->getPlayers())
				room->setPlayerMark(player,"&luanqun+#"+p->objectName(),0);
		}
		return false;
	}
};

class LuanqunPro : public ProhibitSkill
{
public:
	LuanqunPro() : ProhibitSkill("#luanqun-pro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if(card->isKindOf("Slash")&&from->getPhase()==Player::Play){
			foreach (const Player *p, to->getAliveSiblings()){
				if(from->getMark("&luanqun+#"+p->objectName())>0)
					return true;
			}
		}
		return false;
	}
};

NaxueCard::NaxueCard() { will_throw = false; handling_method = Card::MethodNone; }

bool NaxueCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return to_select && to_select != Self && to_select->isAlive() && !targets.contains(to_select) && targets.length() < subcardsLength();
}

bool NaxueCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return !targets.isEmpty() && targets.length() == subcardsLength() && targets.length() <= 2;
}

void NaxueCard::onUse(Room *room, CardUseStruct &use) const { ActiveSkillCard::onUse(room, use); }

class Naxuevs : public ViewAsSkillV2
{
public:
	Naxuevs() : ViewAsSkillV2("naxue", 2) { response_pattern = "@@naxue"; }
	bool willThrowSelectedCards() const override { return false; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@naxue";
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.length() < 2 && !request.selectedCardIds.contains(card->getEffectiveId())
			&& (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty() || request.selectedCardIds.length() > 2) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		for (int id : request.selectedCardIds) {
			if (!canSelectCard(selection, Sanguosha->getCard(id))) return false;
			selection.selectedCardIds << id;
		}
		return true;
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
	{
		return target && target != request.initiator && target->isAlive() && !targets.contains(target) && targets.length() < request.selectedCardIds.length();
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
	{
		return !targets.isEmpty() && targets.length() == request.selectedCardIds.length();
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		NaxueCard *card = new NaxueCard;
		card->setActiveSkill(this);
		card->setSkillName(objectName());
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override { return cardSelectionFeasible(request); }
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		const int index = ctx.targets.indexOf(target);
		if (!ctx.use_card || index < 0 || index >= ctx.use_card->subcardsLength()) return ContinueEffects;
		const int id = ctx.use_card->getSubcards().at(index);
		Room *room = ctx.owner->getRoom();
		if (room->getCardOwner(id) == ctx.owner && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
			room->giveCard(ctx.owner, target, Sanguosha->getCard(id), objectName());
		return ContinueEffects;
	}
};

class Naxue : public TriggerSkillV2
{
public:
	Naxue() : TriggerSkillV2("naxue") { events << EventPhaseChanging; view_as_skill = new Naxuevs; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && data.value<PhaseChangeStruct>().to == Player::Play
			&& !player->isSkipped(Player::Play) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = {player};
		return !player->isSkipped(Player::Play) && player->askForSkillInvoke(this, *ctx.original_data);
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		Room::AcceptedViewAsEffectScope selection(room, player, objectName(), ctx);
		if (!selection.isValid()) return false;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (const QString &action : QStringList{"skip", "discard", "draw", "give"}) {
			if (!player->isAlive()) break;
			QVariantMap saved = ctx.extra_data.toMap();
			if (action != "skip" && !saved.value("skipped").toBool()) break;
			saved.insert("action", action);
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, player);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		if (action == "skip") {
			target->skip(Player::Play);
			saved.insert("skipped", true);
			ctx.extra_data = saved;
		} else if (action == "draw") {
			const int count = saved.value("discarded").toInt() * getEffectiveAmount(ctx);
			if (count > 0) target->drawCards(count, objectName());
		} else if (action == "give") room->askForUseCard(target, "@@naxue", "naxue1:");
		else if (action == "discard") {
			const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
			const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
			if (!before.value("complete").toBool() || skillEvent <= 0) return false;
			const Card *cards = room->askForDiscard(target, objectName(), target->getCardCount(), 0, true, true, "naxue0:");
			if (!cards) return false;
			const QList<int> ids = cards->getSubcards();
			QSet<int> discarded;
			QVariantMap query{{"after", before.value("watermark")}, {"from", target->objectName()}};
			for (;;) {
				const QVariantMap page = room->queryHistoryMoves(query);
				if (!page.value("complete").toBool()) return false;
				for (const QVariant &entry : page.value("items").toList()) {
					const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
					const int id = move.value("card_id").toInt();
					if (ids.contains(id) && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
						&& room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == skillEvent) discarded.insert(id);
				}
				if (!page.value("has_more").toBool()) break;
				query.insert("after", page.value("next_after"));
				query.insert("watermark", page.value("watermark"));
			}
			// Replacement or interception cannot inflate the number of cards to draw.
			saved.insert("discarded", discarded.size());
			ctx.extra_data = saved;
		}
		return false;
	}
};

class Yijie : public TriggerSkillV2
{
public:
	Yijie() : TriggerSkillV2("yijie") { events << Death; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		return player && player->hasSkill(objectName()) && data.value<DeathStruct>().who == player
			&& !room->getOtherPlayers(player).isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = room->getOtherPlayers(player);
		if (ctx.targets.isEmpty()) return false;
		int total = 0;
		for (ServerPlayer *target : ctx.targets) total += target->getHp();
		// The average is frozen once; changing an earlier recipient cannot change later recipients.
		ctx.extra_data = qMax(1, total / static_cast<int>(ctx.targets.size()));
		return true;
	}
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		room->sendCompulsoryTriggerLog(player, this);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->setPlayerProperty(target, "hp", qMin(target->getMaxHp(), ctx.extra_data.toInt() * getEffectiveAmount(ctx)));
		return false;
	}
};

// Xietu's upgrade is shared by that separate skill. Success takes precedence
// over failure when independent Weiming missions have different outcomes.
static int xietuShimingOutcome(const Player *player)
{
    int outcome = 0;
    foreach (int id, player->getSkillInstanceIds("weiming")) {
        const int status = player->getSkillInstanceStateValue("weiming", id, "shiming_status", 0).toInt();
        if (status == 1) return 1;
        if (status == 2) outcome = 2;
    }
    return outcome;
}

XietuCard::XietuCard()
{
	mute = true;
}

bool XietuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void XietuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(xietuShimingOutcome(from)==1){
		QStringList choices;
		if(from->getMark("xietu1-PlayClear")<1)
			choices << "xietu1";
		if(from->getMark("xietu2-PlayClear")<1)
			choices << "xietu2";
		if(choices.isEmpty()) return;
		from->peiyin(getSkillName(),qsanRandomBounded(2)+1);
		if(room->askForChoice(from,getSkillName(),choices.join("+"),QVariant::fromValue(to))=="xietu1"){
			from->addMark("xietu1-PlayClear");
			room->recover(to,RecoverStruct(getSkillName(),from));
		}else{
			from->addMark("xietu2-PlayClear");
			to->drawCards(2,getSkillName());
		}
	}else if(from->getChangeSkillState(getSkillName())==1){
		room->setChangeSkillState(from,getSkillName(),2);
		if(xietuShimingOutcome(from)==2){
			from->peiyin(getSkillName(),qsanRandomBounded(2)+3);
			room->recover(from,RecoverStruct(getSkillName(),from));
			room->askForDiscard(to,getSkillName(),2,2,false,true);
		}else{
			from->peiyin(getSkillName(),qsanRandomBounded(2)+1);
			room->recover(to,RecoverStruct(getSkillName(),from));
		}
	}else{
		room->setChangeSkillState(from,getSkillName(),1);
		if(xietuShimingOutcome(from)==2){
			from->peiyin(getSkillName(),qsanRandomBounded(2)+3);
			from->drawCards(1,getSkillName());
			room->damage(DamageStruct(getSkillName(),from,to));
		}else{
			from->peiyin(getSkillName(),qsanRandomBounded(2)+1);
			to->drawCards(2,getSkillName());
		}
	}
}

class Xietu : public ZeroCardViewAsSkill
{
public:
	Xietu() : ZeroCardViewAsSkill("xietu")
	{
		change_skill = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		int m = 1;
		if(xietuShimingOutcome(player)==1)
			m = 2;
		return player->usedTimes("XietuCard") < m;
	}

	const Card *viewAs() const
	{
		return new XietuCard;
	}
};

class Weiming : public TriggerSkill
{
public:
    Weiming() : TriggerSkill("weiming")
    {
        events << Death << EventPhaseStart;
        shiming_skill = true;
    }

    bool triggerable(const ServerPlayer *target) const override { return target != nullptr; }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        QList<ServerPlayer *> owners;
        if (event == EventPhaseStart) {
            if (!player->isAlive() || player->getPhase() != Player::Play) return false;
            owners << player;
        } else {
            if (data.value<DeathStruct>().who != player) return false;
            owners = room->getOtherPlayers(player);
        }
        foreach (ServerPlayer *owner, owners) {
            const QList<int> ids = owner->getValidSkillInstanceIds(objectName());
            foreach (int id, ids) {
                const SkillInstanceRef ref(owner->objectName(), SkillInstanceKey(objectName(), id));
                if (!owner->hasSkillInstance(objectName(), id) || owner->isSkillInvalid(objectName(), id)
                    || room->getShimingStatus(ref) != 0) continue;
                QStringList targets = owner->getSkillInstanceStateValue(objectName(), id, "weiming_targets").toStringList();
                const QString displayMark = "&weiming+#" + owner->objectName() + "+" + QString::number(id) + "_num";
                if (event == EventPhaseStart) {
                    QList<ServerPlayer *> candidates;
                    foreach (ServerPlayer *p, room->getOtherPlayers(owner))
                        if (!targets.contains(p->objectName())) candidates << p;
                    if (candidates.isEmpty()) continue;
                    ServerPlayer *to = room->askForPlayerChosen(owner, candidates, objectName(), "weiming0", false, true);
                    if (!to || !owner->hasSkillInstance(objectName(), id) || room->getShimingStatus(ref) != 0) continue;
                    room->broadcastSkillInvoke(objectName(), owner, 2);
                    targets << to->objectName();
                    owner->setSkillInstanceStateValue(objectName(), id, "weiming_targets", targets);
                    room->setPlayerMark(to, displayMark, 1);
                } else {
                    DeathStruct death = data.value<DeathStruct>();
                    const bool failed = targets.contains(player->objectName());
                    if (!failed && (!death.damage || death.damage->from != owner)) continue;
                    if (!room->sendShimingLog(ref, !failed, failed ? 1 : 3)) continue;
                    owner->removeSkillInstanceStateValue(objectName(), id, "weiming_targets");
                    foreach (ServerPlayer *p, room->getAllPlayers(true))
                        room->setPlayerMark(p, displayMark, 0);
                    // Xietu is a separate skill: its player-wide upgrade is an explicit
                    // projection of all live Weiming outcomes, never mission storage.
                    const int outcome = xietuShimingOutcome(owner);
                    if (outcome == 1) {
                        const int n = owner->getChangeSkillState("xietu");
                        room->setPlayerMark(owner, QString("&xietu+%1_num").arg(n), 0);
                        room->changeTranslation(owner, "xietu", 3);
                    } else if (outcome == 2) {
                        room->changeTranslation(owner, "xietu1", Sanguosha->translate(":xietu4"));
                        room->changeTranslation(owner, "xietu2", Sanguosha->translate(":xietu5"));
                        room->changeTranslation(owner, "xietu", owner->getChangeSkillState("xietu"));
                    }
                }
            }
        }
        return false;
    }
};

class Chengxiong : public TriggerSkillV2
{
public:
	Chengxiong() : TriggerSkillV2("chengxiong") { events << TargetSpecified; }
	static int usedCards(Room *room, ServerPlayer *owner)
	{
		const QVariantMap current = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		if (current.value("phase_id").toLongLong() <= 0) return -1;
		QVariantMap query{{"kind", "use_card"}, {"phase_id", current.value("phase_id")}, {"from", owner->objectName()}};
		int count = 0;
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap card = entry.toMap().value("data").toMap().value("card").toMap();
				if (!card.contains("type")) return -1;
				if (card.value("type").toInt() != Card::TypeSkill) ++count;
			}
			if (!page.value("has_more").toBool()) return count;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("TrickCard")) return {};
		bool other = false;
		for (ServerPlayer *target : use.to) if (target != player) other = true;
		if (!other) return {};
		const int count = usedCards(room, player);
		if (count < 0) { qWarning("Chengxiong: incomplete phase use history"); return {}; }
		for (ServerPlayer *target : room->getOtherPlayers(player))
			if (target->getCardCount() >= count && player->canDiscard(target, "he")) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const int count = usedCards(room, player);
		if (count < 0) return false;
		QList<ServerPlayer *> candidates;
		for (ServerPlayer *target : room->getOtherPlayers(player))
			if (target->getCardCount() >= count && player->canDiscard(target, "he")) candidates << target;
		if (candidates.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, candidates, objectName(), "chengxiong0:" + QString::number(count), true, true);
		if (!target) return false;
		const Card *card = ctx.original_data->value<CardUseStruct>().card;
		if (!card) return false;
		ctx.extra_data = static_cast<int>(card->getColor());
		ctx.targets = {target};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!player->isAlive() || !player->canDiscard(target, "he")) return false;
		const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
		if (id < 0 || room->getCardOwner(id) != target || !player->canDiscard(target, id)) return false;
		// Freeze the discarded card's color before leaving a filter skill's hand.
		const bool matching = static_cast<int>(Sanguosha->getCard(id)->getColor()) == ctx.extra_data.toInt();
		room->throwCard(id, objectName(), target, player);
		if (matching && player->isAlive() && target->isAlive())
			room->damage(DamageStruct(objectName(), player, target, getEffectiveAmount(ctx)));
		return false;
	}
};
class Wangzhuan : public TriggerSkillV2
{
public:
	Wangzhuan() : TriggerSkillV2("wangzhuan") { events << Damaged << EventPhaseChanging; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
		const QVariant turn = room->historyScopes().value("turn_id");
		QVariantList keep;
		int remove = 0;
		for (const QVariant &entry : player->getTag("mobile_wangzhuan_invalidity").toList()) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("turn") == turn) remove += receipt.value("delta").toInt();
			else keep << entry;
		}
		// Retire before MarkChange callbacks; only this skill's actually committed contribution expires.
		player->setTag("mobile_wangzhuan_invalidity", keep);
		if (remove > 0) room->removePlayerMark(player, "@skill_invalidity", remove);
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		if (event != Damaged) return result;
		const DamageStruct damage = data.value<DamageStruct>();
		if (damage.card) return result;
		if (player && player->isAlive() && player->hasSkill(objectName())) result[player] << objectName();
		if (damage.from && damage.from != player && damage.from->isAlive() && damage.from->hasSkill(objectName())) result[damage.from] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		return player->askForSkillInvoke(this, *ctx.original_data);
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.extra_data = QStringLiteral("draw");
		skillEffect(event, room, player, ctx, player);
		ServerPlayer *current = room->getCurrent();
		if (current && current->isAlive() && current->getPhase() != Player::NotActive) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			ctx.extra_data = QStringLiteral("invalidate");
			skillEffect(event, room, player, ctx, current);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.extra_data.toString() == "draw") { target->drawCards(2 * getEffectiveAmount(ctx), objectName()); return false; }
		const QVariant turn = room->historyScopes().value("turn_id");
		QVariantList receipts = target->getTag("mobile_wangzhuan_invalidity").toList();
		for (const QVariant &entry : receipts) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("turn") == turn && receipt.value("owner").toString() == ctx.sourceRef.ownerObjectName
				&& receipt.value("skill").toString() == ctx.sourceRef.key.skillName
				&& receipt.value("instance").toInt() == ctx.sourceRef.key.instanceID) return false;
		}
		const quint64 serial = room->getTag("mobile_wangzhuan_serial").toULongLong() + 1;
		room->setTag("mobile_wangzhuan_serial", serial);
		QVariantMap receipt{{"serial", serial}, {"turn", turn}, {"owner", ctx.sourceRef.ownerObjectName},
			{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
			{"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
			{"activation_instance", ctx.activationRef.key.instanceID}, {"delta", 0}};
		receipts << receipt;
		target->setTag("mobile_wangzhuan_invalidity", receipts);
		room->setPlayerMarkWithReceipt(target, "@skill_invalidity", target->getMark("@skill_invalidity") + 1,
			[target, receipt](const QString &mark, int before, int after) {
				return mark == "@skill_invalidity" && after > before && target->isAlive()
					&& target->getTag("mobile_wangzhuan_invalidity").toList().contains(receipt);
			},
			[target, receipt](const QString &, int before, int after) {
				QVariantList current = target->getTag("mobile_wangzhuan_invalidity").toList();
				const int index = current.indexOf(receipt);
				if (index < 0) return;
				QVariantMap committed = receipt;
				committed.insert("delta", after - before);
				current[index] = committed;
				target->setTag("mobile_wangzhuan_invalidity", current);
			});
		// A redirected, cancelled, or already expired MarkChange grants no durable receipt.
		receipts = target->getTag("mobile_wangzhuan_invalidity").toList();
		receipts.removeOne(receipt);
		target->setTag("mobile_wangzhuan_invalidity", receipts);
		return false;
	}
};





class Cuizhen : public TriggerSkillV2
{
public:
	Cuizhen() : TriggerSkillV2("cuizhen") { events << TargetSpecified << GameStart << DrawNCards; }
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != TargetSpecified) return false;
		if (!player || !player->isAlive() || player->getPhase() != Player::Play) return true;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || (!use.card->isKindOf("Slash") && !(use.card->isKindOf("TrickCard") && use.card->isDamageCard()))) return true;
		for (int id : player->getValidSkillInstanceIds(objectName())) {
			const SkillInstanceRef activation(player->objectName(), SkillInstanceKey(objectName(), id));
			for (ServerPlayer *target : use.to) {
				if (target == player || !target->isAlive() || !target->hasEquipArea(0) || target->getHandcardNum() < target->getHp()) continue;
				SkillContext ctx;
				ctx.skill_name = objectName();
				ctx.owner = ctx.invoker = ctx.initiator = player;
				ctx.activationRef = activation;
				ctx.sourceRef = room->resolveSkillInstanceRootRef(activation);
				if (!ctx.sourceRef.isValid()) continue;
				ctx.instanceID = id;
				ctx.amount = room->getSkillInstanceAmount(activation);
				ctx.original_data = &data;
				ctx.current_event = event;
				ctx.preferredTarget = target;
				ctx.preferredTargetSeat = target->getSeat();
				ctx.targets = {target};
				contexts << ctx;
			}
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == GameStart) return {{player, {objectName()}}};
		if (event != DrawNCards || data.value<DrawStruct>().reason != "draw_phase") return {};
		for (ServerPlayer *target : room->getAlivePlayers()) if (!target->hasEquipArea(0)) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == TargetSpecified) return ctx.preferredTarget && player->askForSkillInvoke(this, ctx.preferredTarget);
		if (event == GameStart) {
			QList<ServerPlayer *> candidates;
			for (ServerPlayer *target : room->getOtherPlayers(player)) if (target->hasEquipArea(0)) candidates << target;
			ctx.targets = room->askForPlayersChosen(player, candidates, objectName(), 0, 2, "cuizhen0:", true);
			return !ctx.targets.isEmpty();
		}
		ctx.targets = {player};
		ctx.is_forced = true;
		return true;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event != DrawNCards) { if (target->hasEquipArea(0)) target->throwEquipArea(0); return false; }
		int count = 0;
		for (ServerPlayer *candidate : room->getAlivePlayers()) if (!candidate->hasEquipArea(0)) ++count;
		DrawStruct draw = ctx.original_data->value<DrawStruct>();
		draw.num += qMin(2, count) * getEffectiveAmount(ctx);
		ctx.original_data->setValue(draw);
		return false;
	}
};

class Kuili : public TriggerSkillV2
{
public:
	Kuili() : TriggerSkillV2("kuili") { events << Damaged; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && damage.from && damage.from->isAlive()
			&& !damage.from->hasEquipArea(0) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		ctx.targets = {ctx.original_data->value<DamageStruct>().from};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
	{
		if (!target->hasEquipArea(0)) target->obtainEquipArea(0);
		return false;
	}
};

ZuoyouCard::ZuoyouCard()
{
}

bool ZuoyouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(targets.length()>0) return false;
	if(Self->getChangeSkillState(getSkillName())==2&&!to_select->canDiscard(to_select,"h"))
		return false;
	return true;
}

void ZuoyouCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(from->getChangeSkillState(getSkillName())==1){
		room->setChangeSkillState(from,getSkillName(),2);
		to->drawCards(3,getSkillName());
		room->askForDiscard(to,getSkillName(),2,2);
	}else{
		room->setChangeSkillState(from,getSkillName(),1);
		room->askForDiscard(to,getSkillName(),1,1);
		to->gainHujia(1,5);
	}
}

class Zuoyou : public ZeroCardViewAsSkill
{
public:
	Zuoyou() : ZeroCardViewAsSkill("zuoyou")
	{
		change_skill = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZuoyouCard") < 1;
	}

	const Card *viewAs() const
	{
		return new ZuoyouCard;
	}
};

class ShishouLJ : public TriggerSkill
{
public:
	ShishouLJ() : TriggerSkill("shishoulj")
	{
		events << CardFinished;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard")&&use.card->getSkillNames().contains("zuoyou")){
				foreach (ServerPlayer *p, use.to){
					if(p!=player){
						room->sendCompulsoryTriggerLog(player,this);
						if(player->getChangeSkillState("zuoyou")==1){
							player->drawCards(3,"zuoyou");
							room->askForDiscard(player,"zuoyou",2,2);
						}else if(player->canDiscard(player,"h")){
							room->askForDiscard(player,"zuoyou",1,1);
							player->gainHujia(1,5);
						}
					}
				}
			}
		}
		return false;
	}
};

class MobileQianlong : public TriggerSkill
{
public:
	MobileQianlong() : TriggerSkill("mobile_qianlong")
	{
		events << Damage << GameStart << Damaged << CardsMoveOneTime << MarkChanged;
		waked_skills = "qlqingzheng,qljiushi,qlfangzhu,qljuejin";
		setProperty("IgnoreInvalidity",true);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->hasSkill(objectName(),true);
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 0; i < damage.damage; i++) {
				if(player->getMark("&daoxin")>=99) continue;
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),15));
			}
		}else if(event==GameStart){
			room->sendCompulsoryTriggerLog(player,this);
			player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),20));
		}else if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 0; i < damage.damage; i++) {
				if(player->getMark("&daoxin")>=99) continue;
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),10));
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.reason.m_skillName!="InitialHandCards"&&move.to_place==Player::PlaceHand&&move.to==player&&player->getMark("&daoxin")<99){
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),5));
			}
		}else if(event==MarkChanged){
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&daoxin"){
				int n = player->getMark("&daoxin");
				if(n>=99){
					room->acquireSkill(player,"qljuejin");
				}
				if(n>=75){
					room->acquireSkill(player,"qlfangzhu");
				}
				if(n>=50){
					room->acquireSkill(player,"qljiushi");
				}
				if(n>=25){
					room->acquireSkill(player,"qlqingzheng");
				}
			}
		}
		return false;
	}
};

QlQingzhengCard::QlQingzhengCard()
{
}

bool QlQingzhengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self&&Self->canDiscard(to_select,"h");
}

void QlQingzhengCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	int id = room->doGongxin(from,to,to->handCards(),"qlqingzheng");
	DummyCard *dummy = new DummyCard();
	if(id>-1){
		foreach (const Card *h, to->getHandcards()){
			if(Sanguosha->getCard(id)->getSuit()==h->getSuit()&&from->canDiscard(to,h->getId()))
				dummy->addSubcard(h);
		}
	}
	dummy->deleteLater();
	if(dummy->subcardsLength()>0)
		room->throwCard(dummy,"qlqingzheng",to,from);
	if(subcardsLength()>dummy->subcardsLength())
		room->damage(DamageStruct("qlqingzheng",from,to));
}

class QlQingzhengVs : public ViewAsSkill
{
public:
	QlQingzhengVs() : ViewAsSkill("qlqingzheng")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *card) const
	{
		return selected.isEmpty()&&!Self->isJilei(card)&&!card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		QlQingzhengCard *sc = new QlQingzhengCard;
		foreach (const Card *c, cards){
			foreach (const Card *h, Self->getHandcards()){
				if(c->getSuit()==h->getSuit()&&!Self->isJilei(h))
					sc->addSubcard(h);
			}
		}
		return sc;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@qlqingzheng";
	}
};

class QlQingzheng : public TriggerSkill
{
public:
	QlQingzheng() : TriggerSkill("qlqingzheng")
	{
		events << EventPhaseStart;
		view_as_skill = new QlQingzhengVs;
		setProperty("IgnoreInvalidity",true);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->hasSkill(objectName(),true);
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(player->getPhase()==Player::Play&&player->canDiscard(player,"h")){
			room->askForUseCard(player,"@@qlqingzheng","qlqingzheng0:",-1,Card::MethodDiscard);
		}
		return false;
	}
};

class QlJiushiVS : public ZeroCardViewAsSkill
{
public:
	QlJiushiVS() : ZeroCardViewAsSkill("qljiushi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Analeptic::IsAvailable(player) && player->faceUp();
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern.contains("analeptic") && player->faceUp();
	}

	const Card *viewAs() const
	{
		Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
		analeptic->setSkillName(objectName());
		return analeptic;
	}
};

class QlJiushi : public TriggerSkill
{
public:
	QlJiushi() : TriggerSkill("qljiushi")
	{
		events << PreCardUsed << DamageDone << Damaged << TurnedOver;
		view_as_skill = new QlJiushiVS;
		setProperty("IgnoreInvalidity",true);
	}
	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->hasSkill(objectName(),true);
	}
	void getTrick(ServerPlayer *player) const
	{
		Room *room = player->getRoom();
		QList<int> tricks;
		foreach (int id, room->getDrawPile()) {
			if (Sanguosha->getCard(id)->isKindOf("TrickCard"))
				tricks << id;
		}
		if (tricks.isEmpty()) return;
		int id = tricks.at(qsanRandomBounded(tricks.length()));
		room->obtainCard(player, id, true);
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains(objectName()))
				player->turnOver();
		} else if (triggerEvent == TurnedOver) {
			room->sendCompulsoryTriggerLog(player, this);
			getTrick(player);
		} else if (triggerEvent == Damaged) {
			bool facedown = player->getTag("qljiushiFace").toBool();
			player->removeTag("qljiushiFace");
			if (facedown && !player->faceUp() && player->askForSkillInvoke(this, data)) {
				room->broadcastSkillInvoke(objectName());
				player->turnOver();
				//getTrick(player);
			}
		} else if (triggerEvent == DamageDone)
			player->setTag("qljiushiFace", !player->faceUp());
		return false;
	}
};

QlFangzhuCard::QlFangzhuCard()
{
}

bool QlFangzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void QlFangzhuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(room->askForChoice(from,getSkillName(),"qlfangzhu1+qlfangzhu2",QVariant::fromValue(to))=="qlfangzhu1"){
		room->setPlayerMark(to,"&qlfangzhu-SelfClear",1);
	}else{
		room->setPlayerCardLimitation(to,"use","^TrickCard|.|.|hand",true);
	}
}

class QlFangzhu : public ZeroCardViewAsSkill
{
public:
	QlFangzhu() : ZeroCardViewAsSkill("qlfangzhu")
	{
		setProperty("IgnoreInvalidity",true);
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("QlFangzhuCard") < 1;
	}

	const Card *viewAs() const
	{
		return new QlFangzhuCard;
	}
};

class QlFangzhuBf : public InvaliditySkill
{
public:
	QlFangzhuBf() : InvaliditySkill("#qlfangzhu_inv")
	{
	}

	bool isSkillValid(const Player *player, const Skill *) const
	{
		if(player->getMark("&mobilemoufangzhu+6-SelfClear")>0) return false;
		return player->getMark("&qlfangzhu-SelfClear")<1;
	}
};

class Weitong : public TriggerSkill
{
public:
	Weitong() : TriggerSkill("weitong$")
	{
		events << MarkChange << GameOverJudge;
		setProperty("IgnoreInvalidity",true);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==GameOverJudge){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who==player&&player->getGeneralName().startsWith("mobile_caomao")){
				room->doLightbox("image=image/animate/mobile_caomao_death.png",4444);
			}
		}else {
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&daoxin"&&mark.gain==20&&player->hasLordSkill(this)
				&&player->hasSkill("mobile_qianlong",true)&&room->getLieges("wei",player).length()>0){
				room->sendCompulsoryTriggerLog(player,this);
				mark.count = qMin(99-player->getMark("&daoxin"),60);
				data.setValue(mark);
			}
		}
		return false;
	}
};

QlJuejinCard::QlJuejinCard()
{
	target_fixed = true;
}

void QlJuejinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->doAnimate(1,source->objectName(),p->objectName());
	}
	QString gn = source->getGeneralName();
	if(gn.endsWith("caomao")){
		gn = "mobile_caomao2";
		source->setAvatarIcon(gn);
	}
	room->changeBGM("caomaoBGM",true);
	room->doSuperLightbox(gn, "qljuejin");
	room->removePlayerMark(source, "@qljuejin");
	foreach (ServerPlayer *p, room->getAllPlayers()){
		int n = p->getHp()-1;
		if(n>0){
			room->loseHp(p,n,true,source,"qljuejin");
			p->gainHujia(p==source?n+2:n,5);
		}
	}
	room->getThread()->delay();
	room->doLightbox("xiangsicunwei", 2000, 100);
	QList<CardsMoveStruct> moves;
	foreach (int id, Sanguosha->getRandomCards()){
		const Card *c = Sanguosha->getCard(id);
		if(c->isKindOf("Jink")||c->isKindOf("Peach")||c->isKindOf("Analeptic")){
			if(room->getCardPlace(id)!=Player::PlaceSpecial&&room->getCardPlace(id)!=Player::PlaceTable){
				CardsMoveStruct move = CardsMoveStruct(id,nullptr,Player::PlaceTable,CardMoveReason(CardMoveReason::S_MASK_BASIC_REASON, source->objectName()));
				move.reason.m_skillName = "xiangsicunwei";
				moves << move;
			}
		}
	}
	room->setTag("xiangsicunwei",true);
	room->moveCardsAtomic(moves,true);
}

class QlJuejinVs : public ZeroCardViewAsSkill
{
public:
	QlJuejinVs() : ZeroCardViewAsSkill("qljuejin")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@qljuejin")>0;
	}

	const Card *viewAs() const
	{
		return new QlJuejinCard;
	}
};

class QlJuejin : public TriggerSkill
{
public:
	QlJuejin() : TriggerSkill("qljuejin")
	{
		events << CardsMoveOneTime;
		view_as_skill = new QlJuejinVs;
		frequency = Limited;
		limit_mark = "@qljuejin";
		setProperty("IgnoreInvalidity",true);
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place!=Player::PlaceSpecial&&move.to_place!=Player::PlaceTable&&room->getTag("xiangsicunwei").toBool()){
				QList<CardsMoveStruct> moves;
				foreach (int id, move.card_ids){
					const Card *c = Sanguosha->getCard(id);
					if(c->isKindOf("Jink")||c->isKindOf("Peach")||c->isKindOf("Analeptic")){
						if(room->getCardPlace(id)!=Player::PlaceSpecial&&room->getCardPlace(id)!=Player::PlaceTable){
							CardsMoveStruct move1 = CardsMoveStruct(id,nullptr,Player::PlaceTable,CardMoveReason(CardMoveReason::S_MASK_BASIC_REASON, ""));
							move1.reason.m_skillName = "xiangsicunwei";
							moves << move1;
						}
					}
				}
				room->moveCardsAtomic(moves,true);
			}
		}
		return false;
	}
};

class Kuangli : public TriggerSkill
{
public:
	Kuangli() : TriggerSkill("kuangli")
	{
		events << EventPhaseStart << TargetSpecified;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(player->getPhase()!=Player::Play)
			return false;
		if(event==EventPhaseStart){
			room->sendCompulsoryTriggerLog(player,this);
			QList<ServerPlayer *>aps = room->getOtherPlayers(player);
			qsanShuffle(aps);
			int n = qsanRandomBounded(aps.length());
			for (int i = 0; i < n; i++) {
				room->doAnimate(1,player->objectName(),aps[i]->objectName());
				room->setPlayerMark(aps[i],"&kuangli-Clear",1);
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to){
					if(p!=player&&p->getMark("&kuangli-Clear")>0&&player->isAlive()&&player->getMark("kuangliUse-PlayClear")<2){
						room->sendCompulsoryTriggerLog(player,this);
						player->addMark("kuangliUse-PlayClear");
						QList<const Card *>cs = player->getCards("he");
						qsanShuffle(cs);
						foreach (const Card *c, cs){
							if(player->canDiscard(player,c->getId())){
								room->throwCard(c,objectName(),player);
								break;
							}
						}
						cs = p->getCards("he");
						qsanShuffle(cs);
						foreach (const Card *c, cs){
							if(p->canDiscard(p,c->getId())){
								room->throwCard(c,objectName(),p);
								break;
							}
						}
						if(player->isAlive())
							player->drawCards(1,objectName());
					}
				}
			}
		}
		return false;
	}
};

XiongshiCard::XiongshiCard()
{
	target_fixed = true;
}

void XiongshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->doAnimate(1,source->objectName(),p->objectName());
	}
	room->doSuperLightbox(source, "xiongshi");
	room->removePlayerMark(source, "@xiongshi");
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->loseHp(p,1,true,source,getSkillName());
	}
}

class Xiongshi : public ZeroCardViewAsSkill
{
public:
	Xiongshi() : ZeroCardViewAsSkill("xiongshi")
	{
		frequency = Limited;
		limit_mark = "@xiongshi";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@xiongshi")>0&&player->getHandcardNum()>2;
	}

	const Card *viewAs() const
	{
		XiongshiCard *sc = new XiongshiCard;
		sc->addSubcards(Self->getHandcards());
		return sc;
	}
};

class Pmobileanxiang : public TriggerSkill
{
public:
	Pmobileanxiang() : TriggerSkill("pmobileanxiang")
	{
		events << DamageInflicted;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->isAlive()&&p->hasSkill(objectName())){
					QString ban = player->getTag("pmobileanxiangChoice").toString();
					QStringList choices;
					choices << "pmobileanxiang1" << "pmobileanxiang2";
					if(!ban.isEmpty()) choices.removeOne(ban);
					ban = "info:"+player->objectName();
					if(choices.length()<2){
						if(choices.contains("pmobileanxiang1"))
							ban = "info1:"+player->objectName();
						else
							ban = "info2:"+player->objectName();
					}
					p->setTag("pmobileanxiangData", data);
					if(p->askForSkillInvoke(this,ban)){
						ban = room->askForChoice(p,objectName(),choices.join("+"),data);
						player->setTag("pmobileanxiangChoice", ban);
						if(ban.contains("1")){
							damage.damage--;
							if(damage.from&&damage.from->isAlive())
								damage.from->drawCards(2,objectName());
						}else{
							damage.damage++;
							if(damage.to->isAlive())
								damage.to->drawCards(3,objectName());
						}
						data.setValue(damage);
					}
				}
			}
			return damage.damage<1;
		}
		return false;
	}
};

class NewChenjie : public TriggerSkill
{
public:
	NewChenjie() : TriggerSkill("newchenjie")
	{
		events << Death;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who->getTag("pmobileanxiangChoice").toString()!=""&&player->hasSkill("pmobileanxiang",true)){
			room->sendCompulsoryTriggerLog(player,this);
			player->throwAllCards(objectName());
			if(player->isAlive())
				player->drawCards(4,objectName());
		}
		return false;
	}
};

class Zhujin : public ViewAsSkillV2
{
public:
	Zhujin() : ViewAsSkillV2("zhujin", 1) { response_or_use = true; }
	LimitScope getLimitScope() const override { return Limit_Custom; }
	bool willThrowSelectedCards() const override { return false; }
	static QString usageKey(const SkillInstanceRef &ref, const QString &name)
	{
		return SkillInstanceUtils::formatUsageMarkKey(ref.key.skillName, ref.key.instanceID, "_" + name + "-Clear");
	}
	static const Player *usageHolder(const Player *player, const SkillInstanceRef &ref)
	{
		if (!player || !ref.isValid()) return nullptr;
		for (const Player *holder : player->getSiblings(true)) if (holder->objectName() == ref.ownerObjectName) return holder;
		return nullptr;
	}
	static bool allowed(const Player *player, const QString &name)
	{
		if (name == "jink" || name == "nullification") return player->isWounded();
		if (name != "slash") return false;
		if (!player->isWounded()) return true;
		for (const Player *other : player->getAliveSiblings()) if (other->getHp() < player->getHp()) return true;
		return false;
	}
	QString cardName(const ActiveSkillRequest &request) const
	{
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "slash") return "slash";
		if (request.pattern == "jink" || request.pattern == "nullification") return request.pattern;
		return QString();
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const QString name = cardName(request);
		const Player *holder = usageHolder(request.initiator, request.activationRef);
		if (!holder || !allowed(request.initiator, name) || holder->getMark(usageKey(request.activationRef, name)) > 0) return false;
		return request.reason != CardUseStruct::CARD_USE_REASON_PLAY || Slash::IsAvailable(request.initiator);
	}
	bool checkCustomUsage(const SkillContext &ctx) const override
	{
		const SkillInstanceRef ref = getUsageRef(ctx);
		const Player *holder = usageHolder(ctx.owner, ref);
		if (!holder) return false;
		if (ctx.use_card) return holder->getMark(usageKey(ref, ctx.use_card->objectName())) == 0;
		for (const QString &name : QStringList{"slash", "jink", "nullification"})
			if (allowed(ctx.owner, name) && holder->getMark(usageKey(ref, name)) == 0) return true;
		return false;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		if (!ctx.owner || !ctx.use_card) return;
		const SkillInstanceRef ref = getUsageRef(ctx);
		ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
		if (holder && ref.isValid()) ctx.owner->getRoom()->addPlayerMark(holder, usageKey(ref, ctx.use_card->objectName()));
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty() && card->isKindOf("BasicCard")
			&& (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getHandPile().contains(card->getEffectiveId()));
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.length() != 1) return false;
		ActiveSkillRequest selection = request;
		selection.selectedCardIds.clear();
		return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		const QString name = cardName(request);
		return name == "slash" ? "Slash" : name == "jink" ? "Jink" : "Nullification";
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request) || !canActivate(request)) return nullptr;
		Card *card = Sanguosha->cloneCard(cardName(request));
		if (!card) return nullptr;
		card->setSkillName(objectName());
		card->addSubcards(request.selectedCardIds);
		return card;
	}
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		if (!isUsable(ctx) || !cardSelectionFeasible(request) || !canActivate(request)) return false;
		// Only the quota commits here; the ordinary use/response pipeline pays its material.
		addUsage(ctx);
		return true;
	}
};

JiejianCard::JiejianCard()
{
}

static QHash<QString, int> jiejianNum;

bool JiejianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(to_select!=Self&&targets.length()<subcardsLength()){
		int n = 0;
		foreach (const Player *p, targets){
			n += jiejianNum[p->objectName()];
		}
		jiejianNum[to_select->objectName()] = subcardsLength()-n;
		return true;
	}
	return false;
}

bool JiejianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	int n = 0;
	foreach (const Player *p, targets){
		n += jiejianNum[p->objectName()];
	}
	return n == subcardsLength();
}

void JiejianCard::onUse(Room *room, CardUseStruct &use) const
{
	foreach (ServerPlayer *p, use.to){
		room->setPlayerMark(p,"&jiejianNum",jiejianNum[p->objectName()]);
	}
}

class JiejianVs : public ViewAsSkill
{
public:
	JiejianVs() : ViewAsSkill("jiejian")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *card) const
	{
		return !card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		JiejianCard *sc = new JiejianCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@jiejian";
	}
};

class Jiejian : public TriggerSkill
{
public:
	Jiejian() : TriggerSkill("jiejian")
	{
		events << EventPhaseStart << EventPhaseChanging << TargetConfirming;
		//view_as_skill = new JiejianVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive&&player->getMark("&jiejian")>0){
				player->loseMark("&jiejian");
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName(),true)){
						if(player->getMark("jiejianHp+#"+p->objectName())<=player->getHp()){
							room->sendCompulsoryTriggerLog(p,this);
							p->drawCards(2,objectName());
						}
					}
				}
			}
		}else if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.size()==1){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(player->getMark("&jiejian")>0&&p->hasSkill(objectName())&&p->askForSkillInvoke(this,data)){
						p->peiyin(this);
						use.to.removeOne(player);
						use.to.append(p);
						data.setValue(use);
						p->drawCards(1,objectName());
					}
				}
			}
		}else if(player->getPhase()==Player::Start&&player->getHandcardNum()>0&&player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
			player->peiyin(this);
			QList<int> ids = player->handCards();
			QList<ServerPlayer *> tos = player->assignmentCards(ids,"jiejian",room->getOtherPlayers(player),-1,1);
			foreach (ServerPlayer *p, tos){
				p->gainMark("&jiejian");
				room->setPlayerMark(p,"jiejianHp+#"+player->objectName(),p->getHp());
			}
		}
		return false;
	}
};

class Jueyong : public TriggerSkill
{
public:
	Jueyong() : TriggerSkill("jueyong")
	{
		events << EventPhaseStart << TargetConfirming;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isVirtualCard()&&use.to.size()==1){
				if(use.card->isKindOf("Peach")||use.card->isKindOf("Analeptic")||use.card->hasFlag("jueyongUse"))
					return false;
				QList<int>ids = player->getPile("jue_yong");
				if(ids.length()>=player->getHp()) return false;
				room->sendCompulsoryTriggerLog(player,this);
				player->setTag("jueyong"+use.card->toString(), data);
				player->addToPile("jue_yong",use.card);
				use.to.removeAll(player);
				data.setValue(use);
			}
		}else if(player->getPhase()==Player::Finish){
			QList<int>ids = player->getPile("jue_yong");
			if(ids.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player,this);
			foreach (int id, ids){
				CardUseStruct use = player->getTag("jueyong"+QString::number(id)).value<CardUseStruct>();
				if(use.from&&use.from->isAlive()){
					use.card->setFlags("jueyongUse");
					room->useCard(use);
				}else
					room->throwCard(id,objectName(),nullptr);
				if(player->isDead()) break;
			}
		}
		return false;
	}
};

PoxiangCard::PoxiangCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool PoxiangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void PoxiangCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	room->giveCard(from,to,this,getSkillName());
	if(from->isDead()) return;
	room->ignoreCards(from,room->drawCardsList(from,3,getSkillName()));
	DummyCard *dummy = new DummyCard();
	dummy->addSubcards(from->getPile("jue_yong"));
	if(dummy->subcardsLength()>0)
		room->throwCard(dummy,getSkillName(),nullptr);
	dummy->deleteLater();
	room->loseHp(from,1,true,from,getSkillName());
}

class Poxiang : public ViewAsSkill
{
public:
	Poxiang() : ViewAsSkill("poxiang")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *) const
	{
		return selected.isEmpty();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		PoxiangCard *sc = new PoxiangCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("PoxiangCard")<1&&player->getCardCount()>0;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@poxiang";
	}
};

ZhujianCard::ZhujianCard()
{
}

bool ZhujianCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *) const
{
	return to_select->hasEquip();
}

bool ZhujianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length()>1;
}

void ZhujianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const { ActiveSkillCard::use(room, source, targets); }

class Zhujian : public ViewAsSkillV2
{
public:
	Zhujian() : ViewAsSkillV2("zhujian") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &targets, const Player *target) const override
	{
		return target && target->isAlive() && target->hasEquip() && !targets.contains(target);
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() >= 2; }
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		ZhujianCard *card = new ZhujianCard;
		card->setActiveSkill(this); card->setSkillName(objectName());
		return card;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		// Equipment was a selection condition; a prior target's draw may move later targets' equipment.
		target->drawCards(getEffectiveAmount(ctx), objectName());
		return ContinueEffects;
	}
};

DuansuoCard::DuansuoCard()
{
}

bool DuansuoCard::targetFilter(const QList<const Player *> &, const Player *to, const Player *) const
{
	return to->isChained();
}

void DuansuoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const { ActiveSkillCard::use(room, source, targets); }

class Duansuo : public ViewAsSkillV2
{
public:
	Duansuo() : ViewAsSkillV2("duansuo") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &targets, const Player *target) const override
	{
		return target && target->isAlive() && target->isChained() && !targets.contains(target);
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		DuansuoCard *card = new DuansuoCard;
		card->setActiveSkill(this); card->setSkillName(objectName());
		return card;
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		const QList<ServerPlayer *> targets = ctx.targets;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		// Reset everyone before the first fire damage, while exposing each recipient and each effect separately.
		for (const QString &stage : QStringList{"reset", "damage"}) {
			ctx.choice = stage;
			for (ServerPlayer *target : targets) {
				ctx.modified_amount = amount; ctx.modified_amount_set = modified;
				if (target && target->isAlive()) skillEffect(ctx, target);
			}
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = ctx.owner->getRoom();
		if (ctx.choice == "reset") {
			if (target->isChained()) room->setPlayerChained(target);
		} else room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
		return ContinueEffects;
	}
};

class Chengye : public TriggerSkill
{
public:
	Chengye() : TriggerSkill("chengye")
	{
		events << EventPhaseStart << CardsMoveOneTime << CardFinished;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from&&move.to_place==Player::DiscardPile&&move.from!=player&&player->hasSkill(objectName())){
				int i = 0;
				foreach (int id, move.card_ids){
					if(move.from_places.at(i)==Player::PlaceHand
					||move.from_places.at(i)==Player::PlaceEquip
					||move.from_places.at(i)==Player::PlaceDelayedTrick){
						if(room->getCardPlace(id)==Player::DiscardPile){
							const Card *c = Sanguosha->getCard(id);
							if(c->isKindOf("EquipCard")){
								bool can = true;
								foreach (int d, player->getPile("cy_dian")){
									if(Sanguosha->getCard(d)->isKindOf("EquipCard"))
										can = false;
								}
								if(can){
									room->sendCompulsoryTriggerLog(player,this);
									player->addToPile("cy_dian",c);
								}
							}else if(c->isKindOf("DelayedTrick")){
								bool can = c->isKindOf("Indulgence");
								foreach (int d, player->getPile("cy_dian")){
									if(Sanguosha->getCard(d)->isKindOf("Indulgence"))
										can = false;
								}
								if(can){
									room->sendCompulsoryTriggerLog(player,this);
									player->addToPile("cy_dian",c);
								}
								can = c->isDamageCard();
								foreach (int d, player->getPile("cy_dian")){
									if(Sanguosha->getCard(d)->isDamageCard())
										can = false;
								}
								if(can){
									room->sendCompulsoryTriggerLog(player,this);
									player->addToPile("cy_dian",c);
								}
							}
						}
					}
					i++;
				}
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isVirtualCard()||room->getCardOwner(use.card->getEffectiveId())) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->isAlive()&&p->hasSkill(objectName())){
					bool can = use.card->isKindOf("TrickCard")&&use.card->isDamageCard();
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("TrickCard")&&Sanguosha->getCard(d)->isDamageCard())
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("BasicCard");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("BasicCard"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("Nullification");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("Nullification"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("ExNihilo");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("ExNihilo"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("Indulgence");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("Indulgence"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("EquipCard");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("EquipCard"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
				}
			}
		}else if(player->getPhase()==Player::Play&&player->hasSkill(objectName())){
			QList<int>ids = player->getPile("cy_dian");
			if(ids.length()<6) return false;
			room->sendCompulsoryTriggerLog(player,this);
			DummyCard *dummy = new DummyCard(ids);
			player->obtainCard(dummy);
			dummy->deleteLater();
		}
		return false;
	}
};

BuxuCard::BuxuCard()
{
	target_fixed = true;
}

void BuxuCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &) const
{
	QStringList choices;
	bool can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("TrickCard")&&Sanguosha->getCard(d)->isDamageCard())
			can = false;
	}
	if(can){
		choices << "d_shi";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("BasicCard"))
			can = false;
	}
	if(can){
		choices << "d_shu";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("Nullification"))
			can = false;
	}
	if(can){
		choices << "d_li";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("Indulgence"))
			can = false;
	}
	if(can){
		choices << "d_yue";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("ExNihilo"))
			can = false;
	}
	if(can){
		choices << "d_yi";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("EquipCard"))
			can = false;
	}
	if(can){
		choices << "d_chunqiu";
	}
	if(choices.isEmpty()||player->isDead()) return;
	QString choice = room->askForChoice(player,getSkillName(),choices.join("+"));
	foreach (int id, room->getDrawPile()+room->getDiscardPile()){
		const Card *c = Sanguosha->getCard(id);
		if(choice=="d_shi"&&c->isKindOf("TrickCard")&&c->isDamageCard()){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_shu"&&c->isKindOf("BasicCard")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_li"&&c->isKindOf("Nullification")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_yue"&&c->isKindOf("Indulgence")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_yi"&&c->isKindOf("ExNihilo")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_chunqiu"&&c->isKindOf("EquipCard")){
			player->addToPile("cy_dian",c);
			break;
		}
	}
}

class Buxu : public ViewAsSkill
{
public:
	Buxu() : ViewAsSkill("buxu")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *c) const
	{
		return selected.length()<=Self->usedTimes("BuxuCard")&&!Self->isJilei(c);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<=Self->usedTimes("BuxuCard")) return nullptr;
		BuxuCard *sc = new BuxuCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BuxuCard")<player->getCardCount()
			&&player->getPile("cy_dian").length()<6&&player->hasSkill("chengye",true);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@buxu";
	}
};

class Mingcha : public TriggerSkill
{
public:
	Mingcha() : TriggerSkill("mingcha")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(player->getPhase()==Player::Draw){
			room->sendCompulsoryTriggerLog(player,objectName());
			QList<int>ids = room->showDrawPile(player,3,objectName());
			DummyCard *dummy = new DummyCard();
			foreach (int id, ids){
				if(Sanguosha->getCard(id)->getNumber()<=8)
					dummy->addSubcard(id);
			}
			room->getThread()->delay();
			if(dummy->subcardsLength()>0&&player->askForSkillInvoke(this,dummy->subcardsLength())){
				foreach (int id, dummy->getSubcards())
					ids.removeOne(id);
				player->peiyin(this);
				player->obtainCard(dummy);
				if(player->isAlive()){
					ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"mingcha0:");
					if(to){
						room->doAnimate(1,player->objectName(),to->objectName());
						QList<int> toids = to->handCards()+to->getEquipsId();
						if(toids.length()>0){
							room->obtainCard(player,toids.at(qsanRandomBounded(toids.length())));
						}
					}
				}
			}
			dummy->deleteLater();
			dummy = new DummyCard(ids);
			room->throwCard(dummy,objectName(),nullptr);
			dummy->deleteLater();
			return ids.length()<3;
		}
		return false;
	}
};

class Jingzhong : public TriggerSkill
{
public:
	Jingzhong() : TriggerSkill("jingzhong")
	{
		events << CardsMoveOneTime << EventPhaseEnd << CardFinished;
		global = true;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player&&player->getPhase()==Player::Discard&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD) {
				QVariantList ids = player->getTag("jingzhongIds").toList();
				foreach (int card_id, move.card_ids)
					ids << card_id;
				player->setTag("jingzhongIds", ids);
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!player->hasFlag("CurrentPlayer")||player->getPhase()!=Player::Play
				||room->getCardPlace(use.card->getEffectiveId())!=Player::DiscardPile) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->getMark("&jingzhong+#"+p->objectName()+"-SelfClear")>0&&p->getMark("jingzhongOC-PlayClear")<3){
					p->addMark("jingzhongOC-PlayClear");
					p->obtainCard(use.card);
					break;
				}
			}
		} else if (triggerEvent == EventPhaseEnd&&player->getPhase()==Player::Discard) {
			QVariantList Vids = player->getTag("jingzhongIds").toList();
			player->removeTag("jingzhongIds");
			if (player->isDead()||!player->hasSkill(objectName()))
				return false;
			QVariantList ids;
			foreach (QVariant v, Vids) {
				if (!ids.contains(v)&&Sanguosha->getCard(v.toInt())->isBlack())
					ids << v;
			}
			if (ids.length()<2) return false;
			ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"jingzhong0:",true,true);
			if(to){
				room->broadcastSkillInvoke(objectName());
				room->setPlayerMark(to,"&jingzhong+#"+player->objectName()+"-SelfClear",1);
			}
		}
		return false;
	}
};

QuchongCard::QuchongCard()
{
	target_fixed = true;
	will_throw = false;
	can_recast = true;
	handling_method = Card::MethodRecast;
}

void QuchongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	LogMessage log;
	log.type = "#UseCard_Recast";
	log.from = source;
	log.card_str = QString::number(getSubcards().first());
	room->sendLog(log);
	room->moveCardTo(this, source, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, source->objectName(), getSkillName(), ""));
	source->drawCards(1, "recast");
}

class QuchongVs : public OneCardViewAsSkill
{
public:
	QuchongVs() : OneCardViewAsSkill("quchong")
	{
	}

	const Card *viewAs(const Card *c) const
	{
		QuchongCard *card = new QuchongCard;
		card->addSubcard(c);
		return card;
	}
};

class Quchong : public TriggerSkill
{
public:
	Quchong() : TriggerSkill("quchong")
	{
		events << EventPhaseStart << EventPhaseChanging;
		waked_skills = "_dagongche_jinji,_dagongche_shouyu";
		view_as_skill = new QuchongVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						DummyCard*dc = new DummyCard();
						foreach (int id, room->getDiscardPile()){
							if(Sanguosha->getCard(id)->isKindOf("EquipCard"))
								dc->addSubcard(id);
						}
						if(dc->subcardsLength()>0){
							room->sendCompulsoryTriggerLog(p,this);
							room->moveCardTo(dc,nullptr,Player::PlaceTable);
							p->gainMark("&zhuzhaoNum",dc->subcardsLength());
						}
						dc->deleteLater();
					}
				}
			}
		}else if(player->getPhase()==Player::Play&&player->hasSkill(objectName())){
			const Card *ec = nullptr;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				foreach (const Card *c, p->getCards("ej")){
					if(c->objectName().contains("dagongche"))
						ec = c;
				}
			}
			if(ec){
				ServerPlayer *to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"quchong0:",true,true);
				if(to){
					player->peiyin(this);
					room->giveCard(player,to,ec,objectName());
					if(to->hasCard(ec)&&ec->isAvailable(to))
						room->useCard(CardUseStruct(ec,to));
				}
			}else if(player->getMark("&zhuzhaoNum")>=player->getMark("&quchong")&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				int n = player->getMark("&quchong");
				player->loseMark("&zhuzhaoNum",n);
				n += 5;
				QStringList cht;
				QList<int>ids;
				room->setPlayerMark(player,"&quchong",qMin(n,10));
				foreach (int id, Sanguosha->getRandomCards(true)){
					ec = Sanguosha->getCard(id);
					if(ec->isKindOf("EquipCard")&&ec->objectName().contains("dagongche")&&room->getCardPlace(id)==Player::PlaceTable){
						if(!cht.contains(ec->objectName())){
							cht.append(ec->objectName());
							ids << id;
						}
					}
				}
				if(ids.isEmpty()) return false;
				room->fillAG(ids,player);
				n = room->askForAG(player,ids,ids.length()<2,objectName());
				if (n<0) n = ids.first();
				room->clearAG(player);
				cht.clear();
				cht << Sanguosha->getCard(n)->objectName();
				cht << Card::Suit2String(room->askForSuit(player,objectName()));
				cht << room->askForChoice(player,objectName(),"1+2+3+4+5+6+7+8+9+10+11+12+13");
				cht << "quchong";
				foreach (ServerPlayer *p, room->getPlayers())
					room->acquireSkill(p, "#zhizhe");
				n = -1;
				foreach (int id, Sanguosha->getRandomCards(true)){
					ec = Sanguosha->getCard(id);
					if(ec->isKindOf("EquipCard")&&ec->objectName().contains("_zhizhe_")&&room->getCardPlace(id)==Player::PlaceTable){
						n = id;
						break;
					}
				}
				if(n>-1){
					ServerPlayer *to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"quchong1:");
					if(to){
						room->setTag("ZhizheFilter_"+QString::number(n),cht.join("+"));
						room->setTag("dagongche"+QString::number(n),player->objectName());
						room->doAnimate(1,player->objectName(),to->objectName());
						room->giveCard(player,to,ec,objectName());
						ec = Sanguosha->getCard(n);
						if(to->hasCard(ec)&&ec->isAvailable(to))
							room->useCard(CardUseStruct(ec,to));
					}
				}
			}
		}
		return false;
	}
};

class Xunjie : public TriggerSkill
{
public:
	Xunjie() : TriggerSkill("xunjie")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->getHp()>player->getHp()){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					foreach (const Card *c, p->getCards("ej")){
						if(c->objectName().contains("dagongche")&&room->getTag("dagongche"+c->toString()).toString()==player->objectName())
							return false;
					}
				}
				room->sendCompulsoryTriggerLog(player,this);
				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".|spade";
				judge.good = false;
				room->judge(judge);
				if(judge.isGood())
					return player->damageRevises(data,-1);
			}
		}
		return false;
	}
};

class Bojian : public TriggerSkill
{
public:
	Bojian() : TriggerSkill("bojian")
	{
		events << EventPhaseEnd << CardUsed;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive()&&target->getPhase()==Player::Play;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseEnd){
			QStringList sus = player->getTag("bojianSuit").toStringList();
			int n = player->getMark("bojianUse-PlayClear");
			if(player->hasSkill(objectName())){
				if(player->getMark("bojianSuit")!=sus.size()&&n!=player->getMark("bojianUse")){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(2,objectName());
				}else{
					QList<int> ids,ids2 = ListV2I(player->getTag("bojianIds").toList());
					foreach (int id, room->getDiscardPile()){
						if(ids2.contains(id))
							ids << id;
					}
					if(ids.length()>0){
						room->sendCompulsoryTriggerLog(player,this);
						room->fillAG(ids,player);
						int id = room->askForAG(player,ids,ids.length()<2,objectName());
						if(id<0) id = ids.first();
						room->clearAG(player);
						ServerPlayer *to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"bojian0:");
						if(to){
							room->doAnimate(1,player->objectName(),to->objectName());
							room->giveCard(player,to,Sanguosha->getCard(id),objectName());
						}
					}
				}
				foreach (QString m, player->getMarkNames()){
					if(m.contains("&bojian+use+"))
						room->setPlayerMark(player,m,0);
				}
				room->setPlayerMark(player,"&bojian+use+"+QString::number(n)+"+suit+"+QString::number(sus.size()),1);
			}
			player->setMark("bojianUse",n);
			player->setMark("bojianSuit",sus.size());
			player->removeTag("bojianSuit");
			player->removeTag("bojianIds");
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				player->addMark("bojianUse-PlayClear");
				QStringList sus = player->getTag("bojianSuit").toStringList();
				if(!sus.contains(use.card->getSuitString())) sus << use.card->getSuitString();
				player->setTag("bojianSuit", sus);
				if(player->hasSkill(objectName(),true)){
					int n = player->getMark("bojianUse-PlayClear");
					foreach (QString m, player->getMarkNames()){
						if(m.contains("&bojian2+use+"))
							room->setPlayerMark(player,m,0);
					}
					room->setPlayerMark(player,"&bojian2+use+"+QString::number(n)+"+suit+"+QString::number(sus.size())+"-PlayClear",1);
				}
				QVariantList ids = player->getTag("bojianIds").toList();
				foreach (int id, use.card->getSubcards()){
					ids << id;
				}
				player->setTag("bojianIds", ids);
			}
		}
		return false;
	}
};

class Jiwei : public TriggerSkill
{
public:
	Jiwei() : TriggerSkill("jiwei")
	{
		events << EventPhaseStart << EventPhaseChanging << DamageDone << CardsMoveOneTime;
		frequency = Compulsory;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->isAlive()){
				int r = 0, b = player->getHandcardNum();
				if(b>=room->getAlivePlayers().length()&&b>=player->getHp()&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					b = 0;
					foreach (const Card *h, player->getHandcards()){
						if(h->isRed()) r++;
						else if(h->isBlack()) b++;
					}
					QString rb = "red+black";
					if(r>b) rb = "red";
					else if(r<b) rb = "black";
					else rb = room->askForChoice(player,objectName(),rb);
					QList<int>ids;
					foreach (const Card *h, player->getHandcards()){
						if(h->getColorString()==rb) ids << h->getId();
					}
					player->assignmentCards(ids,objectName(),room->getOtherPlayers(player),ids.length(),ids.length());
				}
			}
		}else if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				int n = 0;
				if(room->getTag("jiweiDamage").toBool()) n++;
				if(room->getTag("jiweiMove").toBool()) n++;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(n>0&&p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(n,objectName());
					}
				}
				room->setTag("jiweiDamage",false);
				room->setTag("jiweiMove",false);
			}
		}else if(event==DamageDone){
			room->setTag("jiweiDamage",true);
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player&&(move.to!=player||(move.to_place!=Player::PlaceHand&&move.to_place!=Player::PlaceEquip)))
				room->setTag("jiweiMove",true);
		}
		return false;
	}
};

class Biluan : public TriggerSkillV2
{
public:
	Biluan() : TriggerSkillV2("biluan") { events << EventPhaseStart; }
	static void apply(Room *room, const SkillContext &ctx, ServerPlayer *target, int delta)
	{
		QVariantList receipts = target->getTag("mobile_biluan_distance").toList();
		receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"delta", delta}};
		target->setTag("mobile_biluan_distance", receipts);
		int positive = 0, negative = 0;
		for (const QVariant &receipt : receipts) {
			const int amount = receipt.toMap().value("delta").toInt();
			positive += qMax(0, amount);
			negative += qMax(0, -amount);
		}
		room->setPlayerMark(target, "mobile_biluan_distance_positive", positive);
		room->setPlayerMark(target, "mobile_biluan_distance_negative", negative);
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw) return {};
		for (ServerPlayer *other : room->getOtherPlayers(player)) if (other->distanceTo(player) == 1) return {{player, {objectName()}}};
		return {};
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!player->askForSkillInvoke(this)) return false;
		ctx.targets = {player};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->broadcastSkillInvoke(objectName());
		// Retain each applied source while the distance expression uses the live kingdom count.
		apply(room, ctx, target, getEffectiveAmount(ctx));
		room->addPlayerMark(target, "biluanUse", getEffectiveAmount(ctx));
		return true;
	}
};

class BiluanDist : public DistanceSkillV2
{
public:
	BiluanDist() : DistanceSkillV2("#biluan-dist") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.secondary) return CorrectSkillResult::noEffect();
		QSet<QString> kingdoms;
		for (const Player *other : ctx.secondary->getAliveSiblings(true)) kingdoms << other->getKingdom();
		const int distance = ctx.secondary->getMark("mobile_biluan_distance_positive") * kingdoms.size() - ctx.secondary->getMark("mobile_biluan_distance_negative");
		return distance ? CorrectSkillResult::useAmount(distance * ctx.currentAmount) : CorrectSkillResult::noEffect();
	}
};

class Lixia : public TriggerSkillV2
{
public:
	Lixia() : TriggerSkillV2("lixia") { events << EventPhaseStart; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
		for (ServerPlayer *owner : room->getOtherPlayers(player))
			if (owner->hasSkill(objectName()) && !player->inMyAttackRange(owner)) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override { ctx.targets = {player}; return true; }
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		room->sendCompulsoryTriggerLog(ctx.owner, this);
		const int amount = getEffectiveAmount(ctx);
		target->drawCards(amount, objectName());
		Biluan::apply(room, ctx, target, -amount);
		room->addPlayerMark(target, "lixiaUse", amount);
		return false;
	}
};
MobileJiyuCard::MobileJiyuCard()
{
	target_fixed = true;
}

void MobileJiyuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	const Card*sc = Sanguosha->getCard(getEffectiveId());
	QList<int>ids = room->getDiscardPile();
	ids << room->getDrawPile();
	qsanShuffle(ids);
	QStringList types;
	types << sc->getType();
	Card*dc = new DummyCard;
	foreach (int id, ids){
		const Card *c = Sanguosha->getCard(id);
		if(types.contains(c->getType())) continue;
		types << c->getType();
		dc->addSubcard(id);
	}
	source->obtainCard(dc);
	dc->deleteLater();
	foreach (int id, dc->getSubcards()){
		if(source->handCards().contains(id))
			room->setCardTip(id,"mobilejiyu-PlayClear");
	}
}

class MobileJiyuVs : public OneCardViewAsSkill
{
public:
	MobileJiyuVs() : OneCardViewAsSkill("mobilejiyu")
	{
	}

	const Card *viewAs(const Card *c) const
	{
		MobileJiyuCard *card = new MobileJiyuCard;
		card->addSubcard(c);
		return card;
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileJiyuCard");
	}
};

class MobileJiyu : public TriggerSkill
{
public:
	MobileJiyu() : TriggerSkill("mobilejiyu")
	{
		events << PreCardUsed;
		view_as_skill = new MobileJiyuVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getPhase()==Player::Play;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasTip("mobilejiyu")){
				player->addMark("sgsmobilejiyuUse-PlayClear");
				if(player->getMark("sgsmobilejiyuUse-PlayClear")==2)
					room->addPlayerHistory(player,"MobileJiyuCard",-1);
			}
		}
		return false;
	}
};

class Guansha : public TriggerSkillV2
{
public:
	Guansha() : TriggerSkillV2("guansha")
	{
		events << EventPhaseEnd << EventSkillInvoking << EventPhaseChanging << TurnStart;
		frequency = Limited; limit_mark = "@guansha";
	}
	LimitScope getLimitScope() const override { return Limit_Game; }
	void addUsage(const SkillContext &ctx) const override
	{
		TriggerSkillV2::addUsage(ctx);
		const SkillInstanceRef ref = getUsageRef(ctx);
		ServerPlayer *holder = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
		if (holder) holder->getRoom()->removePlayerMark(holder, limit_mark);
	}
	void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	static int applied(Room *room, const Player *player)
	{
		int count = 0;
		for (const QVariant &entry : player->getTag("mobile_guansha_receipts").toList())
			if (entry.toMap().value("turn") == room->historyScopes().value("turn_id")) count += entry.toMap().value("amount").toInt();
		return count;
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
	{
		if (event == EventSkillInvoking) return false;
		for (ServerPlayer *player : room->getAllPlayers(true)) {
			QVariantList keep;
			for (const QVariant &entry : player->getTag("mobile_guansha_receipts").toList())
				if (room->historyEvent(entry.toMap().value("turn").toLongLong()).value("status").toString() != "finished") keep << entry;
			player->setTag("mobile_guansha_receipts", keep);
			room->setPlayerMark(player, "guansha_max", applied(room, player));
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseEnd && player && player->isAlive() && player->hasSkill(objectName())
			&& player->getPhase() == Player::Play && !player->isNude() ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
		ctx.targets = {ctx.owner}; ctx.manual_effect = true;
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.choice = "replace";
		skillEffect(event, room, ctx.owner, ctx, ctx.owner);
		if (ctx.owner->isAlive() && ctx.extra_data.toInt() > 0) {
			ctx.choice = "maxcards"; ctx.modified_amount = amount; ctx.modified_amount_set = modified;
			skillEffect(event, room, ctx.owner, ctx, ctx.owner);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.choice == "maxcards") {
			QVariantList receipts = target->getTag("mobile_guansha_receipts").toList();
			receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
				{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
				{"turn", room->historyScopes().value("turn_id")}, {"amount", ctx.extra_data.toInt() * getEffectiveAmount(ctx)}};
			target->setTag("mobile_guansha_receipts", receipts);
			room->setPlayerMark(target, "guansha_max", applied(room, target));
			return false;
		}
		const QList<int> original = target->handCards() + target->getEquipsId();
		if (original.isEmpty()) return false;
		room->doSuperLightbox(ctx.owner, objectName()); ctx.owner->peiyin(this);
		QList<int> pile = room->getDrawPile(), selected;
		qsanShuffle(pile);
		for (int id : pile) {
			if (!Sanguosha->getCard(id)->isKindOf("BasicCard")) continue;
			selected << id;
			if (selected.length() >= original.length()) break;
		}
		const QVariantMap before = room->queryHistoryMoves({});
		const qint64 skill = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
		room->moveCardsInToDrawpile(target, original, objectName());
		DummyCard cards;
		for (int id : selected) if (room->getCardPlace(id) == Player::DrawPile) cards.addSubcard(id);
		if (target->isAlive() && !cards.getSubcards().isEmpty()) room->obtainCard(target, &cards, false);
		if (!before.value("complete").toBool() || skill <= 0) return false;
		QVariantMap query{{"to", target->objectName()}, {"after", before.value("watermark")}};
		QSet<QString> names;
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) return false;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
				if (!selected.contains(move.value("card_id").toInt()) || move.value("to_place").toInt() != Player::PlaceHand
					|| room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() != skill) continue;
				const QString name = move.value("card").toMap().value("name").toString();
				if (name.isEmpty()) return false;
				names << name;
			}
			if (!page.value("has_more").toBool()) break;
			query["after"] = page.value("next_after"); query["watermark"] = page.value("watermark");
		}
		// Count actual obtained names, not the preselected cards that a move interceptor may have diverted.
		ctx.extra_data = names.size();
		return false;
	}
};

class GuanshaMax : public MaxCardsSkillV2
{
public:
	GuanshaMax() : MaxCardsSkillV2("#guansha-max") { setHolderSelector(CorrectSkill_System); }
	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.primary) return CorrectSkillResult::noEffect();
		const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(ctx.primary);
		const int amount = server ? Guansha::applied(server->getRoom(), server) : ctx.primary->getMark("guansha_max");
		return amount ? CorrectSkillResult::useAmount(amount) : CorrectSkillResult::noEffect();
	}
};

MZengouCard::MZengouCard()
{
}

bool MZengouCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self&&!to->isKongcheng()&&to->getMark(Self->objectName()+"mzengouBan")<1;
}

void MZengouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		room->doGongxin(source,p,p->handCards(),"mzengou");
		QList<int>ids = room->getAvailableCardList(source,"basic","mzengou");
		QStringList choices,cns;
		foreach (int id, ids) {
			const Card*c = Sanguosha->getCard(id);
			foreach (const Card*h, p->getHandcards()) {
				if(c->sameNameWith(h)){
					ids.removeOne(id);
					break;
				}
			}
			if(ids.contains(id))
				choices << "mzengou1="+c->objectName();
		}
		foreach (const Card*c, source->getHandcards()) {
			foreach (const Card*h, p->getHandcards()) {
				if(c->sameNameWith(h)){
					cns << c->objectName();
					break;
				}
			}
		}
		if(cns.length()>0) choices << "mzengou2";
		if(choices.isEmpty()) continue;
		QString choice = room->askForChoice(source,"mzengou",choices.join("+"));
		if(choice!="mzengou2"){
			choice = choice.split("=").last();
			room->setPlayerProperty(source,"mzengouUse",choice);
			room->askForUseCard(source,"@@mzengou!","mzengou0:"+choice,-1,Card::MethodUse,false);
		}else{
			QList<CardsMoveStruct> moves;
			CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, source->objectName(), "mzengou", "");
			foreach (const Card*c, source->getHandcards()) {
				if(cns.contains(c->objectName())){
					CardsMoveStruct move(c->getId(), source, nullptr, Player::PlaceHand, Player::DrawPile, reason);
					moves << move;
				}
			}
			ids.clear();
			int n = moves.length()*2;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("Slash")){
					CardsMoveStruct move(id, nullptr,source, Player::DrawPile, Player::PlaceHand, reason);
					moves << move;
					ids << id;
					if(moves.length()>=n) break;
				}
			}
			room->moveCardsAtomic(moves,false);
			foreach (int id, ids) {
				if(source->handCards().contains(id)){
					room->ignoreCards(source,id);
					room->setCardTip(id,"mzengou-SelfClear");
				}
			}
			if(p->isDead()) continue;
			moves.clear();
			reason.m_playerId = p->objectName();
			foreach (const Card*c, p->getHandcards()) {
				if(cns.contains(c->objectName())){
					CardsMoveStruct move(c->getId(), p, nullptr, Player::PlaceHand, Player::DrawPile, reason);
					moves << move;
				}
			}
			n = moves.length()*2;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("Slash")){
					CardsMoveStruct move(id, nullptr,p, Player::DrawPile, Player::PlaceHand, reason);
					moves << move;
					ids << id;
					if(moves.length()>=n) break;
				}
			}
			room->moveCardsAtomic(moves,false);
			foreach (int id, ids) {
				if(p->handCards().contains(id)){
					room->ignoreCards(p,id);
					room->setCardTip(id,"mzengou-SelfClear");
				}
			}
		}
		if(source->isDead()||p->isDead()) continue;
		cns = Sanguosha->getCardNames("BasicCard");
		choice = room->askForChoice(source,"mzengou",cns.join("+"));
		room->setPlayerMark(p,"&mzengou_wu+:+"+choice,1);
	}
}

class MZengouVs : public ZeroCardViewAsSkill
{
public:
	MZengouVs() : ZeroCardViewAsSkill("mzengou")
	{
		response_pattern = "@@mzengou!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MZengouCard")<1;
	}

	const Card *viewAs() const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@mzengou!"){
			Card*dc = Sanguosha->cloneCard(Self->property("mzengouUse").toString());
			dc->setSkillName("_mzengou");
			return dc;
		}
		return new MZengouCard;
	}
};

class MZengou : public TriggerSkill
{
public:
	MZengou() : TriggerSkill("mzengou")
	{
		events << CardFinished;
		view_as_skill = new MZengouVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("mzengou-Clear")<1){
				player->addMark("mzengou-Clear");
				if(player->getMark("&mzengou_wu+:+"+use.card->objectName())>0){
					room->sendCompulsoryTriggerLog(player,objectName());
					room->loseHp(player,1,true,player,objectName());
					room->setPlayerMark(player,"&mzengou_wu+:+"+use.card->objectName(),0);
				}
			}
		}
		return false;
	}
};

class Feili : public TriggerSkill
{
public:
	Feili() : TriggerSkill("feili")
	{
		events << DamageInflicted;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageInflicted&&player->hasSkill("mzengou",true)) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from) {
				foreach (QString m, damage.from->getMarkNames()) {
					if(m.contains("&mzengou_wu+:+")&&damage.from->getMark(m)>0){
						if(player->askForSkillInvoke(this,damage.from)){
							player->peiyin(this);
							room->setPlayerMark(damage.from,m,0);
							player->damageRevises(data,-damage.damage);
							player->drawCards(2,objectName());
							room->setPlayerMark(damage.from,player->objectName()+"mzengouBan",1);
							return true;
						}
						break;
					}
				}
			}
			if(player->getCardCount()>1&&player->canDiscard(player,"he")
				&&room->askForDiscard(player,objectName(),2,2,true,true,"feili0",".",objectName())){
				player->peiyin(this);
				return player->damageRevises(data,-damage.damage);
			}
		}
		return false;
	}
};

LvemingCard::LvemingCard() { }

bool LvemingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select && to_select != Self && to_select->isAlive() && to_select->getEquips().length() < Self->getEquips().length();
}

void LvemingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const { ActiveSkillCard::use(room, source, targets); }

class Lveming : public ViewAsSkillV2
{
public:
	Lveming() : ViewAsSkillV2("lveming") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->getEquips().isEmpty();
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
	{
		return request.initiator && targets.isEmpty() && target && target->isAlive() && target != request.initiator
			&& target->getEquips().length() < request.initiator->getEquips().length();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		LvemingCard *card = new LvemingCard;
		card->setActiveSkill(this);
		card->setSkillName(objectName());
		return card;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		ViewAsSkillV2::addUsage(ctx);
		if (ctx.owner) ctx.owner->getRoom()->addPlayerMark(ctx.owner, "&lveming");
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.targets.value(0);
		if (!target) return FinishSkill;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		const auto run = [&](const QString &action, ServerPlayer *recipient) {
			QVariantMap saved = ctx.extra_data.toMap();
			saved.insert("action", action);
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			if (recipient && recipient->isAlive()) skillEffect(ctx, recipient);
		};
		run("choose", target);
		if (!ctx.extra_data.toMap().contains("number")) return FinishSkill;
		run("judge", ctx.owner);
		if (!ctx.extra_data.toMap().contains("matched")) return FinishSkill;
		if (ctx.extra_data.toMap().value("matched").toBool()) run("damage", target);
		else {
			run("select", target);
			if (!ctx.extra_data.toMap().value("cards").toList().isEmpty()) run("obtain", ctx.owner);
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = ctx.owner->getRoom();
		QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		if (action == "choose") {
			QStringList numbers;
			for (int i = 1; i <= 13; ++i) numbers << QString::number(i);
			const QString choice = room->askForChoice(target, objectName(), numbers.join("+"));
			if (!numbers.contains(choice)) return ContinueEffects;
			LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = target;
			log.arg = choice;
			room->sendLog(log);
			saved.insert("number", choice.toInt());
		} else if (action == "judge") {
			JudgeStruct judge;
			judge.who = target;
			judge.reason = objectName();
			judge.pattern = ".|.|" + saved.value("number").toString();
			judge.good = true;
			judge.play_animation = false;
			room->judge(judge);
			// isGood freezes the verified judgment result; no shared FinishJudge pattern bridge.
			saved.insert("matched", judge.isGood());
		} else if (action == "damage") room->damage(DamageStruct(objectName(), ctx.owner, target, 2 * getEffectiveAmount(ctx)));
		else if (action == "select") {
			QList<int> available;
			for (const Card *card : target->getCards("hej")) if (ctx.owner->canGet(target, card->getEffectiveId())) available << card->getEffectiveId();
			QVariantList selected;
			for (int i = 0; i < getEffectiveAmount(ctx) && !available.isEmpty(); ++i) selected << available.takeAt(qsanRandomBounded(available.length()));
			saved.insert("cards", selected);
			saved.insert("from", target->objectName());
		} else if (action == "obtain") {
			ServerPlayer *from = room->findPlayerByObjectName(saved.value("from").toString());
			if (!from) return ContinueEffects;
			DummyCard cards;
			bool open = true;
			for (const QVariant &value : saved.value("cards").toList()) {
				const int id = value.toInt();
				if (room->getCardOwner(id) != from || !target->canGet(from, id)) continue;
				cards.addSubcard(id);
				if (room->getCardPlace(id) == Player::PlaceHand) open = false;
			}
			if (!cards.getSubcards().isEmpty()) {
				const CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), from->objectName(), objectName(), "");
				room->obtainCard(target, &cards, reason, open);
			}
		}
		ctx.extra_data = saved;
		return ContinueEffects;
	}
};

TunjunCard::TunjunCard() { }

bool TunjunCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *) const
{
	return targets.isEmpty() && target && target->isAlive();
}

void TunjunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const { ActiveSkillCard::use(room, source, targets); }

class Tunjun : public ViewAsSkillV2
{
public:
	Tunjun() : ViewAsSkillV2("tunjun") { frequency = Limited; limit_mark = "@tunjunMark"; }
	LimitScope getLimitScope() const override { return Limit_Game; }
	static int invokedCount(const Player *player)
	{
		const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(player);
		if (!server) return player ? player->getMark("&lveming") : -1;
		QVariantMap query{{"kind", "skill_invoked"}, {"player", player->objectName()}};
		int count = 0;
		for (;;) {
			const QVariantMap page = server->getRoom()->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList())
				if (entry.toMap().value("data").toMap().value("invoked_skill").toString() == "lveming") ++count;
			if (!page.value("has_more").toBool()) return count;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && invokedCount(request.initiator) > 0;
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &targets, const Player *target) const override
	{
		return target && target->isAlive() && targets.isEmpty();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	const Card *createCard(const ActiveSkillRequest &) const override
	{
		TunjunCard *card = new TunjunCard;
		card->setActiveSkill(this);
		card->setSkillName(objectName());
		return card;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		ViewAsSkillV2::addUsage(ctx);
		const SkillInstanceRef ref = getUsageRef(ctx);
		ServerPlayer *holder = ctx.owner ? ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true) : nullptr;
		if (holder) holder->getRoom()->removePlayerMark(holder, limit_mark);
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = ctx.owner->getRoom();
		const int count = invokedCount(ctx.owner);
		if (count < 0) { qWarning("Tunjun: incomplete Lveming invocation history"); return ContinueEffects; }
		room->doSuperLightbox(ctx.owner, objectName());
		QList<int> available, selected;
		for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf("EquipCard")) available << id;
		for (int i = 0; i < count * getEffectiveAmount(ctx) && !available.isEmpty(); ++i) {
			const int id = available.at(qsanRandomBounded(available.length()));
			selected << id;
			const int location = static_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard())->location();
			for (int j = available.length() - 1; j >= 0; --j)
				if (static_cast<const EquipCard *>(Sanguosha->getCard(available[j])->getRealCard())->location() == location) available.removeAt(j);
		}
		for (int id : selected) {
			if (!target->isAlive()) break;
			const Card *card = Sanguosha->getCard(id);
			if (room->getCardPlace(id) != Player::DrawPile || !card->isAvailable(target) || target->isProhibited(target, card)) continue;
			// Engine-owned physical equipment keeps its material ID and the accepted grant's provenance.
			room->useCardFromSkillEffect(CardUseStruct(card, target), ctx, true);
		}
		return ContinueEffects;
	}
};

ZhuguoCard::ZhuguoCard()
{
}

bool ZhuguoCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void ZhuguoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		int n = p->getMaxHp()-p->getHandcardNum();
		if(n>0){
			p->drawCards(n,"zhuguo");
		}else{
			if(n<0) room->askForDiscard(p,"zhuguo",-n,-n);
			room->recover(p,RecoverStruct("zhuguo",source));
		}
		if(p->isAlive()){
			foreach (ServerPlayer *q, room->getAlivePlayers()) {
				if(q->getHandcardNum()>p->getHandcardNum()) n = 999;
			}
			if(n<999){
				ServerPlayer *tp = room->askForPlayerChosen(source,room->getOtherPlayers(p),"zhuguo","zhuguo0:"+p->objectName(),true);
				if(tp){
					room->askForUseSlashTo(p,tp,"zhuguo1:"+tp->objectName(),false);
				}
			}
		}
	}
}

class Zhuguo : public ZeroCardViewAsSkill
{
public:
	Zhuguo() : ZeroCardViewAsSkill("zhuguo")
	{
		response_pattern = "@@zhuguo!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZhuguoCard")<1;
	}

	const Card *viewAs() const
	{
		return new ZhuguoCard;
	}
};

class AndaVs : public ViewAsSkill
{
public:
	AndaVs() : ViewAsSkill("anda")
	{
		response_pattern = "@@anda";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *c) const
	{
		return selected.isEmpty()||(selected.length()<2&&selected[0]->getColor()!=c->getColor());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<2) return nullptr;
		DummyCard *sc = new DummyCard;
		sc->addSubcards(cards);
		return sc;
	}
};

class Anda : public TriggerSkill
{
public:
	Anda() : TriggerSkill("anda")
	{
		events << Dying;
		view_as_skill = new AndaVs;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark("andaUse-Clear")<1&&player->hasTurn()) {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.damage&&dying.damage->from&&player->askForSkillInvoke(objectName()+"$-1",data)) {
				player->addMark("andaUse-Clear");
				if(dying.damage->from->getCardCount()>1){
					const Card*sc = room->askForUseCard(dying.damage->from,"@@anda","anda0:"+dying.who->objectName());
					if(sc){
						dying.who->obtainCard(sc,false);
						return false;
					}
				}
				room->recover(dying.who,RecoverStruct(objectName(),player));
			}
		}
		return false;
	}
};

MobileJianjiCard::MobileJianjiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool MobileJianjiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *) const
{
	return targets.isEmpty()||(targets.length()<2&&targets[0]->canPindian(to));
}

bool MobileJianjiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length()==2;
}

void MobileJianjiCard::onUse(Room *room, CardUseStruct &use) const
{
	room->setTag("MobileJianjiData",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void MobileJianjiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardUseStruct use = room->getTag("MobileJianjiData").value<CardUseStruct>();
	if(use.to.first()->canPindian(use.to.last())){
		PindianStruct *pd = use.to.first()->PinDian(use.to.last(),"mobilejianji");
		if(pd->from_number>pd->to_number){
			room->askForUseSlashTo(use.to.first(),use.to.last(),"mobilejianji1:"+use.to.last()->objectName(),false);
		}else if(pd->from_number<pd->to_number){
			room->askForUseSlashTo(use.to.last(),use.to.first(),"mobilejianji1:"+use.to.first()->objectName(),false);
		}
		if(Sanguosha->getCard(getEffectiveId())->isKindOf("Slash")){
			if(pd->from_card->getEffectiveId()==getEffectiveId())
				room->damage(DamageStruct("mobilejianji",source,pd->from));
			if(pd->to_card->getEffectiveId()==getEffectiveId())
				room->damage(DamageStruct("mobilejianji",source,pd->to));
		}
	}
}

class MobileJianjiVs : public OneCardViewAsSkill
{
public:
	MobileJianjiVs() : OneCardViewAsSkill("mobilejianji")
	{
	}

	const Card *viewAs(const Card *c) const
	{
		MobileJianjiCard *card = new MobileJianjiCard;
		card->addSubcard(c);
		return card;
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileJianjiCard");
	}
};

class MobileJianji : public TriggerSkill
{
public:
	MobileJianji() : TriggerSkill("mobilejianji")
	{
		events << AskforPindianCard;
		view_as_skill = new MobileJianjiVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if(event==AskforPindianCard){
			PindianStruct *pd = data.value<PindianStruct*>();
			if(pd->reason==objectName()){
				CardUseStruct use = room->getTag("MobileJianjiData").value<CardUseStruct>();
				if(pd->from->askForSkillInvoke(this,"mobilejianji0:"+use.from->objectName(),false))
					pd->from_card = use.card;
				if(pd->to->askForSkillInvoke(this,"mobilejianji0:"+use.from->objectName(),false))
					pd->to_card = use.card;
				data.setValue(pd);
			}
		}
		return false;
	}
};

class MobileYuanmo : public TriggerSkill
{
public:
	MobileYuanmo() : TriggerSkill("mobileyuanmo")
	{
		events << Damaged << EventPhaseStart;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(player->getMark("mobileyuanmoUse")>1||!player->hasTurn()) return false;
		if (triggerEvent==Damaged||player->getPhase()==Player::Start) {
			QList<ServerPlayer *>tps;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				foreach (const Card *c, p->getCards("ej")){
					foreach (ServerPlayer *q, room->getOtherPlayers(p)){
						if(p->isProhibited(q,c)) continue;
						if(c->isKindOf("EquipCard")){
							const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
							if(q->getEquip(equip->location())) continue;
						}
						tps << p;
						break;
					}
				}
			}
			ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"mobileyuanmo0",true,true);
			if(tp){
				player->peiyin(this);
				QList<int>ids;
				player->addMark("mobileyuanmoUse");
				foreach (const Card *c, tp->getCards("ej")){
					bool has = false;
					foreach (ServerPlayer *q, room->getOtherPlayers(tp)){
						if(tp->isProhibited(q,c)) continue;
						if(c->isKindOf("EquipCard")){
							const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
							if(q->getEquip(equip->location())) continue;
						}
						has = true;
						break;
					}
					if(!has)
						ids << c->getId();
				}
				int id = room->askForCardChosen(player,tp,"ej",objectName(),false,Card::MethodNone,ids);
				if(id<0) return false;
				tps.clear();
				const Card *c = Sanguosha->getCard(id);
				foreach (ServerPlayer *q, room->getOtherPlayers(tp)){
					if(tp->isProhibited(q,c)) continue;
					if(c->isKindOf("EquipCard")){
						const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
						if(q->getEquip(equip->location())) continue;
					}
					tps << q;
				}
				ServerPlayer *qp = room->askForPlayerChosen(player,tps,objectName(),"mobileyuanmo1:"+c->objectName());
				if(qp){
					room->doAnimate(1,player->objectName(),qp->objectName());
					int x = 0;
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(tp->inMyAttackRange(p)) x++;
					}
					room->moveCardTo(c,qp,room->getCardPlace(id),true);
					id = 0;
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(tp->inMyAttackRange(p)) id++;
					}
					x -= id;
					if(x>0&&tp->isAlive()&&player->isAlive()
						&&player->askForSkillInvoke(this,"mobileyuanmo2:"+tp->objectName(),false)){
						tp->drawCards(qMin(x,5),objectName());
					}
				}
			}
		}
		return false;
	}
};

class Xuye : public TriggerSkillV2
{
public:
	Xuye() : TriggerSkillV2("xuye") { events << Damaged; m_baseAmount = 2; }
	static QString damageKey(Room *room)
	{
		return room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toString();
	}
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive()) return true;
		const QString key = damageKey(room);
		if (key.toLongLong() <= 0) return true;
		QVariantMap snapshots = room->getTag("mobile_xuye_candidates").toMap();
		for (auto it = snapshots.begin(); it != snapshots.end();) {
			if (room->historyEvent(it.key().toLongLong()).value("status").toString() == "finished") it = snapshots.erase(it);
			else ++it;
		}
		bool least = true;
		for (ServerPlayer *other : room->getAlivePlayers()) if (other->getHandcardNum() < player->getHandcardNum()) least = false;
		// Freeze this timing's condition: the first holder's draw must not erase other holders' candidates.
		snapshots[key] = QVariantMap{{"player", player->objectName()}, {"least", least}};
		room->setTag("mobile_xuye_candidates", snapshots);
		return true;
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive()) return {};
		const QVariantMap snapshot = room->getTag("mobile_xuye_candidates").toMap().value(damageKey(room)).toMap();
		if (snapshot.value("player").toString() != player->objectName() || !snapshot.value("least").toBool()) return {};
		TriggerList result;
		for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.invoker || !ctx.invoker->isAlive() || !ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
		ctx.targets = {ctx.invoker}; ctx.manual_effect = true;
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *target = ctx.targets.value(0);
		if (!target) return false;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.choice = "draw";
		skillEffect(event, room, ctx.owner, ctx, target);
		if (target->isAlive() && ctx.owner->isAlive() && ctx.extra_data.toBool()) {
			ctx.choice = "top"; ctx.modified_amount = amount; ctx.modified_amount_set = modified;
			skillEffect(event, room, ctx.owner, ctx, target);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.choice == "draw") {
			ctx.owner->peiyin(this);
			target->drawCards(getEffectiveAmount(ctx), objectName());
			ctx.extra_data = true;
			return false;
		}
		for (ServerPlayer *other : room->getAlivePlayers()) if (other->getHandcardNum() > target->getHandcardNum()) return false;
		if (target->getCards("ej").isEmpty()) return false;
		const int id = room->askForCardChosen(ctx.owner, target, "ej", objectName());
		if (id >= 0 && room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceDelayedTrick))
			room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, false);
		return false;
	}
};

MobileKuangxiangCard::MobileKuangxiangCard()
{
}

bool MobileKuangxiangCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&Self->getHandcardNum()>to->getHandcardNum();
}

namespace {
const char *kuangxiangReceipts = "mobile_kuangxiang_receipts";
const char *kuangxiangDispatches = "mobile_kuangxiang_dispatches";
QString kuangxiangDepth(Room *room)
{ return QString::number(room->getThread()->getEventStack()->size()); }
qint64 nextKuangxiangSerial(Room *room)
{
    const qint64 serial = room->getTag("mobile_kuangxiang_serial").toLongLong() + 1;
    room->setTag("mobile_kuangxiang_serial", serial);
    return serial;
}
SkillInstanceRef kuangxiangSource(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("source_owner").toString(),
        SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
}
}

void MobileKuangxiangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (!source || !source->isAlive()) return;
    if (!getActivationSkillName().isEmpty() && getActivationSkillName() != "mobilekuangxiang") return;
    // Ordinary legacy view-as/AI cards have no instance field. Use the same
    // explicit-copy choice policy as the native activation resolver.
    int activationId = getActivationSkillInstanceId();
    if (activationId == 0) {
        QList<int> available;
        for (int id : source->getValidSkillInstanceIds("mobilekuangxiang")) {
            const SkillInstanceRef ref(source->objectName(), SkillInstanceKey("mobilekuangxiang", id));
            if (room->canShowGeneralForSkill(ref)) available << id;
        }
        if (available.isEmpty()) return;
        activationId = available.first();
        if (available.size() > 1) {
            QStringList choices;
            for (int id : available) choices << SkillInstanceUtils::formatName("mobilekuangxiang", id);
            const int selected = choices.indexOf(room->askForChoice(source, "mobilekuangxiang", choices.join("+")));
            if (selected < 0) return;
            activationId = available.at(selected);
        }
    }
    const SkillInstanceRef activation(source->objectName(), SkillInstanceKey("mobilekuangxiang", activationId));
    if (!source->isAlive() || !source->getValidSkillInstanceIds("mobilekuangxiang").contains(activationId)
        || !room->canShowGeneralForSkill(activation)) return;
    // Pin the selected source before swap callbacks can remove its grant.
    const SkillInstanceRef root = room->resolveSkillInstanceRootRef(activation);
    if (!root.isValid()) return;
    for (ServerPlayer *target : targets) {
        const QList<int> sourceCards = source->handCards(), targetCards = target->handCards();
        const qint64 swap = nextKuangxiangSerial(room);
        QVariantList receipts = room->getTag(kuangxiangReceipts).toList();
        if (root.isValid()) {
            const auto add = [&](ServerPlayer *recipient, const QList<int> &ids) {
                if (ids.isEmpty()) return;
                QVariantList values;
                for (int id : ids) values << id;
                receipts << QVariantMap{{"serial", nextKuangxiangSerial(room)}, {"swap", swap},
                    {"issuer", source->objectName()}, {"recipient", recipient->objectName()},
                    {"activation_skill", activation.key.skillName}, {"activation_instance", activation.key.instanceID},
                    {"source_owner", root.ownerObjectName}, {"source_skill", root.key.skillName},
                    {"source_instance", root.key.instanceID}, {"ids", values}, {"received", QVariantList()},
                    {"initializing", true}, {"spent", false}, {"consumed", false}};
            };
            add(source, targetCards);
            add(target, sourceCards);
            room->setTag(kuangxiangReceipts, receipts);
        }
        room->swapCards(source, target, "h", "mobilekuangxiang");
        receipts = room->getTag(kuangxiangReceipts).toList();
        for (QVariant &entry : receipts) {
            QVariantMap receipt = entry.toMap();
            if (receipt.value("swap").toLongLong() != swap) continue;
            receipt["initializing"] = false;
            receipt["ids"] = receipt.value("received");
            entry = receipt;
        }
        room->setTag(kuangxiangReceipts, receipts);
        for (ServerPlayer *recipient : {source, target}) {
            for (int id : recipient->handCards()) {
                bool received = false;
                for (const QVariant &entry : receipts) {
                    const QVariantMap receipt = entry.toMap();
                    if (receipt.value("swap").toLongLong() == swap
                        && receipt.value("recipient").toString() == recipient->objectName()
                        && receipt.value("received").toList().contains(id)) received = true;
                }
                if (!received) continue;
                room->setCardTip(id, "mobilekuangxiang");
                room->setCardFlag(id, "mobilekuangxiang");
            }
        }
    }
}

class MobileKuangxiangVs : public ZeroCardViewAsSkill
{
public:
	MobileKuangxiangVs() : ZeroCardViewAsSkill("mobilekuangxiang")
	{
		response_pattern = "@@mobilekuangxiang!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileKuangxiangCard")<1&&player->getHandcardNum()>0;
	}

	const Card *viewAs() const
	{
		return new MobileKuangxiangCard;
	}
};

class MobileKuangxiang : public TriggerSkillV2
{
public:
    MobileKuangxiang() : TriggerSkillV2("mobilekuangxiang")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        global = true;
        view_as_skill = new MobileKuangxiangVs;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        QVariantList receipts = room->getTag(kuangxiangReceipts).toList();
        if (receipts.isEmpty()) return true;
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play) return true;
            const int before = receipts.size();
            for (int i = receipts.size() - 1; i >= 0; --i)
                if (receipts[i].toMap().value("issuer").toString() == player->objectName()) receipts.removeAt(i);
            if (receipts.size() == before) return true;
            room->setTag(kuangxiangReceipts, receipts);
            for (ServerPlayer *holder : room->getAllPlayers(true)) {
                for (int id : holder->handCards()) {
                    const Card *card = Sanguosha->getCard(id);
                    if (!card || (!card->hasFlag("mobilekuangxiang") && !card->hasTip("mobilekuangxiang"))) continue;
                    bool retained = false;
                    for (const QVariant &entry : receipts) {
                        const QVariantMap receipt = entry.toMap();
                        if (receipt.value("recipient").toString() == holder->objectName()
                            && receipt.value("received").toList().contains(id)) retained = true;
                    }
                    if (!retained) { room->setCardTip(id, "-mobilekuangxiang"); room->setCardFlag(id, "-mobilekuangxiang"); }
                }
            }
            return true;
        }
        // A move is broadcast to every player; inspect its physical endpoints once.
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || move.from != player) return true;
        QVariantMap dispatches = room->getTag(kuangxiangDispatches).toMap();
        const qint64 dispatch = nextKuangxiangSerial(room);
        dispatches[kuangxiangDepth(room)] = dispatch;
        room->setTag(kuangxiangDispatches, dispatches);
        for (QVariant &entry : receipts) {
            QVariantMap receipt = entry.toMap();
            const QString recipientName = receipt.value("recipient").toString();
            const QVariantList ids = receipt.value("ids").toList();
            QVariantList received = receipt.value("received").toList();
            // Atomic swaps commit every destination before notifying listeners.
            // Observe all delivered cards before a nested callback can lose them.
            if (receipt.value("initializing").toBool()) {
                ServerPlayer *recipient = room->findPlayerByObjectName(recipientName, true);
                if (recipient) for (const QVariant &id : ids)
                    if (recipient->handCards().contains(id.toInt()) && !received.contains(id)) received << id;
                // An earlier move recorder may already have removed a newly
                // delivered card. Its departure from this recipient's hand is
                // also delivery evidence, even when the live hand is now empty.
                if (player->objectName() == recipientName) {
                    for (int i = 0; i < move.card_ids.size(); ++i) {
                        const int id = move.card_ids[i];
                        if (move.from_places.value(i) == Player::PlaceHand
                            && ids.contains(id) && !received.contains(id)) received << id;
                    }
                }
                receipt["received"] = received;
            }
            if (move.to && move.to->objectName() == recipientName && move.to_place == Player::PlaceHand) {
                for (int id : move.card_ids)
                    if (ids.contains(id) && !received.contains(id)) received << id;
                receipt["received"] = received;
            }
            if (receipt.value("spent").toBool()
                || player->objectName() != recipientName || received.isEmpty()) { entry = receipt; continue; }
            bool lostReceived = false;
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceHand && received.contains(move.card_ids[i])) lostReceived = true;
            bool stillHeld = false;
            for (const QVariant &id : received) if (player->handCards().contains(id.toInt())) stillHeld = true;
            if (lostReceived && !stillHeld) {
                receipt["spent"] = true;
                receipt["dispatch"] = dispatch;
            }
            entry = receipt;
        }
        room->setTag(kuangxiangReceipts, receipts);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != CardsMoveOneTime || !player) return true;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player) return true;
        const qint64 dispatch = room->getTag(kuangxiangDispatches).toMap().value(kuangxiangDepth(room)).toLongLong();
        for (const QVariant &entry : room->getTag(kuangxiangReceipts).toList()) {
            const QVariantMap receipt = entry.toMap();
            if (!receipt.value("spent").toBool() || receipt.value("consumed").toBool()
                || receipt.value("dispatch").toLongLong() != dispatch) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
            if (!owner || !owner->isAlive() || !player->isAlive()) continue;
            SkillContext ctx;
            ctx.owner = owner; ctx.invoker = ctx.initiator = player;
            ctx.skill_name = objectName();
            ctx.instanceID = receipt.value("activation_instance").toInt();
            ctx.trigger_count = receipt.value("serial").toInt();
            ctx.sourceRef = kuangxiangSource(receipt);
            ctx.original_data = &data; ctx.current_event = event;
            ctx.amount = 2; ctx.manual_effect = true;
            ctx.targets = {player}; ctx.extra_data = receipt;
            out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.sourceRef.isValid()) return false;
        const qint64 serial = ctx.extra_data.toMap().value("serial").toLongLong();
        for (const QVariant &entry : room->getTag(kuangxiangReceipts).toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("serial").toLongLong() == serial && kuangxiangSource(receipt) == ctx.sourceRef) return true;
        }
        return false;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const qint64 serial = ctx.extra_data.toMap().value("serial").toLongLong();
        QVariantList receipts = room->getTag(kuangxiangReceipts).toList();
        bool found = false;
        for (QVariant &entry : receipts) {
            QVariantMap receipt = entry.toMap();
            if (receipt.value("serial").toLongLong() != serial || receipt.value("consumed").toBool()) continue;
            receipt["consumed"] = true; entry = receipt; found = true; break;
        }
        room->setTag(kuangxiangReceipts, receipts);
        if (!found || !ctx.invoker || !ctx.invoker->isAlive()) return false;
        const auto *xuye = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getTriggerSkill("xuye"));
        return xuye && xuye->cost(event, room, ctx.owner, ctx);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Borrow only the effect. Do not dispatch Damaged or apply its least-hand condition.
        const auto *xuye = dynamic_cast<const TriggerSkillV2 *>(Sanguosha->getTriggerSkill("xuye"));
        if (!xuye) return false;
        SkillContext borrowed = ctx;
        borrowed.skill_name = "xuye";
        return xuye->effect(event, room, ctx.owner, borrowed);
    }
};

GanjueCard::GanjueCard()
{
	handling_method = Card::MethodUse;
}

bool GanjueCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("ganjue");
	dc->addSubcards(subcards);
	dc->deleteLater();
	return Self->canSlash(to,dc,false)&&targets.length()<=Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, dc);
}

const Card *GanjueCard::validate(CardUseStruct &card_use) const
{
	card_use.m_addHistory = false;
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("ganjue");
	dc->addSubcards(subcards);
	dc->deleteLater();
	return dc;
}

class GanjueVs : public OneCardViewAsSkill
{
public:
	GanjueVs() : OneCardViewAsSkill("ganjue")
	{
		response_or_use = true;
	}

	const Card *viewAs(const Card *c) const
	{
		GanjueCard *card = new GanjueCard;
		card->addSubcard(c);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("GanjueCard");
	}
};

class Ganjue : public TriggerSkill
{
public:
	Ganjue() : TriggerSkill("ganjue")
	{
		events << TargetSpecified;
		view_as_skill = new GanjueVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				foreach (ServerPlayer *p, use.to){
					bool has = false;
					foreach (const Card*h, p->getHandcards()){
						if(h->getSuit()==use.card->getSuit())
							has = true;
					}
					if(has) continue;
					use.no_respond_list << p->objectName();
				}
				data.setValue(use);
			}
		}
		return false;
	}
};

class Zhuhe : public TriggerSkill
{
public:
	// Tom 2026-09-29: obtain one equipment of the discarded suit from the draw pile, then use it.
	Zhuhe() : TriggerSkill("zhuhe")
	{
		events << EventPhaseEnd;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseEnd){
			if(player->getPhase()==Player::Play&&player->canDiscard(player,"h")){
				const Card*c = room->askForCard(player,".","zhuhe0:",data,Card::MethodNone);
				if(c){
					player->skillInvoked(this);
					Card*dc = dummyCard();
					foreach (const Card*h, player->getHandcards()){
						if(h->getSuit()==c->getSuit()&&!player->isJilei(h))
							dc->addSubcard(h);
					}
					room->throwCard(dc,objectName(),player);
					bool has = dc->subcardsLength()>=player->getEquips().length();
					dc->clearSubcards();
					foreach (int id, room->getDrawPile()){
						const Card*sc = Sanguosha->getCard(id);
						if(sc->getSuit()==c->getSuit()&&sc->isKindOf("EquipCard"))
							dc->addSubcard(id);
					}
					if(dc->subcardsLength()>0&&player->isAlive()){
						room->fillAG(dc->getSubcards(),player);
						int id = room->askForAG(player,dc->getSubcards(),false,objectName());
						room->clearAG(player);
						c = Sanguosha->getCard(id);
						if(c && room->getCardPlace(id)==Player::DrawPile)
							room->obtainCard(player,c,objectName());
						if(c && player->isAlive() && player->handCards().contains(id) && c->isAvailable(player))
							room->useCard(CardUseStruct(c,player));
					}
					if(has&&player->isAlive()){
						QString choice = "zhuhe1+zhuhe3";
						if(player->isWounded()) choice = "zhuhe1+zhuhe2+zhuhe3";
						choice = room->askForChoice(player,objectName(),choice,data);
						if(choice=="zhuhe1")
							player->drawCards(2,objectName());
						else if(choice=="zhuhe2")
							room->recover(player,RecoverStruct(objectName(),player));
						else if(choice=="zhuhe3")
							player->gainHujia();
					}
				}
			}
		}
		return false;
	}
};

MobileDaoshuCard::MobileDaoshuCard()
{
	mute = true;
}

bool MobileDaoshuCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->getHandcardNum()>1&&to!=Self;
}

void MobileDaoshuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->peiyin("mobiledaoshu",1);
	foreach (ServerPlayer *p, targets) {
		const Card*sc = room->askForExchange(p,"mobiledaoshu",1,1,false,"mobiledaoshu0");
		if(!sc) continue;
		QStringList pns,pns2 = Sanguosha->getCardNames(".");
		qsanShuffle(pns2);
		pns << pns2[0] << pns2[1] << pns2[2];
		QString choice = room->askForChoice(p,"mobiledaoshu",pns.join("+"),QVariant::fromValue(source));
		pns.clear();
		foreach (const Card*h, p->getHandcards()) {
			if(h->getId()==sc->getEffectiveId())
				pns << "mobiledaoshu1="+choice+"="+h->getSuitString()+"_char="+h->getNumberString();
			else
				pns << "mobiledaoshu1="+h->objectName()+"="+h->getSuitString()+"_char="+h->getNumberString();
		}
		QString choice2 = room->askForChoice(source,"mobiledaoshu",pns.join("+"),QVariant::fromValue(p),"","mobiledaoshu2");
		if(choice2.contains(choice)&&choice2.contains(sc->getSuitString())&&choice2.contains("_char="+sc->getNumberString())){
			source->peiyin("mobiledaoshu",2);
			room->damage(DamageStruct("mobiledaoshu",source,p));
		}else{
			source->peiyin("mobiledaoshu",3);
			if(source->getHandcardNum()<2){
				room->loseHp(source,1,true,source,objectName());
				continue;
			}
			QList<const Card*>hs = source->getHandcards();
			qsanShuffle(hs);
			Card*dc = dummyCard();
			foreach (const Card*h,hs) {
				if(source->isJilei(h)) continue;
				dc->addSubcard(h);
				if(dc->subcardsLength()>1) break;
			}
			room->throwCard(dc,"mobiledaoshu",source);
		}
	}
}

class MobileDaoshu : public ZeroCardViewAsSkill
{
public:
	MobileDaoshu() : ZeroCardViewAsSkill("mobiledaoshu")
	{
		response_pattern = "@@mobiledaoshu";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileDaoshuCard")<1;
	}

	const Card *viewAs() const
	{
		return new MobileDaoshuCard;
	}
};

class Daizui : public TriggerSkill
{
public:
	Daizui() : TriggerSkill("daizui")
	{
		events << DamageInflicted << EventPhaseChanging;
		frequency = Limited;
		limit_mark = "@daizui";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.damage>=player->getHp()+player->getHujia()&&player->hasSkill(objectName())
				&&player->getMark("@daizui")>0&&player->askForSkillInvoke(this,data)){
				player->peiyin("daizui");
				room->doSuperLightbox(player, "daizui");
				room->removePlayerMark(player, "@daizui");
				player->damageRevises(data,-damage.damage);
				if(damage.from&&damage.from->isAlive()&&damage.card&&room->getCardOwner(damage.card->getEffectiveId())==nullptr)
					damage.from->addToPile("dz_shi",damage.card);
				return true;
			}
		}else{
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->getPile("dz_shi").length()>0){
						p->obtainCard(dummyCard(p->getPile("dz_shi")));
					}
				}
			}
		}
		return false;
	}
};

class Kuangwu : public TriggerSkillV2
{
public:
	Kuangwu() : TriggerSkillV2("kuangwu") { events << EventPhaseStart << EventPhaseChanging << TurnStart; }
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
	{
		const QVariant round = room->historyScopes().value("round_id");
		for (ServerPlayer *holder : room->getAllPlayers(true)) {
			QVariantList keep, expired;
			for (const QVariant &entry : holder->getTag("mobile_kuangwu_invalidity").toList())
				(entry.toMap().value("round") == round ? keep : expired) << entry;
			holder->setTag("mobile_kuangwu_invalidity", keep);
			for (const QVariant &entry : expired) {
				const QVariantMap receipt = entry.toMap();
				room->removeSkillInvalidity(holder, objectName(), receipt.value("source").toString(), receipt.value("reason").toString(), receipt.value("instance").toInt());
			}
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play) return {};
		TriggerList result;
		for (ServerPlayer *owner : room->getOtherPlayers(player)) {
			const int own = owner->getHandcardNum(), other = player->getHandcardNum();
			if (owner->hasSkill(objectName()) && ((own < other && own < 5) || (own > other && own > 1))) result[owner] << objectName();
		}
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!ctx.invoker || !ctx.invoker->isAlive() || !ctx.owner->askForSkillInvoke(objectName() + "$" + QString::number(qsanRandomBounded(2) + 1), ctx.invoker)) return false;
		ctx.targets = {ctx.invoker}; ctx.manual_effect = true;
		ctx.extra_data = QVariantMap{{"other", ctx.invoker->objectName()}, {"hand", ctx.invoker->getHandcardNum()}};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		ServerPlayer *opponent = ctx.targets.value(0);
		if (!opponent) return false;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		const auto run = [&](const QString &stage, ServerPlayer *target) {
			ctx.choice = stage; ctx.modified_amount = amount; ctx.modified_amount_set = modified;
			if (target && target->isAlive()) skillEffect(event, room, ctx.owner, ctx, target);
		};
		run("adjust", ctx.owner);
		if (ctx.owner->isAlive() && opponent->isAlive() && ctx.extra_data.toMap().value("adjusted").toBool()) run("duel", opponent);
		if (ctx.owner->isAlive() && ctx.extra_data.toMap().value("failed").toBool()) run("penalty", ctx.owner);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		if (ctx.choice == "adjust") {
			const int other = saved.value("hand").toInt(), own = target->getHandcardNum();
			if (own < other) target->drawCards(qMax(0, qMin(5, other) - own), objectName());
			else if (own > other) {
				const int count = qMax(0, own - qMax(1, other));
				if (count > 0) room->askForDiscard(target, objectName(), count, count);
			}
			saved["adjusted"] = true;
		} else if (ctx.choice == "duel") {
			std::unique_ptr<Card> duel(Sanguosha->cloneCard("duel"));
			duel->setSkillName("_kuangwu");
			if (!ctx.owner->canUse(duel.get(), target)) return false;
			CardUseStruct use(duel.get(), ctx.owner, target);
			use.setOwnedCard(duel.release());
			if (!room->useCardFromSkillEffect(use, ctx, true)) return false;
			const qint64 useId = use.targetModReveal.useHistoryEventId;
			if (useId <= 0) return false;
			QVariantMap query{{"to", target->objectName()}, {"turn_id", room->historyEvent(useId).value("turn_id")}};
			bool damaged = false;
			for (;;) {
				const QVariantMap page = room->queryActualDamage(query);
				if (!page.value("complete").toBool()) return false;
				for (const QVariant &entry : page.value("items").toList()) {
					const QVariantMap fact = entry.toMap();
					if (fact.value("data").toMap().value("card").toMap().value("classes").toStringList().contains("Duel")
						&& room->historyParent(fact.value("event_id").toLongLong(), "use_card", true).value("id").toLongLong() == useId) damaged = true;
				}
				if (damaged || !page.value("has_more").toBool()) break;
				query["after"] = page.value("next_after"); query["watermark"] = page.value("watermark");
			}
			saved["failed"] = !damaged;
		} else {
			const SkillInstanceRef ref = getUsageRef(ctx);
			ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
			if (holder && holder->findSkillInstance(ref.key.skillName, ref.key.instanceID)) {
				const QString reason = "kuangwu:" + room->historyScopes().value("round_id").toString() + ":" + QString::number(ref.key.instanceID);
				const QString source = "kuangwu-effect:" + ref.ownerObjectName + ":" + QString::number(ref.key.instanceID);
				QVariantList receipts = holder->getTag("mobile_kuangwu_invalidity").toList();
				receipts << QVariantMap{{"instance", ref.key.instanceID}, {"source", source}, {"root_owner", ctx.sourceRef.ownerObjectName}, {"reason", reason},
					{"root_skill", ctx.sourceRef.key.skillName}, {"root_instance", ctx.sourceRef.key.instanceID},
					{"activation_owner", ref.ownerObjectName}, {"activation_skill", ref.key.skillName}, {"round", room->historyScopes().value("round_id")}};
				holder->setTag("mobile_kuangwu_invalidity", receipts);
				room->addSkillInvalidity(holder, objectName(), source, reason, ref.key.instanceID);
			}
			ctx.owner->peiyin(objectName(), qsanRandomBounded(2) + 3);
			room->loseHp(target, getEffectiveAmount(ctx), true, ctx.owner, objectName());
		}
		ctx.extra_data = saved;
		return false;
	}
};

class Futu : public TriggerSkillV2
{
public:
	Futu() : TriggerSkillV2("futu") { events << DamageInflicted << EventPhaseChanging; }
	static QStringList leadingColors(Room *room, ServerPlayer *owner, bool &complete)
	{
		complete = false;
		const QVariant turn = room->historyScopes().value("turn_id");
		if (turn.toLongLong() <= 0) return {};
		QMap<QString, int> damage, recovery;
		for (int mode = 0; mode < 2; ++mode) {
			QVariantMap query{{"turn_id", turn}};
			if (mode == 1) query.insert("kind", "actual_recover");
			for (;;) {
				const QVariantMap page = mode == 0 ? room->queryActualDamage(query) : room->queryHistoryFacts(query);
				if (!page.value("complete").toBool()) return {};
				for (const QVariant &entry : page.value("items").toList()) {
					const QVariantMap fact = entry.toMap().value("data").toMap();
					if (mode == 0) damage[fact.value("from").toString()] += fact.value("amount").toInt();
					else recovery[fact.value("from").toString()] += fact.value("amount").toInt();
				}
				if (!page.value("has_more").toBool()) break;
				query.insert("after", page.value("next_after"));
				query.insert("watermark", page.value("watermark"));
			}
		}
		int otherDamage = 0, otherRecovery = 0;
		for (ServerPlayer *other : room->getOtherPlayers(owner)) {
			otherDamage = qMax(otherDamage, damage.value(other->objectName()));
			otherRecovery = qMax(otherRecovery, recovery.value(other->objectName()));
		}
		QStringList colors;
		if (damage.value(owner->objectName()) > otherDamage) colors << "black";
		if (recovery.value(owner->objectName()) > otherRecovery) colors << "red";
		complete = true;
		return colors;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		if (event == DamageInflicted) {
			if (player && player->isAlive() && player->hasSkill(objectName()) && !player->getPile("futu_yue").isEmpty()) result[player] << objectName();
			return result;
		}
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
		for (ServerPlayer *owner : room->getAlivePlayers()) {
			if (!owner->hasSkill(objectName())) continue;
			bool complete = false;
			const QStringList colors = leadingColors(room, owner, complete);
			if (!complete) qWarning("Futu: incomplete damage or recovery history");
			else if (!colors.isEmpty()) result[owner] << objectName();
		}
		return result;
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = {player};
		if (event == DamageInflicted) {
			const QList<int> ids = player->getPile("futu_yue");
			if (ids.isEmpty() || !player->askForSkillInvoke(this, *ctx.original_data)) return false;
			room->fillAG(ids, player);
			int id = -1;
			try { id = room->askForAG(player, ids, false, objectName()); }
			catch (...) { room->clearAG(player); throw; }
			room->clearAG(player);
			if (!ids.contains(id)) return false;
			ctx.extra_data = id;
			return true;
		}
		bool complete = false;
		ctx.extra_data = leadingColors(room, player, complete);
		ctx.is_forced = true;
		return complete && !ctx.extra_data.toStringList().isEmpty();
	}
	bool pay(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event != DamageInflicted) return true;
		const int id = ctx.extra_data.toInt();
		if (!player->getPile("futu_yue").contains(id)) return false;
		room->throwCard(id, objectName(), player);
		return true;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event == DamageInflicted) return target->damageRevises(*ctx.original_data, -ctx.original_data->value<DamageStruct>().damage);
		for (const QString &color : ctx.extra_data.toStringList()) {
			for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
				int selected = -1;
				for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->getColorString() == color) { selected = id; break; }
				if (selected < 0) break;
				// Choosing the first matching pile card leaves every preceding card in its original order.
				target->addToPile("futu_yue", selected);
			}
		}
		return false;
	}
};

JingtuCard::JingtuCard() { setSkillName("jingtu"); will_throw = false; }
bool JingtuCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *self) const
{
	return ActiveSkillCard::targetFilter(targets, target, self);
}
bool JingtuCard::targetsFeasible(const QList<const Player *> &targets, const Player *self) const { return ActiveSkillCard::targetsFeasible(targets, self); }
void JingtuCard::onUse(Room *room, CardUseStruct &use) const { ActiveSkillCard::onUse(room, use); }
void JingtuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const { ActiveSkillCard::use(room, source, targets); }
class Jingtu : public ViewAsSkillV2
{
public:
	Jingtu() : ViewAsSkillV2("jingtu", -1) { expand_pile = "futu_yue"; frequency = Limited; limit_mark = "@jingtu"; setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Game; }
	bool willThrowSelectedCards() const override { return false; }
	static QStringList colors(const QList<int> &ids)
	{
		QStringList result;
		for (int id : ids) {
			const QString color = Sanguosha->getCard(id)->getColorString();
			if ((color == "black" || color == "red") && !result.contains(color)) result << color;
		}
		return result;
	}
	static bool balanced(const Player *player)
	{
		int black = 0, red = 0;
		for (int id : player->getPile("futu_yue")) {
			const Card *card = Sanguosha->getCard(id);
			if (card->isBlack()) ++black;
			if (card->isRed()) ++red;
		}
		return black == red && black > 1;
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->getPhase() == Player::Play && !request.initiator->getPile("futu_yue").isEmpty();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		if (!request.initiator || !card || !request.initiator->getPile("futu_yue").contains(card->getEffectiveId()) || (!card->isRed() && !card->isBlack())) return false;
		return request.selectedCardIds.isEmpty() || balanced(request.initiator)
			|| Sanguosha->getCard(request.selectedCardIds.first())->getColor() == card->getColor();
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
		const QStringList selectedColors = colors(request.selectedCardIds);
		if (selectedColors.isEmpty() || (selectedColors.length() > 1 && !balanced(request.initiator))) return false;
		QSet<int> wanted, selected;
		for (int id : request.initiator->getPile("futu_yue")) if (selectedColors.contains(Sanguosha->getCard(id)->getColorString())) wanted << id;
		for (int id : request.selectedCardIds) selected << id;
		return wanted == selected && selected.size() == request.selectedCardIds.length();
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		return target && !selected.contains(target) && selected.length() < colors(request.selectedCardIds).length();
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
	{
		return !targets.isEmpty() && targets.length() == colors(request.selectedCardIds).length();
	}
	const Card *createCard(const ActiveSkillRequest &request) const override { auto *card = new JingtuCard; card->addSubcards(request.selectedCardIds); return card; }
	bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override { return cardSelectionFeasible(request); }
	void addUsage(const SkillContext &ctx) const override
	{
		ViewAsSkillV2::addUsage(ctx);
		if (!ctx.owner) return;
		Room *room = ctx.owner->getRoom();
		int remaining = 0;
		for (int id : ctx.owner->getSkillInstanceIds(objectName())) {
			SkillContext candidate = ctx;
			candidate.activationRef = SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(objectName(), id));
			candidate.sourceRef = room->resolveSkillInstanceRootRef(candidate.activationRef);
			candidate.instanceID = id;
			if (candidate.sourceRef.isValid() && isUsable(candidate)) ++remaining;
		}
		room->setPlayerMark(ctx.owner, limit_mark, remaining);
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const QList<int> ids = ctx.use_card->getSubcards();
		int black = 0, red = 0;
		for (int id : ids) { if (Sanguosha->getCard(id)->isBlack()) ++black; if (Sanguosha->getCard(id)->isRed()) ++red; }
		ctx.extra_data = QVariantMap{{"black", black}, {"red", red}, {"colors", colors(ids)}, {"action", "obtain"}};
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		skillEffect(ctx, ctx.owner);
		const QStringList actions = black > 0 && red > 0 ? QStringList{"damage", "maxhp", "recover"}
			: black > 0 ? QStringList{"damage"} : QStringList{"maxhp", "recover"};
		for (const QString &action : actions) {
			if (ctx.targets.isEmpty()) break;
			QVariantMap saved = ctx.extra_data.toMap();
			saved.insert("action", action);
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(ctx, action == "damage" ? ctx.targets.first() : ctx.targets.last());
		}
		QVariantMap saved = ctx.extra_data.toMap();
		saved.insert("action", "transform");
		ctx.extra_data = saved;
		ctx.modified_amount = amount;
		ctx.modified_amount_set = modified;
		skillEffect(ctx, ctx.owner);
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = ctx.owner->getRoom();
		const QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		if (action == "obtain") {
			QList<int> ids;
			for (int id : ctx.use_card->getSubcards()) if (target->getPile("futu_yue").contains(id)) ids << id;
			if (!ids.isEmpty()) { DummyCard gain(ids); room->obtainCard(target, &gain); }
		} else if (action == "damage") room->damage(DamageStruct(objectName(), ctx.owner, target, saved.value("black").toInt() * getEffectiveAmount(ctx)));
		else if (action == "maxhp") room->gainMaxHp(target, saved.value("red").toInt() * getEffectiveAmount(ctx), objectName());
		else if (action == "recover") room->recover(target, RecoverStruct(objectName(), ctx.owner, saved.value("red").toInt() * getEffectiveAmount(ctx)));
		else {
			const SkillInstance *origin = target->findSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
			QStringList candidates, related, sameSlot;
			for (int id : target->getSkillInstanceIds("futu")) {
				const SkillInstance *instance = target->findSkillInstance("futu", id);
				if (!instance) continue;
				const QString name = SkillInstanceUtils::formatName("futu", id);
				candidates << name;
				const SkillInstanceRef ref(target->objectName(), SkillInstanceKey("futu", id));
				if (room->resolveSkillInstanceRootRef(ref) == ctx.sourceRef || (origin &&
					((origin->parentRef.isValid() && origin->parentRef == instance->parentRef)
					|| (origin->grantActivationRef.isValid() && origin->grantActivationRef == instance->grantActivationRef)))) related << name;
				if (origin && origin->bindHead > 0 && origin->bindHead == instance->bindHead) sameSlot << name;
			}
			if (!related.isEmpty()) candidates = related;
			QStringList slotCandidates;
			for (const QString &name : candidates) if (sameSlot.contains(name)) slotCandidates << name;
			if (!slotCandidates.isEmpty()) candidates = slotCandidates;
			if (!candidates.isEmpty()) {
				const QString remove = candidates.length() == 1 ? candidates.first() : room->askForChoice(target, objectName(), candidates.join("+"));
				if (candidates.contains(remove)) room->detachSkillFromPlayer(target, remove, false, true);
			}
			if (!target->isAlive()) return ContinueEffects;
			const QStringList grantedColors = saved.value("colors").toStringList();
			room->acquireSkillFromEffect(target, "mobilefozhong", ctx, [target, grantedColors](int id) {
				target->setSkillInstanceStateValue("mobilefozhong", id, "colors", grantedColors);
			});
			if (target->getGeneralName().contains("zerong")) target->setAvatarIcon(grantedColors.length() > 1 ? "mobile_zerong4" : grantedColors.contains("black") ? "mobile_zerong2" : "mobile_zerong3");
		}
		return ContinueEffects;
	}
};
JiebianCard::JiebianCard() { setSkillName("jiebian"); will_throw = false; }
bool JiebianCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *self) const
{
	return ActiveSkillCard::targetFilter(targets, target, self);
}
void JiebianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const { ActiveSkillCard::use(room, source, targets); }
class Jiebianvs : public ViewAsSkillV2
{
public:
	Jiebianvs() : ViewAsSkillV2("jiebian", 1) { response_pattern = "@@jiebian"; expand_pile = "futu_yue"; }
	bool willThrowSelectedCards() const override { return false; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.pattern == response_pattern && (!request.initiator->isKongcheng() || !request.initiator->getPile("futu_yue").isEmpty());
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.isEmpty() && request.initiator->getPile("futu_yue").contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.selectedCardIds.length() <= 1 && (!request.selectedCardIds.isEmpty() || !request.initiator->isKongcheng());
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
	{
		const Player *player = request.initiator;
		if (!player || !target || !selected.isEmpty() || player == target || target->isKongcheng() || player->isPindianProhibited(target)) return false;
		int lowest = player->getHp();
		for (const Player *candidate : player->getAliveSiblings()) lowest = qMin(lowest, candidate->getHp());
		return target->hasFlag("CurrentPlayer") || target->getHp() == lowest;
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.length() == 1; }
	const Card *createCard(const ActiveSkillRequest &request) const override { auto *card = new JiebianCard; card->addSubcards(request.selectedCardIds); return card; }
	bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
	{
		return ctx.owner && (request.selectedCardIds.isEmpty() ? !ctx.owner->isKongcheng()
			: request.selectedCardIds.length() == 1 && ctx.owner->getPile("futu_yue").contains(request.selectedCardIds.first()));
	}
	EffectFlow effect(SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		Room *room = ctx.owner->getRoom();
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (ServerPlayer *target : ctx.targets) {
			ctx.extra_data = QVariantMap{{"action", "pindian"}, {"other", target->objectName()}};
			skillEffect(ctx, target);
			if (!ctx.extra_data.toMap().value("won").toBool() || !ctx.owner->isAlive() || !target->isAlive()) continue;
			const QString choice = room->askForChoice(ctx.owner, objectName(), "jiebian1+jiebian2");
			const QStringList actions = choice == "jiebian1" ? QStringList{"damage"} : QStringList{"recover", "draw", "choose", "obtain"};
			for (const QString &action : actions) {
				if (!ctx.owner->isAlive() || !target->isAlive()) break;
				QVariantMap saved = ctx.extra_data.toMap();
				saved.insert("action", action);
				ctx.extra_data = saved;
				ctx.modified_amount = amount;
				ctx.modified_amount_set = modified;
				skillEffect(ctx, action == "obtain" ? ctx.owner : target);
			}
		}
		return FinishSkill;
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		Room *room = ctx.owner->getRoom();
		QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		if (action == "pindian") {
			const QList<int> ids = ctx.use_card->getSubcards();
			if (target->isKongcheng() || ctx.owner->isPindianProhibited(target)) return ContinueEffects;
			if (ids.isEmpty() ? ctx.owner->isKongcheng() : !ctx.owner->getPile("futu_yue").contains(ids.first())) return ContinueEffects;
			const bool won = ctx.owner->pindian(target, objectName(), ids.isEmpty() ? nullptr : Sanguosha->getCard(ids.first()));
			saved.insert("won", won);
			ctx.extra_data = saved;
		} else if (action == "damage") room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
		else if (action == "recover") room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
		else if (action == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
		else if (action == "choose") {
			QList<int> ids;
			for (int i = 0; i < 2 * getEffectiveAmount(ctx) && ids.length() < target->getCardCount(); ++i) {
				const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodNone, ids);
				if (id < 0 || ids.contains(id)) break;
				ids << id;
			}
			QVariantList selected;
			for (int id : ids) selected << id;
			saved.insert("cards", selected);
			ctx.extra_data = saved;
		} else if (action == "obtain") {
			ServerPlayer *other = room->findPlayerByObjectName(saved.value("other").toString());
			QList<int> ids;
			if (other) for (const QVariant &entry : saved.value("cards").toList()) {
				const int id = entry.toInt();
				if (room->getCardOwner(id) == other && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) ids << id;
			}
			if (!ids.isEmpty()) { DummyCard gain(ids); room->obtainCard(target, &gain, false); }
		}
		return ContinueEffects;
	}
};
class Jiebian : public TriggerSkillV2
{
public:
	Jiebian() : TriggerSkillV2("jiebian") { events << EventPhaseEnd; view_as_skill = new Jiebianvs; }
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (!player || player->getPhase() != Player::Play) return result;
		const QVariant turn = room->historyScopes().value("turn_id");
		if (turn.toLongLong() <= 0) { qWarning("Jiebian: missing turn identity"); return result; }
		const QVariantMap damage = room->queryActualDamage({{"turn_id", turn}, {"limit", 1}});
		if (!damage.value("complete").toBool()) { qWarning("Jiebian: incomplete damage history"); return result; }
		if (!damage.value("items").toList().isEmpty()) return result;
		for (ServerPlayer *owner : room->getAlivePlayers())
			if (owner->hasSkill(objectName()) && (!owner->isKongcheng() || !owner->getPile("futu_yue").isEmpty())) result[owner] << objectName();
		return result;
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override { ctx.targets = {player}; return true; }
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		Room::AcceptedViewAsEffectScope scope(room, target, objectName(), ctx);
		if (scope.isValid()) room->askForUseCard(target, "@@jiebian", "jiebian0");
		return false;
	}
};
class MobileFozhong : public TriggerSkillV2
{
public:
	MobileFozhong() : TriggerSkillV2("mobilefozhong") { events << ConfirmDamage << PreHpRecover << EventPhaseProceeding; frequency = Compulsory; }
	static QStringList colors(Room *room, SkillInstanceRef ref)
	{
		QList<SkillInstanceRef> seen;
		while (ref.isValid() && !seen.contains(ref)) {
			seen << ref;
			ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
			const SkillInstance *instance = holder ? holder->findSkillInstance(ref.key.skillName, ref.key.instanceID) : nullptr;
			if (!instance) break;
			if (ref.key.skillName == "mobilefozhong") {
				const QStringList value = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "colors").toStringList();
				if (!value.isEmpty()) return value;
			}
			ref = instance->parentRef.isValid() ? instance->parentRef : SkillInstanceRef(ref.ownerObjectName, instance->parent);
		}
		return {};
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (!player || !player->isAlive()) return true;
		ServerPlayer *owner = player, *recipient = player;
		const Card *card = nullptr;
		if (event == PreHpRecover) {
			const RecoverStruct recover = data.value<RecoverStruct>();
			owner = recover.who;
			card = recover.card;
		} else if (event == ConfirmDamage) {
			const DamageStruct damage = data.value<DamageStruct>();
			owner = damage.from;
			recipient = damage.to;
			card = damage.card;
		} else if (player->getPhase() != Player::Discard || player->isKongcheng()) return true;
		if (!owner || !owner->isAlive() || !recipient || !recipient->isAlive()) return true;
		if (event != EventPhaseProceeding && (!card || card->getTypeId() == Card::TypeSkill)) return true;
		for (int id : owner->getValidSkillInstanceIds(objectName())) {
			const SkillInstanceRef activation(owner->objectName(), SkillInstanceKey(objectName(), id));
			const QStringList selected = colors(room, activation);
			if (selected.isEmpty() || (card && !selected.contains(card->getColorString()))) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = ctx.initiator = owner;
			ctx.invoker = player;
			ctx.activationRef = activation;
			ctx.sourceRef = room->resolveSkillInstanceRootRef(activation);
			if (!ctx.sourceRef.isValid()) continue;
			ctx.instanceID = id;
			ctx.amount = room->getSkillInstanceAmount(activation);
			ctx.extra_data = selected;
			ctx.targets = {recipient};
			ctx.original_data = &data;
			ctx.current_event = event;
			contexts << ctx;
		}
		return true;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event == EventPhaseProceeding) {
			QList<int> ids;
			for (const Card *card : target->getHandcards()) if (ctx.extra_data.toStringList().contains(card->getColorString())) ids << card->getEffectiveId();
			room->ignoreCards(target, ids);
		} else if (event == ConfirmDamage) {
			DamageStruct damage = ctx.original_data->value<DamageStruct>();
			damage.damage += getEffectiveAmount(ctx);
			ctx.original_data->setValue(damage);
		} else {
			// Modify the request before HP is committed; HpRecover only reports the completed recovery.
			RecoverStruct recover = ctx.original_data->value<RecoverStruct>();
			recover.recover += getEffectiveAmount(ctx);
			ctx.original_data->setValue(recover);
		}
		return false;
	}
};

class Shishu : public TriggerSkillV2
{
public:
	Shishu() : TriggerSkillV2("shishu") { events << CardsMoveOneTime; frequency = Compulsory; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || move.from != player || !move.to
			|| move.to == player || move.to_place != Player::PlaceHand || !move.to->hasFlag("CurrentPlayer")) return {};
		return move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		const qint64 event = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
		if (event <= 0) { qWarning("Shishu: missing exact movement identity"); return false; }
		QVariantMap query{{"event_id", event}, {"from", player->objectName()}, {"to", move.to->objectName()}};
		QSet<int> types;
		QVariantList ids;
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) { qWarning("Shishu: incomplete movement history"); return false; }
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap().value("data").toMap();
				const int id = fact.value("card_id").toInt(), place = fact.value("from_place").toInt();
				if (!move.card_ids.contains(id) || (place != Player::PlaceHand && place != Player::PlaceEquip)) continue;
				const QVariantMap card = fact.value("card_before").toMap();
				if (!card.contains("type")) { qWarning("Shishu: missing pre-move card type"); return false; }
				types << card.value("type").toInt();
				if (!ids.contains(id)) ids << id;
			}
			if (!page.value("has_more").toBool()) break;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
		QStringList allowed;
		if (!types.contains(Card::TypeBasic)) allowed << "BasicCard";
		if (!types.contains(Card::TypeTrick)) allowed << "TrickCard";
		if (!types.contains(Card::TypeEquip)) allowed << "EquipCard";
		if (ids.isEmpty()) return false;
		ctx.targets = {room->findPlayerByObjectName(move.to->objectName())};
		ctx.extra_data = QVariantMap{{"ids", ids}, {"allowed", allowed}};
		return ctx.targets.first() != nullptr;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (ServerPlayer *target : ctx.targets) {
			skillEffect(event, room, player, ctx, target);
			if (!ctx.extra_data.toMap().value("gift").toList().isEmpty() && player->isAlive()) {
				ctx.modified_amount = amount;
				ctx.modified_amount_set = modified;
				skillEffect(event, room, player, ctx, player);
			}
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		if (target == player) {
			ServerPlayer *donor = room->findPlayerByObjectName(saved.value("donor").toString());
			QList<int> present;
			if (donor) for (const QVariant &entry : saved.value("gift").toList()) {
				const int id = entry.toInt();
				if (room->getCardOwner(id) == donor && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) present << id;
			}
			if (!present.isEmpty()) { DummyCard gift(present); room->giveCard(donor, target, &gift, objectName()); }
			return false;
		}
		const QStringList allowed = saved.value("allowed").toStringList();
		const int count = getEffectiveAmount(ctx);
		const Card *gift = allowed.isEmpty() || count <= 0 ? nullptr : room->askForExchange(target, objectName(), count, count,
			true, "shishu0:" + player->objectName(), true, allowed.join(","));
		if (gift) {
			QVariantList ids;
			for (int id : gift->getSubcards()) {
				const Card *card = Sanguosha->getCard(id);
				bool validType = false;
				for (const QString &type : allowed) if (card->isKindOf(type.toLatin1().constData())) validType = true;
				if (!validType || room->getCardOwner(id) != target) return false;
				ids << id;
			}
			saved.insert("gift", ids);
			saved.insert("donor", target->objectName());
			ctx.extra_data = saved;
		} else {
			QList<int> discard;
			for (const QVariant &entry : saved.value("ids").toList()) {
				const int id = entry.toInt();
				if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand && target->canDiscard(target, id)) discard << id;
			}
			if (!discard.isEmpty()) room->throwCard(discard, objectName(), target);
		}
		return false;
	}
};

class MobileJili : public TriggerSkill
{
public:
	MobileJili() : TriggerSkill("mobilejili")
	{
		events << TargetSpecified << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to)
					p->addMark(player->objectName()+"mobilejiliUse-Clear");
			}
		}else{
			if(player->getPhase()==Player::RoundStart) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())&&p->inMyAttackRange(player)){
						QStringList choices;
						for(int i=0;i<=2;i++){
							if(p->getMark(QString("mobilejili%1_lun").arg(i))<1)
								choices << QString::number(i);
						}
						if(choices.length()>0&&p->askForSkillInvoke(objectName()+"$-1",player)){
							QString choice = room->askForChoice(p,objectName(),choices.join("+"));
							p->addMark(QString("mobilejili%1_lun").arg(choice));
							choice = QString("&mobilejili+%1+#%2-Clear").arg(choice).arg(p->objectName());
							room->setPlayerMark(player,choice,1,QList<ServerPlayer*>()<<p);
						}
						
					}
				}
			}else if(player->getPhase()==Player::Finish){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					foreach (QString m, player->getMarkNames()){
						if(m.contains("&mobilejili+")&&m.contains("+#"+p->objectName()+"-Clear")){
							int n = m.split("+")[1].toInt();
							int x = p->getMark(player->objectName()+"mobilejiliUse-Clear");
							if(x<n){
								p->drawCards(4-x,objectName());
							}else if(x==n){
								m = QString("mobilejili0:%1::%2").arg(player->objectName()).arg(x);
								Card*dc = room->askForExchange(p,objectName(),x,x,true,m);
								if(dc) room->giveCard(p,player,dc,objectName());
							}else{
								Card*dc = Sanguosha->cloneCard("slash");
								dc->setSkillName("_mobilejili");
								if(p->canSlash(player,dc,false)&&p->askForSkillInvoke(objectName(),player,false)){
									room->useCard(CardUseStruct(dc,p,player));
								}
								dc->deleteLater();
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class BsGongmou : public TriggerSkillV2
{
public:
	BsGongmou() : TriggerSkillV2("bsgongmou") { events << EventPhaseChanging << EventPhaseStart; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
		const QVariant turn = room->historyScopes().value("turn_id");
		for (ServerPlayer *target : room->getAllPlayers(true)) {
			QVariantList keep, expire;
			for (const QVariant &entry : target->getTag("mobile_bsgongmou_grants").toList())
				if (entry.toMap().value("turn") == turn) expire << entry;
				else keep << entry;
			target->setTag("mobile_bsgongmou_grants", keep);
			for (const QVariant &entry : expire) {
				const QVariantMap grant = entry.toMap();
				room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(grant.value("grant_skill").toString(), grant.value("grant_id").toInt()), false, true);
			}
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start && player->hasSkill(objectName())
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName() + "$-1", "bsgongmou0", true, true);
		if (!target) return false;
		ctx.targets = {target};
		ctx.extra_data = QVariantMap{{"turn", room->historyScopes().value("turn_id")}};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (ServerPlayer *target : ctx.targets) {
			QVariantMap saved = ctx.extra_data.toMap();
			saved.insert("action", "swap_permission");
			saved.insert("swap", false);
			ctx.extra_data = saved;
			skillEffect(event, room, player, ctx, player);
			saved = ctx.extra_data.toMap();
			saved.insert("action", "swap");
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, target);
			saved = ctx.extra_data.toMap();
			saved.insert("action", "qice");
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, player);
			saved = ctx.extra_data.toMap();
			saved.insert("action", "kanpo");
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, target);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		if (action == "swap_permission") { saved.insert("swap", true); ctx.extra_data = saved; return false; }
		if (action == "swap") {
			if (saved.value("swap").toBool() && player->isAlive()) room->swapCards(player, target, "h", objectName());
			return false;
		}
		if (room->historyScopes().value("turn_id") != saved.value("turn") || !room->getCurrent()
			|| room->getCurrent()->getPhase() == Player::NotActive) return false;
		const QVariantMap receipt{{"turn", saved.value("turn")}, {"owner", ctx.sourceRef.ownerObjectName},
			{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
			{"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
			{"activation_instance", ctx.activationRef.key.instanceID}, {"grant_skill", action}};
		// The commit callback stores identity before acquisition triggers can end the turn or remove the issuer.
		room->acquireSkillFromEffect(target, action, ctx, [target, receipt](int id) {
			QVariantMap grant = receipt;
			grant.insert("grant_id", id);
			QVariantList receipts = target->getTag("mobile_bsgongmou_grants").toList();
			receipts << grant;
			target->setTag("mobile_bsgongmou_grants", receipts);
		});
		return false;
	}
};

ZhengshuoCard::ZhengshuoCard()
{
	target_fixed = true;
}

void ZhengshuoCard::onUse(Room *room, CardUseStruct &use) const
{
	use.to = room->getAlivePlayers();
	SkillCard::onUse(room,use);
}

void ZhengshuoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->removePlayerMark(source,"@zhengshuo");
	room->doSuperLightbox(source, "zhengshuo");
	foreach (ServerPlayer *p, targets) {
		p->throwAllHandCardsAndEquips("zhengshuo");
	}
	room->swapPile();
	room->drawCards(targets,4,"zhengshuo");
}

class Zhengshuo : public ZeroCardViewAsSkill
{
public:
	Zhengshuo() : ZeroCardViewAsSkill("zhengshuo")
	{
		limit_mark = "@zhengshuo";
		frequency = Limited;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings(true)) {
			if(p->getHandcardNum()==4) return false;
		}
		return player->getMark(limit_mark)>0;
	}

	const Card *viewAs() const
	{
		return new ZhengshuoCard;
	}
};

class Chizhang : public TriggerSkillV2
{
public:
	Chizhang() : TriggerSkillV2("chizhang") { events << TargetSpecified << PostCardEffected << CardFinished << EventPhaseChanging; global = true; }
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
		for (ServerPlayer *target : room->getAllPlayers(true)) {
			QVariantList keep, expired;
			for (const QVariant &entry : target->getTag("mobile_chizhang_limits").toList()) {
				const QVariantMap receipt = entry.toMap();
				const qint64 receiptUse = receipt.value("use").toLongLong();
				const bool complete = room->historyEvent(receiptUse).value("status").toString() == "finished";
				if (complete || (receiptUse == useId && (event == CardFinished || (event == PostCardEffected && target == player)))) expired << entry;
				else keep << entry;
			}
			target->setTag("mobile_chizhang_limits", keep);
			for (const QVariant &entry : expired) room->removePlayerCardLimitationByReason(target, entry.toMap().value("reason").toString());
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName()) || !player->canDiscard(player, "h")) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		return use.card && use.card->isDamageCard() && use.m_isHandcard && !use.to.isEmpty()
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
		if (useId <= 0) { qWarning("Chizhang: missing exact use identity"); return false; }
		const Card *selection = room->askForExchange(player, objectName(), player->getHandcardNum(), 1, false, "", true);
		if (!selection || selection->getSubcards().isEmpty()) return false;
		QVariantList ids;
		QStringList colors;
		for (int id : selection->getSubcards()) {
			if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand || !player->canDiscard(player, id)) return false;
			ids << id;
			const QString color = Sanguosha->getCard(id)->getColorString();
			if (!colors.contains(color)) colors << color;
		}
		ctx.extra_data = QVariantMap{{"use", QString::number(useId)}, {"ids", ids}, {"colors", colors}};
		ctx.targets = ctx.original_data->value<CardUseStruct>().to;
		return true;
	}
	bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QList<int> ids;
		for (const QVariant &entry : ctx.extra_data.toMap().value("ids").toList()) {
			const int id = entry.toInt();
			if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand || !player->canDiscard(player, id)) return false;
			ids << id;
		}
		if (ids.isEmpty()) return false;
		room->throwCard(ids, objectName(), player);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const QVariantMap saved = ctx.extra_data.toMap();
		const quint64 serial = room->getTag("mobile_chizhang_serial").toULongLong() + 1;
		room->setTag("mobile_chizhang_serial", serial);
		const QString reason = QString("mobile_chizhang_%1").arg(serial);
		QVariantMap receipt{{"use", saved.value("use")}, {"reason", reason}, {"owner", ctx.sourceRef.ownerObjectName},
			{"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
			{"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
			{"activation_instance", ctx.activationRef.key.instanceID}};
		QVariantList receipts = target->getTag("mobile_chizhang_limits").toList();
		receipts << receipt;
		target->setTag("mobile_chizhang_limits", receipts);
		// The private reason prevents nested uses and other sources from removing one another's restriction.
		room->setPlayerCardLimitation(target, "use,response", ".|" + saved.value("colors").toStringList().join(",") + "|.|.", false, reason);
		return false;
	}
};

class Duanyangvs : public OneCardViewAsSkill
{
public:
	Duanyangvs() : OneCardViewAsSkill("duanyang")
	{
		response_pattern = "@@duanyang";
		expand_pile = "duanyang";
	}

	const Card *viewAs(const Card *c) const
	{
		return c;
	}
};

class Duanyang : public TriggerSkill
{
public:
	Duanyang() : TriggerSkill("duanyang")
	{
		events << CardsMoveOneTime << EventPhaseEnd << Damage;
		view_as_skill = new Duanyangvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)&&player->getMark("duanyangUse-Clear")<1
            &&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE){
				QList<int>ids;
				for(int i=0;i<move.card_ids.length();i++){
					if(move.from_places.at(i)==Player::PlaceHand
					&&Sanguosha->getCard(move.card_ids.at(i))->isKindOf("Slash"))
						ids << move.card_ids.at(i);
				}
				if(ids.length()>0){
					player->addMark("duanyangUse-Clear");
					room->sendCompulsoryTriggerLog(player,this);
					qsanShuffle(ids);
					player->addToPile("duanyang",ids.last(),true);
				}
			}
		}else if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("duanyangUse")
				&&damage.to->getCardCount(true,true)>0&&player->askForSkillInvoke("duanyang$-1",damage.to)){
				QList<int>ids;
				for(int i=0;i<2;i++){
					int id = room->askForCardChosen(player,damage.to,"hej",objectName(),false,Card::MethodRecast,ids);
					if(id<0) break;
					ids << id;
				}
				LogMessage log;
				log.type = "$RecastCard";
				log.from = damage.to;
				log.card_str = ListI2S(ids).join("+");
				room->sendLog(log);
				room->moveCardTo(dummyCard(ids), damage.to, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, player->objectName(), damage.to->objectName(), "duanyang",""));
				damage.to->drawCards(ids.length(), "recast");
				player->drawCards(4, "duanyang");
			}
		}else{
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				while(p->getPile("duanyang").length()>0
				&&room->askForUseCard(p,"@@duanyang","duanyang0",-1,Card::MethodUse,true,nullptr,nullptr,"duanyangUse")){
					
				}
			}
		}
		return false;
	}
};














class MobileXuehenvs : public OneCardViewAsSkill
{
public:
	MobileXuehenvs() : OneCardViewAsSkill("mobilexuehen")
	{
		response_pattern = "slash";
	}
	bool viewFilter(const Card *card) const
	{
		return card->hasTip("mobilexuehen");
	}

	const Card *viewAs(const Card *c) const
	{
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName(objectName());
		dc->addSubcard(c);
		return dc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}
};

class MobileXuehen : public TriggerSkill
{
public:
	MobileXuehen() : TriggerSkill("mobilexuehen")
	{
		events << Damage << Damaged << CardFinished;
		view_as_skill = new MobileXuehenvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.card->getSkillNames().contains(objectName())){
				if(player->getMark("mobilexuehenUp")>0)
					player->drawCards(1,objectName());
			}
		}else{
			if(event==Damage){
				DamageStruct damage = data.value<DamageStruct>();
				if(damage.card&&damage.card->getSkillNames().contains(objectName())){
					foreach (int id, player->handCards()) {
						room->setCardTip(id,"-mobilexuehen");
					}
				}
			}
			if(player->getMark("mobilexuehenDamage-Clear")>0){
				player->addMark("mobilexuehenDamage-Clear");
				int n = player->getLostHp();
				if(n>0&&player->hasSkill(objectName())&&player->getHandcardNum()>0){
					Card*dc = room->askForExchange(player,objectName(),n,1,false,QString("mobilexuehen0:%1").arg(n),true);
					if(dc){
						player->peiyin("xuehen");
						player->skillInvoked(objectName());
						room->showCard(player,dc->getSubcards());
						foreach (int id, dc->getSubcards()) {
							room->setCardTip(id,"mobilexuehen");
						}
					}
				}
			}
		}
		return false;
	}
};

class MobileXuehenbf : public CardLimitSkill
{
public:
	MobileXuehenbf() : CardLimitSkill("#MobileXuehenbf")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *,const Card *card) const
	{
		if (card->hasTip("mobilexuehen"))
			return card->toString();
		return "";
	}
};

MobileHuxiaoCard::MobileHuxiaoCard()
{
	mute = true;
}

bool MobileHuxiaoCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->getHp()>=Self->getHp();
}

bool MobileHuxiaoCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
	return true;
}

void MobileHuxiaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->peiyin("huxiao");
	if(targets.isEmpty()||subcardsLength()>0)
		room->setPlayerMark(source,"&mobilehuxiao-Clear",1);
	foreach (ServerPlayer *p, targets) {
		room->damage(DamageStruct("mobilehuxiao",source,p,1,DamageStruct::Fire));
	}
}

class MobileHuxiao: public ViewAsSkill
{
public:
	MobileHuxiao() : ViewAsSkill("mobilehuxiao")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *card) const
	{
		return cards.isEmpty()&&card->isRed()
		&&!Self->isJilei(card);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		MobileHuxiaoCard *sc = new MobileHuxiaoCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileHuxiaoCard")>0;
	}
};

MobileWujiCard::MobileWujiCard()
{
	target_fixed = true;
	mute = true;
}

void MobileWujiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->peiyin("wuji");
	room->removePlayerMark(source,"@mobilewuji");
	room->doSuperLightbox(source, "mobilewuji");
	source->addMark("mobilewujiRestore");
	room->setPlayerMark(source,"mobilexuehenUp",1);
	room->changeTranslation(source,"mobilexuehen",1);
}

class MobileWujivs : public ZeroCardViewAsSkill
{
public:
	MobileWujivs() : ZeroCardViewAsSkill("mobilewuji")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@mobilewuji")>0;
	}

	const Card *viewAs() const
	{
		return new MobileWujiCard;
	}
};

class MobileWuji : public TriggerSkill
{
public:
	MobileWuji() : TriggerSkill("mobilewuji")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new MobileWujivs;
		limit_mark = "@mobilewuji";
		frequency = Limited;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().from==Player::Play&&player->getMark("mobilewujiRestore")>0) {
				room->setPlayerMark(player,"mobilexuehenUp",0);
				player->setMark("mobilewujiRestore",0);
				room->changeTranslation(player,"mobilexuehen",0);
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&move.reason.m_skillName=="mobilexuehen") {
				player->addMark("mobilewujiNum",move.card_ids.length());
				if(player->getMark("mobilewujiNum")>1)
					player->setMark("mobilewujiRestore",0);
			}
		}
		return false;
	}
};

class MobileJieyuan : public TriggerSkillV2
{
public:
	MobileJieyuan() : TriggerSkillV2("mobilejieyuan") { events << DamageCaused << DamageInflicted; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const SkillInstanceRef ref = getUsageRef(ctx);
		ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
		if (!ref.isValid() || !holder) return false;
		const int disabled = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "disabled_event").toInt();
		if (disabled == event || !player->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
		const QString color = event == DamageCaused ? "black" : "red";
		const int magnitude = disabled > 0 ? 2 : 1;
		const QString gain = QString::number(event == DamageCaused ? 1 : 3) + "=" + QString::number(magnitude);
		const QString revise = QString::number(event == DamageCaused ? 2 : 4) + "=" + QString::number(magnitude);
		QStringList choices{gain};
		for (const Card *card : player->getCards("he")) {
			if (card->getColorString() != color || !player->canDiscard(card->getEffectiveId())) continue;
			choices << revise;
			if (disabled == 0) choices << "beishui";
			break;
		}
		const QString choice = room->askForChoice(player, objectName(), choices.join("+"), *ctx.original_data);
		if (!choices.contains(choice)) return false;
		ctx.targets = {player};
		ctx.extra_data = QVariantMap{{"color", color}, {"magnitude", magnitude}, {"gain", choice != revise},
			{"revise", choice != gain}, {"backwater", choice == "beishui"}, {"paid", false}};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		const auto run = [&](const QString &action, ServerPlayer *target) {
			QVariantMap saved = ctx.extra_data.toMap();
			saved.insert("action", action);
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			return target && target->isAlive() && skillEffect(event, room, player, ctx, target);
		};
		if (ctx.extra_data.toMap().value("gain").toBool()) run("gain", player);
		if (ctx.extra_data.toMap().value("revise").toBool()) run("discard", player);
		bool prevented = false;
		if (ctx.extra_data.toMap().value("paid").toBool())
			prevented = run("revise", ctx.original_data->value<DamageStruct>().to);
		if (ctx.extra_data.toMap().value("backwater").toBool()) run("transform", player);
		return prevented;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		const int magnitude = saved.value("magnitude").toInt() * getEffectiveAmount(ctx);
		if (action == "gain") {
			DummyCard cards;
			for (int id : room->getDrawPile()) {
				if (cards.subcardsLength() >= magnitude) break;
				if (Sanguosha->getCard(id)->getColorString() == saved.value("color").toString()) cards.addSubcard(id);
			}
			if (!cards.getSubcards().isEmpty()) room->obtainCard(target, &cards);
		} else if (action == "discard") {
			const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
			const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
			if (!before.value("complete").toBool() || skillEvent <= 0) return false;
			const QString color = saved.value("color").toString();
			const Card *discarded = room->askForDiscard(target, objectName(), 1, 1, false, true, "mobilejieyuan0:" + color, ".|" + color);
			if (!discarded) return false;
			const QList<int> ids = discarded->getSubcards();
			QVariantMap query{{"after", before.value("watermark")}, {"from", target->objectName()}};
			bool paid = false;
			for (;;) {
				const QVariantMap page = room->queryHistoryMoves(query);
				if (!page.value("complete").toBool()) return false;
				for (const QVariant &entry : page.value("items").toList()) {
					const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
					if (ids.contains(move.value("card_id").toInt())
						&& (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
						&& move.value("card_before").toMap().value(color).toBool()
						&& room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == skillEvent) paid = true;
				}
				if (paid || !page.value("has_more").toBool()) break;
				query.insert("after", page.value("next_after"));
				query.insert("watermark", page.value("watermark"));
			}
			// A refused or replaced discard never manufactures a damage modifier.
			saved.insert("paid", paid);
			ctx.extra_data = saved;
		} else if (action == "revise") {
			return target->damageRevises(*ctx.original_data, event == DamageCaused ? magnitude : -magnitude);
		} else if (action == "transform") {
			const SkillInstanceRef ref = getUsageRef(ctx);
			ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
			if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
			// Applied upgrades belong to this grant, including independently acquired copies.
			holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "disabled_event", event == DamageCaused ? DamageInflicted : DamageCaused);
			room->changeTranslation(holder, objectName(), event == DamageCaused ? 1 : 2, ref.key.instanceID);
		}
		return false;
	}
};

class MobileFenxin : public TriggerSkill
{
public:
	MobileFenxin() : TriggerSkill("mobilefenxin")
	{
		events << BeforeGameOverJudge << GameOverJudge;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isDead();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(event==BeforeGameOverJudge){
			if(player->hasShownRole()) return false;
			if(death.damage&&death.damage->from&&death.damage->from->isAlive()
			&&death.damage->from->askForSkillInvoke(objectName()+"$-1",data)){
				QString choice = "1";
				if(!death.damage->from->hasShownRole()) choice = "1+2";
				choice = room->askForChoice(death.damage->from,objectName(),choice,data);
				if(choice=="1"){
					death.damage->from->setTag("mobilefenxinTo", player->objectName());
				}else{
					QString pr = player->getRole();
					choice = death.damage->from->getRole();
					player->setRole(choice);
					death.damage->from->setRole(pr);
                    room->notifyProperty(player,player,"role",choice);
                    room->notifyProperty(death.damage->from,death.damage->from,"role",pr);
				}
			}
		}else{
			if(death.damage&&death.damage->from&&death.damage->from->isAlive()
			&&death.damage->from->getTag("mobilefenxinTo").toString()==player->objectName()){
				death.damage->from->removeTag("mobilefenxinTo");
				QStringList skills;
				foreach (const Skill*s, player->getGeneral()->getVisibleSkillList()) {
					if(s->isLimitedSkill()||s->isLordSkill()||s->isShiMingSkill()
						||s->getFrequency()==Wake||s->property("IgnoreInvalidity").toBool()) continue;
					skills << s->objectName();
				}
				room->handleAcquireDetachSkills(death.damage->from,skills);
			}
		}
		return false;
	}
};


mobileSpPackage::mobileSpPackage()
	: Package("mobile_sp")
{
	General *mobile_huangzhu = new General(this, "mobile_huangzhu", "qun", 4);
	mobile_huangzhu->addSkill(new Chizhang);
	mobile_huangzhu->addSkill(new ChizhangDistance);
	related_skills.insert("chizhang", "#chizhang-distance");
	mobile_huangzhu->addSkill(new Duanyang);







	
	General *new_mobile_maliang = new General(this, "new_mobile_maliang", "shu", 3);
	new_mobile_maliang->addSkill("zishu");
	new_mobile_maliang->addSkill(new MobileYingyuan);

	General *mobile_guanyinping = new General(this, "mobile_guanyinping", "shu", 3, false);
	mobile_guanyinping->addSkill(new MobileXuehen);
	mobile_guanyinping->addSkill(new MobileXuehenbf);
	mobile_guanyinping->addSkill(new MobileHuxiao);
	mobile_guanyinping->addSkill(new MobileWuji);
	addMetaObject<MobileHuxiaoCard>();
	addMetaObject<MobileWujiCard>();

	General *mobilesp_jiaxu = new General(this, "mobilesp_jiaxu", "wei", 3);
	mobilesp_jiaxu->addSkill("zhenlve");
	mobilesp_jiaxu->addSkill("jianshu");
	mobilesp_jiaxu->addSkill(new MobileYongdi);




	General *mobile_heqi = new General(this, "mobile_heqi", "wu", 4);
	mobile_heqi->addSkill(new MobileQizhou);
	mobile_heqi->addSkill(new MobileShanxi);
	mobile_heqi->addSkill(new MobileShanxiGet);
	related_skills.insert("mobileshanxi", "#mobileshanxi-get");
	addMetaObject<MobileShanxiCard>();

	General *tadun = new General(this, "tadun", "qun", 4);
	tadun->addSkill(new Luanzhan);
	tadun->addSkill(new LuanzhanTargetMod);
	related_skills.insert("luanzhan", "#luanzhan-target");
	addMetaObject<LuanzhanCard>();



	General *new_mayunlu = new General(this, "new_mayunlu*xh_nvshi", "shu", 4, false);
	new_mayunlu->addSkill(new NewFengpo);
	new_mayunlu->addSkill(new NewFengpoEffect);
	new_mayunlu->addSkill("mashu");
	related_skills.insert("newfengpo", "#newfengpo-effect");

	General *mazhong = new General(this, "mazhong", "shu", 4);
	mazhong->addSkill(new Fuman);
	mazhong->addSkill(new MobileActiveQuota("fuman"));
	related_skills.insert("fuman", "#fuman-quota");
	addMetaObject<FumanCard>();


	General *mobile_zhaoxiang = new General(this, "mobile_zhaoxiang", "shu", 4, false);
	mobile_zhaoxiang->addSkill("mobilefanghun");
	mobile_zhaoxiang->addSkill(new MobileFuhan);

	General *mobile_weiwenzhugezhi = new General(this, "mobile_weiwenzhugezhi", "wu", 4);
	mobile_weiwenzhugezhi->addSkill(new MobileFuhai);
	addMetaObject<MobileFuhaiCard>();

	General *sp_taishici = new General(this, "sp_taishici", "qun", 4);
	sp_taishici->addSkill(new Jixu);
	addMetaObject<JixuCard>();

	General *dongcheng = new General(this, "dongcheng", "qun", 4);
	dongcheng->addSkill(new Chengzhao);

	General *guansuo = new General(this, "guansuo", "shu", 4);
	guansuo->addSkill(new Xiefang);
	guansuo->addSkill(new Zhengnan);

	General *yangyi = new General(this, "yangyi", "shu", 3);
	yangyi->addSkill(new Duoduan);
	yangyi->addSkill(new Gongsun);
	addMetaObject<GongsunCard>();

	General *duji = new General(this, "duji", "wei", 3);
	duji->addSkill(new Andong);
	duji->addSkill(new AndongIgnore);
	related_skills.insert("andong", "#andong-ignore");
	duji->addSkill(new Yingshi);
	duji->addSkill(new YingshiDeath);
	related_skills.insert("yingshi", "#yingshi-death");
	addMetaObject<YingshiCard>();

	General *lvdai = new General(this, "lvdai", "wu", 4);
	lvdai->addSkill(new Qinguo);
	addMetaObject<QinguoCard>();

	General *liuyao = new General(this, "liuyao", "qun", 4);
	liuyao->addSkill(new Kannan);
	addMetaObject<KannanCard>();

	General *shixie = new General(this, "shixie*xh_sibi", "qun", 3);
	shixie->addSkill(new Biluan);
	shixie->addSkill(new BiluanDist);
	shixie->addSkill(new Lixia);
	related_skills.insert("biluan", "#biluan-dist");

	General *taoqian = new General(this, "taoqian", "qun", 3);
	taoqian->addSkill(new Zhaohuo);
	taoqian->addSkill(new Yixiang);
	taoqian->addSkill(new Yirang);

	General *fanchou = new General(this, "fanchou", "qun", 4);
	fanchou->addSkill(new Xingluan("xingluan"));
	skills << new Xingluan("tenyearxingluan");


	General *liuyan = new General(this, "liuyan", "qun", 3);
	liuyan->addSkill(new Tushe);
	liuyan->addSkill(new Limu);
	liuyan->addSkill(new LimuTargetMod);
	related_skills.insert("limu", "#limu-target");
	addMetaObject<LimuCard>();

	General *sp_zhangji = new General(this, "sp_zhangji*xh_tianzhu", "qun", 4);
	sp_zhangji->addSkill(new Lveming);
	sp_zhangji->addSkill(new Tunjun);
	addMetaObject<LvemingCard>();
	addMetaObject<TunjunCard>();

	General *mobile_shenpei = new General(this, "mobile_shenpei", "qun", 3, true, false, false, 2);
	mobile_shenpei->addSkill(new MobileShouye);
	mobile_shenpei->addSkill(new MobileShouyeGet);
	related_skills.insert("mobileshouye", "#mobileshouye-get");
	mobile_shenpei->addSkill(new MobileLiezhi);
	addMetaObject<MobileLiezhiCard>();

	General *zhangliang = new General(this, "zhangliang", "qun", 4);
	zhangliang->addSkill(new Jijun);
	zhangliang->addSkill(new Fangtong);
	addMetaObject<FangtongCard>();

	General *mobile_wangyun = new General(this, "mobile_wangyun", "qun", 4);
	mobile_wangyun->addSkill(new MobileLianji);
	mobile_wangyun->addSkill(new MobileMoucheng);
	mobile_wangyun->addRelateSkill("jingong");
	addMetaObject<MobileLianjiCard>();


	General *mobile_baosanniang = new General(this, "mobile_baosanniang", "shu", 3, false);
	mobile_baosanniang->addSkill(new Shuyong);
	mobile_baosanniang->addSkill(new MobileXushen);
	mobile_baosanniang->addSkill(new MoboleZhennan);
	addMetaObject<MobileXushenCard>();

	General *mobile_zhanggong = new General(this, "mobile_zhanggong", "wei", 3);
	mobile_zhanggong->addSkill(new MobileSpQianxin);
	mobile_zhanggong->addSkill(new MobileSpQianxinMove);
	mobile_zhanggong->addSkill(new MobileZhenxing);
	related_skills.insert("mobilespqianxin", "#mobilespqianxin-move");
	addMetaObject<MobileSpQianxinCard>();

	General *jiakui = new General(this, "jiakui", "wei", 3);
	jiakui->addSkill(new Zhongzuo);
	jiakui->addSkill(new Wanlan);
	jiakui->addSkill(new WanlanDamage);
	related_skills.insert("wanlan", "#wanlan-damage");

	General *new_jiakui = new General(this, "new_jiakui", "wei", 4);
	new_jiakui->addSkill(new Tongqu);
	new_jiakui->addSkill(new TongquTrigger);
	new_jiakui->addSkill(new NewWanlan);
	related_skills.insert("tongqu", "#tongqu-trigger");
	addMetaObject<TongquCard>();

	General *xugong = new General(this, "xugong", "wu", 3);
	xugong->addSkill(new Biaozhao);
	xugong->addSkill(new Yechou);
	xugong->addSkill(new YechouEffect);
	related_skills.insert("yechou", "#yechou-effect");

	General *mobile_sufei = new General(this, "mobile_sufei", "qun", 4);
	mobile_sufei->addSkill(new Zhengjian);
	mobile_sufei->addSkill(new Gaoyuan);
	addMetaObject<GaoyuanCard>();


	General *furong = new General(this, "furong", "shu", 4);
	furong->addSkill(new Xuewei);
	furong->addSkill(new Liechi);

	General *zhouqun = new General(this, "zhouqun", "shu", 3);
	zhouqun->addSkill(new Tiansuan);
	zhouqun->addSkill(new TiansuanEffect);
	related_skills.insert("tiansuan", "#tiansuan");
	addMetaObject<TiansuanCard>();

	General *sp_zhangyi = new General(this, "sp_zhangyi", "shu", 4);
	sp_zhangyi->addSkill(new Zhiyi);

	General *second_sp_zhangyi = new General(this, "second_sp_zhangyi", "shu", 4);
	second_sp_zhangyi->addSkill(new SecondZhiyi);
	second_sp_zhangyi->addSkill(new SecondZhiyiRecord);
	related_skills.insert("secondzhiyi", "#secondzhiyi-record");

	General *dengzhi = new General(this, "dengzhi", "shu", 3);
	dengzhi->addSkill(new Jimeng);
	dengzhi->addSkill(new Shuaiyan);

	General *dingyuan = new General(this, "dingyuan", "qun", 4);
	dingyuan->addSkill(new Beizhu);
	dingyuan->addSkill(new BeizhuDraw);
	related_skills.insertMulti("beizhu", "#beizhu-draw");
	addMetaObject<BeizhuCard>();

	General *gongsunkang = new General(this, "gongsunkang", "qun", 4);
	gongsunkang->addSkill(new Juliao);
	gongsunkang->addSkill(new Taomie);
	gongsunkang->addSkill(new TaomieMark);
	related_skills.insert("taomie", "#taomie-mark");

	General *hucheer = new General(this, "hucheer", "qun", 4);
	hucheer->addSkill(new Daoji);
	addMetaObject<DaojiCard>();

	General *chendeng = new General(this, "chendeng", "qun", 3);
	chendeng->addSkill(new Zhouxuan);
	chendeng->addSkill(new Fengji);
    chendeng->addSkill(new FengjiMax);
    related_skills.insert("fengji", "#fengji-max");
	addMetaObject<ZhouxuanCard>();


	General *maojie = new General(this, "maojie", "wei", 3);
	maojie->addSkill(new Bingqing);
	maojie->addSkill(new Yingfeng);
	maojie->addSkill(new YingfengTarget);
	related_skills.insert("yingfeng", "#yingfeng");

	General *mobile_lingju = new General(this, "mobile_lingju", "qun", 3, false);
	mobile_lingju->addSkill(new MobileJieyuan);
	mobile_lingju->addSkill(new MobileFenxin);

	General *yanpu = new General(this, "yanpu*xh_sibi", "qun", 3);
	yanpu->addSkill(new Huantu);
	yanpu->addSkill(new HuantuFinish);
	related_skills.insert("huantu", "#huantu-finish");
	yanpu->addSkill(new Bihuo);
	yanpu->addSkill(new BihuoDistance);
	related_skills.insert("bihuo", "#bihuo");

	General *mayuanyi = new General(this, "mayuanyi", "qun", 4);
	mayuanyi->addSkill(new Jibing);
	mayuanyi->addSkill(new Wangjing);
	mayuanyi->addSkill(new Moucuan);
	skills << new Binghuo;


	General *hujinding = new General(this, "hujinding", "shu", 6, false, false, false, 2);
	hujinding->addSkill(new Renshi("renshi"));
	hujinding->addSkill(new Wuyuan("wuyuan"));
	hujinding->addSkill(new Huaizi);
	skills << new Renshi("tenyeardeshi") << new Wuyuan("tenyearwuyuan");
	addMetaObject<WuyuanCard>();
	addMetaObject<TenyearWuyuanCard>();

	General *new_lifeng = new General(this, "new_lifeng", "shu", 3);
	new_lifeng->addSkill(new NewTunchu);
	new_lifeng->addSkill(new NewTunchuPut);
	new_lifeng->addSkill(new NewTunchuLimit);
	new_lifeng->addSkill(new NewShuliang);
	related_skills.insert("newtunchu", "#newtunchu-put");
	related_skills.insert("newtunchu", "#newtunchu-limit");
	addMetaObject<NewShuliangCard>();

	General *zhaotongzhaoguang = new General(this, "zhaotongzhaoguang", "shu", 4);
	zhaotongzhaoguang->addSkill(new Yizan);
	zhaotongzhaoguang->addSkill(new Longyuan);
	addMetaObject<YizanCard>();

	General *wangyuanji = new General(this, "wangyuanji", "wei", 3, false);
	wangyuanji->addSkill(new Qianchong);
	wangyuanji->addSkill(new QianchongTargetMod);
	wangyuanji->addSkill(new Shangjian);
	related_skills.insert("qianchong", "#qianchong-target");

	General *mobile_yanghuiyu = new General(this, "mobile_yanghuiyu", "wei", 3, false);
	mobile_yanghuiyu->addSkill(new Hongyi);
	mobile_yanghuiyu->addSkill(new Quanfeng);
	addMetaObject<HongyiCard>();

	General *liuye = new General(this, "liuye", "wei", 3);
	liuye->addSkill(new Polu("polu"));
	liuye->addSkill(new Choulve);
	liuye->addSkill(new ChoulveRecord);
	related_skills.insert("choulve", "#choulve-record");

	General *second_liuye = new General(this, "second_liuye", "wei", 3);
	second_liuye->addSkill(new Polu("secondpolu"));
	second_liuye->addSkill("choulve");

	General *simazhao = new General(this, "simazhao", "wei", 3);
	simazhao->addSkill(new Daigong);
	simazhao->addSkill(new SpZhaoxin);
	addMetaObject<SpZhaoxinCard>();
	addMetaObject<SpZhaoxinChooseCard>();

	General *second_zhuling = new General(this, "second_zhuling", "wei", 4);
	second_zhuling->addSkill(new SecondZhanyi);
	addMetaObject<SecondZhanyiViewAsBasicCard>();
	addMetaObject<SecondZhanyiCard>();

	General *fuqian = new General(this, "fuqian", "shu", 4);
	fuqian->addSkill(new Jueyong);
	fuqian->addSkill(new Poxiang);
	addMetaObject<PoxiangCard>();

	General *wangjun = new General(this, "wangjun", "qun", 4);
	wangjun->addSkill(new Zhujian);
	wangjun->addSkill(new Duansuo);
	addMetaObject<ZhujianCard>();
	addMetaObject<DuansuoCard>();

	General *mobile_mamidi = new General(this, "mobile_mamidi", "qun", 3);
	mobile_mamidi->addSkill(new Chengye);
	mobile_mamidi->addSkill(new Buxu);
	addMetaObject<BuxuCard>();

	General *ruanhui = new General(this, "ruanhui", "wei", 3, false);
	ruanhui->addSkill(new Mingcha);
	ruanhui->addSkill(new Jingzhong);

	General *yangbiao = new General(this, "yangbiao", "qun", 3);
	yangbiao->addSkill(new Zhaohan);
	yangbiao->addSkill(new Rangjie);
	yangbiao->addSkill(new Yizheng);
	addMetaObject<YizhengCard>();

	General *mobile_hansui = new General(this, "mobile_hansui", "qun", 4);
	mobile_hansui->addSkill(new MobileNiluan);
	mobile_hansui->addSkill(new MobileXiaoxi);

	General *nanshengmi = new General(this, "nanshengmi", "qun", 3);
	nanshengmi->addSkill(new Chijiec);
	nanshengmi->addSkill(new Waishi);
	nanshengmi->addSkill(new Renshe);
	addMetaObject<WaishiCard>();

	General *simafu = new General(this, "simafu", "wei", 3);
	simafu->addSkill(new Xunde);
	simafu->addSkill(new Chenjie);

	General *liyi = new General(this, "liyi", "shu", 4);
	liyi->addSkill(new jiaohua);
	addMetaObject<JiaohuaCard>();

	General *guonvwang = new General(this, "guonvwang", "wei", 3, false);
	guonvwang->addSkill(new Yichong);
	guonvwang->addSkill(new Wufei);

	General *qianzhao = new General(this, "qianzhao", "wei", 4);
	qianzhao->addSkill(new Shihe);
	qianzhao->addSkill(new Zhenfu);
	addMetaObject<ShiheCard>();

	General *mobile_chengui = new General(this, "mobile_chengui", "qun", 3);
	mobile_chengui->addSkill(new Guimou);
	mobile_chengui->addSkill(new Zhouxian);

	General *muludawang = new General(this, "muludawang", "qun" ,3);
	muludawang->setStartHujia(1);
	muludawang->addSkill(new Shoufa);
	muludawang->addSkill(new Zhoulin);
	muludawang->addSkill(new Yuxiang);
	muludawang->addSkill(new YuxiangDistance);
	related_skills.insert("yuxiang", "#yuxiang");
	addMetaObject<ZhoulinCard>();

	General *mobile_huban = new General(this, "mobile_huban", "wei", 4);
	mobile_huban->addSkill(new spYilie);

	General *mobile_jianggan = new General(this, "mobile_jianggan", "wei", 3);
	mobile_jianggan->addSkill(new MobileDaoshu);
	mobile_jianggan->addSkill(new Daizui);
	addMetaObject<MobileDaoshuCard>();

	General *laimin = new General(this, "laimin", "shu", 3);
	laimin->addSkill(new Laishou);
	laimin->addSkill(new Luanqun);
	laimin->addSkill(new LuanqunPro);
	addMetaObject<LuanqunCard>();

	General *mobile_xianglang = new General(this, "mobile_xianglang", "shu", 3);
	mobile_xianglang->addSkill(new Naxue);
	mobile_xianglang->addSkill(new Yijie);
	addMetaObject<NaxueCard>();

	General *yangfeng = new General(this, "yangfeng", "qun", 4);
	yangfeng->addSkill(new Xietu);
	yangfeng->addSkill(new Weiming);
	addMetaObject<XietuCard>();

	General *zhangbu = new General(this, "zhangbu", "wu", 4);
	zhangbu->addSkill(new Chengxiong);
	zhangbu->addSkill(new Wangzhuan);	

	General *mobile_zhangfen = new General(this, "mobile_zhangfen", "wu", 4);
	mobile_zhangfen->addSkill(new Quchong);
	mobile_zhangfen->addSkill(new Xunjie);
	addMetaObject<QuchongCard>();

	General *mobilesp_zhenji = new General(this, "mobilesp_zhenji", "qun", 3, false);
	mobilesp_zhenji->addSkill(new Bojian);
	mobilesp_zhenji->addSkill(new Jiwei);

	General *mobile_qinghe = new General(this, "mobile_qinghe", "wei", 3,false);
	mobile_qinghe->addSkill(new MZengou);
	mobile_qinghe->addSkill(new Feili);
	addMetaObject<MZengouCard>();

	General *wuke = new General(this, "wuke", "wu", 3,false);
	wuke->addSkill(new Zhuguo);
	wuke->addSkill(new Anda);
	addMetaObject<ZhuguoCard>();

	General *mobile_xingdaorong = new General(this, "mobile_xingdaorong", "qun", 4);
	mobile_xingdaorong->addSkill(new Kuangwu);

	General *mobile_zerong = new General(this, "mobile_zerong", "qun", 4);
	mobile_zerong->addSkill(new Futu);
	mobile_zerong->addSkill(new Jingtu);
	mobile_zerong->addSkill(new Jiebian);
	skills << new MobileFozhong;
	addMetaObject<JingtuCard>();
	addMetaObject<JiebianCard>();

	General *mobile_mengda = new General(this, "mobile_mengda", "qun", 4);
	mobile_mengda->addSkill(new Shishu);
	mobile_mengda->addSkill(new MobileJili);





}
ADD_PACKAGE(mobileSp)


class Beiming : public TriggerSkillV2
{
public:
	Beiming() : TriggerSkillV2("beiming") { events << GameStart; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 2, "beiming0:", true);
		return !ctx.targets.isEmpty();
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		QSet<int> suits;
		QVariantMap query{{"to", target->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) { qWarning("Beiming: incomplete initial-hand history"); return false; }
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap().value("data").toMap();
				if (fact.value("reason_skill").toString() != "InitialHandCards" || fact.value("to_place").toInt() != Player::PlaceHand) continue;
				const QVariantMap card = fact.value("card").toMap();
				if (!card.contains("suit")) { qWarning("Beiming: missing initial-hand suit"); return false; }
				suits << card.value("suit").toInt();
			}
			if (!page.value("has_more").toBool()) break;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
		for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
			int selected = -1;
			for (int id : room->getDrawPile()) {
				const Weapon *weapon = qobject_cast<const Weapon *>(Sanguosha->getCard(id)->getRealCard());
				if (weapon && weapon->getRange() == suits.size()) { selected = id; break; }
			}
			if (selected < 0) break;
			room->obtainCard(target, selected);
		}
		return false;
	}
};

class Choumang : public TriggerSkill
{
public:
	Choumang() : TriggerSkill("choumang")
	{
		events << TargetSpecified << TargetConfirmed;
		waked_skills = "#choumang_bf";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(player->getMark("choumangUse-Clear")>0||!player->hasTurn()) return false;
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.size()==1&&player->askForSkillInvoke(this,data)){
				room->addPlayerMark(player,"choumangUse-Clear");
				room->broadcastSkillInvoke(objectName());
				QStringList choices;
				choices << "choumang1" << "choumang2";
				if(player->getWeapon()||use.to.first()->getWeapon())
					choices << "choumang3";
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				if(choice=="choumang3"){
					const Card *w = player->getWeapon();
					if(w) room->throwCard(w,objectName(),player);
					w = use.to.first()->getWeapon();
					if(w) room->throwCard(w,objectName(),use.to.first(),player);
				}
				if(choice!="choumang2"){
					room->setCardFlag(use.card,"choumangDamage");
				}
				if(choice!="choumang1"){
					room->setCardFlag(use.card,"choumangJink");
					player->setFlags("choumangJink"+use.card->toString());
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.size()==1&&use.to.contains(player)&&player->askForSkillInvoke(this,data)){
				room->addPlayerMark(player,"choumangUse-Clear");
				room->broadcastSkillInvoke(objectName());
				QStringList choices;
				choices << "choumang1" << "choumang2";
				if(player->getWeapon()||use.to.first()->getWeapon())
					choices << "choumang3";
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				if(choice=="choumang3"){
					const Card *w = player->getWeapon();
					if(w) room->throwCard(w,objectName(),player);
					w = use.to.first()->getWeapon();
					if(w) room->throwCard(w,objectName(),use.to.first(),player);
				}
				if(choice!="choumang2"){
					room->setCardFlag(use.card,"choumangDamage");
				}
				if(choice!="choumang1"){
					room->setCardFlag(use.card,"choumangJink");
					player->setFlags("choumangJink"+use.card->toString());
				}
			}
		}
		return false;
	}
};

class ChoumangBf : public TriggerSkill
{
public:
	ChoumangBf() : TriggerSkill("#choumang_bf")
	{
		events << ConfirmDamage << CardOffset;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (triggerEvent == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card&&damage.card->hasFlag("choumangDamage")) {
				damage.damage++;
				data.setValue(damage);
			}
		}else if(triggerEvent==CardOffset){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->hasFlag("choumangJink")&&effect.offset_card->isKindOf("Jink")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasFlag("choumangJink"+effect.card->toString())){
						p->setFlags("-choumangJink"+effect.card->toString());
						QList<ServerPlayer *>tos;
						foreach (ServerPlayer *q, room->getAlivePlayers()){
							if(p->distanceTo(q)==1&&q->getCardCount()>0)
								tos << q;
						}
						ServerPlayer *to = room->askForPlayerChosen(p,tos,"choumang","choumang_bf0:",true);
						if(to){
							int id = room->askForCardChosen(p,to,"hej","choumang");
							if(id>-1) room->obtainCard(p,id,false);
						}
					}
				}
			}
		}
		return false;
	}
};

class Bifeng : public TriggerSkill
{
public:
	Bifeng() : TriggerSkill("bifeng")
	{
		events << TargetConfirming << CardFinished << PreCardResponded;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetConfirming) {
			CardUseStruct use = data.value<CardUseStruct>();
			if ((use.card->isKindOf("BasicCard")||use.card->isNDTrick())&&use.to.length()<=4) {
				if(player->isAlive()&&player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
					room->setCardFlag(use.card,"bifengUse");
					use.nullified_list << player->objectName();
					player->setFlags("bifengUse"+use.card->toString());
					data.setValue(use);
				}
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("bifengUse")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasFlag("bifengUse"+use.card->toString())){
						p->setFlags("-bifengUse"+use.card->toString());
						if(use.card->hasFlag("bifengUseWho"))
							p->drawCards(2,objectName());
						else
							room->loseHp(p,1,true,p,objectName());
					}
				}
			}else if(use.whocard&&use.whocard->hasFlag("bifengUse")){
				room->setCardFlag(use.whocard,"bifengUseWho");
			}
		}else{
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(res.m_toCard&&res.m_toCard->hasFlag("bifengUse")){
				room->setCardFlag(res.m_toCard,"bifengUseWho");
			}
		}
		return false;
	}
};

class Suwang : public TriggerSkillV2
{
public:
	Suwang() : TriggerSkillV2("suwang") { events << EventPhaseChanging << EventPhaseProceeding; }
	static int eligible(Room *room, ServerPlayer *owner, ServerPlayer *actor)
	{
		const QVariant turn = room->historyScopes().value("turn_id");
		if (turn.toLongLong() <= 0 || !actor) return -1;
		const QVariantMap damage = room->queryActualDamage({{"turn_id", turn}, {"to", owner->objectName()}, {"limit", 1}});
		if (!damage.value("complete").toBool()) return -1;
		if (!damage.value("items").toList().isEmpty()) return 0;
		QVariantMap query{{"kind", "use_card_targets"}, {"turn_id", turn}, {"from", actor->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap().value("data").toMap();
				const QVariantMap card = fact.value("card").toMap();
				if (!card.contains("type") || !fact.contains("targets")) return -1;
				if (card.value("type").toInt() == Card::TypeSkill) continue;
				for (const QVariant &target : fact.value("targets").toList()) if (target.toString() == owner->objectName()) return 1;
			}
			if (!page.value("has_more").toBool()) return 0;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		TriggerList result;
		if (event == EventPhaseProceeding) {
			if (player && player->isAlive() && player->getPhase() == Player::Draw && player->hasSkill(objectName()) && !player->getPile("suwang").isEmpty()) result[player] << objectName();
			return result;
		}
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
		for (ServerPlayer *owner : room->getAlivePlayers()) {
			if (!owner->hasSkill(objectName())) continue;
			const int ready = eligible(room, owner, player);
			if (ready < 0) qWarning("Suwang: incomplete turn history");
			if (ready == 1) result[owner] << objectName();
		}
		return result;
	}
	bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = {player};
		ctx.is_forced = event == EventPhaseChanging;
		return ctx.is_forced || (!player->getPile("suwang").isEmpty() && player->askForSkillInvoke(this, "suwang0:"));
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == EventPhaseChanging) return false;
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		ctx.extra_data = QVariantMap{{"action", "obtain"}};
		skillEffect(event, room, player, ctx, player);
		const QVariantMap saved = ctx.extra_data.toMap();
		if (saved.value("gained").toInt() >= 3 && player->isAlive()) {
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "suwang1", true);
			if (target) {
				ctx.extra_data = QVariantMap{{"action", "draw"}};
				ctx.modified_amount = amount;
				ctx.modified_amount_set = modified;
				skillEffect(event, room, player, ctx, target);
			}
		}
		return saved.value("replace").toBool();
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event == EventPhaseChanging) {
			const int count = getEffectiveAmount(ctx);
			if (count > 0) target->addToPile("suwang", room->getNCards(count));
			return false;
		}
		if (ctx.extra_data.toMap().value("action").toString() == "draw") {
			target->drawCards(2 * getEffectiveAmount(ctx), objectName());
			return false;
		}
		const QList<int> ids = target->getPile("suwang");
		if (ids.isEmpty()) return false;
		QVariantMap saved = ctx.extra_data.toMap();
		saved.insert("replace", true);
		ctx.extra_data = saved;
		const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
		const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
		DummyCard cards(ids);
		room->obtainCard(target, &cards);
		if (!before.value("complete").toBool() || skillEvent <= 0) return false;
		QSet<int> gained;
		QVariantMap query{{"after", before.value("watermark")}, {"to", target->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) return false;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
				const int id = move.value("card_id").toInt();
				if (ids.contains(id) && move.value("to_place").toInt() == Player::PlaceHand
					&& room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == skillEvent) gained.insert(id);
			}
			if (!page.value("has_more").toBool()) break;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
		// Count cards actually obtained even if a nested effect immediately moves them again.
		saved.insert("gained", gained.size());
		ctx.extra_data = saved;
		return false;
	}
};

XiezhengCard::XiezhengCard()
{
	handling_method = Card::MethodUse;
}

bool XiezhengCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	Card*dc = Sanguosha->cloneCard("_ov_binglinchengxia");
	dc->setSkillName("xiezheng");
	dc->deleteLater();
	if(targets.isEmpty()){
		if(dc->targetFilter(targets,to,Self)){
			if(to->getKingdom()==Self->getKingdom()||Self->getMark("ZXChangeXiezheng")>0) return true;
			foreach (const Player *p, to->getAliveSiblings()){
				if(to->getKingdom()==p->getKingdom()&&dc->targetFilter(targets,p,Self)) return false;
			}
			return true;
		}
		return false;
	}
	return dc->targetFilter(targets,to,Self);
}

const Card *XiezhengCard::validate(CardUseStruct &) const
{
	Card*dc = Sanguosha->cloneCard("_ov_binglinchengxia");
	dc->setSkillName("_xiezheng");
	dc->deleteLater();
	return dc;
}

class XiezhengVs : public ZeroCardViewAsSkill
{
public:
	XiezhengVs() : ZeroCardViewAsSkill("xiezheng")
	{
		response_pattern = "@@xiezheng";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		return new XiezhengCard;
	}
};

class Xiezheng : public TriggerSkill
{
public:
	Xiezheng() : TriggerSkill("xiezheng")
	{
		events << EventPhaseStart << DamageDone;
		view_as_skill = new XiezhengVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Finish&&player->hasSkill(objectName())) {
				if(room->getMode()=="03_1v2"&&player->getMark("xiezhengUse")>0) return false;
				int n = room->getMode()=="03_1v2"?2:1;
				QList<ServerPlayer *>tps = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,n,"xiezheng0",true);
				if(tps.length()>0){
					player->peiyin("xingxiezheng");
					player->addMark("xiezhengUse");
					room->removeTag("xiezhengDamage");
					room->setPlayerMark(player,"xiezhengMode",n);
					foreach (ServerPlayer *p, tps){
						n = p->getRandomHandCardId();
						if(n>-1) room->moveCardTo(Sanguosha->getCard(n),nullptr,Player::DrawPile,false);
					}
					if(player->isAlive()){
						room->askForUseCard(player,"@@xiezheng","xiezheng1");
						if(room->getTag("xiezhengDamage").toBool()) return false;
						room->loseHp(player,1,true,player,objectName());
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash"))
				room->setTag("xiezhengDamage",true);
		}
		return false;
	}
};

QiantunCard::QiantunCard()
{
	handling_method = Card::MethodUse;
	mute = true;
}

bool QiantunCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->getHandcardNum()>0&&to!=Self;
}

void QiantunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->peiyin("xingqiantun");
	foreach (ServerPlayer *p, targets) {
		Card*sc = room->askForExchange(p,"qiantun",998,1,false,"qiantun0");
		p->removeTag("qiantunCard");
		if(sc){
			room->showCard(p,sc->getSubcards());
			p->setTag("qiantunCard", QVariant::fromValue(sc));
			if(source->canPindian(p)){
				if(source->pindian(p,"qiantun")){
					if(source->isDead()) continue;
					Card*dc = dummyCard();
					if(room->getMode()=="03_1v2"){
						QList<int>ids;
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id))
								ids << id;
						}
						if(ids.length()<3){
							dc->addSubcards(ids);
						}else{
							room->fillAG(ids,source);
							for (int i = 0; i < 2; i++) {
								int id = room->askForAG(source,ids,false,"qiantun");
								room->takeAG(source,id,false,QList<ServerPlayer*>()<<source);
								dc->addSubcard(id);
								ids.removeOne(id);
							}
							room->clearAG(source);
						}
					}else{
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id))
								dc->addSubcard(id);
						}
					}
					source->obtainCard(dc);
				}else{
					if(source->isDead()) continue;
					Card*dc = dummyCard();
					if(room->getMode()=="03_1v2"){
						QList<int>ids;
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id)) continue;
							ids << id;
						}
						if(ids.length()<3){
							dc->addSubcards(ids);
						}else{
							room->fillAG(ids,source);
							for (int i = 0; i < 2; i++) {
								int id = room->askForAG(source,ids,false,"qiantun");
								room->takeAG(source,id,false,QList<ServerPlayer*>()<<source);
								dc->addSubcard(id);
								ids.removeOne(id);
							}
							room->clearAG(source);
						}
					}else{
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id)) continue;
							dc->addSubcard(id);
						}
					}
					source->obtainCard(dc);
				}
			}
			room->showAllCards(source);
		}
	}
}

class QiantunVs : public ZeroCardViewAsSkill
{
public:
	QiantunVs() : ZeroCardViewAsSkill("qiantun")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("QiantunCard")<1&&player->getKingdom()=="wei";
	}

	const Card *viewAs() const
	{
		return new QiantunCard;
	}
};

class Qiantun : public TriggerSkill
{
public:
	Qiantun() : TriggerSkill("qiantun")
	{
		events << AskforPindianCard;
		view_as_skill = new QiantunVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (triggerEvent == AskforPindianCard) {
			PindianStruct *pd = data.value<PindianStruct*>();
			if(pd->reason==objectName()){
				const Card*sc = pd->to->getTag("qiantunCard").value<const Card*>();
				if(sc){
					sc = room->askForExchange(pd->to,objectName(),1,1,false,"qiantun1",false,ListI2S(sc->getSubcards()).join(","));
					if(sc){
						pd->to_card = sc;
						data.setValue(pd);
					}
				}
			}
		}
		return false;
	}
};




WeisiCard::WeisiCard()
{
	handling_method = Card::MethodUse;
	mute = true;
}

bool WeisiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self;
}

void WeisiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->peiyin("xingweisi");
	foreach (ServerPlayer *p, targets) {
		const Card*sc = room->askForExchange(p,"weisi",998,1,false,"weisi0",true);
		if(sc) p->addToPile("weisi",sc,false);
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("_weisi");
		source->addMark(p->objectName()+"weisiUse-PlayClear");
		if(source->canUse(dc,p))
			room->useCard(CardUseStruct(dc,source,p));
	}
}

class WeisiVs : public ZeroCardViewAsSkill
{
public:
	WeisiVs() : ZeroCardViewAsSkill("weisi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("WeisiCard")<1&&player->getKingdom()=="qun";
	}

	const Card *viewAs() const
	{
		return new WeisiCard;
	}
};

class Weisi : public TriggerSkill
{
public:
	Weisi() : TriggerSkill("weisi")
	{
		events << Damage << EventPhaseChanging;
		view_as_skill = new WeisiVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getSkillNames().contains(objectName())){
				if(player->getMark(damage.to->objectName()+"weisiUse-PlayClear")>0){
					if(room->getMode().contains("p"))
						player->obtainCard(dummyCard(damage.to->handCards()),false);
					else if(damage.to->getHandcardNum()>0){
						int id = room->askForCardChosen(player,damage.to,"h",objectName());
						room->obtainCard(player,id,false);
					}
				}
			}
		}else{
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->getPile("weisi").length()>0){
						p->obtainCard(dummyCard(p->getPile("weisi")),false);
					}
				}
			}
		}
		return false;
	}
};

class Zhaoxiong : public TriggerSkill
{
public:
	Zhaoxiong() : TriggerSkill("zhaoxiong")
	{
		events << EventPhaseStart;
		frequency = Limited;
		limit_mark = "@zhaoxiong";
		setProperty("IgnoreInvalidity",true);
		waked_skills = "dangyi";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->isWounded()&&player->getMark("@zhaoxiong")>0){
				if(player->askForSkillInvoke(this)){
					player->peiyin("xingzhaoxiong");
					room->doSuperLightbox(player, "zhaoxiong");
					room->removePlayerMark(player, "@zhaoxiong");
					room->changeKingdom(player,"qun");
					room->acquireSkill(player,"dangyi");
					if(room->getMode().contains("p")){
						room->changeTranslation(player,"xiezheng",1);
						room->addPlayerMark(player,"ZXChangeXiezheng");
					}
					if(player->getGeneralName().contains("simazhao"))
						player->setAvatarIcon("xing_simazhao2");
				}
			}
		}
		return false;
	}
};

class Dangyi : public TriggerSkillV2
{
public:
	Dangyi() : TriggerSkillV2("dangyi$")
	{
		events << DamageCaused << EventSkillInvoking;
		setProperty("IgnoreInvalidity", true);
	}
	LimitScope getLimitScope() const override { return Limit_Game; }
	int getMaxUsageLimit(const SkillContext &) const override { return 2; }
	bool isUsable(const SkillContext &ctx) const override
	{
		if (!TriggerSkillV2::isUsable(ctx) || !ctx.owner) return false;
		const SkillInstanceRef ref = getUsageRef(ctx);
		Room *room = ctx.owner->getRoom();
		ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
		const QVariant turn = room->historyScopes().value("turn_id");
		return holder && turn.toLongLong() > 0
			&& holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_turn") != turn;
	}
	void addUsage(const SkillContext &ctx) const override
	{
		TriggerSkillV2::addUsage(ctx);
		const SkillInstanceRef ref = getUsageRef(ctx);
		Room *room = ctx.owner->getRoom();
		ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
		if (!holder) return;
		holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_turn", room->historyScopes().value("turn_id"));
		// This public mark is a display; the grant's game quota and exact turn are authoritative.
		room->addPlayerMark(holder, "&dangyi");
	}
	void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != DamageCaused || !player || !player->isAlive() || !player->hasTurn() || !player->hasLordSkill(this, true)) return {};
		const DamageStruct damage = data.value<DamageStruct>();
		return damage.to && damage.to->isAlive() && damage.damage > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (!ctx.owner->askForSkillInvoke(this, damage.to)) return false;
		ctx.targets = {damage.to};
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (damage.to != target) return false;
		ctx.owner->peiyin("xingdangyi");
		ctx.owner->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
		return false;
	}
};




mobileXhPackage::mobileXhPackage()
	: Package("mobile_xh")
{





	General *xuanguanqiujian = new General(this, "xuanguanqiujian", "wei", 4);
	xuanguanqiujian->addSkill(new Cuizhen);
	xuanguanqiujian->addSkill(new Kuili);

	General *lizhaojiaobo = new General(this, "lizhaojiaobo", "wei", 4);
	lizhaojiaobo->addSkill(new Zuoyou);
	lizhaojiaobo->addSkill(new ShishouLJ);
	addMetaObject<ZuoyouCard>();

	General *mobile_caomao = new General(this, "mobile_caomao$", "wei", 3);
	mobile_caomao->addSkill(new MobileQianlong);
	mobile_caomao->addSkill(new Weitong);
	skills << new QlQingzheng << new QlJiushi << new QlFangzhu << new QlJuejin << new QlFangzhuBf;
	addMetaObject<QlQingzhengCard>();
	addMetaObject<QlFangzhuCard>();
	addMetaObject<QlJuejinCard>();

	General *chengji = new General(this, "chengji", "wei", 4);
	chengji->addSkill(new Kuangli);
	chengji->addSkill(new Xiongshi);
	addMetaObject<XiongshiCard>();

	General *nsw_simafu = new General(this, "nsw_simafu", "wei", 3);
	nsw_simafu->addSkill(new Pmobileanxiang);
	nsw_simafu->addSkill(new NewChenjie);

	General *mobile_wangjing = new General(this, "mobile_wangjing", "wei", 4);
	mobile_wangjing->addSkill(new Zhujin);
	mobile_wangjing->addSkill(new MobileActiveQuota("zhujin"));
	related_skills.insert("zhujin", "#zhujin-quota");
	mobile_wangjing->addSkill(new Jiejian);
	addMetaObject<JiejianCard>();

	General *mobile_wenqin = new General(this, "mobile_wenqin", "wei", 4);
	mobile_wenqin->addSkill(new Beiming);
	mobile_wenqin->addSkill(new Choumang);
	mobile_wenqin->addSkill(new ChoumangBf);

	General *mobile_simazhou = new General(this, "mobile_simazhou", "wei", 4);
	mobile_simazhou->addSkill(new Bifeng);
	mobile_simazhou->addSkill(new Suwang);

	General *mobile_simazhao = new General(this, "mobile_simazhao", "wei", 4);
	mobile_simazhao->addSkill(new Xiezheng);
	mobile_simazhao->addSkill(new Qiantun);
	mobile_simazhao->addSkill(new Weisi);
	mobile_simazhao->addSkill(new Zhaoxiong);
	addMetaObject<XiezhengCard>();
	addMetaObject<QiantunCard>();
	addMetaObject<WeisiCard>();
	skills << new Dangyi;



}
ADD_PACKAGE(mobileXh)





BsHanzhanCard::BsHanzhanCard()
{
}

bool BsHanzhanCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self;
}

void BsHanzhanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	int n = source->getMaxHp();
	QString xs = source->getTag("bshanzhanXzhenfeng").toString();
	if(xs.contains("2hp")) n = source->getHp();
	else if(xs.contains("2lp")) n = source->getLostHp();
	else if(xs.contains("2ap")) n = source->aliveCount();
	n -= source->getHandcardNum();
	if(n>0) source->drawCards(qMin(n,3),"bshanzhan");
	foreach (ServerPlayer *p, targets) {
		n = p->getMaxHp();
		if(xs.contains("2hp")) n = p->getHp();
		else if(xs.contains("2lp")) n = p->getLostHp();
		else if(xs.contains("2ap")) n = p->aliveCount();
		n -= p->getHandcardNum();
		if(n>0) p->drawCards(qMin(n,3),"bshanzhan");
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("_bshanzhan");
		if(source->canUse(dc,p))
			room->useCard(CardUseStruct(dc,source,p));
		dc->deleteLater();
	}
}

class BsHanzhan : public ZeroCardViewAsSkill
{
public:
	BsHanzhan() : ZeroCardViewAsSkill("bshanzhan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsHanzhanCard")<1;
	}

	const Card *viewAs() const
	{
		return new BsHanzhanCard;
	}
};

class Zhanlie : public TriggerSkill
{
public:
	Zhanlie() : TriggerSkill("zhanlie")
	{
		events << EventPhaseChanging << CardFinished << TargetSpecified
		<< ConfirmDamage << CardsMoveOneTime << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().from==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						int n = p->getAttackRange();
						QString xs = p->getTag("bshanzhanXzhenfeng").toString();
						if(xs.contains("2hp")) n = p->getHp();
						else if(xs.contains("2lp")) n = p->getLostHp();
						else if(xs.contains("2ap")) n = p->aliveCount();
						room->setPlayerMark(p,"&zhanlie-Clear",n);
					}
				}
			}
		}else if(triggerEvent==EventPhaseEnd){
			int n = player->getMark("&zhan_lie");
			if(n>0&&player->getPhase()==Player::Play&&player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				player->loseMark("&zhan_lie",n);
				player->setMark("zhan_lie",n);
				QList<ServerPlayer *>aps;
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_zhanlie");
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(player->canSlash(p,dc)) aps << p;
				}
				int x = 1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, dc);
				QList<ServerPlayer *>tps = room->askForPlayersChosen(player,aps,objectName(),1,x,"zhanlie0");
				if(tps.length()>0){
					if(n>2){
						n = n/3;
						QStringList choices;
						if(aps.length()>tps.length()) choices << "";
						choices << "zhanlie2" << "zhanlie3" << "zhanlie4";
						for (int i = 0; i < n; i++) {
							QString choice = room->askForChoice(player,objectName(),choices.join("+"));
							if(choice=="cancel") break;
							room->setCardFlag(dc,choice);
							choices.removeOne(choice);
							if(!choices.contains("cancel"))
								choices.append("cancel");
							if(choice=="zhanlie1"){
								foreach (ServerPlayer *p, aps){
									if(tps.contains(p)) aps.removeOne(p);
								}
								ServerPlayer *tp = room->askForPlayerChosen(player,aps,objectName(),"zhanlie10");
								if(tp) tps << tp;
							}
						}
					}
					room->useCard(CardUseStruct(dc,player,tps));
				}
				dc->deleteLater();
			}
		}else if(triggerEvent==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				if(use.card->hasFlag("zhanlie3")){
					foreach (ServerPlayer *p, use.to){
						if(room->askForCard(p,"..","zhanlie30",data)) continue;
						use.no_respond_list << p->objectName();
					}
				}
				data.setValue(use);
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				if(use.card->hasFlag("zhanlie4")){
					player->drawCards(2,objectName());
				}
			}
		}else if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&player->getMark("&zhanlie-Clear")>0){
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getEngineCard(id);
					if(c->isKindOf("Slash")){
						room->removePlayerMark(player,"&zhanlie-Clear");
						if(room->getCardPlace(id)==move.to_place&&player->getMark("&zhan_lie")<6){
							room->sendCompulsoryTriggerLog(player,objectName());
							player->gainMark("&zhan_lie");
						}
					}
				}
			}
		}else if(triggerEvent==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getSkillNames().contains(objectName())){
				if(damage.card->hasFlag("zhanlie2")){
					player->damageRevises(data,1);
				}
			}
		}
		return false;
	}
};

ZhenfengCard::ZhenfengCard()
{
	target_fixed = true;
}

void ZhenfengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->doSuperLightbox(source, "zhenfeng");
	room->removePlayerMark(source, "@zhenfeng");
	QStringList choices;
	if(source->isWounded())
		choices << "zhenfeng1";
	if(source->hasSkill("bshanzhan",true))
		choices << "zhenfeng2hp=bshanzhan" << "zhenfeng2lp=bshanzhan" << "zhenfeng2ap=bshanzhan";
	if(source->hasSkill("zhanlie",true))
		choices << "zhenfeng2hp=zhanlie" << "zhenfeng2lp=zhanlie" << "zhenfeng2ap=zhanlie";
	if(choices.length()>0){
		for (int i = 0; i < 2; i++) {
			QString choice = room->askForChoice(source,"zhenfeng",choices.join("+"));
			if(choice=="zhenfeng1"){
				room->recover(source,RecoverStruct("zhenfeng",source,2));
				break;
			}else
				choices.removeOne("zhenfeng1");
			QStringList ms = choice.split("=");
			foreach (QString m, choices){
				if(m.contains(ms.last()))
					choices.removeOne(m);
			}
			source->setTag(ms.last()+"Xzhenfeng", ms.first());
		}
	}
}

class Zhenfeng : public ZeroCardViewAsSkill
{
public:
	Zhenfeng() : ZeroCardViewAsSkill("zhenfeng")
	{
		frequency = Limited;
		limit_mark = "@zhenfeng";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@zhenfeng")>0;
	}

	const Card *viewAs() const
	{
		return new ZhenfengCard;
	}
};

DaozhuanCard::DaozhuanCard()
{
	m_skillName = "daozhuan";
	handling_method = Card::MethodUse;
}

bool DaozhuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool DaozhuanCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool DaozhuanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card *DaozhuanCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();

	QStringList list;
	foreach (QString pn, user_string.split("+")){
		if(use.from->getMark("daozhuan_guhuo_remove_"+pn+"_lun")<1)
			list << pn;
	}
	QString to_pn = room->askForChoice(use.from, "daozhuan", list.join("+"));
	room->addPlayerMark(use.from,"daozhuan_guhuo_remove_"+to_pn+"_lun");
	int x = 0,n = use.from->getMark("daozhuanType-Clear");
	if(n>0){
		ServerPlayer *cp = room->getCurrent();
		if(cp!=use.from){
			const Card*sc = room->askForExchange(use.from,"daozhuan",n,1,true,"daozhuan0:"+QString::number(n),true);
			Card*dc = dummyCard();
			if(sc){
				x = n-sc->subcardsLength();
				dc->addSubcards(sc->getSubcards());
			}else x = n;
			for (int i = 0; i < x; i++) {
				int id = room->askForCardChosen(use.from,cp,"he","daozhuan",false,Card::MethodNone,dc->getSubcards());
				if(id<0) break;
				dc->addSubcard(id);
			}
			room->throwCard(dc,objectName(),nullptr);
		}else{
			const Card*sc = room->askForExchange(use.from,"daozhuan",n,n,true,"daozhuan1:"+QString::number(n));
			if(sc) room->throwCard(sc,objectName(),nullptr);
			room->addPlayerMark(use.from,"daozhuanBan_lun");
			x = n;
		}
	}
	if(x>=n-1)
		room->addPlayerMark(use.from,"daozhuanBan_lun");

	Card *use_card = Sanguosha->cloneCard(to_pn);
	use_card->setSkillName("daozhuan");
	use_card->deleteLater();
	return use_card;
}

const Card *DaozhuanCard::validateInResponse(ServerPlayer *from) const
{
	Room *room = from->getRoom();

	QStringList list;
	foreach (QString pn, user_string.split("+")){
		if(from->getMark("daozhuan_guhuo_remove_"+pn+"_lun")<1)
			list << pn;
	}
	QString to_pn = room->askForChoice(from, "daozhuan", list.join("+"));
	room->addPlayerMark(from,"daozhuan_guhuo_remove_"+to_pn+"_lun");
	int x = 0,n = from->getMark("daozhuanType-Clear");
	if(n>0){
		ServerPlayer *cp = room->getCurrent();
		if(cp!=from){
			const Card*sc = room->askForExchange(from,"daozhuan",n,1,true,"daozhuan0:"+QString::number(n),true);
			Card*dc = dummyCard();
			if(sc){
				dc->addSubcards(sc->getSubcards());
				x = n-sc->subcardsLength();
			}else x = n;
			for (int i = 0; i < x; i++) {
				int id = room->askForCardChosen(from,cp,"he","daozhuan",false,Card::MethodNone,dc->getSubcards());
				if(id<0) break;
				dc->addSubcard(id);
			}
			room->throwCard(dc,objectName(),nullptr);
		}else{
			const Card*sc = room->askForExchange(from,"daozhuan",n,n,true,"daozhuan1:"+QString::number(n));
			if(sc) room->throwCard(sc,objectName(),nullptr);
			room->addPlayerMark(from,"daozhuanBan_lun");
			x = n;
		}
	}
	if(x>=n-1)
		room->addPlayerMark(from,"daozhuanBan_lun");

	Card *use_card = Sanguosha->cloneCard(to_pn);
	use_card->setSkillName("daozhuan");
	use_card->deleteLater();
	return use_card;
}

class DaozhuanVs : public ZeroCardViewAsSkill
{
public:
	DaozhuanVs() : ZeroCardViewAsSkill("daozhuan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if(player->getMark("daozhuanBan_lun")>0) return false;
		int n = player->getMark("daozhuanType-Clear");
		foreach (const Player *p, player->getAliveSiblings(true)){
			if(p->hasFlag("CurrentPlayer")){
				if(p==player) return p->getCardCount()>=n;
				return p->getCardCount()+player->getCardCount()>=n;
			}
		}
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		&&isEnabledAtPlay(player)){
			foreach (QString pn, pattern.split("+")){
				if(player->getMark("daozhuan_guhuo_remove_"+pn+"_lun")<1){
					Card*dc = Sanguosha->cloneCard(pn);
					if(dc){
						dc->deleteLater();
						if(dc->isKindOf("BasicCard"))
							return true;
					}
				}
			}
		}
		return false;
	}

	const Card *viewAs() const
	{
		QString pn = Sanguosha->getCurrentCardUsePattern();
		if(pn.isEmpty()){
			const Card *c = Self->getTag(objectName()).value<const Card *>();
			if(c) pn = c->objectName();
			else return nullptr;
		}
		DaozhuanCard *sc = new DaozhuanCard;
		sc->setUserString(pn);
		return sc;
	}
};

class Daozhuan : public TriggerSkill
{
public:
	Daozhuan() : TriggerSkill("daozhuan")
	{
		events << PreCardUsed;
		view_as_skill = new DaozhuanVs;
	}
	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::guhuo(objectName(), true, false);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark(use.card->getType()+"daozhuanType-Clear")<1){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					player->addMark(use.card->getType()+"daozhuanType-Clear");
					room->addPlayerMark(p,"daozhuanType-Clear");
				}
			}
		}
		return false;
	}
};

FujiCard::FujiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool FujiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length()<subcardsLength()&&to_select!=Self;
}

bool FujiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == subcardsLength();
}

void FujiCard::onUse(Room *room, CardUseStruct &use) const
{
	room->setTag("FujiData",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void FujiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int i = 0;
	room->showCard(source,getSubcards());
	CardUseStruct use = room->getTag("FujiData").value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to){
		if(p->isAlive()){
			const Card*c = Sanguosha->getCard(subcards.at(i));
			room->giveCard(source,p,c,"fuji",true);
			if(p->handCards().contains(c->getId())){
				room->setCardTip(c->getId(),"fuji");
				room->setCardFlag(c,"fujiF"+source->objectName());
			}
		}
		i++;
	}
	foreach (ServerPlayer *p, room->getAlivePlayers()){
		if(p->getHandcardNum()<source->getHandcardNum()) return;
	}
	if(source->getGeneralName().endsWith("yuji"))
		source->setAvatarIcon("mobilebs_yuji2");
	source->drawCards(1,objectName());
	room->setPlayerMark(source,"&fuji",1);
}

class FujiVs : public ViewAsSkill
{
public:
	FujiVs() : ViewAsSkill("fuji")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *) const
	{
		return cards.length()<=Self->getAliveSiblings().length();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		FujiCard *sc = new FujiCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("FujiCard")<1;
	}
};

class Fuji : public TriggerSkill
{
public:
	Fuji() : TriggerSkill("fuji")
	{
		events << CardFinished << ConfirmDamage << EventPhaseChanging << CardUsed;
		view_as_skill = new FujiVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Jink")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(use.card->hasFlag("fujiF"+p->objectName())){
						room->sendCompulsoryTriggerLog(p,objectName());
						player->drawCards(1,objectName());
					}
				}
			}
		}else if(triggerEvent==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(damage.card->hasFlag("fujiF"+p->objectName())){
						room->sendCompulsoryTriggerLog(p,objectName());
						player->damageRevises(data,1);
					}
				}
			}
		} else if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(use.card->hasFlag("fujiF"+p->objectName())){
						room->sendCompulsoryTriggerLog(p,objectName());
						QList<int>ids = room->getDiscardPile()+room->getDrawPile();
						qsanShuffle(ids);
						foreach (int id, ids){
							if(Sanguosha->getCard(id)->getSuit()==use.card->getSuit()){
								room->obtainCard(player,id);
								break;
							}
						}
					}
				}
				if(player->getMark("&fuji")>0){
					if(use.card->isKindOf("Slash")){
						if(player->getMark("fujiSlash")<1){
							player->addMark("fujiSlash");
							room->setCardFlag(use.card,"fujiF"+player->objectName());
						}
					}else if(use.card->isKindOf("Jink")){
						if(player->getMark("fujiJink")<1){
							player->addMark("fujiJink");
							room->setCardFlag(use.card,"fujiF"+player->objectName());
						}
					}
				}
			}
		}else if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().from==Player::NotActive&&player->getMark("&fuji")>0) {
				if(player->getGeneralName().endsWith("yuji"))
					player->setAvatarIcon("");
				room->setPlayerMark(player,"&fuji",0);
				player->removeMark("fujiSlash");
				player->removeMark("fujiJink");
			}
		}
		return false;
	}
};

class BsWanglie : public TriggerSkill
{
public:
	BsWanglie() : TriggerSkill("bswanglie")
	{
		events << CardFinished << EventPhaseStart << PreCardUsed;
		waked_skills = "#BsWangliePro";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("bswanglieBf")){
				room->setPlayerMark(player,"&bswanglie-PlayClear",1);
			}
		} else if (triggerEvent == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasTip("bswanglie")&&player->getMark(use.card->toString()+"bswanglieId-PlayClear")>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				room->setCardFlag(use.card,"bswanglieBf");
				use.no_respond_list << "_ALL_TARGETS";
				data.setValue(use);
			}
		}else if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Play&&player->getHandcardNum()>0&&player->hasSkill(objectName())) {
				const Card*c = room->askForCard(player,".","bswanglie0:",data,Card::MethodNone);
				if(c){
					player->skillInvoked(this);
					room->setCardTip(c->getEffectiveId(),"bswanglie-Clear");
					player->addMark(c->toString()+"bswanglieId-PlayClear");
				}
			}
		}
		return false;
	}
};

class BsWangliePro : public ProhibitSkill
{
public:
	BsWangliePro() : ProhibitSkill("#BsWangliePro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const
	{
		return from!=to&&from->getMark("&bswanglie-PlayClear")>0&&from->hasSkill("bswanglie");
	}
};

class BsHongyi : public TriggerSkillV2
{
public:
	BsHongyi() : TriggerSkillV2("bshongyi")
	{
		events << EventPhaseStart << EventPhaseChanging << GameStart << Damage << Damaged;
		frequency = Compulsory;
		global = true;
	}
	static QString identity(const QVariantMap &receipt)
	{
		return receipt.value("activation_owner").toString() + ":" + receipt.value("activation_instance").toString();
	}
	static void project(Room *room, ServerPlayer *player)
	{
		QMap<QString, int> counts;
		for (const QVariant &entry : player->getTag("mobile_hongyi_pending").toList())
			counts[identity(entry.toMap())] = entry.toMap().value("count").toInt();
		for (int id : player->getSkillInstanceIds("bshongyi"))
			counts[player->objectName() + ":" + QString::number(id)] = player->getSkillInstanceStateValue("bshongyi", id, "count").toInt();
		int total = 0;
		for (int count : counts) total += count;
		room->setPlayerMark(player, "&bshong_yi", total);
	}
	static void setCount(Room *room, ServerPlayer *player, const SkillInstanceRef &ref, int count)
	{
		ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
		if (holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID))
			holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "count", count);
		QVariantList receipts = player->getTag("mobile_hongyi_pending").toList();
		for (QVariant &entry : receipts) {
			QVariantMap receipt = entry.toMap();
			if (receipt.value("activation_owner").toString() != ref.ownerObjectName || receipt.value("activation_instance").toInt() != ref.key.instanceID) continue;
			receipt.insert("count", count);
			entry = receipt;
		}
		player->setTag("mobile_hongyi_pending", receipts);
		project(room, player);
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		const bool ready = event == EventPhaseStart && player->getPhase() == Player::Finish;
		const bool expire = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
		if (!ready && !expire) return true;
		const QVariant turn = room->historyScopes().value("turn_id");
		QVariantList receipts;
		int ordinal = 0;
		for (const QVariant &entry : player->getTag("mobile_hongyi_pending").toList()) {
			QVariantMap receipt = entry.toMap();
			if (receipt.value("turn") == turn) {
				if (expire) continue;
				receipt.insert("ordinal", ordinal++);
				receipt.insert("ready", room->historyScopes().value("phase_id"));
			}
			receipts << receipt;
		}
		player->setTag("mobile_hongyi_pending", receipts);
		if (expire) project(room, player);
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || event == EventPhaseChanging) return {};
		if (event == EventPhaseStart && player->getPhase() != Player::Start) return {};
		return TriggerList{{player, {objectName()}}};
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != EventPhaseStart || !player || player->getPhase() != Player::Finish) return false;
		if (!player->isAlive()) return true;
		for (const QVariant &entry : player->getTag("mobile_hongyi_pending").toList()) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("ready").toLongLong() <= 0 || receipt.value("ready") != room->historyScopes().value("phase_id")) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = player;
			ctx.invoker = player;
			ctx.initiator = player;
			ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.trigger_count = receipt.value("ordinal").toInt();
			ctx.preferredTarget = player;
			ctx.preferredTargetSeat = player->getSeat();
			ctx.amount = receipt.value("amount").toInt();
			ctx.extra_data = receipt;
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.is_forced = true;
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (!ctx.extra_data.toMap().contains("serial")) return TriggerSkillV2::isSourceAvailable(room, ctx);
		if (!ctx.owner || !ctx.owner->isAlive()) return false;
		for (const QVariant &entry : ctx.owner->getTag("mobile_hongyi_pending").toList())
			if (entry.toMap().value("serial") == ctx.extra_data.toMap().value("serial")) return true;
		return false;
	}
	bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.targets = {player};
		if (ctx.extra_data.toMap().contains("serial")) {
			for (const QVariant &entry : player->getTag("mobile_hongyi_pending").toList()) {
				if (entry.toMap().value("serial") != ctx.extra_data.toMap().value("serial")) continue;
				ctx.extra_data = entry;
				return true;
			}
			return false;
		}
		const SkillInstanceRef ref = getUsageRef(ctx);
		ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
		if (!holder || !ref.isValid()) return false;
		const int count = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "count").toInt();
		if (event != EventPhaseStart) return count < 4;
		const QString draw = "bshongyi1=" + QString::number(count);
		const QString choice = room->askForChoice(player, objectName(), draw + "+bshongyi2");
		ctx.extra_data = QVariantMap{{"draw", choice != "bshongyi2"}};
		return true;
	}
	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *) const override
	{
		QVariantMap receipt = ctx.extra_data.toMap();
		const bool pending = receipt.contains("serial");
		const SkillInstanceRef ref = pending
			? SkillInstanceRef(receipt.value("activation_owner").toString(), SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()))
			: getUsageRef(ctx);
		ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
		const bool live = holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID);
		const int count = live ? holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "count").toInt() : receipt.value("count").toInt();
		if (!pending && event != EventPhaseStart) {
			setCount(room, player, ref, qBound(0, count + (event == GameStart ? 2 : 1) * getEffectiveAmount(ctx), 4));
			return false;
		}
		const bool draw = receipt.value("draw").toBool();
		QVariantList receipts = player->getTag("mobile_hongyi_pending").toList();
		if (pending) {
			// Consume before draw callbacks; later source loss cannot replay the delayed action.
			for (int i = receipts.length() - 1; i >= 0; --i)
				if (receipts[i].toMap().value("serial") == receipt.value("serial")) receipts.removeAt(i);
		} else {
			const quint64 serial = room->getTag("mobile_hongyi_serial").toULongLong() + 1;
			room->setTag("mobile_hongyi_serial", serial);
			receipt = QVariantMap{{"serial", serial}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
				{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ref.ownerObjectName}, {"activation_skill", ref.key.skillName},
				{"activation_instance", ref.key.instanceID}, {"turn", room->historyScopes().value("turn_id")}, {"count", count},
				{"amount", getEffectiveAmount(ctx)}, {"draw", !draw}};
			receipts << receipt;
		}
		player->setTag("mobile_hongyi_pending", receipts);
		if (draw) player->drawCards(count * getEffectiveAmount(ctx), objectName());
		else setCount(room, player, ref, 0);
		project(room, player);
		return false;
	}
};

class BsHaoshi : public TriggerSkill
{
public:
	BsHaoshi() : TriggerSkill("bshaoshi")
	{
		events << EventPhaseStart << CardsMoveOneTime;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	static void BsHaoshiMove(QList<int> ids, bool movein, ServerPlayer *target)
	{
		if(ids.isEmpty()) return;
		QList<CardsMoveStruct> moves;
		Room *room = target->getRoom();
		if (movein) {
			CardsMoveStruct move(ids,room->getCardOwner(ids.first()),target,Player::PlaceHand,Player::PlaceSpecial,
			CardMoveReason(CardMoveReason::S_REASON_PUT,target->objectName(),"bshaoshi",""));
			move.to_pile_name = "&bshaoshi";
			moves.append(move);
		} else {
			CardsMoveStruct move(ids,target,nullptr,Player::PlaceSpecial,Player::PlaceTable,
			CardMoveReason(CardMoveReason::S_REASON_PUT,target->objectName(),"bshaoshi",""));
			move.from_pile_name = "&bshaoshi";
			moves.append(move);
		}
		QList<ServerPlayer *> _caoxiu;
		_caoxiu << target;
		room->notifyMoveCards(true, moves, true, _caoxiu);
		room->notifyMoveCards(false, moves, true, _caoxiu);
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&player->getMark("&bshaoshi+#"+move.to->objectName())>0){
				QList<int> ids;
				foreach (int id, move.to->handCards()){
					if(move.card_ids.contains(id)) ids << id;
				}
				BsHaoshiMove(ids,true,player);
			}
			if(move.from_places.contains(Player::PlaceHand)){
				if(player->getMark("&bshaoshi+#"+move.from->objectName())>0){
					QList<int> ids;
					int n = 0;
					foreach (int id, move.card_ids){
						if(move.from->handCards().contains(id)) continue;
						if(move.from_places.at(n)==Player::PlaceHand) ids << id;
						n++;
					}
					BsHaoshiMove(ids,false,player);
				}
				if(move.is_last_handcard&&player==move.from&&player->objectName()!=move.reason.m_playerId&&player->hasSkill(objectName())
                &&((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE||move.reason.m_reason==CardMoveReason::S_REASON_RESPONSE)){
					ServerPlayer *tp = room->findPlayerByObjectName(move.reason.m_playerId);
					if(tp&&tp->getMark("&bshaoshi+#"+player->objectName())>0){
						room->sendCompulsoryTriggerLog(player,this);
						player->drawCards(player->getMaxHp()-player->getHandcardNum(),objectName());
					}
				}
			}
		}else if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Finish&&player->hasSkill(objectName())) {
				QList<ServerPlayer *>tps;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getHp()<=player->getHp()) tps << p;
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"bshaoshi0:",true,true);
				if(tp){
					player->peiyin(this);
					room->setPlayerMark(tp,"&bshaoshi+#"+player->objectName(),1);
					BsHaoshiMove(player->handCards(),true,tp);
				}
			}else if(player->getPhase()==Player::RoundStart){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(p->getMark("&bshaoshi+#"+player->objectName())>0){
						//BsHaoshiMove(player->handCards(),false,p);
						QList<CardsMoveStruct> moves;
						CardsMoveStruct move(player->handCards(),p,player,Player::PlaceSpecial,Player::PlaceHand,
						CardMoveReason(CardMoveReason::S_REASON_PUT,p->objectName(),"bshaoshi",""));
						move.from_pile_name = "&bshaoshi";
						moves.append(move);
						QList<ServerPlayer *> _caoxiu;
						_caoxiu << p;
						room->notifyMoveCards(true, moves, true, _caoxiu);
						room->notifyMoveCards(false, moves, true, _caoxiu);
					}
					room->setPlayerMark(p,"&bshaoshi+#"+player->objectName(),0);
				}
			}
		}
		return false;
	}
};

BsDimengCard::BsDimengCard()
{
}

bool BsDimengCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	if(targets.length()==1){
		if(qAbs(targets.first()->getHandcardNum()-to->getHandcardNum())>Self->getLostHp()) return false;
	}
	return targets.length()<2;
}

bool BsDimengCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void BsDimengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	int n = source->getLostHp();
	room->swapCards(targets.first(),targets.last(),"h","bsdimeng");
	if(n<1) return;
	if(source->canDiscard(source,"he")&&room->askForChoice(source,"bsdimeng","1+2")=="1"){
		room->askForDiscard(source,"bsdimeng",n,n,false,true);
	}else{
		int x = 999;
		foreach (ServerPlayer *p, targets){
			if(p->isAlive()) x = qMin(x,p->getHandcardNum());
		}
		foreach (ServerPlayer *p, targets){
			if(p->isAlive()&&p->getHandcardNum()<=x)
				p->drawCards(n,"bsdimeng");
		}
	}
}

class BsDimeng : public ZeroCardViewAsSkill
{
public:
	BsDimeng() : ZeroCardViewAsSkill("bsdimeng")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsDimengCard")<1;
	}

	const Card *viewAs() const
	{
		return new BsDimengCard;
	}
};

class BsXianshuai : public TriggerSkillV2
{
public:
	BsXianshuai() : TriggerSkillV2("bsxianshuai") { events << CardUsed; frequency = Compulsory; }
	static int firstSuitUse(Room *room, ServerPlayer *player, const Card *card)
	{
		const QVariantMap current = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		const qint64 useId = current.value("id").toLongLong();
		if (useId <= 0 || current.value("turn_id").toLongLong() <= 0) return -1;
		QVariantMap query{{"kind", "use_card"}, {"turn_id", current.value("turn_id")}, {"from", player->objectName()}};
		for (;;) {
			const QVariantMap page = room->queryHistoryFacts(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap(), data = fact.value("data").toMap();
				const QVariantMap snapshot = data.value("card").toMap();
				if (!data.contains("is_handcard") || !snapshot.contains("type") || !snapshot.contains("suit")) return -1;
				if (!data.value("is_handcard").toBool() || snapshot.value("type").toInt() == Card::TypeSkill
					|| snapshot.value("suit").toInt() != card->getSuit()) continue;
				return fact.value("event_id").toLongLong() == useId ? 1 : 0;
			}
			if (!page.value("has_more").toBool()) return -1;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasFlag("CurrentPlayer")) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.m_isHandcard || !use.card->hasSuit()) return {};
		const int first = firstSuitUse(room, player, use.card);
		if (first < 0) qWarning("BsXianshuai: incomplete hand-card use history");
		return first == 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override { ctx.targets = {player}; return true; }
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		use.m_addHistory = false;
		ctx.original_data->setValue(use);
		QStringList suits;
		for (const QString &name : target->getMarkNames()) if (name.startsWith("&bsxianshuai+:+") && target->getMark(name) > 0) {
			QString previous = name;
			previous.remove("-Clear");
			const QStringList parts = previous.split("+");
			for (int i = 2; i < parts.length(); ++i) if (!suits.contains(parts.at(i))) suits << parts.at(i);
			room->setPlayerMark(target, name, 0);
		}
		const QString suit = use.card->getSuitString() + "_char";
		if (!suits.contains(suit)) suits << suit;
		// This aggregate mark is presentation only; first-use authority is immutable history.
		room->setPlayerMark(target, "&bsxianshuai+:+" + suits.join("+") + "-Clear", 1);
		return false;
	}
};

XiongtuCard::XiongtuCard()
{
}

bool XiongtuCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self&&to->getHandcardNum()>0;
}

void XiongtuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		int id = room->askForCardChosen(source,p,"h","xiongtu");
		if(id<0) continue;
		room->showCard(p,id);
		int n = 4;
		foreach (QString m, source->getMarkNames()){
			if(m.contains("&xiongtu+:+")){
				QStringList ms = m.split("+");
				n = 6-ms.length();
				break;
			}
		}
		QStringList choices;
		if(source->canDiscard(p,id)) choices << "xiongtu1";
		if(source->getCardCount()>=n) choices << "xiongtu2="+QString::number(n);
		if(choices.isEmpty()) continue;
		if(room->askForChoice(source,"xiongtu",choices.join("+"),id)=="xiongtu1")
			room->throwCard(id,"xiongtu",p,source);
		else{
			room->askForDiscard(source,"xiongtu",n,n,false,true);
			room->damage(DamageStruct("xiongtu",source,p));
		}
	}
}

class Xiongtuvs : public ZeroCardViewAsSkill
{
public:
	Xiongtuvs() : ZeroCardViewAsSkill("xiongtu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("XiongtuCard")<1;
	}

	const Card *viewAs() const
	{
		return new XiongtuCard;
	}
};

class Xiongtu : public TriggerSkill
{
public:
	Xiongtu() : TriggerSkill("xiongtu")
	{
		events << CardsMoveOneTime;
		view_as_skill = new Xiongtuvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&player->hasSkill(objectName(),true)){
				foreach (int id, move.card_ids){
					const Card*c = Sanguosha->getEngineCard(id);
					if(player->getMark(c->getSuitString()+"xiongtu-Clear")<1){
						player->addMark(c->getSuitString()+"xiongtu-Clear");
						foreach (QString m, player->getMarkNames()){
							if(m.contains("&xiongtu+:+")){
								room->setPlayerMark(player,m,0);
								m.remove("-Clear");
								QStringList ms = m.split("+");
								ms << c->getSuitString()+"_char";
								room->setPlayerMark(player,ms.join("+")+"-Clear",1);
								c = nullptr;
								break;
							}
						}
						if(c)
							room->setPlayerMark(player,"&xiongtu+:+"+c->getSuitString()+"_char-Clear",1);
					}
				}
			}
		}
		return false;
	}
};

class Xiaoge : public TriggerSkill
{
public:
	Xiaoge() : TriggerSkill("xiaoge")
	{
		events << DamageCaused << CardFinished;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("feijingBf"+damage.to->objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				player->damageRevises(data,-damage.damage);
				room->recover(player,RecoverStruct(objectName(),player));
				int id = damage.to->getMark("feijingId");
				if(!room->getCardOwner(id))
					room->obtainCard(player,id);
				return true;
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("feijingBf")&&use.to.length()==1&&use.to.first()->isAlive()){
				Card*dc = Sanguosha->cloneCard("duel");
				dc->setSkillName("_xiaoge");
				dc->deleteLater();
				if(player->canUse(dc,use.to.first())){
					room->sendCompulsoryTriggerLog(player,this);
					room->useCard(CardUseStruct(dc,player,use.to));
				}
			}
		}
		return false;
	}
};

class FeijingVs : public OneCardViewAsSkill
{
public:
	FeijingVs() : OneCardViewAsSkill("ganjue")
	{
		response_pattern = "slash";
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->isDamageCard()&&to_select->isKindOf("TrickCard");
	}

	const Card *viewAs(const Card *c) const
	{
		Card *card = Sanguosha->cloneCard("slash");
		card->addSubcard(c);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}
};

class Feijing : public TriggerSkill
{
public:
	Feijing() : TriggerSkill("feijing")
	{
		events << TargetSpecifying;
		view_as_skill = new FeijingVs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.length()==1&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				QList<ServerPlayer *>aps;
				room->setCardFlag(use.card,"feijingBf");
				if(room->askForChoice(player,objectName(),"1+2",data)=="1"){
					ServerPlayer *ap = use.to.first()->getNextAlive();
					while(ap!=player){
						aps << ap;
						ap = ap->getNextAlive();
					}
				}else{
					ServerPlayer *ap = player->getNextAlive();
					while(ap!=use.to.first()){
						aps << ap;
						ap = ap->getNextAlive();
					}
				}
				foreach (ServerPlayer *p, aps){
					const Card*c = room->askForCardShow(p,player,objectName());
					if(c) p->setMark("feijingId",c->getId());
					else aps.removeOne(p);
				}
				foreach (ServerPlayer *p, aps)
					room->showCard(p,p->getMark("feijingId"));
				foreach (ServerPlayer *p, aps){
					int id = p->getMark("feijingId");
					if(p->canDiscard(p,id))
						room->throwCard(id,objectName(),p);
					else aps.removeOne(p);
				}
				QString choice = room->askForChoice(player,objectName(),"red+black+cancel",data);
				foreach (ServerPlayer *p, aps){
					const Card*c = Sanguosha->getCard(p->getMark("feijingId"));
					if(c->getColorString()==choice){
						use.to << p;
						room->doAnimate(1,player->objectName(),p->objectName());
						room->setCardFlag(use.card,"feijingBf"+p->objectName());
					}
				}
				if(choice!="cancel"){
					room->sortByActionOrder(use.to);
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class Zhuangshi : public TriggerSkill
{
public:
	Zhuangshi() : TriggerSkill("zhuangshi")
	{
		events << CardUsed << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getPhase()==Player::Play){
				if(player->getMark("&zhuangshi+1-PlayClear")>0){
					room->removePlayerMark(player,"&zhuangshi+1-PlayClear");
					use.no_respond_list << "_ALL_TARGETS";
					data.setValue(use);
				}
				if(player->getMark("&zhuangshi+2-PlayClear")>0){
					room->removePlayerMark(player,"&zhuangshi+2-PlayClear");
					use.m_addHistory = false;
					data.setValue(use);
				}
			}
		}else if(player->getPhase()==Player::Play){
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
				int n = qsanRandomBounded(2)+1;
				if(player->property("avatarIcon").toString().endsWith("weiyan2"))
					n += 2;
				player->peiyin(this,n);
				const Card*sc = room->askForDiscard(player,objectName(),999,1,true,false,"zhuangshi1");
				if(sc){
					player->setMark("zhuangshi1-PlayClear",sc->subcardsLength());
					room->setPlayerMark(player,"&zhuangshi+1-PlayClear",sc->subcardsLength());
				}
				QStringList choices;
				for (int i = 1; i <= player->getHp(); i++)
					choices << QString("zhuangshi2=%1").arg(i);
				if(sc) choices << "cancel";
				QString choice = room->askForChoice(player,"zhuangshiLoseHp",choices.join("+"));
				if(choice!="cancel"){
					int n = choice.split("=").last().toInt();
					player->setMark("zhuangshi2-PlayClear",n);
					room->setPlayerMark(player,"&zhuangshi+2-PlayClear",n);
					room->loseHp(player,n,true,player,objectName());
				}
			}
		}
		return false;
	}
};

class Yinzhan : public TriggerSkill
{
public:
	Yinzhan() : TriggerSkill("yinzhan")
	{
		events << DamageCaused << CardFinished;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")){
				bool hp = player->getHp()<=damage.to->getHp();
				bool hn = player->getCardCount()<=damage.to->getCardCount();
				int n = qsanRandomBounded(3)+1;
				if(player->property("avatarIcon").toString().endsWith("weiyan2"))
					n += 3;
				else if(player->property("avatarIcon").toString().endsWith("weiyan3"))
					n += 3;
				if(hp){
					room->sendCompulsoryTriggerLog(player,this,n);
					player->damageRevises(data,1);
				}
				if(hn){
					if(!hp) room->sendCompulsoryTriggerLog(player,this,n);
					room->setCardFlag(damage.card,player->objectName()+"yinzhanBf"+damage.to->objectName());
				}
				if(hp&&hn){
					room->setCardFlag(damage.card,"yinzhanBf");
					room->recover(player,RecoverStruct(objectName(),player));
				}
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(use.card->hasFlag(player->objectName()+"yinzhanBf"+p->objectName())&&player->canDiscard(p,"he")){
						int id = room->askForCardChosen(player,p,"h",objectName(),false,Card::MethodDiscard);
						if(id>=0){
							room->throwCard(id,objectName(),p,player);
							if(player->isDead()) break;
							if(use.card->hasFlag("yinzhanBf")&&!room->getCardOwner(id)){
								room->obtainCard(player,id);
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhongao : public TriggerSkill
{
public:
	Zhongao() : TriggerSkill("zhongao")
	{
		events << GameStart << Death << CardUsed << Dying << ChoiceMade;
		waked_skills = "tenyearkuanggu,kunfen";
		shiming_skill = true;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardUsed) {
            const QList<int> ids = player->getSkillInstanceIds(objectName());
            bool pending = ids.isEmpty();
            foreach (int id, ids)
                if (room->getShimingStatus(SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id))) == 0)
                    pending = true;
            if (!pending) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() > 0 && player->getPhase() == Player::Play)
                player->addMark("zhongaoUse-PlayClear");
            return false;
        }
        const QList<int> ids = player->getValidSkillInstanceIds(objectName());
        foreach (int id, ids) {
            if (!player->hasSkillInstance(objectName(), id) || player->isSkillInvalid(objectName(), id)) continue;
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (triggerInstance(event, room, player, data, ref)) return true;
        }
        return false;
    }

    bool triggerInstance(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                         QVariant &data, const SkillInstanceRef &ref) const
    {
		if(room->getShimingStatus(ref)>0)
			return false;
		if(triggerEvent==GameStart){
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this,1);
				room->acquireSkill(player,"tenyearkuanggu");
			}
		}else if(triggerEvent==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.damage&&death.damage->from==player&&player->hasSkill(objectName())){
				if(player->getGeneralName().contains("weiyan"))
					player->setAvatarIcon("mobilebs_weiyan2");
				if (!room->sendShimingLog(ref,true,qsanRandomBounded(2)+2)) return false;
				player->addMark("zhongaoUptenyearkuanggu");
				room->changeTranslation(player,"tenyearkuanggu",1);
				if(player->getMark("zhongaoUse-PlayClear")<player->getMark("zhuangshi1-PlayClear")){
					player->drawCards(1,objectName());
				}
				if(player->getMark("zhongaoUse-PlayClear")<player->getMark("zhuangshi2-PlayClear")){
					if(player->getLostHp()>0) room->recover(player,RecoverStruct(objectName(),player));
					else player->drawCards(1,objectName());
				}
			}
		}else {
			if(triggerEvent==Dying){
				DyingStruct dy = data.value<DyingStruct>();
				if(dy.who!=player) return false;
			}else{
				QString str = data.toString();
				if(!str.contains("skillInvoke:zhuangshi")||str.contains(":yes")) return false;
			}
			if(player->hasSkill(objectName())){
				if (!room->sendShimingLog(ref,false,qsanRandomBounded(2)+4)) return false;
				room->handleAcquireDetachSkills(player,"-zhuangshi|kunfen");
				if(player->getGeneralName().contains("weiyan"))
					player->setAvatarIcon("mobilebs_weiyan3");
			}
		}
		return false;
	}
};

BsRunweiCard::BsRunweiCard()
{
}

bool BsRunweiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->getMark(Self->objectName()+"bsrunweiTo-Clear")<1;
}

void BsRunweiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		room->addPlayerMark(p,source->objectName()+"bsrunweiTo-Clear");
		QStringList choices;
		for (int i = 1; i <= 5; i++)
			choices << QString("bsrunwei1=%1").arg(i);
		QString choice = room->askForChoice(source,"bsrunwei",choices.join("+"));
		choice.remove("bsrunwei1=");
		int n = choice.toInt();
		QList<int>ids = room->showDrawPile(source,n,"bsrunwei",false);
		room->fillAG(ids,source);
		n = room->askForAG(source,ids,false,"bsrunwei","bsrunwei2");
		room->clearAG(source);
		Card*dc = dummyCard();
		foreach (int id, ids) {
			if(Sanguosha->getCard(id)->getColor()==Sanguosha->getCard(n)->getColor())
				dc->addSubcard(id);
		}
		p->obtainCard(dc);
		if(source->isDead()) break;
		if(p==source){
			foreach (int id, p->handCards()) {
				if(dc->getSubcards().contains(id))
					room->setCardTip(id,"bsrunwei-PlayClear");
			}
		}
		source->addMark("bsrunweiUse-PlayClear");
		if(source->getMark("bsrunweiUse-PlayClear")<2)
			source->setMark("bsrunweiNum-PlayClear",dc->subcardsLength());
	}
}

class BsRunweivs : public ZeroCardViewAsSkill
{
public:
	BsRunweivs() : ZeroCardViewAsSkill("bsrunwei")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsRunweiCard")<1;
	}

	const Card *viewAs() const
	{
		return new BsRunweiCard;
	}
};

class BsRunwei : public TriggerSkill
{
public:
	BsRunwei() : TriggerSkill("bsrunwei")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
		view_as_skill = new BsRunweivs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&player->getMark("bsrunweiNum-PlayClear")>0){
				if(move.to!=move.from||(move.to_place!=Player::PlaceHand&&move.to_place!=Player::PlaceEquip)){
					if(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip)){
						for (int i = 0; i < move.from_places.length(); i++){
							if(move.from_places.at(i)==Player::PlaceHand||move.from_places.at(i)==Player::PlaceEquip){
								player->removeMark("bsrunweiNum-PlayClear");
							}
						}
						if(player->getMark("bsrunweiNum-PlayClear")<1)
							room->addPlayerHistory(player,"BsRunweiCard",0);
					}
				}
			}
		}else{
			if(player->getPhase()==Player::Play&&player->getMark("bsrunweiUse-PlayClear")>0){
				QList<int>ids;
				foreach (const Card*h, player->getHandcards()) {
					if(h->hasTip(objectName())&&player->canDiscard(h->getId()))
						ids << h->getId();
				}
				room->throwCard(ids,objectName(),player);
			}
		}
		return false;
	}
};

class Shuanghuai : public TriggerSkill
{
public:
	Shuanghuai() : TriggerSkill("shuanghuai")
	{
		events << DamageInflicted;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			bool has = false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->isAlive()&&p->hasTurn()&&p->hasSkill(objectName())&&p->getMark("shuanghuaiUse-Clear")<1
				&&p->distanceTo(player)==1&&p->askForSkillInvoke(objectName()+"$-1",player)){
					p->addMark("shuanghuaiUse-Clear");
					QList<int>ids;
					foreach (int id, room->getDiscardPile()) {
						if(Sanguosha->getCard(id)->isKindOf("Peach"))
							ids << id;
					}
					QString tp = p->getTag("shuanghuaiTo").toString();
					if(ids.length()>0&&room->askForChoice(p,objectName(),"1+2",data)=="2"){
						room->fillAG(ids,player);
						int id = room->askForAG(player,ids,false,objectName(),"shuanghuai2");
						room->clearAG(player);
						room->obtainCard(player,id);
					}else{
						has = player->damageRevises(data,-damage.damage);
					}
					p->setTag("shuanghuaiTo", player->objectName());
					if(p->isDead()) continue;
					if(tp!=player->objectName()){
						room->loseHp(p,1,true,p,objectName());
					}else{
						QList<ServerPlayer *>aps;
						aps << p << player;
						room->sortByActionOrder(aps);
						room->drawCards(aps,1,objectName());
					}
				}
			}
			return has;
		}
		return false;
	}
};

GanggengCard::GanggengCard()
{
	will_throw = false;
}

bool GanggengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void GanggengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &tos) const
{
	foreach (ServerPlayer *p, tos){
		room->setPlayerMark(p,"&ganggeng+#"+source->objectName()+"-Clear",1);
		room->giveCard(source,p,this,"ganggeng");
	}
}

class Ganggengvs : public ViewAsSkill
{
public:
	Ganggengvs() : ViewAsSkill("ganggeng")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *card) const
	{
		return !card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<2) return nullptr;
		GanggengCard *sc = new GanggengCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("GanggengCard")<1&&player->getHandcardNum()>1;
	}
};

class Ganggeng : public TriggerSkill
{
public:
	Ganggeng() : TriggerSkill("ganggeng")
	{
		events << EventPhaseChanging;
		view_as_skill = new Ganggengvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *q, room->getOtherPlayers(player)){
					if(q->getMark("&ganggeng+#"+player->objectName()+"-Clear")>0){
						bool has = true;
						foreach (ServerPlayer *t, room->getAlivePlayers()){
							if(t->getHandcardNum()>q->getHandcardNum()){
								has = false;
								break;
							}
						}
						if(has){
							player->drawCards(1,objectName());
						}else if(player->canDiscard(q,"hej")){
							room->doAnimate(1,player->objectName(),q->objectName());
							int id = room->askForCardChosen(player,q,"hej",objectName(),false,Card::MethodDiscard);
							if(id>=0) room->throwCard(id,objectName(),q,player);
						}
						if(player->isDead()) break;
					}
				}
			}
		}
		return false;
	}
};

class BsSijian : public TriggerSkill
{
public:
	BsSijian() : TriggerSkill("bssijian")
	{
		events << CardsMoveOneTime << EnterDying << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("&bssijian+1")>0){
				room->setPlayerMark(player,"&bssijian+1",0);
				room->askForDiscard(player,objectName(),1,1,false,true);
			}
			return false;
		}
		if(player->getMark("&bssijianUse-Clear")>1||!player->hasTurn()||!player->hasSkill(objectName())) return false;
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(!move.is_last_handcard||move.from!=player) return false;
		}
		if(!player->askForSkillInvoke(objectName()+"$-1",data)) return false;
		player->addMark("&bssijianUse-Clear");
		QString choice = "bssijian1+bssijian2+beishui";
		foreach (ServerPlayer *q, room->getAlivePlayers()){
			if(q->hasFlag("Global_Dying")){
				choice = "bssijian1+bssijian2";
				break;
			}
		}
		choice = room->askForChoice(player,objectName(),choice,data);
		if(choice=="beishui"){
			int n = player->getMark("bssijian_beishui");
			if(n>0){
				room->loseHp(player,n,true,player,objectName());
				if(player->isDead()) return false;
			}
			player->addMark("bssijian_beishui");
		}
		if(choice!="bssijian2"){
			ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"bssijian10");
			if(tp){
				room->doAnimate(1,player->objectName(),tp->objectName());
				room->setPlayerMark(tp,"&bssijian+1",1);
			}
		}
		if(choice!="bssijian1"){
			room->getCurrent()->drawCards(2,objectName());
		}
		return false;
	}
};

class Quanchong : public TriggerSkill
{
public:
	Quanchong() : TriggerSkill("quanchong")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(triggerEvent==EventPhaseStart){
			if(player->getPhase()==Player::Finish){
				if(player->getMark("quanchongUse_lun")==0&&player->hasSkill(objectName())&&player->canDiscard("he")){
					room->sendCompulsoryTriggerLog(player,this);
					player->addMark("quanchongUse_lun");
					player->throwAllHandCardsAndEquips(objectName());
				}
			}else if(player->getPhase()==Player::NotActive){
				if(player->getMark("quanchongUse_lun")==1){
					player->addMark("quanchongUse_lun");
					player->gainAnExtraTurn();
				}
			}else if(player->getPhase()==Player::RoundStart){
				if(player->getMark("quanchongUse_lun")==2){
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(p!=player&&p->getHp()>=player->getHp()){
							room->loseHp(player,1,true,player,objectName());
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class Renxing : public TriggerSkillV2
{
public:
	Renxing() : TriggerSkillV2("renxing") { events << CardsMoveOneTime << EventSkillInvoking; }
	LimitScope getLimitScope() const override { return Limit_Round; }
	int getMaxUsageLimit(const SkillContext &) const override { return 2; }
	void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event != EventSkillInvoking || !ctx.original_data) return;
		const SkillContext accepted = ctx.original_data->value<SkillContext>();
		if (accepted.skill_name == objectName() && accepted.activationRef.isValid()
			&& accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
	}
	static int firstDiscard(Room *room)
	{
		const QVariant turn = room->historyScopes().value("turn_id");
		const qint64 currentMove = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
		if (turn.toLongLong() <= 0 || currentMove <= 0) return -1;
		QVariantMap query{{"turn_id", turn}};
		for (;;) {
			const QVariantMap page = room->queryHistoryMoves(query);
			if (!page.value("complete").toBool()) return -1;
			for (const QVariant &entry : page.value("items").toList()) {
				const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
				if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) continue;
				const qint64 phaseId = fact.value("phase_id").toLongLong();
				if (phaseId > 0) {
					const QVariantMap phase = room->historyEvent(phaseId).value("data").toMap();
					if (!phase.contains("phase")) return -1;
					if (phase.value("phase").toInt() == Player::Discard) continue;
				}
				return fact.value("event_id").toLongLong() == currentMove ? 1 : 0;
			}
			if (!page.value("has_more").toBool()) return 0;
			query.insert("after", page.value("next_after"));
			query.insert("watermark", page.value("watermark"));
		}
	}
	static QList<ServerPlayer *> discardTargets(Room *room, ServerPlayer *owner)
	{
		QList<ServerPlayer *> result;
		for (ServerPlayer *target : room->getAlivePlayers()) {
			if (!owner->canDiscard(target, "he")) continue;
			const int used = room->countHistoryCards(target, "turn", "Slash");
			const int responded = room->countHistoryCards(target, "turn", "Slash", true);
			if (used == 0 && responded == 0) result << target;
		}
		return result;
	}
	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillInvoking || !player || !player->isAlive() || !player->hasSkill(objectName()) || !room->hasCurrent()) return {};
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return {};
		const int first = firstDiscard(room);
		if (first < 0) qWarning("Renxing: incomplete discard history");
		// Move callbacks already visit every holder once; never enumerate all owners here.
		return first == 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!isUsable(ctx) || !room->hasCurrent() || !player->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, discardTargets(room, player), objectName(), "renxing0", true);
		ctx.extra_data = QVariantMap{{"discard", target != nullptr}, {"current", room->getCurrent()->objectName()}};
		ctx.targets = target ? QList<ServerPlayer *>{target} : QList<ServerPlayer *>{player};
		if (!target && room->getCurrent() != player) ctx.targets << room->getCurrent();
		return true;
	}
	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		if (!isUsable(ctx)) return false;
		addUsage(ctx);
		return true;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		const QVariantMap saved = ctx.extra_data.toMap();
		if (!saved.value("discard").toBool()) {
			const int copies = target == player && saved.value("current").toString() == player->objectName() ? 2 : 1;
			target->drawCards(copies * getEffectiveAmount(ctx), objectName());
			return false;
		}
		if (!discardTargets(room, player).contains(target)) return false;
		for (int i = 0; i < getEffectiveAmount(ctx) && player->isAlive() && target->isAlive() && player->canDiscard(target, "he"); ++i) {
			const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
			if (room->getCardOwner(id) == target && player->canDiscard(target, id)) room->throwCard(id, objectName(), target, player);
		}
		return false;
	}
};

	static void bsqingshiUse(Room *room, ServerPlayer *player, ServerPlayer *to)
	{
		LogMessage log;
		log.type = "#ChoosePlayerWithSkill";
		log.from = player;
		log.arg = "bsqingshi";
		log.to << to;
		room->sendLog(log);
		player->peiyin("bsqingshi");
		room->doAnimate(1,player->objectName(),to->objectName());
		room->notifySkillInvoked(player, "bsqingshi");
		if(player->isYourFriend(to)){
			QList<ServerPlayer *>aps;
			aps << player << to;
			room->sortByActionOrder(aps);
			room->drawCards(aps,1,"bsqingshi");
		}else{
			room->askForDiscard(player,"bsqingshi",1,1,false,true);
			if(player->canDiscard(to,"he")){
				int id = room->askForCardChosen(player,to,"he","bsqingshi",false,Card::MethodDiscard);
				if(id>=0) room->throwCard(id,"bsqingshi",to,player);
			}
		}
	}

JiejieCard::JiejieCard()
{
	mute = true;
}

bool JiejieCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *) const
{
	return targets.isEmpty()&&to->hasSkill("jiejie");
}

void JiejieCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to) {
		use.from->skillInvoked("jiejie",-1,p);
		if(p!=use.from) room->doGongxin(use.from,p,p->handCards(),"jiejie");
		QString choice = "heart+diamond+spade+club+cancel";
		choice = room->askForChoice(p,"jiejie",choice,QVariant::fromValue(use.from));
		if(choice=="cancel") continue;
		bool has = false;
		QList<int>ids;
		QStringList suits;
		foreach (const Card*h, use.from->getHandcards()) {
			if(h->getSuitString()==choice){
				has = true;
			}else if(use.from->canDiscard(h->getId())){
				ids << h->getId();
			}
			if(suits.contains(h->getSuitString())) continue;
			suits.append(h->getSuitString());
		}
		if(has){
			room->setPlayerMark(use.from,"&jiejie+"+choice+"_char-Clear",1);
			room->throwCard(ids,objectName(),use.from);
		}else{
			foreach (int id, room->getDrawPile()+room->getDiscardPile()) {
				Card*c = Sanguosha->getCard(id);
				if(c->getSuitString()==choice){
					room->obtainCard(use.from,c);
					break;
				}
			}
		}
		if(use.from->isDead()||p->isDead()||p->getMark("jiejievsUse_lun")>1) continue;
		if(p->getMark("jiejievsNum_lun")<suits.length()){
			p->setMark("jiejievsNum_lun",suits.length());
			p->addMark("jiejievsUse_lun");
			bsqingshiUse(room,p,use.from);
		}
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class Jiejievs : public ZeroCardViewAsSkill
{
public:
	Jiejievs() : ZeroCardViewAsSkill("jiejievs&")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JiejieCard")<1;
	}

	const Card *viewAs() const
	{
		return new JiejieCard;
	}
};

class Jiejie : public TriggerSkill
{
public:
	Jiejie() : TriggerSkill("jiejie")
	{
		events << EventPhaseProceeding << EventPhaseEnd << EventAcquireSkill;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==EventPhaseEnd){
			if(player->getPhase()==Player::Play)
				room->detachSkillFromPlayer(player,"jiejievs",true);
		}else if(triggerEvent==EventPhaseProceeding){
			if(player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->hasSkill(objectName(),true)){
						room->attachSkillToPlayer(player,"jiejievs");
						break;
					}
				}
			}
		}else if(data.toString()==objectName()){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(p->getPhase()==Player::Play)
					room->attachSkillToPlayer(p,"jiejievs");
			}
		}
		return false;
	}
};

class BsQingshi : public TriggerSkill
{
public:
	BsQingshi() : TriggerSkill("bsqingshi")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"bsqingshi0",true);
		if(tp){
			bsqingshiUse(room,player,tp);
		}
		return false;
	}
};

class Qingdao : public TriggerSkill
{
public:
	Qingdao() : TriggerSkill("qingdao")
	{
		events << PreCardUsed << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()){
				foreach (ServerPlayer *p, use.to){
					if(p->isAlive()&&player!=p&&p->hasSkill(objectName())
					&&p->askForSkillInvoke(objectName()+"$-1",data)){
						if(use.card->hasFlag("DamageDone_"+p->objectName())){
							QList<ServerPlayer *>tps;
							foreach (ServerPlayer *q, room->getAlivePlayers()){
								if(p->canDiscard(q,"hej")) tps << q;
							}
							ServerPlayer *tp = room->askForPlayerChosen(p,tps,objectName(),"qingdao0",true);
							if(tp){
								room->doAnimate(1,p->objectName(),tp->objectName());
								int id = room->askForCardChosen(p,tp,"hej",objectName(),false,Card::MethodDiscard);
								if(id>=0) room->throwCard(id,objectName(),tp,p);
							}else{
								foreach (int id, room->getDrawPile()+room->getDiscardPile()){
									if(Sanguosha->getCard(id)->isKindOf("Jink")){
										room->obtainCard(p,id);
										break;
									}
								}
							}
						}else{
							room->setPlayerFlag(p,"qingdaoBf");
							const Card*h = room->askForUseCard(p,".|.|.|hand","qingdao1");
							room->setPlayerFlag(p,"-qingdaoBf");
							if(h==nullptr){
								foreach (int id, room->getDrawPile()+room->getDiscardPile()){
									if(Sanguosha->getCard(id)->isKindOf("Slash")){
										room->obtainCard(p,id);
										break;
									}
								}
							}
						}
					}
				}
			}
		}else{
			if(player->hasFlag("qingdaoBf"))
				room->setPlayerFlag(player,"-qingdaoBf");
		}
		return false;
	}
};

class Xiugeng : public TriggerSkillV2
{
public:
	Xiugeng() : TriggerSkillV2("xiugeng") { events << EventPhaseStart << EventPhaseChanging; global = true; }
	static QString mark(const QVariantMap &receipt)
	{
		return QString("&xiugeng+%1+#%2").arg(receipt.value("hand").toInt()).arg(receipt.value("author").toString());
	}
	bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		const bool ready = event == EventPhaseStart && player->getPhase() == Player::Draw;
		const bool expire = event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Draw;
		if (!ready && !expire) return true;
		QVariantList keep;
		QStringList marks;
		int ordinal = 0;
		for (const QVariant &entry : player->getTag("mobile_xiugeng_pending").toList()) {
			QVariantMap receipt = entry.toMap();
			marks << mark(receipt);
			if (ready) {
				receipt.insert("ready_phase", room->historyScopes().value("phase_id"));
				receipt.insert("observed_hand", player->getHandcardNum());
				receipt.insert("ready_ordinal", ordinal++);
			}
			if (!expire || receipt.value("ready_phase").toLongLong() <= 0) keep << receipt;
		}
		player->setTag("mobile_xiugeng_pending", keep);
		if (expire) {
			marks.removeDuplicates();
			for (const QString &name : marks) {
				bool active = false;
				for (const QVariant &entry : keep) if (mark(entry.toMap()) == name) active = true;
				room->setPlayerMark(player, name, active ? 1 : 0);
			}
		}
		return true;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::RoundStart && player->hasSkill(objectName())
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
	{
		if (event != EventPhaseStart || !player || player->getPhase() != Player::Draw) return false;
		if (!player->isAlive()) return true;
		for (const QVariant &entry : player->getTag("mobile_xiugeng_pending").toList()) {
			const QVariantMap receipt = entry.toMap();
			if (receipt.value("ready_phase").toLongLong() <= 0 || receipt.value("ready_phase") != room->historyScopes().value("phase_id")) continue;
			SkillContext ctx;
			ctx.skill_name = objectName();
			ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
			if (!ctx.owner) continue;
			ctx.invoker = player;
			ctx.initiator = ctx.owner;
			ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
			ctx.instanceID = ctx.sourceRef.key.instanceID;
			ctx.trigger_count = receipt.value("ready_ordinal").toInt();
			ctx.preferredTarget = player;
			ctx.preferredTargetSeat = player->getSeat();
			ctx.amount = receipt.value("amount").toInt();
			ctx.extra_data = QVariantMap{{"receipt", receipt}, {"hand", receipt.value("observed_hand")}};
			ctx.original_data = &data;
			ctx.current_event = event;
			ctx.is_forced = true;
			contexts << ctx;
		}
		return true;
	}
	bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
	{
		if (ctx.extra_data.toMap().contains("receipt")) return ctx.invoker && ctx.invoker->isAlive()
			&& ctx.invoker->getTag("mobile_xiugeng_pending").toList().contains(ctx.extra_data.toMap().value("receipt"));
		return TriggerSkillV2::isSourceAvailable(room, ctx);
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (ctx.extra_data.toMap().contains("receipt")) return true;
		ctx.targets = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName() + "$-1", 0, 2, "xiugeng0", true, true);
		return !ctx.targets.isEmpty();
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (!ctx.extra_data.toMap().contains("receipt")) return false;
		ctx.manual_effect = true;
		const QVariantMap saved = ctx.extra_data.toMap();
		const QVariantMap receipt = saved.value("receipt").toMap();
		QVariantList receipts = ctx.invoker->getTag("mobile_xiugeng_pending").toList();
		receipts.removeOne(receipt);
		ctx.invoker->setTag("mobile_xiugeng_pending", receipts);
		bool keepMark = false;
		for (const QVariant &entry : receipts) if (mark(entry.toMap()) == mark(receipt)) keepMark = true;
		room->setPlayerMark(ctx.invoker, mark(receipt), keepMark ? 1 : 0);
		// Both conditions observe the hand at this draw-phase boundary, before either effect mutates it.
		const int hand = saved.value("hand").toInt(), recorded = receipt.value("hand").toInt();
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		if (hand <= recorded) {
			ctx.extra_data = QStringLiteral("draw");
			skillEffect(event, room, player, ctx, ctx.invoker);
		}
		if (hand >= recorded && ctx.invoker->isAlive()) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			ctx.extra_data = QStringLiteral("maxcards");
			skillEffect(event, room, player, ctx, ctx.invoker);
		}
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (ctx.extra_data.toString() == "draw") { target->drawCards(2 * getEffectiveAmount(ctx), objectName()); return false; }
		if (ctx.extra_data.toString() == "maxcards") { room->addMaxCards(target, getEffectiveAmount(ctx), false); return false; }
		const quint64 serial = room->getTag("mobile_xiugeng_serial").toULongLong() + 1;
		room->setTag("mobile_xiugeng_serial", serial);
		QVariantMap receipt{{"serial", serial}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
			{"instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
			{"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
			{"author", player->objectName()}, {"hand", target->getHandcardNum()}, {"amount", getEffectiveAmount(ctx)}};
		QVariantList receipts = target->getTag("mobile_xiugeng_pending").toList();
		receipts << receipt;
		target->setTag("mobile_xiugeng_pending", receipts);
		room->setPlayerMark(target, mark(receipt), 1);
		return false;
	}
};

class Chenshe : public TriggerSkillV2
{
public:
	Chenshe() : TriggerSkillV2("chenshe") { events << Dying; }
	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const DyingStruct dying = data.value<DyingStruct>();
		// Dying is broadcast to holders: this dispatch belongs only to its holder, never all owners again.
		return player && player->isAlive() && player->hasSkill(objectName()) && dying.who && dying.who != player
			? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		const DyingStruct dying = ctx.original_data->value<DyingStruct>();
		if (!player->askForSkillInvoke(objectName() + "$-1", dying.who)) return false;
		ctx.targets = {player, dying.who};
		if (dying.damage && dying.damage->from && !ctx.targets.contains(dying.damage->from)) ctx.targets << dying.damage->from;
		ctx.extra_data = QVariantMap{{"saved", dying.who->objectName()}, {"action", "discard"}, {"success", 0}, {"suits", QVariantList()}};
		return true;
	}
	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		ctx.manual_effect = true;
		const int amount = ctx.modified_amount;
		const bool modified = ctx.modified_amount_set;
		for (ServerPlayer *target : ctx.targets) {
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, target);
		}
		QVariantMap saved = ctx.extra_data.toMap();
		const QVariantList suits = saved.value("suits").toList();
		if (saved.value("success").toInt() != ctx.targets.length() || suits.isEmpty()) return false;
		for (const QVariant &suit : suits) if (suit != suits.first()) return false;
		ServerPlayer *dying = room->findPlayerByObjectName(saved.value("saved").toString());
		if (dying && dying->isAlive()) {
			saved.insert("action", "recover");
			ctx.extra_data = saved;
			ctx.modified_amount = amount;
			ctx.modified_amount_set = modified;
			skillEffect(event, room, player, ctx, dying);
		}
		saved.insert("action", "lose");
		ctx.extra_data = saved;
		ctx.modified_amount = amount;
		ctx.modified_amount_set = modified;
		skillEffect(event, room, player, ctx, player);
		return false;
	}
	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		QVariantMap saved = ctx.extra_data.toMap();
		const QString action = saved.value("action").toString();
		if (action == "recover") {
			room->recover(target, RecoverStruct(objectName(), player, qMax(0, target->getMaxHp() - target->getHp()) * getEffectiveAmount(ctx)));
			return false;
		}
		if (action == "lose") {
			ServerPlayer *holder = room->findPlayerByObjectName(ctx.activationRef.ownerObjectName, true);
			if (holder) room->detachSkillFromPlayer(holder, SkillInstanceUtils::formatName(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID), false, true);
			return false;
		}
		const int count = getEffectiveAmount(ctx);
		if (count <= 0) return false;
		QVariantList suits = saved.value("suits").toList();
		for (int i = 0; i < count; ++i) {
			if (!player->isAlive() || !target->isAlive() || !player->canDiscard(target, "he")) return false;
			const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
			if (room->getCardOwner(id) != target || !player->canDiscard(target, id)) return false;
			const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
			const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
			if (!before.value("complete").toBool() || skillEvent <= 0) { qWarning("Chenshe: discard history unavailable"); return false; }
			room->throwCard(id, objectName(), target, player);
			QVariantMap query{{"after", before.value("watermark")}, {"from", target->objectName()}};
			bool discarded = false;
			for (;;) {
				const QVariantMap page = room->queryHistoryMoves(query);
				if (!page.value("complete").toBool()) { qWarning("Chenshe: incomplete discard result"); return false; }
				for (const QVariant &entry : page.value("items").toList()) {
					const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
					if (move.value("card_id").toInt() != id || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
						|| room->historyParent(fact.value("event_id").toLongLong(), "skill", true).value("id").toLongLong() != skillEvent) continue;
					const QVariantMap card = move.value("card_before").toMap();
					if (!card.contains("suit")) { qWarning("Chenshe: missing discarded suit snapshot"); return false; }
					suits << card.value("suit");
					discarded = true;
					break;
				}
				if (discarded || !page.value("has_more").toBool()) break;
				query.insert("after", page.value("next_after"));
				query.insert("watermark", page.value("watermark"));
			}
			if (!discarded) return false;
		}
		saved.insert("suits", suits);
		saved.insert("success", saved.value("success").toInt() + 1);
		ctx.extra_data = saved;
		return false;
	}
};

	static int getChargeMax(const Player *player)
	{
		int n = 0;
		foreach (const Skill *s, player->getVisibleSkillList()){
			QString cn = s->property("ChargeNum").toString();
			if(cn.contains("/")) n += cn.split("/").last().toInt();
		}
		return n;
	}

BsTuntianCard::BsTuntianCard()
{
}

bool BsTuntianCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *) const
{
	return targets.isEmpty()&&to->getCardCount()>0;
}

bool BsTuntianCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	return targets.length()>0||Self->getCardCount()>0;
}

void BsTuntianCard::onUse(Room *room, CardUseStruct &use) const
{
	if(use.to.isEmpty()) use.to << use.from;
	SkillCard::onUse(room,use);
}

void BsTuntianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
        room->removePlayerMark(source,"&charge_num");
		Card*dc = room->askForExchange(p,"bstuntian",1,1,true,"bstuntian0:"+source->objectName());
		if(dc) source->addToPile("bstun_tian",dc);
	}
}

class BsTuntianvs : public ZeroCardViewAsSkill
{
public:
	BsTuntianvs() : ZeroCardViewAsSkill("bstuntian")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsTuntianCard")<1&&player->getMark("&charge_num")>0;
	}

	const Card *viewAs() const
	{
		return new BsTuntianCard;
	}
};

class BsTuntian : public TriggerSkill
{
public:
	BsTuntian() : TriggerSkill("bstuntian")
	{
		events << EventPhaseChanging << CardsMoveOneTime << MarkChanged;
		view_as_skill = new BsTuntianvs;
		setProperty("ChargeNum","1/3");
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&!player->hasFlag("CurrentPlayer")&&player->hasSkill(objectName())){
				if(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip)||move.from_pile_names.contains("bstun_tian")){
					int n = getChargeMax(player)-player->getMark("&charge_num");
					if(n>0) player->gainMark("&charge_num");
				}
			}
		}else if(triggerEvent==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to==Player::NotActive
			&&player->getMark("bstuntianNum-Clear")>0&&player->hasSkill(objectName())) {
				room->sendCompulsoryTriggerLog(player,objectName());
				player->drawCards(player->getMark("bstuntianNum-Clear"),objectName());
			}
		}else{
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&charge_num"&&mark.gain<0){
				player->addMark("bstuntianNum-Clear",-mark.gain);
			}
		}
		return false;
	}
};

class BsJixivs : public OneCardViewAsSkill
{
public:
	BsJixivs() : OneCardViewAsSkill("bsjixi")
	{
		expand_pile = "bstun_tian";
	}

	bool viewFilter(const Card *to_select) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			return Self->getPile("bstun_tian").contains(to_select->getId())&&to_select->isAvailable(Self);
		}
		foreach (QString cn, pattern.split("+")) {
			if(to_select->objectName().endsWith(cn))
				return Self->getPile("bstun_tian").contains(to_select->getId());
		}
		return false;
	}

	const Card *viewAs(const Card *c) const
	{
		return c;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		foreach (QString cn, pattern.split("+")) {
			foreach (int id, player->getPile("bstun_tian")) {
				if(Sanguosha->getEngineCard(id)->objectName().endsWith(cn))
					return player->hasSkill("bstuntian",true)&&player->getMark("&zheng_rong")>0;
			}
		}
		return pattern == "@@bsjixi";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (int id, player->getPile("bstun_tian")) {
			if(Sanguosha->getEngineCard(id)->isAvailable(player))
				return player->hasSkill("bstuntian",true)&&player->getMark("&zheng_rong")>0;
		}
		return false;
	}
};

class BsJixi : public TriggerSkill
{
public:
	BsJixi() : TriggerSkill("bsjixi")
	{
		events << PreCardUsed;
		view_as_skill = new BsJixivs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(player->getPile("bstun_tian").contains(use.card->getId())){
				player->skillInvoked(objectName());
				player->loseMark("&zheng_rong");
				use.m_addHistory = false;
				data.setValue(use);
			}
		}
		return false;
	}
};

class BsZaoxian : public TriggerSkill
{
public:
	BsZaoxian() : TriggerSkill("bszaoxian")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		if(triggerEvent==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to==Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->hasSkill(objectName())&&p->hasSkill("bstuntian",true)){
						int n = p->getMark("&charge_num");
						if(n==0||n==3){
							room->sendCompulsoryTriggerLog(p,this);
							p->gainMark("&zheng_rong");
						}
					}
				}
			}
		}
		return false;
	}
};

BsQingyanCard::BsQingyanCard()
{
	m_skillName = "bsqingyan";
	target_fixed = true;
	handling_method = Card::MethodNone;
}

const Card *BsQingyanCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();
	use.from->skillInvoked("bsqingyan");
	room->addPlayerMark(use.from,"&bsqingyan_lun");
	room->showCard(use.from,subcards);
	foreach (int id, subcards) {
		room->setCardTip(id,"bsqingyan");
	}
	Card *use_card = nullptr;
	if(user_string.contains("jink"))
		use_card = Sanguosha->cloneCard("jink");
	else
		use_card = Sanguosha->cloneCard("nullification");
	use_card->setSkillName("_bsqingyan");
	use_card->deleteLater();
	return use_card;
}

const Card *BsQingyanCard::validateInResponse(ServerPlayer *from) const
{
	Room *room = from->getRoom();
	from->skillInvoked("bsqingyan");
	room->addPlayerMark(from,"&bsqingyan_lun");
	room->showCard(from,subcards);
	foreach (int id, subcards) {
		room->setCardTip(id,"bsqingyan");
	}
	Card *use_card = nullptr;
	if(user_string.contains("jink"))
		use_card = Sanguosha->cloneCard("jink");
	else
		use_card = Sanguosha->cloneCard("nullification");
	use_card->setSkillName("_bsqingyan");
	use_card->deleteLater();
	return use_card;
}

class BsQingyan : public ViewAsSkill
{
public:
	BsQingyan() : ViewAsSkill("bsqingyan")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *card) const
	{
		return cards.length()<=Self->getMark("&bsqingyan_lun")&&!card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<=Self->getMark("&bsqingyan_lun")) return nullptr;
		BsQingyanCard *sc = new BsQingyanCard;
		sc->setUserString(Sanguosha->getCurrentCardUsePattern());
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		foreach (const Card *h, player->getHandcards()) {
			if(h->hasFlag("bsqingyan")) return false;
		}
		return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		&&(pattern.contains("jink")||pattern.contains("nullification"))&&player->getHandcardNum()>qMin(4,player->getMark("&bsqingyan_lun"));
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}
};

CeduanCard::CeduanCard()
{
}

bool CeduanCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->inMyAttackRange(Self);
}

void CeduanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		QHash<QString,int> cs;
		int x = 0;
		foreach (ServerPlayer *q, room->getAllPlayers()) {
			q->setMark("ceduanId",-1);
			if(p->inMyAttackRange(q)){
				const Card*dc = room->askForCardShow(q,p,"ceduan");
				if(dc){
					cs[dc->getColorString()]++;
					x = qMax(x,cs[dc->getColorString()]);
					q->setMark("ceduanId",dc->getEffectiveId());
				}
			}
		}
		foreach (ServerPlayer *q, room->getAllPlayers()) {
			int id = q->getMark("ceduanId");
			if(id<0) continue;
			room->showCard(q,id);
		}
		QStringList cts;
		foreach (QString m, cs.keys()) {
			if(cs[m]>=x) cts << m;
		}
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName("_ceduan");
		foreach (const Card*h, source->getHandcards()) {
			if(cts.contains(h->getColorString())) dc->addSubcard(h);
		}
		if(source->canSlash(p,dc,false)){
			room->useCard(CardUseStruct(dc,source,p));
		}
		dc->deleteLater();
	}
}

class Ceduanvs : public ZeroCardViewAsSkill
{
public:
	Ceduanvs() : ZeroCardViewAsSkill("ceduan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("CeduanCard")<1;
	}

	const Card *viewAs() const
	{
		return new CeduanCard;
	}
};

class Ceduan : public TriggerSkill
{
public:
	Ceduan() : TriggerSkill("ceduan")
	{
		events << CardFinished;
		view_as_skill = new Ceduanvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())&&use.card->hasFlag("DamageDone")){
				player->drawCards(1,objectName());
			}
		}
		return false;
	}
};








mobileBsQiPackage::mobileBsQiPackage()
	: Package("mobilebs_qi")
{
	General *mobilebs_yuji = new General(this, "mobilebs_yuji", "qun", 3);
	mobilebs_yuji->addSkill(new Daozhuan);
	mobilebs_yuji->addSkill(new Fuji);
	addMetaObject<DaozhuanCard>();
	addMetaObject<FujiCard>();
	
	General *mobilebs_yanghong = new General(this, "mobilebs_yanghong", "qun", 4);
	mobilebs_yanghong->addSkill(new MobileJianji);
	mobilebs_yanghong->addSkill(new MobileYuanmo);
	addMetaObject<MobileJianjiCard>();

	General *mobilebs_lougui = new General(this, "mobilebs_lougui", "wei", 3);
	mobilebs_lougui->addSkill(new MobileJiyu);
	mobilebs_lougui->addSkill(new Guansha);
	mobilebs_lougui->addSkill(new GuanshaMax);
	related_skills.insertMulti("guansha", "#guansha-max");
	addMetaObject<MobileJiyuCard>();

	General *mobilebs_sunshao = new General(this, "mobilebs_sunshao", "wu", 4);
	mobilebs_sunshao->addSkill(new Ganjue);
	mobilebs_sunshao->addSkill(new Zhuhe);
	addMetaObject<GanjueCard>();

	General *mobilebs_dengai = new General(this, "mobilebs_dengai", "wei", 3);
	mobilebs_dengai->addSkill(new BsTuntian);
	mobilebs_dengai->addSkill(new MobileAppliedDistance("bstuntian"));
	related_skills.insert("bstuntian", "#bstuntian-distance");
	mobilebs_dengai->addSkill(new BsJixi);
	mobilebs_dengai->addSkill(new BsZaoxian);
	addMetaObject<BsTuntianCard>();









}
ADD_PACKAGE(mobileBsQi)

mobileBsZhengPackage::mobileBsZhengPackage()
	: Package("mobilebs_zheng")
{
	General *mobilebs_taishici = new General(this, "mobilebs_taishici", "wu", 4);
	mobilebs_taishici->addSkill(new BsHanzhan);
	mobilebs_taishici->addSkill(new Zhanlie);
	mobilebs_taishici->addSkill(new Zhenfeng);
	addMetaObject<BsHanzhanCard>();
	addMetaObject<ZhenfengCard>();

	General *mobilebs_chendao = new General(this, "mobilebs_chendao", "shu", 4);
	mobilebs_chendao->addSkill(new BsWanglie);
	mobilebs_chendao->addSkill(new MobileAppliedDistance("bswanglie"));
	related_skills.insert("bswanglie", "#bswanglie-distance");
	mobilebs_chendao->addSkill(new BsWangliePro);
	mobilebs_chendao->addSkill(new BsHongyi);

	General *mobilebs_sunjun = new General(this, "mobilebs_sunjun", "wu", 3);
	mobilebs_sunjun->addSkill(new BsXianshuai);
	mobilebs_sunjun->addSkill(new Xiongtu);
	addMetaObject<XiongtuCard>();
	
	General *mobilebs_tianfeng = new General(this, "mobilebs_tianfeng", "qun", 3);
	mobilebs_tianfeng->addSkill(new Ganggeng);
	mobilebs_tianfeng->addSkill(new BsSijian);
	addMetaObject<GanggengCard>();

	General *mobilebs_guoyuan = new General(this, "mobilebs_guoyuan", "wei", 3);
	mobilebs_guoyuan->addSkill(new Qingdao);
	mobilebs_guoyuan->addSkill(new Xiugeng);
	mobilebs_guoyuan->addSkill(new Chenshe);




}
ADD_PACKAGE(mobileBsZheng)

mobileBsShiPackage::mobileBsShiPackage()
	: Package("mobilebs_shi")
{
	General *mobilebs_zhangyan = new General(this, "mobilebs_zhangyan", "qun", 4);
	mobilebs_zhangyan->addSkill(new Xiaoge);
	mobilebs_zhangyan->addSkill(new Feijing);

	General *mobilebs_weiyan = new General(this, "mobilebs_weiyan", "shu", 4);
	mobilebs_weiyan->addSkill(new Zhuangshi);
	mobilebs_weiyan->addSkill(new MobileAppliedDistance("zhuangshi"));
	related_skills.insert("zhuangshi", "#zhuangshi-distance");
	mobilebs_weiyan->addSkill(new Yinzhan);
	mobilebs_weiyan->addSkill(new Zhongao);
	//Sanguosha->setAudioType("mobilebs_weiyan","kunfen","5,6");

	General *mobilebs_pangxi = new General(this, "mobilebs_pangxi", "shu", 3);
	mobilebs_pangxi->addSkill(new Xuye);
	mobilebs_pangxi->addSkill(new MobileKuangxiang);
	addMetaObject<MobileKuangxiangCard>();

	General *mobilebs_chenzhi = new General(this, "mobilebs_chenzhi", "shu", 3);
	mobilebs_chenzhi->addSkill(new Quanchong);
	mobilebs_chenzhi->addSkill(new Renxing);

	General *mobilebs_chenjiao = new General(this, "mobilebs_chenjiao", "wei", 3);
	mobilebs_chenjiao->addSkill(new BsQingyan);
	mobilebs_chenjiao->addSkill(new Ceduan);
	addMetaObject<BsQingyanCard>();
	addMetaObject<CeduanCard>();



}
ADD_PACKAGE(mobileBsShi)

mobileBsJiePackage::mobileBsJiePackage()
	: Package("mobilebs_jie")
{
	General *mobilebs_lusu = new General(this, "mobilebs_lusu", "wu", 3);
	mobilebs_lusu->addSkill(new BsHaoshi);
	mobilebs_lusu->addSkill(new BsDimeng);
	addMetaObject<BsDimengCard>();

	General *mobilebs_luyusheng = new General(this, "mobilebs_luyusheng", "wu", 3,false);
	mobilebs_luyusheng->addSkill(new BsRunwei);
	mobilebs_luyusheng->addSkill(new Shuanghuai);
	addMetaObject<BsRunweiCard>();

	General *mobilebs_xinxianying = new General(this, "mobilebs_xinxianying", "wei", 3,false);
	mobilebs_xinxianying->addSkill(new Jiejie);
	mobilebs_xinxianying->addSkill(new BsQingshi);
	skills << new Jiejievs;
	addMetaObject<JiejieCard>();

	General *mobilebs_huanjie = new General(this, "mobilebs_huanjie", "wei", 3);
	mobilebs_huanjie->addSkill(new BsGongmou);
	mobilebs_huanjie->addSkill(new Zhengshuo);
	addMetaObject<ZhengshuoCard>();

}
ADD_PACKAGE(mobileBsJie)
