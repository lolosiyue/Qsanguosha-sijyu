#include "yinhu.h"
//#include "skill.h"
#include "engine.h"
//#include "client.h"
//#include "god.h"
//#include "standard.h"
#include "maneuvering.h"
#include "clientplayer.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "json.h"
//#include "clientstruct.h"
#include "wind.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
#include <memory>

YHShecuoCard::YHShecuoCard()
{
}

void YHShecuoCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	QStringList choices;
	choices << "limit=" + to->objectName() << "shuffle=" + to->objectName();
	QString choice = room->askForChoice(from, "yhshecuo", choices.join("+"), QVariant::fromValue(to));
	if (choice.startsWith("limit"))
		room->setPlayerMark(to, "&yhshecuo1", 1);
	else
		room->setPlayerMark(to, "&yhshecuo2", 1);
}

class YHShecuoVS : public ViewAsSkillV2
{
public:
	YHShecuoVS() : ViewAsSkillV2("yhshecuo") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{
		return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator
		    && candidate->isAlive();
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{ return selected.size() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHShecuoCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (request.selectedTargetNames.size() != 1) return nullptr;
		return ViewAsSkillV2::createCard(request);
	}
	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!ctx.invoker || !target) return FinishSkill;
		Room *room = ctx.invoker->getRoom();
		QStringList choices;
		choices << "limit=" + target->objectName() << "shuffle=" + target->objectName();
		const QString choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"),
		                                            QVariant::fromValue(target));
		room->setPlayerMark(target, choice.startsWith("limit") ? "&yhshecuo1" : "&yhshecuo2", 1);
		return ContinueEffects;
	}
};

class YHShecuo : public TriggerSkillV2
{
public:
	YHShecuo() : TriggerSkillV2("yhshecuo")
	{
		events << EventPhaseChanging << EventPhaseStart;
		view_as_skill = new YHShecuoVS;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		if (!target || !target->isAlive() || !target->hasSkill(objectName())) return {};
		if (event == EventPhaseStart && target->getPhase() != Player::Discard) return {};
		if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
		return TriggerList{{target, {objectName()}}};
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (event == EventPhaseStart) {
			if (player->getMark("&yhshecuo2") <= 0) return false;
			room->setPlayerMark(player, "&yhshecuo2", 0);

			QString fulin = player->property("fulin_list").toString();
			if (fulin.isEmpty()) return false;
			QStringList fulins = fulin.split("+");
			QList<int> fulin_ids = ListS2I(fulins), hands = player->handCards(), ids;
			foreach (int id, fulin_ids) {
				if (hands.contains(id))
					ids << id;
			}
			if (ids.isEmpty()) return false;

			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = "yhshecuo";
			room->sendLog(log);
			room->broadcastSkillInvoke("yhshecuo");

			room->shuffleIntoDrawPile(player, ids, objectName(), false);
		} else {
			room->setPlayerMark(player, "&yhshecuo1", 0);
		}
		return false;
	}
};

class YHShecuoliLimit : public CardLimitSkill
{
public:
	YHShecuoliLimit() : CardLimitSkill("#yhshecuo-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->getPhase() != Player::NotActive && target->getMark("&yhshecuo1") > 0) {
			QStringList patterns, fulin_list = target->property("fulin_list").toString().split("+");
			foreach (const Card *card, target->getHandcards()) {
				QString str = card->toString();
				if (fulin_list.contains(str))
					patterns << str;
			}
			return patterns.join(",");
		}
		return "";
	}
};

class YHYingfu : public TriggerSkillV2
{
public:
	YHYingfu() : TriggerSkillV2("yhyingfu")
	{
		events << CardsMoveOneTime << EventPhaseStart;
	}

	static QList<ServerPlayer *> getFuPlayers(ServerPlayer *player)
	{
		QList<ServerPlayer *> players;
		Room *room = player->getRoom();
		QString mark = "&yhyffu+#" + player->objectName();
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getMark(mark) > 0)
				players << p;
		}
		return players;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return {};
		QList<ServerPlayer *> fus = getFuPlayers(player);
		if (event == EventPhaseStart) {
			return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
			    && fus.isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
		}
		if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(objectName())
		    || !player->hasFlag("CurrentPlayer") || fus.isEmpty()) return {};
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to != player || move.to_place != Player::PlaceHand) return {};
		QList<int> ids;
		for (int id : move.card_ids) if (player->handCards().contains(id)) ids << id;
		return ids.isEmpty() ? TriggerList() : TriggerList{{player, {objectName()}}};
	}

	bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> fus = getFuPlayers(ctx.owner);
		if (event == EventPhaseStart) {
			ServerPlayer *fu = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@yhyingfu-invoke", true, true);
			if (!fu) return false;
			ctx.targets = {fu};
			return true;
		}
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		QList<int> ids;
		for (int id : move.card_ids) if (ctx.owner->handCards().contains(id)) ids << id;
		if (ids.isEmpty()) return false;
		ServerPlayer *fu = fus.size() == 1 ? fus.first() : room->askForPlayerChosen(ctx.owner, fus, "yhyingfu_give", "@yhyingfu-give");
		if (!fu) return false;
		ctx.targets = {fu}; ctx.extra_data = QVariant::fromValue(ids);
		return true;
	}

	bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (event == EventPhaseStart) {
			room->broadcastSkillInvoke(this);

			LogMessage log;
			log.type = "#GetMark";
			log.from = target;
			log.arg = "yhyffu";
			log.arg2 = QString::number(1);
			room->sendLog(log);

			room->setPlayerMark(target, "&yhyffu+#" + player->objectName(), 1);

			room->gainMaxHp(target, 1, objectName());
			QString kingdom = target->getKingdom();
			if (player->getKingdom() != kingdom)
				room->setPlayerProperty(player, "kingdom", kingdom);
		} else {
			const QList<int> ids = ctx.extra_data.value<QList<int>>();
			if (ids.isEmpty() || !target->isAlive()) return false;
			room->sendCompulsoryTriggerLog(player, this);
			room->doAnimate(1, player->objectName(), target->objectName());
			room->giveCard(player, target, ids, objectName());
		}
		return false;
	}
};

class YHNabi : public TriggerSkillV2
{
public:
	YHNabi() : TriggerSkillV2("yhnabi")
	{
		events << DamageInflicted;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		return event == DamageInflicted && player && player->isAlive() && player->hasSkill(objectName())
		    && !YHYingfu::getFuPlayers(player).isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
	}
	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const QList<ServerPlayer *> fus = YHYingfu::getFuPlayers(ctx.owner);
		ServerPlayer *fu = room->askForPlayerChosen(ctx.owner, fus, objectName(), "@yhnabi-invoke", true, true);
		if (!fu) return false;
		ctx.targets = {fu};
		return true;
	}
	bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!ctx.original_data || !target) return false;
		ctx.owner->getRoom()->broadcastSkillInvoke(this);
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		damage.to = target; damage.transfer = true; damage.transfer_reason = "yhnabi";
		damage.tips << "yhnabi:" + ctx.owner->objectName();
		*ctx.original_data = QVariant::fromValue(damage);
		return true;
	}
};

class YHNabiTransfer : public TriggerSkillV2
{
public:
	YHNabiTransfer() : TriggerSkillV2("#yhnabi")
	{
		events << DamageComplete;
	}

	int getPriority(TriggerEvent) const
	{
		return 0;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data) const override
	{
		if (event != DamageComplete || !target) return {};
		const DamageStruct damage = data.value<DamageStruct>();
		return damage.transfer && damage.transfer_reason == "yhnabi"
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room* room, ServerPlayer *, SkillContext &ctx) const override
	{
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (!damage.transfer || damage.transfer_reason != "yhnabi") return false;

		ServerPlayer *player = nullptr;
		foreach (QString tip, damage.tips) {
			if (!tip.startsWith("yhnabi:")) continue;
			QStringList tips = tip.split(":");
			if (tips.length() != 2) return false;
			player = room->findChild<ServerPlayer *>(tips.last());
			break;
		}
		if (!player || player->isDead()) return false;

		JudgeStruct judge;
		judge.who = player;
		judge.reason = "yhnabi";
		judge.pattern = ".|heart";
		judge.good = false;
		room->judge(judge);

		if (judge.isBad() || player->isDead()) return true;

		QStringList choices;
		if (player->isWounded())
			choices << "recover";
		choices << "draw";

		QString choice = room->askForChoice(player, "yhnabi", choices.join("+"));
		if (choice == "recover")
			room->recover(player, RecoverStruct("yhnabi", player));
		else
			player->drawCards(2, "yhnabi");

		return false;
	}
};

class YHHuanglong : public TriggerSkillV2
{
public:
	YHHuanglong() : TriggerSkillV2("yhhuanglong")
	{
		frequency = Wake;
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::NotActive
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool huanglongJudge(ServerPlayer *player, int type) const
	{
		Room *room = player->getRoom();
		if (type == 1) {
			int hp = player->getHp();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getHp() >= hp)
					return false;
			}
		} else {
			int hand = player->getHandcardNum();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getHandcardNum() >= hand)
					return false;
			}
		}
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName()) || p->getMark(objectName()) > 0) continue;
			if (p->canWake(objectName()) || huanglongJudge(p, 1) || huanglongJudge(p, 2)) {
				room->sendCompulsoryTriggerLog(p, this);
				room->doSuperLightbox(p, "yhhuanglong");
				room->setPlayerMark(p, "yhhuanglong", 1);
				if (room->changeMaxHpForAwakenSkill(p, 0, objectName())) {
					foreach (ServerPlayer *fu, YHYingfu::getFuPlayers(p)) {
						if (fu->isDead()) continue;
						LogMessage log;
						log.type = "#LoseMark";
						log.from = fu;
						log.arg = "yhyffu";
						log.arg2 = QString::number(1);
						room->sendLog(log);
						room->setPlayerMark(fu, "&yhyffu+#" + p->objectName(), 0);
						room->loseMaxHp(fu, 1, objectName());
					}
					if (p->getKingdom() != "wu")
						room->setPlayerProperty(p, "kingdom", "wu");
					room->handleAcquireDetachSkills(p, "-yhyingfu|-yhnabi|tenyearzhiheng");
					p->gainAnExtraTurn();
				}
			}
		}
		return false;
	}
};

YHYijieCard::YHYijieCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
	mute = true;
}

bool YHYijieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	GodSalvation *gs = new GodSalvation(Card::SuitToBeDecided, -1);
	gs->addSubcards(subcards);
	gs->setSkillName("yhyijie");
	gs->deleteLater();

	return !Self->isLocked(gs) && targets.isEmpty() && !Self->isProhibited(to_select, gs, targets);
}

void YHYijieCard::onUse(Room *room, CardUseStruct &card_use) const
{
	room->addPlayerHistory(card_use.from, "YHYijieCard");

	GodSalvation *gs = new GodSalvation(Card::SuitToBeDecided, -1);
	gs->addSubcards(subcards);
	gs->setSkillName("yhyijie");
	gs->deleteLater();

	if (card_use.from->isLocked(gs)) return;

	foreach (ServerPlayer *p, card_use.to)
		room->addPlayerMark(p, "yhyijie_target-PlayClear");
	room->useCard(CardUseStruct(gs, card_use.from, card_use.to), true);

	QList<ServerPlayer *> targets;
	targets << card_use.from << card_use.to;
	room->sortByActionOrder(targets);
	room->drawCards(targets, 1, "yhyijie");
}

class YHYijieVS : public ViewAsSkillV2
{
public:
	YHYijieVS() : ViewAsSkillV2("yhyijie") { setResponseOrUse(true); setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
		    || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
		    && request.initiator->canDiscard(request.initiator, "h");
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.selectedCardIds.isEmpty() && request.initiator && card
		    && card->getSuit() == Card::Heart && !card->isEquipped()
		    && request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{
		if (!request.initiator || !candidate || !selected.isEmpty()) return false;
		GodSalvation gs(Card::SuitToBeDecided, -1);
		gs.addSubcard(request.selectedCardIds.first());
		return !request.initiator->isLocked(&gs) && !request.initiator->isProhibited(candidate, &gs, selected);
	}
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
	{ return selected.size() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHYijieCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		YHYijieCard *card = new YHYijieCard;
		card->addSubcard(request.selectedCardIds.first());
		return card;
	}
};

class YHYijie : public TriggerSkillV2
{
public:
	YHYijie() : TriggerSkillV2("yhyijie")
	{
		events << PreCardUsed;
		view_as_skill = new YHYijieVS;
	}

	int getPriority(TriggerEvent) const
	{
		return 7;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return event == PreCardUsed && player && player->isAlive() && player->hasSkill(objectName())
	    ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card->isKindOf("GodSalvation") || !use.card->getSkillNames().contains(objectName())) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getMark("yhyijie_target-PlayClear") > 0) {
				room->setPlayerMark(p, "yhyijie_target-PlayClear", 0);
				targets << p;
			}
		}
		if (targets.isEmpty()) return false;
		room->sortByActionOrder(targets);
		use.to = targets;
		*ctx.original_data = QVariant::fromValue(use);
		return false;
	}
};

class YHXinghanVS : public ViewAsSkillV2
{
public:
	YHXinghanVS() : ViewAsSkillV2("yhxinghan") { setResponseOrUse(true); change_skill = true; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.initiator->getPhase() == Player::Play
		    && request.initiator->getMark("yhxinghan-PlayClear") == 1
		    && Slash::IsAvailable(request.initiator)
		    && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
		        || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.selectedCardIds.isEmpty() && request.initiator && card
		    && card->isKindOf("BasicCard") && request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		const Card *source = Sanguosha->getCard(request.selectedCardIds.first());
		Slash *slash = new Slash(source->getSuit(), source->getNumber());
		slash->addSubcard(source);
		slash->setSkillName(objectName());
		return slash;
	}
};

class YHXinghan : public TriggerSkillV2
{
public:
	YHXinghan() : TriggerSkillV2("yhxinghan")
	{
		events << EventPhaseStart;
		view_as_skill = new YHXinghanVS;
		change_skill = true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
	    && player->getPhase() == Player::Play ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (player->getHandcardNum() < 2) return false;
		int state = player->getChangeSkillState(objectName());

		int n = 0;
		bool optional = true, notify = true;
		QString prompt = "@yhxinghan-give";
		QHash<ServerPlayer *, QStringList> hash;
		QList<int> hands = player->handCards();

		while (n < 2) {
			if (hands.isEmpty()) break;
			if (n != 0) {
				optional = false;
				notify = false;
				prompt = "@yhxinghan-give2";
			}

			CardsMoveStruct move = room->askForYijiStruct(player, hands, objectName(), false, false, optional, 2 - n, QList<ServerPlayer *>(),
														CardMoveReason(), prompt, notify, false);
			if (!move.to || move.card_ids.isEmpty()) break;
			n += move.card_ids.length();

			ServerPlayer *to = (ServerPlayer *)move.to;
			QStringList ids = hash[to];
			foreach (int id, move.card_ids) {
				QString str = QString::number(id);
				hands.removeOne(id);
				if (!ids.contains(str))
					ids << str;
			}
			hash[to] = ids;
		}

		QList<CardsMoveStruct> moves;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead()) continue;
			QList<int> ids = ListS2I(hash[p]);
			if (ids.isEmpty()) continue;
			CardsMoveStruct move(ids, player, p, Player::PlaceHand, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName(), p->objectName(), objectName(), ""));
			moves.append(move);
		}
		if (moves.isEmpty()) return false;
		room->moveCardsAtomic(moves, false);

		room->setPlayerMark(player, "yhxinghan-PlayClear", state);
		room->setChangeSkillState(player, objectName(), state == 1 ? 2 : 1);
		return false;
	}
};

class YHXinghanDraw : public TriggerSkillV2
{
public:
	YHXinghanDraw() : TriggerSkillV2("#yhxinghan-draw")
	{
		events << EventPhaseEnd;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == EventPhaseEnd && target && target->isAlive() && target->getMark("yhxinghan-PlayClear") > 0
		    && target->getPhase() == Player::Play ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		room->sendCompulsoryTriggerLog(player, "yhxinghan", true, true);
		player->drawCards(2, "yhxinghan");
		return false;
	}
};

class YHXinghanTarget : public TargetModSkillV2
{
public:
	YHXinghanTarget() : TargetModSkillV2("#yhxinghan-target", "Slash")
	{
		frequency = NotFrequent;
		change_skill = true;
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == TargetModSkill::Residue && ctx.primary
		    && ctx.primary->getMark("yhxinghan-PlayClear") == 2 && ctx.primary->getPhase() == Player::Play
		    ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
	}
};

YHZhushiCard::YHZhushiCard()
{
	will_throw = false;
	mute = true;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void YHZhushiCard::onUse(Room *, CardUseStruct &) const
{
}

class YHZhushiVS : public ViewAsSkillV2
{
public:
	YHZhushiVS() : ViewAsSkillV2("yhzhushi") { response_pattern = "@@yhzhushi!"; expand_pile = "yhzsshi"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.pattern == "@@yhzhushi!"; }
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		if (!request.initiator || !card || request.selectedCardIds.contains(card->getEffectiveId())
		    || !request.initiator->getPile("yhzsshi").contains(card->getEffectiveId())) return false;
		for (int id : request.selectedCardIds)
			if (Sanguosha->getCard(id)->getTypeId() == card->getTypeId()) return false;
		return true;
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty()) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		for (int id : request.selectedCardIds) {
			if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false;
			prefix.selectedCardIds << id;
		}
		return true;
	}
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHZhushiCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		YHZhushiCard *card = new YHZhushiCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
};

class YHZhushi : public TriggerSkillV2
{
public:
	YHZhushi() : TriggerSkillV2("yhzhushi")
	{
		frequency = Compulsory;
		view_as_skill = new YHZhushiVS;
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
	    && player->getPhase() == Player::Start ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		QList<int> shi = player->getPile("yhzsshi");
		if (shi.isEmpty()) return false;

		room->sendCompulsoryTriggerLog(player, this);

		const Card *card = room->askForUseCard(player, "@@yhzhushi!", "@yhzhushi", -1, Card::MethodNone);
		QList<int> get;
		if (card)
			get = card->getSubcards();
		else {
			int id = shi.at(qsanRandomBounded(shi.length()));
			get << id;
		}

		LogMessage log;
		log.type = "$KuangbiGet";
		log.from = player;
		log.arg = "yhzsshi";
		log.card_str = ListI2S(get).join("+");
		room->sendLog(log);

		DummyCard *dummy = new DummyCard(get);
		dummy->deleteLater();

		player->obtainCard(dummy);
		foreach (int id, get)
			shi.removeOne(id);
		if (shi.isEmpty()) return false;

		DummyCard *dummy2 = new DummyCard(shi);
		dummy2->deleteLater();
		CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), "yhzhushi", "");
		room->moveCardTo(dummy2, nullptr, Player::DrawPile, reason, true);
		return false;
	}
};

class YHZhushiPut : public TriggerSkillV2
{
public:
	YHZhushiPut() : TriggerSkillV2("#yhzhushi")
	{
		frequency = Compulsory;
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == EventPhaseStart && target && target->isAlive() && !target->isNude()
		    && target->getPhase() == Player::Finish ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->isDead() || player->isNude()) return false;
			if (p->isDead() || !p->hasSkill("yhzhushi")) continue;
			room->sendCompulsoryTriggerLog(p, "yhzhushi", true, true);

			int num = 1;
			if (p->getMark("yhshijin") > 0) {
				int card_num = player->getCardCount();
				card_num = qMin(card_num, 2) + 1;
				QStringList choices;
				for (int i = 0; i < card_num; i++)
					choices << QString::number(i) + "=" + player->objectName();
				QString choice = room->askForChoice(p, "yhzhushi", choices.join("+"), QVariant::fromValue(player));
				num = choice.split("=").first().toInt();
			}

			if (num <= 0) continue;

			QString prompt = "@yhzsshi-put:" + p->objectName() + "::" + QString::number(num);
			const Card *card = room->askForExchange(player, "yhzhushi", num, num, true, prompt);
			int length = card->subcardsLength();
			p->addToPile("yhzsshi", card);
			player->drawCards(length, "yhzhushi");
		}
		return false;
	}
};

class YHQubi : public TriggerSkillV2
{
public:
    YHQubi() : TriggerSkillV2("yhqubi") { events << DrawNCards; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || !player->isAlive() || draw.reason == "InitialHandCards" || draw.num <= 1) return result;
        // DrawNCards has one drawing actor; sources remain the individual Qubi holders.
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return true; }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != ctx.invoker) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        draw.top = false;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class YHShijin : public TriggerSkillV2
{
public:
	YHShijin() : TriggerSkillV2("yhshijin")
	{
		frequency = Wake;
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && player && player->isAlive()
		    && player->getPhase() == Player::RoundStart && player->getMark(objectName()) < 1
		    && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		QStringList kingdoms;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			QString kingdom = p->getKingdom();
			if (kingdoms.contains(kingdom)) continue;
			kingdoms << kingdom;
		}
		if (kingdoms.length() > 2&& !player->canWake(objectName())) return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->doSuperLightbox(player, "yhshijin");
		room->setPlayerMark(player, "yhshijin", 1);
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName()))
			room->changeTranslation(player, "yhzhushi", 1);
		return false;
	}
};

YHBianzhanCard::YHBianzhanCard()
{
	setSkillName("yhbianzhan");
}

bool YHBianzhanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select) && to_select->getHandcardNum() > Self->getHandcardNum();
}

void YHBianzhanCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;


	PindianStruct *pindian = from->PinDian(to, "yhbianzhan");
	if (!pindian->success) return;

	from->drawCards(2, "yhbianzhan");
	Room *room = from->getRoom();
	if (room->getCardPlace(pindian->from_card->getEffectiveId()) != Player::DiscardPile) return;

	LogMessage log;
	log.type = "$PutCard2";
	log.from = from;
	log.card_str = pindian->from_card->toString();
	room->sendLog(log);

	CardMoveReason reason(CardMoveReason::S_REASON_PUT, from->objectName(), "yhbianzhan", "");
	room->moveCardTo(pindian->from_card, nullptr, Player::DrawPile, reason, true);
}

class YHBianzhan : public ViewAsSkillV2
{
public:
	YHBianzhan() : ViewAsSkillV2("yhbianzhan")
	{
		m_baseAmount = 2;
	}

	QString historyKey(const ActiveSkillRequest &) const override { return "YHBianzhanCard"; }
	TargetMode targetMode() const override { return SelectTargets; }

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canPindian();
	}

	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{
		return request.initiator && candidate && candidate->isAlive() && selected.isEmpty()
		    && request.initiator->canPindian(candidate) && candidate->getHandcardNum() > request.initiator->getHandcardNum();
	}

	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
	}

	EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!ctx.invoker->canPindian(target)) return ContinueEffects;
		std::unique_ptr<PindianStruct> selection(ctx.invoker->pindianSelect(target, objectName()));
		PindianStruct *pindian = ctx.invoker->finishPindian(selection.get());
		if (!pindian || !pindian->success || !pindian->from_card) return ContinueEffects;
		const int id = pindian->from_card->getEffectiveId();
		ctx.invoker->drawCards(getEffectiveAmount(ctx), objectName());
		Room *room = ctx.invoker->getRoom();
		if (room->getCardPlace(id) != Player::DiscardPile) return ContinueEffects;
		LogMessage log;
		log.type = "$PutCard2";
		log.from = ctx.invoker;
		log.card_str = Sanguosha->getCard(id)->toString();
		room->sendLog(log);
		CardMoveReason reason(CardMoveReason::S_REASON_PUT, ctx.invoker->objectName(), objectName(), "");
		room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, reason, true);
		return ContinueEffects;
	}
};

class YHJifeng : public TriggerSkillV2
{
public:
	YHJifeng() : TriggerSkillV2("yhjifeng")
	{
		events << EventPhaseEnd;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play
            || !player->canDiscard(player, "h") || player->getHandcardNum() < 2) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            if (!player->hasSkillInstance(objectName(), id) || player->isSkillInvalid(objectName(), id)) continue;
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (room->getShimingStatus(ref) <= 0)
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
		if (!room->askForDiscard(player, objectName(), 2, 2, true, false, "@yhjifeng-discard", ".", objectName())) return false;
		ctx.sourceRef = ctx.activationRef;
		return true;
	}

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
		const SkillInstanceRef ref = ctx.activationRef;
		room->broadcastSkillInvoke(this, 1);

		QList<int> ids = room->showDrawPile(player, 1, objectName());
		if (ids.isEmpty()) return false;
		if (player->isDead()) {
			DummyCard *dummy = new DummyCard(ids);
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "yhjifeng", "");
			room->throwCard(dummy, reason, nullptr);
			dummy->deleteLater();
			return false;
		}

		int id = ids.first();
		room->obtainCard(player, id);

		const Card *card = Sanguosha->getCard(id);

		Card::Suit suit = card->getSuit();
		if (suit == Card::Diamond) {
			if (!room->sendShimingLog(ref)) return false;
			room->acquireSkill(player, "olhuoji");
			if (player->isDead()) return false;
			int number = card->getNumber();
			ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@yhjifeng-target:" + QString::number(number));
			room->addPlayerMark(target, "&yhjfkuangfeng", number);
		} else if (suit == Card::Spade) {
			if (!room->sendShimingLog(ref, false)) return false;
			room->acquireSkill(player, "bazhen");
			player->throwAllHandCards();
		}
		return false;
	}
};

class YHJifengEffect : public TriggerSkillV2
{
public:
	YHJifengEffect() : TriggerSkillV2("#yhjifeng")
	{
		events << EventPhaseChanging << DamageForseen;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		if (!target) return {};
		if (event == EventPhaseChanging)
			return data.value<PhaseChangeStruct>().to == Player::NotActive ? TriggerList{{target, {objectName()}}} : TriggerList();
		return event == DamageForseen && target->isAlive() && target->getMark("&yhjfkuangfeng") > 0
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == EventPhaseChanging) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				int mark = p->getMark("&yhjfkuangfeng");
				if (mark <= 0) continue;
				room->removePlayerMark(p, "&yhjfkuangfeng");
			}
		} else {
			if (player->isDead() || player->getMark("&yhjfkuangfeng") <= 0) return false;
			DamageStruct damage = ctx.original_data->value<DamageStruct>();
			if (damage.nature == DamageStruct::Fire) {
				LogMessage log;
				log.type = "#GalePower";
				log.from = player;
				log.arg = QString::number(damage.damage);
				log.arg2 = QString::number(++damage.damage);
				room->sendLog(log);
				*ctx.original_data = QVariant::fromValue(damage);
			}
		}
		return false;
	}
};

YHHuntianCard::YHHuntianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void YHHuntianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "yhhuntian", "");
	room->moveCardTo(this, nullptr, Player::DrawPile, reason, false, true);

	if (source->isAlive() && source->askForSkillInvoke("yhhuntian", "yhhuntian", false)) {
		QList<int> card_ids = room->showDrawPile(source, 4, "yhhuntian");

		if (source->isDead()) {
			DummyCard *dummy = new DummyCard(card_ids);
			CardMoveReason reason2(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), "yhhuntian", "");
			room->throwCard(dummy, reason2, nullptr);
			dummy->deleteLater();
			return;
		}

		room->fillAG(card_ids);

		QList<int> to_get, to_throw;
		while (!card_ids.isEmpty()) {
			int card_id = room->askForAG(source, card_ids, false, "yhhuntian");
			card_ids.removeOne(card_id);
			to_get << card_id;
			// throw the rest cards that matches the same suit
			const Card *card = Sanguosha->getCard(card_id);
			Card::Suit suit = card->getSuit();

			room->takeAG(source, card_id, false);

			QList<int> _card_ids = card_ids;
			foreach (int id, _card_ids) {
				const Card *c = Sanguosha->getCard(id);
				if (c->getSuit() == suit) {
					card_ids.removeOne(id);
					room->takeAG(nullptr, id, false);
					to_throw.append(id);
				}
			}
		}
		room->getThread()->delay();

		room->clearAG();

		DummyCard *dummy = new DummyCard;
		if (!to_get.isEmpty()) {
			dummy->addSubcards(to_get);
			source->obtainCard(dummy);
		}
		dummy->clearSubcards();

		if (!to_throw.isEmpty()) {
			room->fillAG(to_throw, source);
			QString choice = room->askForChoice(source, "yhhuntian", "enter+put", ListI2V(to_throw));
			room->clearAG(source);
			dummy->addSubcards(to_throw);
			if (choice == "enter") {
				CardMoveReason reason2(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), "yhhuntian", "");
				room->throwCard(dummy, reason2, nullptr);
			} else {
				LogMessage log;
				log.type = "$PutCard2";
				log.from = source;
				log.card_str = ListI2S(dummy->getSubcards()).join("+");
				room->sendLog(log);
				CardMoveReason reason2(CardMoveReason::S_REASON_PUT, source->objectName(), "yhhuntian", "");
				room->moveCardTo(dummy, nullptr, Player::DrawPile, reason2, true, true);
			}
		}
		dummy->deleteLater();
	}
}

class YHHuntian : public ViewAsSkillV2
{
public:
	YHHuntian() : ViewAsSkillV2("yhhuntian") { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && card && request.selectedCardIds.size() < 4
		    && request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.isEmpty() || request.selectedCardIds.size() > 4) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		for (int id : request.selectedCardIds) {
			if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false;
			prefix.selectedCardIds << id;
		}
		return true;
	}
	QString historyKey(const ActiveSkillRequest &) const override { return "YHHuntianCard"; }
	bool willThrowSelectedCards() const override { return false; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		YHHuntianCard *card = new YHHuntianCard;
		card->addSubcards(request.selectedCardIds);
		return card;
	}
};

class YHCeri : public TriggerSkillV2
{
public:
	YHCeri() : TriggerSkillV2("yhceri")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
		    && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		QList<ServerPlayer *> targets;
		int hand = player->getHandcardNum();
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHandcardNum() < hand)
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@yhceri-invoke", true, true);
		if (!t) return false;
		room->broadcastSkillInvoke(this);

		int n = t->getHandcardNum();

		QList<CardsMoveStruct> exchangeMove;
		CardsMoveStruct move1(player->handCards(), t, Player::PlaceHand,
			CardMoveReason(CardMoveReason::S_REASON_SWAP, player->objectName(), t->objectName(), "yhceri", ""));
		CardsMoveStruct move2(t->handCards(), player, Player::PlaceHand,
			CardMoveReason(CardMoveReason::S_REASON_SWAP, t->objectName(), player->objectName(), "yhceri", ""));
		exchangeMove.push_back(move1);
		exchangeMove.push_back(move2);
		room->moveCardsAtomic(exchangeMove, false);

		LogMessage log;
		log.type = "#Dimeng";
		log.from = player;
		log.to << t;
		log.arg = QString::number(hand);
		log.arg2 = QString::number(n);
		room->sendLog(log);
		room->getThread()->delay();

		if (t->isDead()) return false;

		QString choice = room->askForChoice(t, objectName(), "3,4,5+6,8,10+5,12,13", QVariant::fromValue(player));

		log.type = "#FumianFirstChoice";
		log.from = t;
		log.arg = "yhceri:" + choice;
		room->sendLog(log);

		QStringList choices = choice.split(",");
		int num1 = choices.first().toInt(), num2 = choices.at(1).toInt(), num3 = choices.last().toInt();
		QList<int> nums1, nums2, nums3;
		foreach (int id, room->getDiscardPile()) {
			const Card *card = Sanguosha->getCard(id);
			if (card->getNumber() == num1)
				nums1 << id;
			else if (card->getNumber() == num2)
				nums2 << id;
			else if (card->getNumber() == num3)
				nums3 << id;
		}

		if (nums1.isEmpty() || nums2.isEmpty() || nums3.isEmpty()) {
			if (player->isDead()) return false;
			room->damage(DamageStruct(objectName(), t->isAlive() ? t : nullptr, player));
		} else {
			if (t->isDead()) return false;
			DummyCard *dummy = new DummyCard;
			dummy->deleteLater();
			num1 = nums1.at(qsanRandomBounded(nums1.length()));
			num2 = nums2.at(qsanRandomBounded(nums2.length()));
			num3 = nums3.at(qsanRandomBounded(nums3.length()));
			dummy->addSubcard(num1);
			dummy->addSubcard(num2);
			dummy->addSubcard(num3);
			room->obtainCard(t, dummy);
		}
		return false;
	}
};

class YHSancai : public TriggerSkillV2
{
public:
	YHSancai() : TriggerSkillV2("yhsancai")
	{
		frequency = Frequent;
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
		    && player->getPhase() == Player::Play ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(this);

		player->drawCards(1, objectName());

		QList<ServerPlayer *> targets = room->getOtherPlayers(player);
		QHash<ServerPlayer *, int> hash;
		QList<int> hands = player->handCards();
		while (!hands.isEmpty()) {
			if (player->isDead()) return false;
			if (targets.isEmpty() || player->isKongcheng()) break;

			CardsMoveStruct move = room->askForYijiStruct(player, hands, objectName(), false, false, true, 1, targets,
																CardMoveReason(), "@yhsancai-give", false, false);
			if (!move.to || move.card_ids.isEmpty()) break;
			ServerPlayer *to = (ServerPlayer *)move.to;
			int id = move.card_ids.first();
			hash[to] = id + 1;
			hands.removeOne(id);
			targets.removeOne(to);
			room->setPlayerFlag(to, "yhsancai_give"); //for AI
		}

		foreach (ServerPlayer *p, room->getAllPlayers(true))
			room->setPlayerFlag(p, "-yhsancai_give");

		QList<CardsMoveStruct> moves;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isDead()) continue;
			int id = hash[p] - 1;
			if (id < 0) continue;
			CardsMoveStruct move(QList<int>() << id, player, p, Player::PlaceHand, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName(), p->objectName(), objectName(), ""));
			moves.append(move);
		}
		if (moves.isEmpty()) return false;
		room->moveCardsAtomic(moves, false);

		room->addPlayerMark(player, "&yhsancai-PlayClear", moves.length());
		return false;
	}
};

class YHSancaiAttackRange : public AttackRangeSkillV2
{
public:
	YHSancaiAttackRange() : AttackRangeSkillV2("#yhsancai")
	{
		frequency = Frequent;
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		const Player *target = ctx.primary;
		return target && target->getPhase() == Player::Play
			? CorrectSkillResult::useAmount(qMax(0, target->getMark("&yhsancai-PlayClear")))
			: CorrectSkillResult::noEffect();
	}
};

class YHJuyi : public TriggerSkillV2
{
public:
    YHJuyi() : TriggerSkillV2("yhjuyi") { events << CardsMoveOneTime; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && move.from == player
            && move.to && move.to != player && move.from_places.contains(Player::PlaceHand) && move.to_place == Player::PlaceHand
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *recipient = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().to);
        if (!recipient || !recipient->isAlive()) return false;
        ctx.targets = {recipient};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "reward") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            if (target->isAlive() && room->hasCurrent()) {
                room->addSlashCishu(target, getEffectiveAmount(ctx));
                room->addPlayerMark(target, "&yhjuyi-Clear", getEffectiveAmount(ctx));
            }
            return false;
        }
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (target->canDiscard(target, "he") && room->askForDiscard(target, objectName(), getEffectiveAmount(ctx),
            getEffectiveAmount(ctx), true, true, "@yhjuyi-discard:" + ctx.owner->objectName())) return false;
        // The refusal settles on the giver, with a separate recipient hook.
        SkillContext reward = ctx; reward.choice = "reward"; reward.targets = {ctx.owner};
        skillEffect(event, room, player, reward, ctx.owner);
        return false;
    }
};
class YHHanjie : public TriggerSkillV2
{
public:
    YHHanjie() : TriggerSkillV2("yhhanjie") { events << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.card && use.card->isKindOf("Slash")
            && use.to.contains(player) && !use.card->isVirtualCard() && room->CardInTable(use.card)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "damage") {
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.card->isVirtualCard() || !room->CardInTable(use.card)) return false;
        const int id = use.card->getEffectiveId();
        room->broadcastSkillInvoke(this);
        room->obtainCard(target, use.card);
        if (!target->isAlive() || !use.from || !use.from->isAlive() || !target->canPindian(use.from, false)
            || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
            || target->isCardLimited(Sanguosha->getCard(id), Card::MethodPindian, true)) return false;
        // Own the selection before pindian callbacks can throw or retire the skill source.
        std::unique_ptr<PindianStruct> selection(target->pindianSelect(use.from, objectName(), Sanguosha->getCard(id)));
        if (!selection || !selection->from_card || !selection->to_card) return false;
        target->finishPindian(selection.get());
        use = ctx.original_data->value<CardUseStruct>();
        if (selection->success) use.nullified_list << target->objectName();
        else use.no_respond_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        if (selection->success && use.from) {
            SkillContext damage = ctx; damage.choice = "damage"; damage.targets = {use.from};
            skillEffect(event, room, player, damage, use.from);
        }
        return false;
    }
};

YHJuxianCard::YHJuxianCard()
{
	target_fixed = true;
}

void YHJuxianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<int> &drawpile = room->getDrawPile();
	if (drawpile.isEmpty()) return;
	int times = room->getTag("SwapPile").toInt() + 1;
	QVariant data = times;
	room->setTag("SwapPile",data);
	foreach (ServerPlayer *p, room->getAllPlayers())
		room->getThread()->trigger(SwapPile, room, p, data);
	
	QList<int> drawpile2 = drawpile, drawpile3 = drawpile;
	drawpile.clear();
	qsanShuffle(drawpile2);
	drawpile = drawpile2;
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_RESET_PILE, data);
	QVariant discard = JsonUtils::toJsonArray(room->getDiscardPile());
	room->doBroadcastNotify(QSanProtocol::S_COMMAND_SYNCHRONIZE_DISCARD_PILE, discard);
	foreach (ServerPlayer *p, room->getAllPlayers())
		room->getThread()->trigger(SwappedPile, room, p, data);
	
	QList<int> ids;
	for (int i = 0; i < drawpile2.length(); i++) {
		if (drawpile2[i] == drawpile3[i])
			ids << drawpile2[i];
	}
	if (ids.isEmpty()) return;

	/*QList<int> drawpile = room->getDrawPile();
	if (drawpile.isEmpty()) return;

	QList<int> _drawpile;
	foreach (int id, drawpile)
		_drawpile << id;

	QList<int> all_cards = room->getNCards(drawpile.length(), false);
	while (!all_cards.isEmpty()) {
		int id = all_cards.at(qsanRandomBounded(all_cards.length()));
		room->returnToTopDrawPile(QList<int>() << id);
		all_cards.removeOne(id);
	}
	drawpile = room->getDrawPile();

	QList<int> ids;

	for (int i = 0; i < drawpile.length(); i++) {
		if (i > _drawpile.length()) break;
		if (_drawpile.at(i) == drawpile.at(i))
			ids << drawpile.at(i);
	}
	if (ids.isEmpty()) return;*/

	source->setTag("YHJuxianIDS", ListI2V(ids));  //for AI

	QList<ServerPlayer *> targets = room->getOtherPlayers(source);
	ServerPlayer *geter = nullptr;
	room->fillAG(ids, source);

	if (targets.length() == 1) {
		room->askForAG(source, ids, true, "yhjuxian");
		geter = targets.first();
	} else
		geter = room->askForPlayerChosen(source, targets, "yhjuxian", "@yhjuxian-invoke");

	source->removeTag("YHJuxianIDS");
	room->clearAG(source);

	if (geter) {
		room->doAnimate(1, source->objectName(), geter->objectName());
		DummyCard *dummy = new DummyCard(ids);
		CardMoveReason reason(CardMoveReason::S_REASON_PREVIEWGIVE, source->objectName(), geter->objectName(), "yhjuxian", "");
		room->obtainCard(geter, dummy, reason, false);
		dummy->deleteLater();
	}
}

class YHJuxian : public ViewAsSkillV2
{
public:
	YHJuxian() : ViewAsSkillV2("yhjuxian", 2) { setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHJuxianCard"; }
	const Card *createCard(const ActiveSkillRequest &) const override { return new YHJuxianCard; }
};

class YHDujian : public TriggerSkillV2
{
public:
	YHDujian() : TriggerSkillV2("yhdujian")
	{
		events << TargetSpecified;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && use.card
		        && !use.card->isKindOf("SkillCard") && use.to.length() >= 2 && room->CardInTable(use.card)
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		QList<ServerPlayer *> targets = use.to;
		targets.removeOne(ctx.owner);
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@yhdujian-give", false, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}

	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card || !room->CardInTable(use.card)) return false;
		room->broadcastSkillInvoke(this);
		room->giveCard(ctx.invoker, target, use.card, objectName(), true);
		use.nullified_list << target->objectName();
		*ctx.original_data = QVariant::fromValue(use);
		return false;
	}
};

class YHDujianTarget : public TargetModSkillV2
{
public:
	YHDujianTarget() : TargetModSkillV2("#yhdujian", "^SkillCard")
	{
		pattern = "^SkillCard";
		setBaseAmount(1000);
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == DistanceLimit ? CorrectSkillResult::useAmount(ctx.currentAmount)
		                                   : CorrectSkillResult::noEffect();
	}
};

YHBuquePutCard::YHBuquePutCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

bool YHBuquePutCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty()&& to_select->hasSkill("yhbuque");
}

void YHBuquePutCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *target, use.to) {
		use.from->skillInvoked("yhbuque",0,target);
		QStringList choices;
		for (int i = 1; i <= qMin(3, room->getDrawPile().length()); i++)
			choices << QString::number(i);
		if (choices.isEmpty()) return;
	
		QString choice = room->askForChoice(use.from, "yhbuque_put", choices.join("+"), QVariant::fromValue(target));
		room->moveCardsInToDrawpile(use.from, subcards, "yhbuque", choice.toInt(), true);
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class YHBuquePutVS : public ViewAsSkillV2
{
public:
	YHBuquePutVS() : ViewAsSkillV2("yhbuque_put") { attached_lord_skill = true; setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{ return request.initiator && request.selectedCardIds.isEmpty() && card
	    && request.initiator->handCards().contains(card->getEffectiveId()); }
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{ return request.initiator && selected.isEmpty() && candidate && candidate->hasSkill("yhbuque"); }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.size() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHBuquePutCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		YHBuquePutCard *card = new YHBuquePutCard; card->addSubcard(request.selectedCardIds.first()); return card;
	}
};

class YHBuquePut : public TriggerSkillV2
{
public:
	YHBuquePut() : TriggerSkillV2("yhbuque_put")
	{
		events << CardsMoveOneTime;
		view_as_skill = new YHBuquePutVS;
		attached_lord_skill = true;
		global = true;
	}

	int getPriority(TriggerEvent) const
	{
		return 4;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		return event == CardsMoveOneTime && player && move.to_place == Player::DrawPile
		    && move.reason.m_skillName == "yhbuque" ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		QVariantList list = room->getTag("YHBuqueCards").toList();
		foreach (int id, move.card_ids) {
			if (list.contains(QVariant(id))) continue;
			if (room->getCardPlace(id) != Player::DrawPile) continue;
			list << id;
		}
		room->setTag("YHBuqueCards", list);
		return false;
	}
};

YHBuqueCard::YHBuqueCard()
{
	mute = true;
}

bool YHBuqueCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *card = Sanguosha->cloneCard(user_string.split("+").first());
		if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
		return card && card->targetFilter(targets, to_select, Self);
	} else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
		return false;
	}

	return false;
}

bool YHBuqueCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *card = Sanguosha->cloneCard(user_string.split("+").first());
		if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
		return card && card->targetsFeasible(targets, Self);
	}

	return true;
}

const Card *YHBuqueCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *source = card_use.from;
	Room *room = source->getRoom();

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = source;
	log.arg = "yhbuque";
	room->sendLog(log);
	room->broadcastSkillInvoke("yhbuque");
	room->notifySkillInvoked(source, "yhbuque");

	QString tl = user_string;
	if ((user_string.contains("slash") || user_string.contains("Slash")))
		tl = "slash";

	QList<int> ids, disable_ids, enable_ids;
	foreach (int id, room->getDrawPile()) {
		const Card *card = Sanguosha->getCard(id);
		if (!card->hasFlag("visible")) continue;
		if ((tl.isEmpty() && card->isKindOf("Slash") && Slash::IsAvailable(source)) ||
				(tl.isEmpty() && card->isKindOf("Analeptic") && Analeptic::IsAvailable(source)) ||
				(tl.isEmpty() && card->isAvailable(source)) || (!tl.isEmpty() && card->sameNameWith(tl)))
			enable_ids << id;
		else
			disable_ids << id;
		ids << id;
	}

	if (enable_ids.isEmpty()) {
		room->setPlayerFlag(source, "Global_YHBuqueFailed");
		return nullptr;
	}

	room->fillAG(ids, source, disable_ids);
	int id = room->askForAG(source, enable_ids, false, "yhbuque");
	room->clearAG();

	const Card *card = Sanguosha->getCard(id);
	if (!tl.isEmpty() && !card->sameNameWith(tl)) return nullptr;

	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
		room->setPlayerMark(source, "buqueCard", id + 1);
		if (!room->askForUseCard(source, "@@yhbuque", "@yhbuque:" + card->objectName()))
			room->setPlayerFlag(source, "Global_YHBuqueFailed");
		return nullptr;
	} else
		return card;
	return nullptr;
}

const Card *YHBuqueCard::validateInResponse(ServerPlayer *source) const
{
	Room *room = source->getRoom();

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = source;
	log.arg = "yhbuque";
	room->sendLog(log);
	room->broadcastSkillInvoke("yhbuque");
	room->notifySkillInvoked(source, "yhbuque");

	QString tl;
	if (user_string == "peach+analeptic")
		tl = "analeptic";
	else if ((user_string.contains("slash") || user_string.contains("Slash")))
		tl = "slash";
	else
		tl = user_string;

	QList<int> ids, disable_ids, enable_ids;
	foreach (int id, room->getDrawPile()) {
		const Card *card = Sanguosha->getCard(id);
		if (!card->hasFlag("visible")) continue;
		ids << id;
		if (card->sameNameWith(tl)) {
			enable_ids << id;
			continue;
		}
		if (tl == "analeptic" && card->isKindOf("Peach")) {
			enable_ids << id;
			continue;
		}
		disable_ids << id;
	}

	if (enable_ids.isEmpty()) {
		room->setPlayerFlag(source, "Global_YHBuqueFailed");
		return nullptr;
	}

	room->fillAG(ids, nullptr, disable_ids);
	int id = room->askForAG(source, enable_ids, true, "yhbuque");
	room->clearAG();

	const Card *card = Sanguosha->getCard(id);
	if (card->sameNameWith(tl) || (tl == "analeptic" && card->isKindOf("Peach")))
		return card;

	return nullptr;
}

class YHBuqueVS : public ViewAsSkillV2
{
public:
	YHBuqueVS() : ViewAsSkillV2("yhbuque") { setResponseOrUse(true); }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.initiator->hasFlag("Global_YHBuqueFailed")) return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
		if (request.pattern == "@@yhbuque") return true;
		if (request.pattern == "nullification") return true;
		if (request.pattern == "peach" && request.initiator->getMark("Global_PreventPeach") > 0) return false;
		for (const QString &name : request.pattern.split(QRegExp("[+,]"))) {
			Card *card = Sanguosha->cloneCard(name.toLower());
			if (card && card->isKindOf("BasicCard")) { card->deleteLater(); return true; }
			if (card) card->deleteLater();
		}
		return false;
	}
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHBuqueCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!canActivate(request)) return nullptr;
		if (request.pattern == "@@yhbuque") {
			const int id = request.initiator->getMark("buqueCard") - 1;
			return id >= 0 ? Sanguosha->getCard(id) : nullptr;
		}
		YHBuqueCard *card = new YHBuqueCard; card->setUserString(request.pattern); return card;
	}
};

class YHBuque : public TriggerSkillV2
{
public:
	YHBuque() : TriggerSkillV2("yhbuque")
	{
		view_as_skill = new YHBuqueVS;
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return target && target->isAlive() && (event == EventPhaseStart || event == EventPhaseEnd || event == EventAcquireSkill)
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (triggerEvent == EventAcquireSkill&&player->hasSkill(objectName(),true)) {
			const QList<int> ids = player->getValidSkillInstanceIds(objectName());
			if (ids.isEmpty()) return false;
			const SkillInstanceRef source(player->objectName(), SkillInstanceKey(objectName(), ids.first()));
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("yhbuque_put",true)){
					room->attachSkillToPlayer(p, "yhbuque_put", source);
				}
			}
		}else if(triggerEvent == EventAcquireSkill || player->getPhase()!=Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasSkill(objectName(),true)){
					const QList<int> ids = p->getValidSkillInstanceIds(objectName());
					if (ids.isEmpty()) break;
					const SkillInstanceRef source(p->objectName(), SkillInstanceKey(objectName(), ids.first()));
					room->attachSkillToPlayer(player, "yhbuque_put", source);
					break;
				}
			}
		}else{
			for (const SkillInstance &instance : player->getSkillInstances()) {
				if (instance.skillName == "yhbuque_put" && instance.source == SourceAttached
				    && instance.parentRef.key.skillName == objectName())
					room->detachAttachedSkill(SkillInstanceRef(player->objectName(), instance.key()));
			}
		}
		return false;
	}
};

class YHBuquePindian : public TriggerSkillV2
{
public:
	YHBuquePindian() : TriggerSkillV2("#yhbuque")
	{
		events << AskforPindianCard;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == AskforPindianCard && target ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<int> ids;
		foreach (int id, room->getDrawPile()) {
			const Card *card = Sanguosha->getCard(id);
			if (!card->hasFlag("visible")) continue;
			ids << id;
		}
		if (ids.isEmpty()) return false;
		PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead()) continue;
			if (pindian->from != p && pindian->to != p) continue;
			if (!p->hasSkill("yhbuque")) continue;
			if (pindian->from == p) {
				if (pindian->from_card) continue;
				room->fillAG(ids, p);
				bool invoke = p->askForSkillInvoke("yhbuque", *ctx.original_data);
				room->clearAG(p);
				if (!invoke) continue;
				room->broadcastSkillInvoke("yhbuque");
				room->fillAG(ids, p);
				int id = room->askForAG(p, ids, false, "yhbuque");
				room->clearAG(p);
				pindian->from_card = Sanguosha->getCard(id);
			} else if (pindian->to == p) {
				if (pindian->to_card) continue;
				room->fillAG(ids, p);
				bool invoke = p->askForSkillInvoke("yhbuque", *ctx.original_data);
				room->clearAG(p);
				if (!invoke) continue;
				room->broadcastSkillInvoke("yhbuque");
				room->fillAG(ids, p);
				int id = room->askForAG(p, ids, false, "yhbuque");
				room->clearAG(p);
				pindian->to_card = Sanguosha->getCard(id);
			}
		}
		return false;
	}
};

class YHChenzhen : public TriggerSkillV2
{
public:
	YHChenzhen() : TriggerSkillV2("yhchenzhen")
	{
		events << StartJudge << EventPhaseChanging;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return target && (event == StartJudge || event == EventPhaseChanging)
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		if (event == EventPhaseChanging) {
			if (ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive) return false;
			room->setTag("YHChenzhenJudge", false);
		} else {
			if (!room->hasCurrent() || room->getTag("YHChenzhenJudge").toBool()) return false;
			room->setTag("YHChenzhenJudge", true);

			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (room->getDrawPile().isEmpty()) return false;
				if (p->isDead() || !p->hasSkill(objectName())) continue;
				if (!p->askForSkillInvoke(this)) continue;
				room->broadcastSkillInvoke(this);

				QStringList choices;
				if (p->getMaxCards() > 0)
					choices << "maxcard";
				if (p->getMaxHp() > 0)
					choices << "maxhp";
				QString choice = room->askForChoice(p, objectName(), choices.join("+"));
				if (choice == "maxcard")
					room->addMaxCards(p, -2, false);
				else
					room->loseMaxHp(p, 2, objectName());

				if (p->isDead()) continue;

				int num = qMin(9, room->getDrawPile().length());
				if (num <= 0) continue;

				choices.clear();
				for (int i = 0; i < num; i++)
					choices << QString::number(i + 1);

				choice = room->askForChoice(p, "yhchenzhen_num", choices.join("+"));
				num = choice.toInt();

				QList<int> cards = room->getNCards(num, false);
				room->returnToTopDrawPile(cards);

				QStringList ups, downs;

				foreach (int id, cards) {
					const Card *card = Sanguosha->getCard(id);
					if (card->hasFlag("visible")) {
						card->setFlags("-visible");
						downs << QString::number(id);
					} else {
						card->setFlags("visible");
						ups << QString::number(id);
					}
				}

				LogMessage log;
				log.from = p;
				if (!ups.isEmpty()) {
					log.type = "$YHChenzhenUp";
					log.card_str = ups.join("+");
					room->sendLog(log);
				}
				if (!downs.isEmpty()) {
					log.type = "$YHChenzhenDown";
					log.card_str = downs.join("+");
					room->sendLog(log);
				}
			}
		}
		return false;
	}
};

class YHZhanghua : public TriggerSkillV2
{
public:
	YHZhanghua(const QString &skill) : TriggerSkillV2("#" + skill + "-move"), skill(skill)
	{
		events << CardsMoveOneTime;
	}

	int getPriority(TriggerEvent) const
	{
		return 4;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == CardsMoveOneTime && target ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (player != room->getAllPlayers().first()) return false;
		CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		if (!move.from_places.contains(Player::DrawPile)) return false;
		QVariantList list = room->getTag("YHBuqueCards").toList();
		foreach (int id, move.card_ids) {
			if (!list.contains(QVariant(id))) continue;
			if (room->getCardPlace(id) == Player::DrawPile) continue;
			list.removeOne(id);
		}
		room->setTag("YHBuqueCards", list);
		return false;
	}

private:
	QString skill;
};

class YHSigong : public TriggerSkillV2
{
public:
    YHSigong() : TriggerSkillV2("yhsigong") { events << Appear; hide_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@yhsigong-target", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->broadcastSkillInvoke(this);
        const int serial = room->getTag("YHSigongNextReceipt").toInt() + 1;
        room->setTag("YHSigongNextReceipt", serial);
        QVariantList receipts = target->getTag("YHSigongReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"issuer", ctx.owner->objectName()}, {"amount", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("YHSigongReceipts", receipts);
        // The applied relationship lasts after the original skill instance retires.
        room->addPlayerMark(target, "&yhsigong+#" + ctx.owner->objectName(), amount);
        room->addMaxCards(target, amount, false);
        return false;
    }
};

class YHSigongEffect : public TriggerSkillV2
{
public:
    YHSigongEffect() : TriggerSkillV2("#yhsigong") { events << EventPhaseEnd; hide_skill = true; global = true; frequency = Compulsory; }
    static bool hasDiscard(Room *room)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return false;
        QVariantMap query{{"turn_id", turn}, {"limit", 64}};
        bool found = false;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            // This is a public discard fact; ordinary cards have no skill attribution.
            if (!page.value("error").toString().isEmpty() || !page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (!move.contains("reason") || !move.contains("from_place") || !move.contains("from")) return false;
                const int place = move.value("from_place").toInt();
                if (!move.value("from").toString().isEmpty() && (place == Player::PlaceHand || place == Player::PlaceEquip)
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) found = true;
            }
            if (!page.value("has_more").toBool()) return found;
            query.insert("after", page.value("next_after"));
        }
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || player->getPhase() != Player::Discard || !hasDiscard(room)) return true;
        for (const QVariant &entry : player->getTag("YHSigongReceipts").toList()) {
            const QVariantMap receipt = entry.toMap();
            ServerPlayer *issuer = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
            if (!issuer || !issuer->isAlive() || !issuer->isWounded()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = issuer; ctx.initiator = issuer; ctx.invoker = player; ctx.targets = {issuer};
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (ctx.instanceID <= 0 || !ctx.sourceRef.isValid()) continue;
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return !ctx.activationRef.isValid() && ctx.owner && ctx.owner->isAlive() && ctx.invoker
            && ctx.invoker->getTag("YHSigongReceipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(target, "yhsigong", true, true);
        room->recover(target, RecoverStruct("yhsigong", target, getEffectiveAmount(ctx)));
        return false;
    }
};
YHXijianGiveCard::YHXijianGiveCard()
{
	will_throw = false;
	mute = true;
	handling_method = Card::MethodNone;
	m_skillName = "yhxijian_give";
	mute = true;
}

bool YHXijianGiveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self != to_select && to_select->hasSkill("yhxijian");
}

void YHXijianGiveCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *to, use.to) {
		use.from->skillInvoked("yhxijian",-1,to);
		room->giveCard(use.from, to, this, "yhxijian_give");
		QVariantList list = to->getTag("YHXijianCards").toList();
		int id = getEffectiveId();
		if (!to->handCards().contains(id)) return;
		room->setCardTip(id, "yhxijian");
		if (list.contains(QVariant(id))) return;
		list << id;
		to->setTag("YHXijianCards", list);
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class YHXijianGive : public ViewAsSkillV2
{
public:
	YHXijianGive() : ViewAsSkillV2("yhxijian_give") { attached_lord_skill = true; setPhaseName("Play"); }
	LimitScope getLimitScope() const override { return Limit_Phase; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{ return request.initiator && request.selectedCardIds.isEmpty() && card
	    && request.initiator->handCards().contains(card->getEffectiveId()); }
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{ return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator
	    && candidate->hasSkill("yhxijian"); }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.size() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHXijianGiveCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		YHXijianGiveCard *card = new YHXijianGiveCard; card->addSubcard(request.selectedCardIds.first()); return card;
	}
};

class YHXijian : public TriggerSkillV2
{
public:
	YHXijian() : TriggerSkillV2("yhxijian")
	{
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
	}
	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return target && target->isAlive() && (event == EventAcquireSkill || event == EventPhaseStart || event == EventPhaseEnd)
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}
	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (triggerEvent == EventAcquireSkill&&player->hasSkill(objectName(),true)) {
			const QList<int> ids = player->getValidSkillInstanceIds(objectName());
			if (ids.isEmpty()) return false;
			const SkillInstanceRef source(player->objectName(), SkillInstanceKey(objectName(), ids.first()));
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("yhxijian_give",true)){
					room->attachSkillToPlayer(p, "yhxijian_give", source);
				}
			}
		}else if(triggerEvent == EventAcquireSkill || player->getPhase()!=Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasSkill(objectName(),true)){
					const QList<int> ids = p->getValidSkillInstanceIds(objectName());
					if (ids.isEmpty()) break;
					const SkillInstanceRef source(p->objectName(), SkillInstanceKey(objectName(), ids.first()));
					room->attachSkillToPlayer(player, "yhxijian_give", source);
					break;
				}
			}
		}else{
			for (const SkillInstance &instance : player->getSkillInstances()) {
				if (instance.skillName == "yhxijian_give" && instance.source == SourceAttached
				    && instance.parentRef.key.skillName == objectName())
					room->detachAttachedSkill(SkillInstanceRef(player->objectName(), instance.key()));
			}
		}
		return false;
	}
};

class YHXijianEffect : public TriggerSkillV2
{
public:
	YHXijianEffect() : TriggerSkillV2("#yhxijian")
	{
		events << EventPhaseChanging << CardUsed << CardResponded;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == EventPhaseChanging)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return target && (event == EventPhaseChanging || event == CardUsed || event == CardResponded)
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == EventPhaseChanging) {
			if (ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive) return false;
			QList<ServerPlayer *> players = room->getAllPlayers(true);
			foreach (ServerPlayer *p, players) {
				int mark = p->getMark("yhxijian");
				if (mark == 0) continue;
				p->removeMark("yhxijian", mark);
				room->removePlayerMark(p, "@skill_invalidity", mark);

				foreach(ServerPlayer *q, room->getAllPlayers())
					room->filterCards(q, q->getCards("he"), false);

				JsonArray args;
				args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
				room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
			}
		} else {
			QVariantList list = player->getTag("YHXijianCards").toList();
			if (list.isEmpty()) return false;
			QList<int> ids = ListV2I(list);

			QList<ServerPlayer *> tos;
			const Card *card = nullptr;
			if (event == CardUsed) {
				CardUseStruct use = ctx.original_data->value<CardUseStruct>();
				card = use.card;
				tos = use.to;
			} else {
				CardResponseStruct res = ctx.original_data->value<CardResponseStruct>();
				if (!res.m_isUse) return false;
				card = res.m_card;
			}
			if (!card || card->isKindOf("SkillCard")) return false;

			QList<int> subcards;
			if (card->isVirtualCard())
				subcards = card->getSubcards();
			else
				subcards << card->getEffectiveId();

			bool can_invoke = false;
			foreach (int id, subcards) {
				if (ids.contains(id)) {
					can_invoke = true;
					ids.removeOne(id);
				}
			}

			if (!can_invoke) return false;

			list = ListI2V(ids);
			player->setTag("YHXijianCards", list);

			if (tos.isEmpty() || !room->hasCurrent()) {
				room->sendCompulsoryTriggerLog(player, "yhxijian", true, true);
				room->acquireNextTurnSkills(player, "", "xiangle");
			} else {
				ServerPlayer *to = room->askForPlayerChosen(player, tos, "yhxijian", "@yhxijian-target", true, true);
				if (to) {
					room->broadcastSkillInvoke("yhxijian");
					to->addMark("yhxijian");
					room->addPlayerMark(to, "@skill_invalidity");

					foreach(ServerPlayer *pl, room->getAllPlayers())
						room->filterCards(pl, pl->getCards("he"), true);

					JsonArray args;
					args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
					room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
				} else {
					room->sendCompulsoryTriggerLog(player, "yhxijian", true, true);
					room->acquireNextTurnSkills(player, "", "xiangle");
				}
			}
		}
		return false;
	}
};

class YHBoben : public TriggerSkillV2
{
public:
	YHBoben() : TriggerSkillV2("yhboben")
	{
		events << CardFinished;
        global = true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		return event == CardFinished && player && player->isAlive() && use.card && use.card->isKindOf("Slash")
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		player->addMark("yhboben_slash-Clear");
		int dis_num = 2 - player->getMark("yhboben_slash-Clear");

		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isDead() || p->getMark("yhboben_used-Clear") > 0 || !p->hasSkill(objectName())) continue;
			if (dis_num > 0 && !p->canDiscard(p, "he")) continue;

			Duel *duel = new Duel(Card::NoSuit, 0);
			duel->deleteLater();
			duel->setSkillName("_yhboben");

			if (dis_num <= 0 && (player->isDead() || !p->canUse(duel, player, true))) continue;

			if (dis_num <= 0) {
				if (!p->askForSkillInvoke(this, player)) continue;
				room->broadcastSkillInvoke(objectName());
				room->addPlayerMark(p, "yhboben_used-Clear");
				room->useCard(CardUseStruct(duel, p, player), true);
			} else {
				const Card *card = room->askForDiscard(p, objectName(), dis_num, dis_num, true, true,
					"@yhboben-discard:" + player->objectName() + "::" + QString::number(dis_num), ".", objectName());
				if (!card) continue;
				room->addPlayerMark(p, "yhboben_used-Clear");
				if (player->isDead() || !p->canUse(duel, player, true)) continue;
				foreach (int id, card->getSubcards()) {
					if (Sanguosha->getCard(id)->isKindOf("BasicCard")) {
						room->setCardFlag(duel, "yhboben_basic_" + p->objectName());
						break;
					}
				}
				room->useCard(CardUseStruct(duel, p, player), true);
			}
		}
		return false;
	}
};

class YHBobenDamage : public TriggerSkillV2
{
public:
	YHBobenDamage() : TriggerSkillV2("#yhboben")
	{
		events << DamageInflicted;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		return event == DamageInflicted && target && target->isAlive() && damage.card
		    && damage.card->isKindOf("Duel") && damage.card->hasFlag("yhboben_basic_" + target->objectName())
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		LogMessage log;
		log.type = "#YHBobenDuel";
		log.from = player;
		log.arg = "yhboben";
		log.arg2 = "duel";
		room->sendLog(log);
		room->broadcastSkillInvoke("yhboben");
		room->notifySkillInvoked(player, "yhboben");
		return true;
	}
};

class YHHankai : public TriggerSkillV2
{
public:
	YHHankai() : TriggerSkillV2("yhhankai")
	{
		events << CardEffected << EventPhaseStart;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == EventPhaseStart && player->getPhase() == Player::Play) return {{player, {objectName()}}};
		const CardEffectStruct effect = data.value<CardEffectStruct>();
		return event == CardEffected && effect.card && effect.card->isKindOf("Analeptic")
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == EventPhaseStart) {
			if (!player->getEquips().isEmpty()) return false;
			Analeptic *ana = new Analeptic(Card::NoSuit, 0);
			ana->deleteLater();
			ana->setSkillName("_yhhankai");
			if (!player->canUse(ana, player, true)) return false;
			room->sendCompulsoryTriggerLog(player, this);
			room->useCard(CardUseStruct(ana, player, player), true);
		} else {
			CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
			if (effect.card->isKindOf("Analeptic")) {
				LogMessage log;
				log.type = "#SkillNullify";
				log.from = player;
				log.arg = objectName();
				log.arg2 = "analeptic";
				room->sendLog(log);
				room->broadcastSkillInvoke(this);
				room->notifySkillInvoked(player, objectName());

				int phase = (int)Player::RoundStart;
				room->addPlayerMark(player, "&yhhankai-Self" + QString::number(phase) + "Clear");
				return true;
			}
		}
		return false;
	}
};

class YHHankaiEffect : public TriggerSkillV2
{
public:
	YHHankaiEffect() : TriggerSkillV2("#yhhankai")
	{
		events << ConfirmDamage << Dying;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		if (!target) return {};
		if (event == ConfirmDamage) return TriggerList{{target, {objectName()}}};
		const DyingStruct dying = data.value<DyingStruct>();
		return event == Dying && target->isAlive() && dying.who == target
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		int phase = (int)Player::RoundStart;
		QString mark = "&yhhankai-Self" + QString::number(phase) + "Clear";

		if (event == ConfirmDamage) {
			DamageStruct damage = ctx.original_data->value<DamageStruct>();
			if (damage.from && damage.from->getMark(mark) > 0) {
				int m = damage.from->getMark(mark);
				LogMessage log;
				log.type = "#YHHankaiDamage";
				log.from = damage.from;
				log.to << damage.to;
				log.arg = "yhhankai";
				log.arg2 = QString::number(damage.damage);
				log.arg3 = QString::number(damage.damage += m);
				room->sendLog(log);
				room->broadcastSkillInvoke("yhhankai");
				room->notifySkillInvoked(damage.from, "yhhankai");
				*ctx.original_data = QVariant::fromValue(damage);
			}
			if (damage.to->getMark(mark) > 0) {
				int m = damage.to->getMark(mark);
				LogMessage log;
				log.type = "#YHHankaiDamaged";
				log.from = damage.to;
				log.arg = "yhhankai";
				log.arg2 = QString::number(damage.damage);
				log.arg3 = QString::number(damage.damage += m);
				room->sendLog(log);
				room->broadcastSkillInvoke("yhhankai");
				room->notifySkillInvoked(damage.to, "yhhankai");
				*ctx.original_data = QVariant::fromValue(damage);
			}
		} else {
			if (player->getMark(mark) <= 0) return false;
			DyingStruct dying = ctx.original_data->value<DyingStruct>();
			if (!dying.who || dying.who != player) return false;
			room->sendCompulsoryTriggerLog(player, "yhhankai", true, true);
			int recover = qMin(1 - player->getHp(), player->getMaxHp() - player->getHp());
			room->recover(player, RecoverStruct(player, nullptr, recover, "yhhankai"));
			room->setPlayerMark(player, mark, 0);
		}
		return false;
	}
};

class YHHankaiLimit : public CardLimitSkill
{
public:
	YHHankaiLimit() : CardLimitSkill("#yhhankai-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		int phase = (int)Player::RoundStart;
		if (target->getMark("&yhhankai-Self" + QString::number(phase) + "Clear") > 0) return "Analeptic";
		return "";
	}
};

class YHFeisha : public DistanceSkillV2
{
public:
	YHFeisha() : DistanceSkillV2("yhfeisha")
	{
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.primary || !ctx.secondary) return CorrectSkillResult::noEffect();
		const int conditions = int(ctx.primary->getHp() >= ctx.secondary->getHp())
		    + int(ctx.primary->getHandcardNum() >= ctx.secondary->getHandcardNum());
		return CorrectSkillResult::useAmount(-conditions * ctx.currentAmount);
	}
};

class YHJuantu : public TriggerSkillV2
{
public:
	YHJuantu() : TriggerSkillV2("yhjuantu")
	{
		events << TargetSpecified;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->isKindOf("SkillCard") || use.to.isEmpty()) return {};
		const int mark = player->getMark("yhjuantu_num");
		return mark > 0 && use.card->getNumber() > mark ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		QList<ServerPlayer *> candidates;
		for (ServerPlayer *p : room->getAlivePlayers())
			if (!p->isNude() && ctx.owner->distanceTo(p) == 1) candidates << p;
		ServerPlayer *target = candidates.isEmpty() ? nullptr
			: room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@yhjuantu-get", true, true);
		if (!target) return false;
		ctx.targets = {target};
		return true;
	}

	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
	{
		if (!target->isAlive() || target->isNude()) return false;
		room->broadcastSkillInvoke(this);
		const int id = room->askForCardChosen(ctx.owner, target, "he", objectName());
		room->obtainCard(ctx.owner, id, false);
		return false;
	}
};

class YHJuantuNumber : public TriggerSkillV2
{
public:
	YHJuantuNumber() : TriggerSkillV2("#yhjuantu")
	{
		events << CardFinished;
        global = true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (event != CardFinished || !player || !player->isAlive()) return {};
		const Card *card = data.value<CardUseStruct>().card;
		if (!card || card->isKindOf("SkillCard") || card->getNumber() <= 0) return {};
		return TriggerList{{player, {objectName()}}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const Card *card = ctx.original_data->value<CardUseStruct>().card;
		player->setMark("yhjuantu_num", card->getNumber());
		if (player->hasSkill("yhjuantu", true))
			room->setPlayerMark(player, "&yhjuantu", card->getNumber());
		return false;
	}
};

class YHJuantuClear : public TriggerSkillV2
{
public:
	YHJuantuClear() : TriggerSkillV2("#yhjuantu-clear")
	{
		events << EventLoseSkill;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		return event == EventLoseSkill && target && target->isAlive() && data.toString() == "yhjuantu"
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (player->hasSkill("yhjuantu", true)) return false;
		room->setPlayerMark(player, "&yhjuantu", 0);
		return false;
	}
};

YHQuanwangCard::YHQuanwangCard()
{
}

void YHQuanwangCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();

	room->removePlayerMark(from, "@yhquanwangMark");
	room->doSuperLightbox(from, "yhquanwang");

	DummyCard *handcards = from->wholeHandCards();
	room->giveCard(from, to, handcards, "yhquanwang");

	room->handleAcquireDetachSkills(from, "-yhjuantu|yhchouxi");
	if (to->isDead()) return;
	room->addPlayerMark(to, "yhquanwang_extra_turn");
}

class YHQuanwangVS : public ViewAsSkillV2
{
public:
	YHQuanwangVS() : ViewAsSkillV2("yhquanwang") { setPhaseName("Play"); frequency = Limited; limit_mark = "@yhquanwangMark"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
	    && request.initiator->getMark("@yhquanwangMark") > 0 && !request.initiator->isKongcheng(); }
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{ return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator && candidate->isAlive(); }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.size() == 1; }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHQuanwangCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{ return canActivate(request) ? new YHQuanwangCard : nullptr; }
	bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
	{
		if (!ctx.initiator || ctx.initiator->getMark("@yhquanwangMark") <= 0) return false;
		room->removePlayerMark(ctx.initiator, "@yhquanwangMark");
		return true;
	}
};

class YHQuanwang : public TriggerSkillV2
{
public:
	YHQuanwang() : TriggerSkillV2("yhquanwang")
	{
		frequency = Limited;
		limit_mark = "@yhquanwangMark";
		view_as_skill = new YHQuanwangVS;
		waked_skills = "yhchouxi";
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == EventPhaseStart && target && target->isAlive() && target->getPhase() == Player::NotActive
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			int mark = p->getMark("yhquanwang_extra_turn");
			room->setPlayerMark(p, "yhquanwang_extra_turn", 0);
			if (p->isDead() || mark <= 0) continue;
			for (int i = 0; i < mark; i++) {
				if (p->isDead()) break;
				p->gainAnExtraTurn();
			}
		}
		return false;
	}
};

class YHChouxi : public TriggerSkillV2
{
public:
    YHChouxi() : TriggerSkillV2("yhchouxi") { events << Dying << EventSkillInvoking; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Dying || !player || data.value<DyingStruct>().who != player) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            for (int id : owner->getValidSkillInstanceIds(objectName()))
                if (!owner->getSkillInstanceStateValue(objectName(), id, "dying_players").toStringList().contains(player->objectName()))
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    static void commit(const SkillContext &ctx)
    {
        QStringList used = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "dying_players").toStringList();
        if (!used.contains(ctx.choice)) used << ctx.choice;
        ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "dying_players", used);
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.bypass_cost && accepted.activationRef == ctx.activationRef) commit(accepted);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const ServerPlayer *dying = ctx.original_data->value<DyingStruct>().who;
        if (!dying) return false;
        ctx.choice = dying->objectName(); ctx.targets = {ctx.owner};
        return !ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "dying_players").toStringList().contains(ctx.choice);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "dying_players").toStringList().contains(ctx.choice)) return false;
        commit(ctx); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        const int serial = room->getTag("YHChouxiNextReceipt").toInt() + 1; room->setTag("YHChouxiNextReceipt", serial);
        QVariantList receipts = target->getTag("YHChouxiReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"amount", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("YHChouxiReceipts", receipts);
        int total = 0; for (const QVariant &entry : receipts) total += entry.toMap().value("amount").toInt();
        room->setPlayerMark(target, "&yhchouxi", total); return false;
    }
};

class YHChouxiDamage : public TriggerSkillV2
{
public:
    YHChouxiDamage() : TriggerSkillV2("#yhchouxi") { events << ConfirmDamage; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.from != player || !damage.to) return true;
        for (const QVariant &entry : player->getTag("YHChouxiReceipts").toList()) {
            const QVariantMap receipt = entry.toMap();
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = ctx.initiator = ctx.invoker = player; ctx.targets = {damage.to};
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.sourceRef.isValid() || ctx.instanceID <= 0) continue;
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.current_event = event; ctx.original_data = &data; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return !ctx.activationRef.isValid() && ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("YHChouxiReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target) return false;
        LogMessage log; log.type = "#YHChouxiDamage"; log.from = ctx.owner; log.arg = "yhchouxi"; log.arg2 = QString::number(damage.damage);
        damage.damage += getEffectiveAmount(ctx); log.arg3 = QString::number(damage.damage); room->sendLog(log);
        room->broadcastSkillInvoke("yhchouxi"); room->notifySkillInvoked(ctx.owner, "yhchouxi");
        *ctx.original_data = QVariant::fromValue(damage); return false;
    }
};
class YHChenwen : public TriggerSkillV2
{
public:
	YHChenwen() : TriggerSkillV2("yhchenwen")
	{
		events << DrawNCards << EventPhaseEnd << EventPhaseChanging;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == DrawNCards && data.value<DrawStruct>().reason == "draw_phase") return {{player, {objectName()}}};
		return (event == EventPhaseEnd || event == EventPhaseChanging) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == DrawNCards) {
			DrawStruct draw = ctx.original_data->value<DrawStruct>();
			QStringList choices;
			for (int i = 0; i < 3; i++)
				choices << "yhchenwen=" + QString::number(i + 1);
			choices << "cancel";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "cancel") return false;
			int num = choice.split("=").last().toInt();
			room->loseMaxHp(player, num, objectName());
			draw.num += num;
			*ctx.original_data = QVariant::fromValue(draw);
		} else if (event == EventPhaseEnd) {
			if (player->getPhase() != Player::Play || !player->canDiscard(player, "h")) return false;
			const Card *c = room->askForDiscard(player, objectName(), 3, 1, true, false, "@yhchenwen-discard", ".|.|.|hand", objectName());
			if (!c) return false;
			room->gainMaxHp(player, c->subcardsLength(), objectName());
		} else {
			if (ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive) return false;
			if (player->getMark("damage_point_round") > 0) return false;
			QString _podi = player->property("SkillDescriptionRecord_yhpodi").toString();
			QStringList choices, podi = _podi.isEmpty() ? QStringList() : _podi.split("+");

			if (player->hasSkill("yhshouzhuang", true) && !player->getTag("YHShouzhuangUnlock").toBool())
				choices << "yhshouzhuang";
			if (player->hasSkill("yhpodi", true)) {
				for (int i = 2; i < 5; i++) {
					if (podi.contains(QString::number(i))) continue;
					choices << "yhpodi" + QString::number(i);
				}
			}
			if (choices.isEmpty()) return false;
			choices << "cancel";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "cancel") return false;

			LogMessage log;
			log.type = "#YHChenwenInvoke";
			log.from = player;
			log.arg = objectName();
			log.arg2 = "yhchenwen:" + choice;
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player, objectName());

			if (choice == "yhshouzhuang") {
				player->setTag("YHShouzhuangUnlock", true);
				room->changeTranslation(player, "yhshouzhuang", 1);
			} else {
				QString last = choice.at(choice.length() - 1);
				podi << last;
				room->setPlayerProperty(player, "SkillDescriptionRecord_yhpodi", podi.join("+"));
				player->setSkillDescriptionSwap("yhpodi",last+"*.",last+".");
			}
		}
		return false;
	}
};

class YHShouzhuang : public TriggerSkillV2
{
public:
	YHShouzhuang() : TriggerSkillV2("yhshouzhuang")
	{
		events << Damaged;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return event == Damaged && player && player->isAlive() && player->hasSkill(objectName())
	    ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
	{
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		ServerPlayer *from = damage.from;
		if (!from || from == player) return false;

		int dam = 2 * damage.damage;
		player->setTag("YHShouzhuangData", QVariant::fromValue(damage));

		int invoke = 0;
		if (player->getTag("YHShouzhuangUnlock").toBool())
			invoke = player->askForSkillInvoke(this, from) ? 1 : 0;
		else
			invoke = player->askForSkillInvoke(this, "yhshouzhuang:" + from->objectName() + "::" + QString::number(dam)) ? 2 : 0;
		player->removeTag("YHShouzhuangData");

		if (invoke == 0) return false;
		player->peiyin(this);

		Room *room = player->getRoom();
		if (invoke == 2)
			room->askForDiscard(from, objectName(), dam, dam, false, true);
		else {
			QStringList choices;
			int equip = player->getEquips().length();
			choices << "yhshouzhuang1=" + from->objectName() + "=" + QString::number(dam) << "yhshouzhuang2=" + from->objectName() + "=" + QString::number(equip);
			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(from));
			int dis = choice.startsWith("yhshouzhuang1") ? dam : player->getEquips().length();
			if (dis <= 0) return false;
			room->askForDiscard(from, objectName(), dis, dis, false, true);
		}
		return false;
	}
};

YHPodiCard::YHPodiCard()
{
}

void YHPodiCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();

	room->loseMaxHp(from, 1, "yhpodi");
	if (from->isDead()) return;

	QList<int> hands = to->handCards();
	if (!hands.isEmpty())
		//room->doGongxin(from, to, QList<int>(), "yhpodi");
		room->fillAG(hands, from);

	QString _podi = from->property("SkillDescriptionRecord_yhpodi").toString();
	QStringList podi = _podi.isEmpty() ? QStringList() : _podi.split("+");

	QStringList choices;
	if (!hands.isEmpty()) {
		choices << "1";
		if (podi.contains("2")) {
			bool candis = false;
			foreach (int id, hands) {
				if (from->canDiscard(to, id)) {
					candis = true;
					break;
				}
			}
			if (candis)
				choices << "2=" + to->objectName();
		}
	}

	if (podi.contains("3"))
		choices << "3=" + to->objectName();
	if (podi.contains("4"))
		choices << "4=" + to->objectName();
	choices << "cancel";

	QString choice = room->askForChoice(from, "yhpodi", choices.join("+"), QVariant::fromValue(to));
	room->clearAG(from);
	if (choice == "cancel") return;

	if (choice.startsWith("1")) {
		room->fillAG(hands, from);
		int id = room->askForAG(from, hands, false, "yhpodi", "@yhpodi-show");
		room->clearAG(from);
		room->showCard(to, id);
		if (Sanguosha->getCard(id)->isKindOf("BasicCard")) return;
		room->askForDiscard(from, "yhpodi", 1, 1, false, false);
	} else if (choice.startsWith("2")) {
		QList<int> able, disable;
		foreach (int id, hands) {
			if (from->canDiscard(to, id))
				able << id;
			else
				disable << id;
		}
		if (able.isEmpty()) return;

		room->fillAG(hands, from, disable);
		int id = room->askForAG(from, able, false, "yhpodi", "@yhpodi-discard");
		room->clearAG(from);
		room->throwCard(id, to, from);
		room->addPlayerMark(from, "yhpodi_juli_" + to->objectName() + "-Clear");
	} else if (choice.startsWith("3")) {
		from->drawCards(1, "yhpodi");
		room->addPlayerMark(from, "yhpodi_cishu_" + to->objectName() + "-Clear");
	} else {
		room->addPlayerMark(to, "&yhpodi_wuxiao-Keep");

		foreach(ServerPlayer *p, room->getAllPlayers())
			room->filterCards(p, p->getCards("he"), true);

		JsonArray args;
		args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

		podi.removeOne("4");
		room->setPlayerProperty(from, "SkillDescriptionRecord_yhpodi", podi.join("+"));
		from->setSkillDescriptionSwap("yhpodi","4.","4*.");
	}
}

class YHPodiVS : public ViewAsSkillV2
{
public:
	YHPodiVS() : ViewAsSkillV2("yhpodi") { setPhaseName("Play"); }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHPodiCard"; }
	const Card *createCard(const ActiveSkillRequest &) const override { return new YHPodiCard; }
};

class YHPodi : public TriggerSkillV2
{
public:
	YHPodi() : TriggerSkillV2("yhpodi")
	{
		events << EventPhaseChanging;
		view_as_skill = new YHPodiVS;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		return event == EventPhaseChanging && target && data.value<PhaseChangeStruct>().to == Player::NotActive
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
	{
		foreach (ServerPlayer *p, room->getAllPlayers(true)) {
			if (p->getMark("&yhpodi_wuxiao-Keep") <= 0) continue;
			room->setPlayerMark(p, "&yhpodi_wuxiao-Keep", 0);

			foreach(ServerPlayer *p, room->getAllPlayers())
				room->filterCards(p, p->getCards("he"), false);

			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		}
		return false;
	}
};

class YHPodiInvalidity : public InvaliditySkill
{
public:
	YHPodiInvalidity() : InvaliditySkill("#yhpodi-inv")
	{
	}

	bool isSkillValid(const Player *player, const Skill *) const
	{
		return player->getMark("&yhpodi_wuxiao-Keep")<1;
	}
};

class YHPodiTargetMod : public TargetModSkillV2
{
public:
	YHPodiTargetMod() : TargetModSkillV2("#yhpodi-target", "^SkillCard")
	{
		frequency = NotFrequent;
		pattern = "^SkillCard";
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (!ctx.primary || !ctx.secondary) return CorrectSkillResult::noEffect();
		if (ctx.modType == TargetModSkill::Residue && ctx.primary->getMark("yhpodi_cishu_" + ctx.secondary->objectName() + "-Clear") > 0)
			return CorrectSkillResult::useAmount(ctx.currentAmount);
		if (ctx.modType == TargetModSkill::DistanceLimit && ctx.primary->getMark("yhpodi_juli_" + ctx.secondary->objectName() + "-Clear") > 0)
			return CorrectSkillResult::useAmount(ctx.currentAmount);
		return CorrectSkillResult::noEffect();
	}
};

class YHDuweiVS : public ViewAsSkillV2
{
public:
	YHDuweiVS() : ViewAsSkillV2("yhduwei") { setResponseOrUse(true); response_pattern = "@@yhduwei"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.initiator->getMark("yhduwei-PlayClear") > 0) return false;
		if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern != "@@yhduwei") return false;
		const QString name = request.initiator->property("yhduwei_damage_card").toString();
		Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name);
		if (!card) return false;
		card->setSkillName(objectName());
		const bool available = card->isAvailable(request.initiator);
		card->deleteLater();
		return available;
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!request.initiator || request.selectedCardIds.size() >= 1 || !candidate
		    || !request.initiator->handCards().contains(candidate->getEffectiveId())) return false;
		const QString name = request.initiator->property("yhduwei_damage_card").toString();
		Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
		if (!card) return false;
		card->setSkillName(objectName()); card->addSubcard(candidate);
		const bool available = card->isAvailable(request.initiator);
		card->deleteLater();
		return available;
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
	}
	TargetMode targetMode() const override { return SelectTargets; }
	bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
	                     const Player *candidate) const override
	{
		if (!request.initiator || !candidate || request.selectedCardIds.size() != 1) return false;
		const QString name = request.initiator->property("yhduwei_damage_card").toString();
		Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
		if (!card) return false;
		card->setSkillName(objectName()); card->addSubcard(Sanguosha->getCard(request.selectedCardIds.first()));
		const bool allowed = card->targetFilter(selected, candidate, request.initiator);
		card->deleteLater();
		return allowed;
	}
	bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
	{
		if (request.selectedCardIds.size() != 1 || !request.initiator) return false;
		const QString name = request.initiator->property("yhduwei_damage_card").toString();
		Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
		if (!card) return false;
		card->setSkillName(objectName()); card->addSubcard(Sanguosha->getCard(request.selectedCardIds.first()));
		const bool feasible = card->targetsFeasible(selected, request.initiator);
		card->deleteLater();
		return feasible;
	}
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		const QString name = request.initiator ? request.initiator->property("yhduwei_damage_card").toString() : QString();
		Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name);
		if (!card) return objectName();
		const QString key = card->getClassName();
		card->deleteLater();
		return key;
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		const QString name = request.initiator->property("yhduwei_damage_card").toString();
		Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
		if (card) { card->setSkillName(objectName()); card->addSubcard(Sanguosha->getCard(request.selectedCardIds.first())); }
		return card;
	}
};

class YHDuwei : public TriggerSkillV2
{
public:
	YHDuwei() : TriggerSkillV2("yhduwei")
	{
		events << PreCardUsed << EventPhaseStart;
		view_as_skill = new YHDuweiVS;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == PreCardUsed) {
			const CardUseStruct use = data.value<CardUseStruct>();
			return use.card && use.card->getSkillNames().contains(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
		}
		return event == EventPhaseStart && player->getPhase() == Player::Finish
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == PreCardUsed) {
			CardUseStruct use = ctx.original_data->value<CardUseStruct>();
			room->addPlayerMark(player, "yhduwei-PlayClear");
			use.m_addHistory = false;
			*ctx.original_data = QVariant::fromValue(use);
		} else {
			QString name = player->property("yhduwei_damage_card").toString();
			if (name.isEmpty()) return false;
			room->askForUseCard(player, "@@yhduwei", "@yhduwei:" + name, -1, Card::MethodUse, false);
		}
		return false;
	}
};

class YHDuweiTargetMod : public TargetModSkillV2
{
public:
	YHDuweiTargetMod() : TargetModSkillV2("#yhduwei", ".")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == TargetModSkill::DistanceLimit && ctx.card
		    && ctx.card->getSkillName() == "yhduwei"
		    ? CorrectSkillResult::useAmount(999) : CorrectSkillResult::noEffect();
	}
};

class YHSiku : public TriggerSkillV2
{
public:
    YHSiku() : TriggerSkillV2("yhsiku") { events << Death << EventSkillInvoking; frequency = Limited; limit_mark = "@yhsikuMark"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Death) return {};
        const DeathStruct death = data.value<DeathStruct>();
        // Death arrives once for each observer; this observer supplies only their own sources.
        return player && player->hasSkill(objectName()) && death.who && death.who->getHandcardNum() <= player->getHandcardNum()
            ? availableSources(player) : TriggerList();
    }
    TriggerList availableSources(ServerPlayer *player) const
    {
        TriggerList result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext candidate; candidate.owner = candidate.invoker = player;
            candidate.skill_name = objectName(); candidate.instanceID = id;
            candidate.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(candidate)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        ServerPlayer *from = death.damage ? death.damage->from : nullptr;
        const QVariant previous = ctx.owner->getTag("YHSikuData");
        ctx.owner->setTag("YHSikuData", *ctx.original_data);
        const auto restore = qScopeGuard([&] {
            if (previous.isValid()) ctx.owner->setTag("YHSikuData", previous); else ctx.owner->removeTag("YHSikuData");
        });
        if (!ctx.owner->askForSkillInvoke(this, from ? "yhsiku:" + from->objectName() : "yhsiku2")) return false;
        QVariantList ids;
        for (int id : ctx.owner->handCards()) ids << id;
        ctx.extra_data = ids;
        if (from && from->isAlive()) ctx.targets = {from};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (id < 0 || ids.contains(id) || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            ids << id;
        }
        addUsage(ctx);
        if (!ids.isEmpty()) { DummyCard payment(ids); room->throwCard(&payment, ctx.owner, nullptr); }
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->removePlayerMark(ctx.owner, limit_mark);
        ctx.owner->peiyin(this);
        room->doSuperLightbox(ctx.owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        QStringList detach;
        for (const Skill *skill : target->getVisibleSkillList()) {
            if (skill->inherits("SPConvertSkill") || skill->isAttachedLordSkill()) continue;
            // The rule removes every existing visible source, never a newly acquired replacement by name.
            for (int id : target->getSkillInstanceIds(skill->objectName()))
                detach << "-" + SkillInstanceUtils::formatName(skill->objectName(), id);
        }
        detach.removeDuplicates();
        room->handleAcquireDetachSkills(target, detach);
        return false;
    }
};
class YHQingsi : public TriggerSkillV2
{
public:
	YHQingsi() : TriggerSkillV2("yhqingsi")
	{
		events << CardsMoveOneTime;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		return event == CardsMoveOneTime && player && player->isAlive() && player->hasSkill(objectName())
		    && move.from && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard
		    && move.reason.m_skillName != "yhchuzhu" ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		if (move.from && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard && move.reason.m_skillName != "yhchuzhu") {
			ServerPlayer *from = (ServerPlayer *)move.from;
			if (from->isDead()) return false;

			player->setTag("YHQingsiData", *ctx.original_data);
			bool invoke = player->askForSkillInvoke(this, from);
			player->removeTag("YHQingsiData");
			if (!invoke) return false;
			player->peiyin(this);

			QList<int> hands = from->handCards(), get;
			foreach (int id, move.card_ids) {
				if (hands.contains(id)) continue;
				get << id;
			}
			if (!get.isEmpty()) {
				DummyCard dummy(get);
				room->obtainCard(from, &dummy, false);
			}

			QList<ServerPlayer *> players;
			players << from << player;
			room->sortByActionOrder(players);
			room->drawCards(players, 1, objectName());

			room->loseHp(HpLostStruct(player, 1, objectName(), player));
			return false;
		}
		return false;
	}
};

class YHChuzhu : public TriggerSkillV2
{
public:
	YHChuzhu() : TriggerSkillV2("yhchuzhu")
	{
		events << Dying;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		return event == Dying && player && player->isAlive() && player->hasSkill(objectName())
		    && data.value<DyingStruct>().who == player ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		DyingStruct dy = ctx.original_data->value<DyingStruct>();
		if (dy.who != player) return false;

		int mark = player->getTag("YHQingsiNum").toInt();
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
												"@yhchuzhu-target:" + QString::number(mark), true, true);
		if (!t) return false;
		player->peiyin(this);

		QList<int> give = player->handCards() + player->getEquipsId();
		if (!give.isEmpty())
			room->giveCard(player, t, give, objectName());
		if (mark > 0)
			room->recover(t, RecoverStruct(player, nullptr, qMin(mark, t->getMaxHp() - t->getHp()), "yhchuzhu"));
		DamageStruct damage;
		damage.from = player;
		room->killPlayer(player, &damage);
		return false;
	}
};

class YHChuzhuMark : public TriggerSkillV2
{
public:
	YHChuzhuMark() : TriggerSkillV2("#yhchuzhu")
	{
		events << ChoiceMade << EventAcquireSkill;
        global = true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		return player && (event == ChoiceMade || event == EventAcquireSkill) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == ChoiceMade) {
			QString decisionString = ctx.original_data->toString();
			if (decisionString.isEmpty()) return false;
			QStringList decisionStrings = decisionString.split(":");
			if (decisionStrings.first() != "skillInvoke" || decisionStrings[1] != "yhqingsi" || decisionStrings.last() != "yes") return false;
			int mark = player->getTag("YHQingsiNum").toInt()+1;
			player->setTag("YHQingsiNum", mark);
			if (player->hasSkill("yhqingsi", true))
				room->addPlayerMark(player, "&yhqingsi_num");
		} else {
			if (ctx.original_data->toString() != "yhqingsi" || !player->hasSkill("yhqingsi", true)) return false;
			int mark = player->getTag("YHQingsiNum").toInt();
			if (mark > 0)
				room->setPlayerMark(player, "&yhqingsi_num", mark);
		}
		return false;
	}
};

class YHHuairen : public TriggerSkillV2
{
public:
	YHHuairen() : TriggerSkillV2("yhhuairen")
	{
		events << DrawNCards << CardUsed << CardResponded;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
		if (event == DrawNCards && data.value<DrawStruct>().reason == "InitialHandCards") return {{player, {objectName()}}};
		return (event == CardUsed || event == CardResponded) ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == DrawNCards) {
			DrawStruct draw = ctx.original_data->value<DrawStruct>();
			room->sendCompulsoryTriggerLog(player, this);
			room->setPlayerFlag(player, "YHHuairenDrawCardsSkill");
			int max = player->getMaxHp(), hp = player->getHp();
			room->gainMaxHp(player, max, objectName());
			room->recover(player, RecoverStruct(player, nullptr, qMin(hp, player->getMaxHp() - player->getHp()), "yhhuairen"));
			draw.num = draw.num*2;
			*ctx.original_data = QVariant::fromValue(draw);
		} else {
			if (!player->getPile("yhlmren").isEmpty()) return false;
			const Card *card = nullptr;
			if (event == CardUsed)
				card = ctx.original_data->value<CardUseStruct>().card;
			else {
				CardResponseStruct res = ctx.original_data->value<CardResponseStruct>();
				if (!res.m_isUse) return false;
				card = res.m_card;
			}
			if (!card || card->isKindOf("SkillCard") || !card->isRed()) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(1, objectName());
		}
		return false;
	}
};

class YHHuairenEffect : public TriggerSkillV2
{
public:
	YHHuairenEffect() : TriggerSkillV2("#yhhuairen")
	{
		events << AfterDrawNCards << HpRecover << Death;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return target && (event == AfterDrawNCards || event == HpRecover || event == Death)
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == AfterDrawNCards) {
			DrawStruct draw = ctx.original_data->value<DrawStruct>();
			if(draw.reason!="InitialHandCards"||!player->hasFlag("YHHuairenDrawCardsSkill")) return false;
			room->setPlayerFlag(player, "-YHHuairenDrawCardsSkill");
			if (player->isNude()) return false;
			const Card *c = room->askForExchange(player, objectName(), 1, 1, true, "@yhhuairen-put");
			player->addToPile("yhlmren", c);
		} else if (event == HpRecover) {
			int n = ctx.original_data->value<RecoverStruct>().recover;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->getPile("yhlmren").isEmpty()) continue;
				room->addPlayerMark(p, "&yhhuairen_num", n);
				if (p->getMark("&yhhuairen_num") >= p->aliveCount()) {
					room->sendCompulsoryTriggerLog(p, "yhhuairen", true, true);
					room->setPlayerMark(p, "&yhhuairen_num", 0);
					DummyCard get(p->getPile("yhlmren"));
					CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, p->objectName());
					room->obtainCard(p, &get, reason, true);
					if (p->isAlive()) {
						int max = floor(p->getMaxHp() / 2);
						if (max > 0)
							room->loseMaxHp(p, max, "yhhuairen");
					}
				}
			}
		} else {
			DeathStruct death = ctx.original_data->value<DeathStruct>();
			if (death.who == player) return false;
			if (player->getPile("yhlmren").isEmpty()) return false;
			if (player->getMark("&yhhuairen_num") >= player->aliveCount()) {
				room->sendCompulsoryTriggerLog(player, "yhhuairen", true, true);
				room->setPlayerMark(player, "&yhhuairen_num", 0);
				DummyCard get(player->getPile("yhlmren"));
				CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName());
				room->obtainCard(player, &get, reason, true);
				if (player->isAlive()) {
					int max = floor(player->getMaxHp() / 2);
					if (max > 0)
						room->loseMaxHp(player, max, "yhhuairen");
				}
			}
		}
		return false;
	}
};

class YHHuairenLimit : public CardLimitSkill
{
public:
	YHHuairenLimit() : CardLimitSkill("#yhhuairen-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->getPile("yhlmren").length()>0&&target->hasSkill("yhhuairen"))
			return "Slash|black";
		return "";
	}
};

YHYurenCard::YHYurenCard()
{
	will_throw = false;
	mute = true;
	handling_method = Card::MethodUse;
}

bool YHYurenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *card = Sanguosha->cloneCard(user_string.split("+").first());
		if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
		return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
	}

	// The server has no engine Self or dialog tag; fall back to the declared card.
	const Card *card = Self ? Self->getTag("yhyuren").value<const Card *>() : nullptr;
	if (!card) {
	    Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
	    if (declared) declared->deleteLater();
	    card = declared;
	}
	return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
}

bool YHYurenCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *card = Sanguosha->cloneCard(user_string.split("+").first());
		const bool fixed = card && card->targetFixed();
		if (card) card->deleteLater();
		return fixed;
	}

	Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
	const bool fixed = declared && declared->targetFixed();
	if (declared) declared->deleteLater();
	return fixed;
}

bool YHYurenCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *card = Sanguosha->cloneCard(user_string.split("+").first());
		if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
		return card && card->targetsFeasible(targets, Self);
	}

	// The server has no engine Self or dialog tag; fall back to the declared card.
	const Card *card = Self ? Self->getTag("yhyuren").value<const Card *>() : nullptr;
	if (!card) {
	    Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
	    if (declared) declared->deleteLater();
	    card = declared;
	}
	return card && card->targetsFeasible(targets, Self);
}

const Card *YHYurenCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *player = card_use.from;
	Room *room = player->getRoom();

	QString to_yizan = user_string;
	if ((user_string.contains("slash") || user_string.contains("Slash")) && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_yizan = room->askForChoice(player, "yhyuren_slash", guhuo_list.join("+"));
	}

	Card *use_card = Sanguosha->cloneCard(to_yizan, Card::SuitToBeDecided, -1);
	use_card->setSkillName("yhyuren");
	use_card->addSubcards(getSubcards());
	use_card->deleteLater();
	return use_card;
}

const Card *YHYurenCard::validateInResponse(ServerPlayer *player) const
{
	Room *room = player->getRoom();

	QString to_yizan;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		guhuo_list << "peach";
		if (Sanguosha->hasCard("analeptic"))
			guhuo_list << "analeptic";
		to_yizan = room->askForChoice(player, "yhyuren_saveself", guhuo_list.join("+"));
	} else if (user_string.contains("slash") || user_string.contains("Slash")) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_yizan = room->askForChoice(player, "yhyuren_slash", guhuo_list.join("+"));
	} else
		to_yizan = user_string;

	Card *use_card = Sanguosha->cloneCard(to_yizan, Card::SuitToBeDecided, -1);
	use_card->setSkillName("yhyuren");
	use_card->addSubcards(getSubcards());
	use_card->deleteLater();
	return use_card;
}

class YHYuren : public ViewAsSkillV2
{
public:
    YHYuren() : ViewAsSkillV2("yhyuren", 1)
    {
        setResponseOrUse(true);
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::guhuo("yhyuren", true, false);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.pattern.startsWith(".") || request.pattern.startsWith("@"))
            return false;
        if (request.pattern == "peach" && player->getMark("Global_PreventPeach") > 0)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE)
            return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            || !usableNames(request).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && candidate
            && candidate->isKindOf("Slash")
            && request.initiator->handCards().contains(candidate->getEffectiveId())
            && !request.initiator->isLocked(candidate);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || !request.initiator) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }

protected:
    Card *buildCard(const ActiveSkillRequest &request, const QString &name) const override
    {
        if (!cardSelectionFeasible(request) || name.isEmpty()) return nullptr;
        YHYurenCard *card = new YHYurenCard;
        card->setUserString(name);
        card->addSubcard(request.selectedCardIds.first());
        return card;
    }
};

class YHRangwei : public TriggerSkillV2
{
public:
	YHRangwei() : TriggerSkillV2("yhrangwei")
	{
		events << CardUsed;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		return event == CardUsed && player && player->isAlive() && player->hasSkill(objectName())
		    && use.card && !use.card->isKindOf("SkillCard") && use.to.contains(player) && use.to.length() == 1
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard") || !use.to.contains(player) || use.to.length() != 1) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->isProhibited(p, use.card)) continue;  //这里不用canUse函数，免得【酒】被判定为不能用
			if (!use.card->targetFilter(QList<const Player *>(), p, player)) continue;
			targets << p;
		}
		if (targets.isEmpty()) return false;

		ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@yhrangwei-target:" + use.card->objectName(), true, true);
		if (!t) return false;
		player->peiyin(this);
		room->addPlayerMark(t, "yhrangwei_target-Keep");

		use.to.removeOne(player);
		use.to << t;
		*ctx.original_data = QVariant::fromValue(use);

		targets.clear();
		targets << player << t;
		room->sortByActionOrder(targets);

		foreach (ServerPlayer *p, targets)
			room->recover(p, RecoverStruct("yhrangwei", player));

		return false;
	}
};

class YHPosi : public TriggerSkillV2
{
public:
	YHPosi() : TriggerSkillV2("yhposi")
	{
		events << Death;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == Death && target && target->isAlive() && target->hasSkill(objectName())
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		DeathStruct death = ctx.original_data->value<DeathStruct>();
		if (death.who != player) return false;

		if (death.damage && death.damage->from) {
			ServerPlayer *from = death.damage->from;
			if (from->isDead() || from == player) return false;

			bool invoke = false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->getMark("yhrangwei_target-Keep") <= 0) continue;
				if (!p->canDiscard(from, "he")) continue;
				invoke = true;
				break;
			}
			if (!invoke) return false;

			room->sendCompulsoryTriggerLog(player, this);

			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->getMark("yhrangwei_target-Keep") <= 0) continue;
				if (!p->canDiscard(from, "he")) continue;

				int id = room->askForCardChosen(p, from, "he", objectName(), false, Card::MethodDiscard);
				room->throwCard(id, from, p);

				if (p->getAI())
					room->getThread()->delay();
			}

			if (from->isAlive() && !from->isNude())
				from->turnOver();
		}
		return false;
	}
};

class YHXiaoyun : public TriggerSkillV2
{
public:
    YHXiaoyun() : TriggerSkillV2("yhxiaoyun") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return {};
        for (ServerPlayer *target : room->getAlivePlayers())
            if (player->canDiscard(target, "ej")) return {{player, {objectName()}}};
        return {};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (ctx.owner->canDiscard(target, "ej")) candidates << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@yhxiaoyun-discard", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->canDiscard(target, "ej")) return false;
        ctx.owner->peiyin(this);
        const int id = room->askForCardChosen(ctx.owner, target, "ej", objectName(), false, Card::MethodDiscard);
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        const Player::Place place = room->getCardPlace(id);
        if (!card || card->hasFlag("using") || room->getCardOwner(id) != target
            || (place != Player::PlaceEquip && place != Player::PlaceDelayedTrick)
            || !ctx.owner->canDiscard(target, id)) return false;
        const QString pattern = place == Player::PlaceEquip ? "EquipCard" : "TrickCard";
        room->throwCard(id, target, ctx.owner);
        // The restriction is an applied turn effect and intentionally survives loss of the source skill.
        if (ctx.owner->isAlive()) room->setPlayerCardLimitation(ctx.owner, "use", pattern, true, objectName());
        return false;
    }
};
YHMeiyingCard::YHMeiyingCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void YHMeiyingCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	room->giveCard(from, to, this, "yhmeiying");
	int id = subcards.first();
	if (to->handCards().contains(id)&&to->canUse(Sanguosha->getCard(id))) {
		room->setPlayerMark(to, "yhmeiying_id-PlayClear", id);
		if (room->askForUseCard(to, "@@yhmeiying", "@yhmeiying:" + Sanguosha->getCard(id)->objectName())) {
			from->drawCards(1, "yhmeiying");
			return;
		}
	}
	room->loseHp(HpLostStruct(to, 1, "yhmeiying", from));
	room->addPlayerMark(from, "yhmeiying_used-PlayClear");
}

class YHMeiying : public ViewAsSkillV2
{
public:
    YHMeiying() : ViewAsSkillV2("yhmeiying", 1)
    {
        response_pattern = "@@yhmeiying";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.pattern == "@@yhmeiying")
            return request.reason != CardUseStruct::CARD_USE_REASON_PLAY
                && request.initiator->getMark("yhmeiying_id-PlayClear") >= 0;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("yhmeiying_used-PlayClear") <= 0;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !candidate || request.selectedCardIds.size() != 0) return false;
        const int id = candidate->getEffectiveId();
        if (request.pattern == "@@yhmeiying")
            return id == request.initiator->getMark("yhmeiying_id-PlayClear")
                && request.initiator->handCards().contains(id);
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->handCards().contains(id)
            && !candidate->isAvailable(request.initiator) && !candidate->isEquipped();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || !request.initiator) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (request.pattern == "@@yhmeiying" && !request.selectedCardIds.isEmpty()) {
            const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
            if (card) return card->getClassName();
        }
        return "YHMeiyingCard";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (request.pattern == "@@yhmeiying")
            return Sanguosha->getCard(request.selectedCardIds.first());
        YHMeiyingCard *card = new YHMeiyingCard;
        card->addSubcard(request.selectedCardIds.first());
        card->setSkillName(objectName());
        return card;
    }
};

class YHBozhiVS : public ViewAsSkillV2
{
public:
	YHBozhiVS() : ViewAsSkillV2("yhbozhi") { response_pattern = "@@yhbozhi"; }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && request.pattern == "@@yhbozhi"; }
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &request) const override
	{
		const QString name = request.initiator ? request.initiator->property("YHBozhiRecordTrick").toString() : QString();
		Card *card = name.isEmpty() ? nullptr : Sanguosha->cloneCard(name);
		if (!card) return objectName();
		const QString key = card->getClassName();
		card->deleteLater();
		return key;
	}
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!canActivate(request)) return nullptr;
		const QString name = request.initiator->property("YHBozhiRecordTrick").toString();
		if (name.isEmpty()) return nullptr;
		Card *card = Sanguosha->cloneCard(name, Card::NoSuit, 0);
		if (card) card->setSkillName(objectName());
		return card;
	}
};

class YHKudu : public TriggerSkillV2
{
public:
	YHKudu() : TriggerSkillV2("yhkudu")
	{
		events << TurnStart << EventPhaseStart;
	}

	Frequency getFrequency(const Player *target) const
	{
		if (target && target->getMark(objectName()) > 0)
			return NotFrequent;
		return Compulsory;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{
		return player && player->isAlive() && player->hasSkill(objectName()) && (event == TurnStart || event == EventPhaseStart)
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		QStringList choices, phase_names;
		QString choice, phase_name;
		phase_names << "judge" << "draw" << "play" << "discard";

		if (event == TurnStart) {
			if (getFrequency(player) == NotFrequent || !player->faceUp()) return false;

			foreach (QString phase_name, phase_names) {
				if (player->getMark("LostPlayerPhase_" + phase_name) <= 0)
					choices << "self=" + phase_name;
			}
			if (choices.isEmpty()) return false;

			room->sendCompulsoryTriggerLog(player, this);

			choice = room->askForChoice(player, objectName(), choices.join("+"));
			phase_name = choice.split("=").last();

			room->addPlayerMark(player, "LostPlayerPhase_" + phase_name);
			LogMessage log;
			log.from = player;
			log.type = "#YHKuduLosePhase";
			log.arg = phase_name;
			room->sendLog(log);

			QStringList lostphases;
			QString lostphase = player->property("SkillDescriptionRecord_yhkudu").toString();
			if (!lostphase.isEmpty()) lostphases = lostphase.split("+");
			if (!lostphases.contains(phase_name)) {
				lostphases << phase_name;
				room->setPlayerProperty(player, "SkillDescriptionRecord_yhkudu", lostphases.join("+"));
				QStringList _lostphases;
				foreach (QString src, lostphases)
					_lostphases << src << "|";
				player->setSkillDescriptionSwap(objectName(),"%arg11",_lostphases.join("+"));
				room->changeTranslation(player, objectName(), 1);
			}
			QVariant data = "yhkudu_lose_phase_" + phase_name;
			room->getThread()->trigger(EventForDiy, room, player, data);
		} else {
			if (getFrequency(player) == Compulsory || player->getPhase() != Player::Start) return false;

			for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
				if (!player->hasEquipArea(i)) continue;
				choices << QString::number(i);
			}
			if (choices.isEmpty()) return false;

			if (!player->askForSkillInvoke(this)) return false;
			player->peiyin(this);
			choice = room->askForChoice(player, objectName(), choices.join("+"));

			player->throwEquipArea(choice.toInt());
		}

		if (player->isDead()) return false;

		choices.clear();
		foreach (QString phase_name, phase_names)
			choices << "other=" + phase_name;
		choice = room->askForChoice(player, objectName(), choices.join("+"));
		phase_name = choice.split("=").last();

		ServerPlayer *last = player->getTag("YHKuduPlayer").value<ServerPlayer *>();
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->getMark("LostPlayerPhase_" + phase_name) > 0 || p == last) continue;
			targets << p;
		}
		if (targets.isEmpty()) return false;

		ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@yhkudu-target:" + phase_name);
		room->doAnimate(1, player->objectName(), t->objectName());
		player->setTag("YHKuduPlayer", QVariant::fromValue(t));
		room->setPlayerMark(t, "&yhkudu+:+" + phase_name, 1);
		return false;
	}
};

class YHKuduSkip : public TriggerSkillV2
{
public:
	YHKuduSkip() : TriggerSkillV2("#yhkudu")
	{
		events << EventPhaseChanging;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == EventPhaseChanging && target ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
		QStringList phase_names;
		phase_names << "judge" << "draw" << "play" << "discard";
		QList<Player::Phase> phases;
		phases << Player::Judge << Player::Draw << Player::Play << Player::Discard;

		int n = phases.indexOf(change.to);
		if (n < 0) return false;
		QString phase_name = phase_names.at(n);
		if (phase_name.isEmpty()) return false;

		QString mark = "&yhkudu+:+" + phase_name;
		if (player->getMark(mark) <= 0) return false;
		room->setPlayerMark(player, mark, 0);

		if (player->isSkipped(change.to)) return false;
		LogMessage log;
		log.type = "#ZhenguEffect";
		log.from = player;
		log.arg = "yhkudu";
		room->sendLog(log);
		player->skip(change.to);
		return false;
	}
};

YHKunmoCard::YHKunmoCard()
{
	mute = true;
	handling_method = Card::MethodUse;
	will_throw = false;
}

bool YHKunmoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	const Card *c = Sanguosha->getCard(subcards.first());
	return c->targetFilter(targets, to_select, Self);
}

bool YHKunmoCard::targetFixed() const
{
	const Card *c = Sanguosha->getCard(subcards.first());
	return c->targetFixed();
}

bool YHKunmoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	const Card *c = Sanguosha->getCard(subcards.first());
	return c->targetsFeasible(targets, Self);
}

void YHKunmoCard::onUse(Room *room, CardUseStruct &card_use) const
{
	ServerPlayer *from = card_use.from;
	QList<ServerPlayer *> tos = card_use.to;
	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = from;
	log.arg = "yhkunmo";
	room->sendLog(log);
	from->peiyin("yhkunmo");
	room->notifySkillInvoked(from, "yhkunmo");
	if (tos.isEmpty()) tos << from;
	room->useCard(CardUseStruct(Sanguosha->getCard(subcards.first()), from, tos));
}

class YHKunmoVS : public ViewAsSkillV2
{
public:
	YHKunmoVS() : ViewAsSkillV2("yhkunmo") { response_pattern = "@@yhkunmo"; setResponseOrUse(true); }
	bool canActivate(const ActiveSkillRequest &request) const override
	{ return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
	    || request.pattern == "@@yhkunmo"); }
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{ return request.initiator && request.selectedCardIds.isEmpty() && card
	    && card->isAvailable(request.initiator) && !request.initiator->isLocked(card)
	    && request.initiator->handCards().contains(card->getEffectiveId()); }
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (!request.initiator || request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest replay = request;
		replay.selectedCardIds.clear();
		const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
		return card && canSelectCard(replay, card);
	}
	TargetMode targetMode() const override { return NoTarget; }
	bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
	{ return targets.isEmpty(); }
	QString historyKey(const ActiveSkillRequest &) const override { return "YHKunmoCard"; }
	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		YHKunmoCard *card = new YHKunmoCard; card->addSubcard(Sanguosha->getCard(request.selectedCardIds.first())); return card;
	}
};

class YHKunmo : public TriggerSkillV2
{
public:
	YHKunmo() : TriggerSkillV2("yhkunmo")
	{
		events << EventPhaseSkipped << EventPhaseStart << EventPhaseChanging;
		view_as_skill = new YHKunmoVS;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		return target && (event == EventPhaseSkipped || event == EventPhaseStart || event == EventPhaseChanging)
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == EventPhaseSkipped) {
			if (player->getPhase() == Player::Draw) {
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if (p->isDead() || !p->hasSkill(objectName()) || !p->askForSkillInvoke(this)) continue;
					p->peiyin(this);
					p->drawCards(1, objectName());
				}
			} else if (player->getPhase() == Player::Play) {
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if (p->isDead() || !p->hasSkill(objectName())) continue;
					room->askForUseCard(p, "@@yhkunmo", "@yhkunmo");
				}
			}
		} else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			if (player->isDead() || !player->hasSkill(objectName()) || !player->askForSkillInvoke(this)) return false;
			player->peiyin(this);
			player->drawCards(1, objectName());
		} else {
			if (ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive) return false;
			if (player->isDead() || !player->hasSkill(objectName())) return false;
			room->askForUseCard(player, "@@yhkunmo", "@yhkunmo");
		}
		return false;
	}
};

class YHTanyou : public TriggerSkillV2
{
public:
	YHTanyou() : TriggerSkillV2("yhtanyou")
	{
        // This one-shot effect uses private instance state, not the public UI mark.
        setProperty("DescriptionUsageState", "tanyou_used");
        setProperty("DescriptionUsageLimit", 1);
        setProperty("DescriptionUsageScope", QT_TRANSLATE_NOOP("Player", "This game"));
        setProperty("DescriptionUsageLabel", QT_TRANSLATE_NOOP("Player", "Limited effect"));
		events << EventForDiy << Dying;
		frequency = Limited;
		limit_mark = "@yhtanyouMark";
		shiming_skill = true;
	}

	TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            if (!player->hasSkillInstance(objectName(), id) || player->isSkillInvalid(objectName(), id)) continue;
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (event == EventForDiy && data.toString().startsWith("yhkudu_lose_phase_")
                && !player->getSkillInstanceStateValue(objectName(), id, "tanyou_used", false).toBool())
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
            else if (event == Dying) {
                const DyingStruct dying = data.value<DyingStruct>();
                const QStringList names = player->getSkillInstanceStateValue(objectName(), id, "tanyou_players").toStringList();
                if (room->getShimingStatus(ref) <= 0 && dying.who && (dying.who == player || names.contains(dying.who->objectName())))
                    result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
		const SkillInstanceRef ref = ctx.activationRef;
		QStringList phase_names;
		phase_names << "judge" << "draw" << "play" << "discard";

		if (event == EventForDiy) {
			QString diy = ctx.original_data->toString();
			if (!diy.startsWith("yhkudu_lose_phase_")) return false;

			if (!player->getSkillInstanceStateValue(objectName(), ref.key.instanceID, "tanyou_used", false).toBool()) {
				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (p->getMaxHp() > 1)
						targets << p;
				}

				if (!targets.isEmpty()) {
					ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@yhtanyou-target", true, true);
					if (t && player->hasSkillInstance(objectName(), ref.key.instanceID)
                        && !player->getSkillInstanceStateValue(objectName(), ref.key.instanceID, "tanyou_used", false).toBool()) {
						player->peiyin(this, 1);

						player->setSkillInstanceStateValue(objectName(), ref.key.instanceID, "tanyou_used", true);
                        room->removePlayerMark(player, "@yhtanyouMark");
						room->doSuperLightbox(player, objectName());

						QStringList names = player->getSkillInstanceStateValue(objectName(), ref.key.instanceID, "tanyou_players").toStringList();
						if (!names.contains(t->objectName())) {
							names << t->objectName();
							player->setSkillInstanceStateValue(objectName(), ref.key.instanceID, "tanyou_players", names);
						}

						room->loseMaxHp(t, 1, objectName());
						room->addPlayerMark(t, "&yhtanyou_buff");
                        room->setSkillEffectDescription(t,
                            QString("yhtanyou:%1:%2").arg(player->objectName()).arg(ref.key.instanceID),
                            "@yhtanyou.effect", ref, "@yhtanyou.effect.expiry", "&yhtanyou_buff", true);
					}

				}
			}

			if (room->getShimingStatus(ref) > 0) return false;

			bool lose_all = true;
			foreach (QString phase_name, phase_names) {
				if (player->getMark("LostPlayerPhase_" + phase_name) <= 0) {
					lose_all = false;
					break;
				}
			}
			if (!lose_all) return false;

			if (!room->sendShimingLog(ref)) return false;

			QList<ServerPlayer *> players;
			players << player;
			QStringList names = player->getSkillInstanceStateValue(objectName(), ref.key.instanceID, "tanyou_players").toStringList();
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p && p->isAlive() && !players.contains(p))
					players << p;
			}
			room->sortByActionOrder(players);

			foreach (ServerPlayer *p, players) {
				room->gainMaxHp(p, 1, objectName());
				room->recover(p, RecoverStruct("yhtanyou", player));
			}

			foreach (QString phase_name, phase_names)
				room->setPlayerMark(player, "LostPlayerPhase_" + phase_name, 0);
			room->addPlayerMark(player, "yhkudu");
			room->changeTranslation(player, "yhkudu", 2);
		} else {
			if (room->getShimingStatus(ref) > 0) return false;
			DyingStruct dying = ctx.original_data->value<DyingStruct>();
			if (!dying.who) return false;
			QStringList names = player->getSkillInstanceStateValue(objectName(), ref.key.instanceID, "tanyou_players").toStringList();
			if (dying.who == player || names.contains(dying.who->objectName())) {
				if (!room->sendShimingLog(ref, false)) return false;
				room->handleAcquireDetachSkills(player, "-yhkunmo");

				foreach (QString phase_name, phase_names)
					room->setPlayerMark(player, "LostPlayerPhase_" + phase_name, 0);
				room->addPlayerMark(player, "yhkudu");
				room->changeTranslation(player, "yhkudu", 2);
			}
		}
		return false;
	}
};

class YHTanyouBuff : public TriggerSkillV2
{
public:
	YHTanyouBuff() : TriggerSkillV2("#yhtanyou")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == EventPhaseStart && target && target->isAlive() && target->getMark("&yhtanyou_buff") > 0
		    && target->getPhase() == Player::RoundStart ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		int mark = player->getMark("&yhtanyou_buff");/*

		player->setPhase(Player::Play);
		room->broadcastProperty(player, "phase");*/

		for (int i = 0; i < mark; i++) {
			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = "yhtanyou";
			room->sendLog(log);
			player->insertPhase(Player::Play);/*
			RoomThread *thread = room->getThread();

			if (!thread->trigger(EventPhaseStart, room, player))
				thread->trigger(EventPhaseProceeding, room, player);
			thread->trigger(EventPhaseEnd, room, player);*/
		}/*

		player->setPhase(Player::RoundStart);
		room->broadcastProperty(player, "phase");*/
		return false;
	}
};

class YHBozhi : public TriggerSkillV2
{
public:
	YHBozhi() : TriggerSkillV2("yhbozhi")
	{
		events << CardFinished;
		view_as_skill = new YHBozhiVS;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		return event == CardFinished && player && player->isAlive() && player->hasSkill(objectName())
		    && player->hasFlag("CurrentPlayer") && use.card && use.card->isKindOf("BasicCard")
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (!use.card->isKindOf("BasicCard")) return false;
		QString name = player->property("YHBozhiRecordTrick").toString();
		if (name.isEmpty()) return false;
		Card *c = Sanguosha->cloneCard(name, Card::NoSuit, 0);
		if (!c) return false;
		c->setSkillName(objectName());
		c->deleteLater();
		if (!player->canUse(c)) return false;
		if (c->targetFixed()) {
			if (!player->askForSkillInvoke(this, "yhbozhi:" + name, false)) return false;
			room->useCard(CardUseStruct(c, player, player));
		} else
			room->askForUseCard(player, "@@yhbozhi", "@yhbozhi:" + name);
		return false;
	}
};

class YHBozhiRecord : public TriggerSkillV2
{
public:
	YHBozhiRecord() : TriggerSkillV2("#yhbozhi")
	{
		events << PreCardUsed << EventPhaseChanging;
        global = true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return player && (event == PreCardUsed || event == EventPhaseChanging) ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (event == PreCardUsed) {
			if (player->getPhase() == Player::NotActive) return false;
			CardUseStruct use = ctx.original_data->value<CardUseStruct>();
			if (use.card->isKindOf("TrickCard"))
				player->addMark("yhbozhi_trick-Clear");
			if (use.card->isNDTrick()) {
				if (player->property("YHBozhiRecordTrick").toString().isEmpty())
					room->setPlayerProperty(player, "YHBozhiRecordTrick", use.card->objectName());
			}
		} else {
			if (ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive) return false;
			room->setPlayerProperty(player, "YHBozhiRecordTrick", "");
		}
		return false;
	}
};

class YHJixiang : public TriggerSkillV2
{
public:
	YHJixiang() : TriggerSkillV2("yhjixiang")
	{
		events << EventPhaseEnd;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return event == EventPhaseEnd && player && player->isAlive() && player->hasSkill(objectName())
	    && player->getPhase() == Player::Play ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (player->getPhase() != Player::Play) return false;
		int mark = player->getMark("yhbozhi_trick-Clear");
		mark = qMin(mark, 3);
		mark = qMax(1, mark);
		if (!player->askForSkillInvoke(this, "yhjixiang:" + QString::number(mark))) return false;
		player->peiyin(this);
		player->drawCards(mark, objectName());

		if (player->isDead() || player->isKongcheng()) return false;

		QHash<ServerPlayer *, QStringList> hash;
		QList<int> hands = player->handCards();
		int n = mark;

		while (n > 0) {
			if (player->isKongcheng()) break;

			CardsMoveStruct move = room->askForYijiStruct(player, hands, objectName(), false, false, false, n, room->getOtherPlayers(player),
														CardMoveReason(), "@yhjixiang-give", false, false);
			if (!move.to || move.card_ids.isEmpty()) break;
			n -= move.card_ids.length();

			ServerPlayer *to = (ServerPlayer *)move.to;
			QStringList ids = hash[to];
			foreach (int id, move.card_ids) {
				QString str = QString::number(id);
				hands.removeOne(id);
				if (!ids.contains(str))
					ids << str;
			}
			hash[to] = ids;
		}

		QList<CardsMoveStruct> moves;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isDead()) continue;
			QList<int> ids = ListS2I(hash[p]);
			if (ids.isEmpty()) continue;
			CardsMoveStruct move(ids, player, p, Player::PlaceHand, Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName(), p->objectName(), objectName(), ""));
			moves.append(move);
		}
		if (moves.isEmpty()) return false;
		room->moveCardsAtomic(moves, false);
		return false;
	}
};

class YHGuidu : public TriggerSkillV2
{
public:
    YHGuidu() : TriggerSkillV2("yhguidu") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (other->getHandcardNum() > ctx.owner->getHandcardNum()) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@yhguidu-target", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->peiyin(this);
        room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        return false;
    }
};

class YHDaobi : public TriggerSkillV2
{
public:
    YHDaobi() : TriggerSkillV2("yhdaobi")
    { events << Damage << Damaged; change_skill = true; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (event == Damage)
            return damage.to && damage.to != player && !damage.to->isNude()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        return damage.from && damage.from->isAlive() && damage.from != player && !player->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const int state = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
            ctx.activationRef.key.instanceID, "conversion_state", 1).toInt();
        if ((event == Damage && state != 1) || (event == Damaged && state != 2)) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ctx.choice = event == Damage ? "take" : "give";
        ctx.targets = {event == Damage ? damage.to : damage.from};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Commit only this admitted activation's conversion state before recipient effects.
        const int next = ctx.choice == "take" ? 2 : 1;
        ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName,
            ctx.activationRef.key.instanceID, "conversion_state", next);
        room->setChangeSkillState(ctx.owner, objectName(), next); // presentation compatibility only
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *recipient = ctx.choice == "take" ? ctx.owner : target;
        ServerPlayer *holder = ctx.choice == "take" ? target : ctx.owner;
        for (int i = 0; i < qMax(0, getEffectiveAmount(ctx)); ++i) {
            if (!recipient->isAlive() || holder->isNude()) break;
            const int id = room->askForCardChosen(recipient, holder, "he", objectName());
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card || card->hasFlag("using") || room->getCardOwner(id) != holder
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
            room->obtainCard(recipient, card, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION,
                recipient->objectName()), room->getCardPlace(id) != Player::PlaceHand);
        }
        return false;
    }
};
class YHShanhaiVS : public ViewAsSkillV2
{
public:
	YHShanhaiVS() : ViewAsSkillV2("yhshanhai", 1)
	{
		response_pattern = "@@yhshanhai";
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.pattern == "@@yhshanhai";
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
	{
		if (!canActivate(request) || !candidate || !request.selectedCardIds.isEmpty()) return false;
		const int id = candidate->getEffectiveId();
		const QList<int> received = ListS2I(request.initiator->property("YHShanhaiGetIds").toString().split("+"));
		if (!received.contains(id) || !request.initiator->handCards().contains(id)) return false;
		Slash slash(Card::SuitToBeDecided, -1);
		slash.addSubcard(candidate);
		slash.setSkillName(objectName());
		return slash.isAvailable(request.initiator);
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		if (request.selectedCardIds.size() != 1) return false;
		ActiveSkillRequest prefix = request;
		prefix.selectedCardIds.clear();
		return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		Slash *slash = new Slash(Card::SuitToBeDecided, -1);
		slash->addSubcard(request.selectedCardIds.first());
		slash->setSkillName(objectName());
		return slash;
	}

	bool willThrowSelectedCards() const override { return false; }
	QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class YHShanhai : public TriggerSkillV2
{
public:
	YHShanhai() : TriggerSkillV2("yhshanhai")
	{
		events << CardsMoveOneTime;
		view_as_skill = new YHShanhaiVS;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		return event == CardsMoveOneTime && player && player->isAlive() && player->hasSkill(objectName())
		    && move.to == player && move.to_place == Player::PlaceHand ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
		if (!move.from || move.to != player || move.to_place != Player::PlaceHand) return false;
		QList<int> ids;
		for (int i = 0; i < move.card_ids.length(); i++) {
			if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip)
				ids << move.card_ids.at(i);
		}
		if (ids.isEmpty()) return false;

		room->setPlayerProperty(player, "YHShanhaiGetIds", ListI2S(ids).join("+"));

		try {
			while (player->isAlive()) {  //这里其实有bug，如果插入了获得牌，之前的记录就没了
				if (!room->askForUseCard(player, "@@yhshanhai", "@yhshanhai")) break;
				QList<int> hands = player->handCards();
				foreach (int id, ids) {
					if (!hands.contains(id))
						ids.removeOne(id);
				}
				if (ids.isEmpty()) break;
				room->setPlayerProperty(player, "YHShanhaiGetIds", ListI2S(ids).join("+"));
			}
		}
		catch (TriggerEvent triggerEvent) {
			if (triggerEvent == TurnBroken || triggerEvent == StageChange)
				room->setPlayerProperty(player, "YHShanhaiGetIds", "");
			throw triggerEvent;
		}

		room->setPlayerProperty(player, "YHShanhaiGetIds", "");
		return false;
	}
};

class YHShanhaiTargetMod : public TargetModSkillV2
{
public:
	YHShanhaiTargetMod() : TargetModSkillV2("#yhshanhai", "Slash")
	{
		frequency = NotFrequent;
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.modType == TargetModSkill::Residue && ctx.card && ctx.card->getSkillName() == "yhshanhai"
		    ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
	}
};

class YHYanglian : public TriggerSkillV2
{
public:
	YHYanglian() : TriggerSkillV2("yhyanglian")
	{
		events << Damaged;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{ return event == Damaged && player && player->isAlive() && player->hasSkill(objectName())
	    ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const DamageStruct damage = ctx.original_data->value<DamageStruct>();
		QStringList generals = Sanguosha->getLimitedGeneralNames(), used = player->property("YHYanglianUsedYinni").toString().split("+"), yinnis;
		foreach (QString name, generals) {
			const General *gen = Sanguosha->getGeneral(name);
			if (!gen) continue;
			foreach (const Skill *sk, gen->getVisibleSkillList()) {
				if (!sk->isHideSkill() || yinnis.contains(sk->objectName()) || used.contains(sk->objectName())) continue;
				const TriggerSkill *triggerskill = Sanguosha->getTriggerSkill(sk->objectName());
				if (!triggerskill) continue;
				bool appear = false;
				if (!triggerskill->hasEvent(Appear)) {
					foreach (const Skill *skill, Sanguosha->getRelatedSkills(sk->objectName())) {
						const TriggerSkill *related_trigger = Sanguosha->getTriggerSkill(skill->objectName());
						if (!related_trigger || !related_trigger->hasEvent(Appear)) continue;
						appear = true;
						break;
					}
				} else
					appear = true;
				if (!appear) continue;
				yinnis << sk->objectName();
			}
		}
		if (yinnis.isEmpty()) return false;

		for (int i = 0; i < damage.damage; i++) {
			if (player->isDead()) break;

			QStringList all_sk = yinnis, five_sk;
			for (int i = 0; i < 5; i++) {
				if (all_sk.isEmpty()) break;
				QString sk = all_sk.at(qsanRandomBounded(all_sk.length()));
				all_sk.removeOne(sk);
				five_sk << sk;
			}
			if (five_sk.isEmpty()) break;

			five_sk << "cancel";

			QString sk = room->askForChoice(player, objectName(), five_sk.join("+"), QVariant::fromValue(damage));
			if (sk == "cancel") break;

			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player, objectName());

			used = player->property("YHYanglianUsedYinni").toString().split("+");
			used << sk;
			yinnis.removeOne(sk);
			room->setPlayerProperty(player, "YHYanglianUsedYinni", used.join("+"));

			const TriggerSkill *triggerskill = Sanguosha->getTriggerSkill(sk);
			if (!triggerskill) continue;

			//const TriggerSkill *triggerskill_copy = triggerskill;

			if (!triggerskill->hasEvent(Appear)) {
				foreach (const Skill *skill, Sanguosha->getRelatedSkills(sk)) {
					const TriggerSkill *related_trigger = Sanguosha->getTriggerSkill(skill->objectName());
					if (!related_trigger || !related_trigger->hasEvent(Appear)) continue;
					triggerskill = related_trigger;
					break;
				}
			}

			room->setPlayerProperty(player, "pingjian_triggerskill", sk);
			room->getThread()->addTriggerSkill(triggerskill);

			try {
				QVariant data;
				triggerskill->trigger(Appear, room, player, data);
				if (player->isAlive() && !player->getGeneral2()) {

					QStringList gens;
					foreach (QString name, generals) {
						const General *gen = Sanguosha->getGeneral(name);
						if (!gen) continue;
						foreach (const Skill *g_sk, gen->getVisibleSkillList()) {
							if (g_sk->objectName() == sk) {
								gens << name;
								continue;
							}
						}
					}
					if (gens.isEmpty()) continue;
					QString genn = room->askForGeneral(player, gens.join("+"));
					if (!gens.contains(genn))
						genn = gens.at(qsanRandomBounded(gens.length()));
					room->addPlayerMark(player, "yhyanglian_add_general2-Keep");
					room->changeHero(player, genn, false, false, true);
				}
			}
			catch (TriggerEvent triggerEvent) {
				if (triggerEvent == TurnBroken || triggerEvent == StageChange)
					room->setPlayerProperty(player, "pingjian_triggerskill", "");
				throw triggerEvent;
			}
			room->setPlayerProperty(player, "pingjian_triggerskill", "");
		}
		return false;
	}
};

class YHYanglianRemove : public TriggerSkillV2
{
public:
	YHYanglianRemove() : TriggerSkillV2("#yhyanglian")
	{
		events << DamageInflicted;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return event == DamageInflicted && target && target->isAlive() && target->getMark("yhyanglian_add_general2-Keep") > 0
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		room->setPlayerMark(player, "yhyanglian_add_general2-Keep", 0);
		if (!player->getGeneral2()) return false;
		room->sendCompulsoryTriggerLog(player, "yhyanglian", true, true);
		LogMessage log;
		log.type = "#YHXiandao2";
		log.arg = player->getGeneral2Name();
		log.from = player;
		room->sendLog(log);
		room->changeHero(player, "", false, false, true, false);
		return false;
	}
};

class YHXiandao : public TriggerSkillV2
{
public:
    YHXiandao() : TriggerSkillV2("yhxiandao") { events << EventPhaseStart; m_baseAmount = 4; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QString count = QString::number(qMax(0, getEffectiveAmount(ctx) - ctx.owner->getHandcardNum()));
        QStringList choices; choices << "zhujiang=" + count;
        if (ctx.owner->getGeneral2()) choices << "fujiang=" + count;
        choices << "cancel";
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        if (choice == "cancel" || !choices.contains(choice)) return false;
        ctx.choice = choice.startsWith("fujiang") ? "deputy" : "head";
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "deputy" && !target->getGeneral2()) return false;
        ctx.owner->peiyin(this);
        LogMessage log; log.from = target;
        log.type = ctx.choice == "deputy" ? "#YHXiandao2" : "#YHXiandao1";
        log.arg = ctx.choice == "deputy" ? target->getGeneral2Name() : target->getGeneralName();
        room->sendLog(log);
        if (ctx.choice == "deputy") room->changeHero(target, "", false, false, true, false);
        else if (target->getGeneral2()) {
            const QString deputy = target->getGeneral2Name();
            room->changeHero(target, "", false, false, true, false);
            room->changeHero(target, deputy, false, false, false, false);
        } else {
            room->setPlayerMark(target, "yhyanglian_add_general2-Keep", 0);
            const QString soldier = target->isFemale() ? "sujiangf" : "sujiang";
            const int maxHp = target->getMaxHp(), hp = target->getHp();
            const General::Gender gender = target->getGender();
            room->changeHero(target, soldier, false, false, false, false);
            target->setGender(gender);
            target->setMaxHp(maxHp); room->broadcastProperty(target, "maxhp");
            target->setHp(hp); room->broadcastProperty(target, "hp");
        }
        // The accepted draw survives the hero replacement that retired its original source.
        if (target->isAlive()) target->drawCards(qMax(0, getEffectiveAmount(ctx) - target->getHandcardNum()), objectName());
        return false;
    }
};
class YHQizu : public TriggerSkillV2
{
public:
	YHQizu() : TriggerSkillV2("yhqizu")
	{
		events << CardUsed;
		global = true;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		return event == CardUsed && target && target->isAlive() && use.card
		    && (use.card->isKindOf("Duel") || (use.card->isRed() && use.card->isKindOf("Slash")))
		    ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool canBeTarget(ServerPlayer *from, ServerPlayer *to, const Card *card) const
	{
		if (card->isKindOf("Slash"))
			return from->canSlash(to, card, false);
		else
			return from->canUse(card, to, true);
		return false;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		if (use.card->isKindOf("Duel") || (use.card->isRed() && use.card->isKindOf("Slash"))) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->isDead() || !p->hasSkill(objectName()) || !canBeTarget(player, p, use.card)) continue;
				use = ctx.original_data->value<CardUseStruct>();
				if (use.to.contains(p)) continue;

				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *q, room->getOtherPlayers(p)) {
					if (canBeTarget(player, q, use.card))
						targets << q;
				}
				if (targets.isEmpty()) return false;

				p->setTag("YHQizuData", QVariant::fromValue(use));
				ServerPlayer *t = room->askForPlayerChosen(p, targets, objectName(), "@yhqizu-target:" + use.card->objectName(), true, true);
				p->removeTag("YHQizuData");
				if (!t) continue;
				p->peiyin(this);

				use.to << t;
				if (canBeTarget(player, p, use.card))
					use.to << p;
				room->sortByActionOrder(use.to);
				*ctx.original_data = QVariant::fromValue(use);

				int mark = p->getMark("&yhqizu_x");
				mark = 3 - mark;

				targets.clear();
				targets << p << t;
				room->sortByActionOrder(targets);

				if (mark > 0)
					room->drawCards(targets, mark, objectName());
				else if (mark < 0) {
					mark = -mark;
					foreach (ServerPlayer *pp, targets) {
						if (pp->isDead() || pp->isNude()) continue;
						room->askForDiscard(pp, objectName(), mark, mark, false, true);
					}
				}

				if (p->isAlive())
					room->addPlayerMark(p, "&yhqizu_x");
			}
		}
		return false;
	}
};

class YHShoulu : public TriggerSkillV2
{
public:
	YHShoulu() : TriggerSkillV2("yhshoulu")
	{
		events << DamageInflicted;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return event == DamageInflicted && player && player->isAlive() && player->hasSkill(objectName())
	    ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QList<int> move_ids;
		int judge = 0;

		for (int i = 0; i < 2; i++) {
			QList<ServerPlayer *> from_players;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				bool add = false;
				foreach (int id, p->getEquipsId() + p->getJudgingAreaID()) {
					if (!move_ids.contains(id)) {
						add = true;
						break;
					}
				}
				if (add)
					from_players << p;
			}
			if (from_players.isEmpty()) return false;

			ServerPlayer *from = room->askForPlayerChosen(player, from_players, "yhshoulu_from", "@movefield-from-optional", true);
			if (!from) return false;

			if (i == 0) {
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				player->peiyin(this);
				room->notifySkillInvoked(player, objectName());
			}

			int id = room->askForCardChosen(player, from, "ej", objectName(), false, Card::MethodNone, move_ids);
			if (id < 0) return false;

			const Card *c = Sanguosha->getCard(id);
			Player::Place place = room->getCardPlace(id);
			move_ids << id;

			QList<ServerPlayer *> to_players;
			foreach (ServerPlayer *p, room->getOtherPlayers(from)) {
				if (place == Player::PlaceEquip) {
					const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
					if (p->getEquip(equip->location()) == nullptr && !from->isProhibited(p, c) && p->hasEquipArea(equip->location()))
						to_players << p;
				} else if (place == Player::PlaceDelayedTrick) {
					if (!from->isProhibited(p, c) && !p->containsTrick(c->objectName()) && p->hasJudgeArea())
						to_players << p;
				}
			}
			if (to_players.isEmpty()) return false;

			if (place == Player::PlaceDelayedTrick)
				judge++;

			ServerPlayer *to = room->askForPlayerChosen(player, to_players, "yhshoulu_to", "@movefield-to:" + c->objectName());
			room->moveCardTo(c, from, to, place, CardMoveReason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), objectName(), ""));
		}

		if (judge != 2) return false;
		DamageStruct damage = ctx.original_data->value<DamageStruct>();

		int d = damage.damage + 1;
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
							"@yhshoulu-transfer:" + QString::number(d), true, true);
		if (!t) return false;
		player->peiyin(this);

		damage.to = t;
		damage.transfer = true;
		damage.transfer_reason = objectName();
		damage.damage = d;
		player->setTag("TransferDamage", QVariant::fromValue(damage));
		*ctx.original_data = QVariant::fromValue(damage);
		return true;
	}
};

class YHQupai : public TriggerSkillV2
{
public:
	YHQupai() : TriggerSkillV2("yhqupai")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
	{ return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
	    && player->getPhase() == Player::Play ? TriggerList{{player, {objectName()}}} : TriggerList(); }

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		QString choice = room->askForChoice(player, objectName(), "1+2+cancel");
		if (choice == "cancel") return false;

		LogMessage log;
		log.type = "#YHQupaiInvoke";
		log.arg = objectName();
		log.from = player;
		log.arg2 = "yhqupai:" + choice;
		room->sendLog(log);
		player->peiyin(this);
		room->notifySkillInvoked(player, objectName());

		room->addPlayerMark(player, "yhqupai_choice_" + choice + "-PlayClear", 1);
		return false;
	}
};

class YHQupaiEffect : public TriggerSkillV2
{
public:
	YHQupaiEffect() : TriggerSkillV2("#yhqupai")
	{
		events << ConfirmDamage << CardUsed << CardResponded << PreCardUsed << PreCardResponded;
		global = true;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed || triggerEvent == PreCardResponded)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
	{
		return target && (event == ConfirmDamage || event == CardUsed || event == CardResponded
		    || event == PreCardUsed || event == PreCardResponded) ? TriggerList{{target, {objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QVariant &data = *ctx.original_data;
		if (event == ConfirmDamage) {
			int mark = player->getMark("yhqupai_choice_1-PlayClear");
			if (mark <= 0) return false;

			DamageStruct damage = data.value<DamageStruct>();
			LogMessage log;
			log.type = "#YHHankaiDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = "yhqupai";
			log.arg2 = QString::number(damage.damage);
			log.arg3 = QString::number(damage.damage += mark);
			room->sendLog(log);
			player->peiyin("yhqupai");
			room->notifySkillInvoked(player, "yhqupai");
			*ctx.original_data = QVariant::fromValue(damage);
		} else if (event == CardUsed) {
			int mark = player->getMark("yhqupai_choice_2-PlayClear");
			if (mark <= 0) return false;

			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;

			QString color = use.card->getColorString(), last_color;
			foreach (QString mark, player->getMarkNames()) {
				if (!mark.startsWith("&yhqupailast+") || !mark.endsWith("-PlayClear") || player->getMark(mark) <= 0) continue;
				QStringList marks = mark.split("+");
				if (marks.length() != 2) continue;
				QStringList last_colors = marks.last().split("-");
				last_color = last_colors.first();
				break;
			}

			foreach (QString mark, player->getMarkNames()) {
				if (!mark.startsWith("&yhqupailast+") || !mark.endsWith("-PlayClear") || player->getMark(mark) <= 0) continue;
				room->setPlayerMark(player, mark, 0);
			}
			room->setPlayerMark(player, "&yhqupailast+" + color + "-PlayClear", 1);

			if (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) return false;
			if (last_color.isEmpty() || last_color == color) return false;

			QList<ServerPlayer *> targets = room->getCardTargets(player, use.card, use.to);

			player->setTag("YHQupaiData", data);
			QString prompt = QString("@yhqupai-target:%1::%2").arg(use.card->objectName()).arg(mark);
			QList<ServerPlayer *> adds = room->askForPlayersChosen(player, targets, objectName(), 0, mark, prompt);
			if (adds.isEmpty()) return false;

			LogMessage log;
			log.type = "#QiaoshuiAdd";
			log.from = player;
			log.to = adds;
			log.card_str = use.card->toString();
			log.arg = "yhqupai";
			room->sendLog(log);
			foreach(ServerPlayer *p, adds) {
				room->doAnimate(1, player->objectName(), p->objectName());
				use.to << p;
			}
			player->peiyin("yhqupai");
			room->notifySkillInvoked(player, "yhqupai");

			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		} else if (event == CardResponded) {
			int mark = player->getMark("yhqupai_choice_2-PlayClear");
			if (mark <= 0) return false;

			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_isUse || res.m_card->isKindOf("SkillCard")) return false;

			QString color = res.m_card->getColorString();
			foreach (QString mark, player->getMarkNames()) {
				if (!mark.startsWith("&yhqupailast+") || !mark.endsWith("-PlayClear") || player->getMark(mark) <= 0) continue;
				room->setPlayerMark(player, mark, 0);
			}
			room->setPlayerMark(player, "&yhqupailast+" + color + "-PlayClear", 1);
		} else {
			int mark = player->getMark("yhqupai_choice_1-PlayClear");
			if (mark <= 0) return false;

			const Card *card = nullptr;
			if (event == PreCardUsed)
				card = data.value<CardUseStruct>().card;
			else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if (!res.m_isUse) return false;
				card = res.m_card;
			}
			if (!card || card->isKindOf("SkillCard")) return false;

			bool used = false;
			foreach (QString mark, player->getMarkNames()) {
				if (!mark.startsWith("&yhqupaicolor+") || !mark.endsWith("-PlayClear") || player->getMark(mark) <= 0) continue;
				used = true;
				break;
			}
			if (used) return false;
			room->setPlayerMark(player, "&yhqupaicolor+" + card->getColorString() + "-PlayClear", 1);
		}
		return false;
	}
};

class YHQupaiLimit : public CardLimitSkill
{
public:
	YHQupaiLimit() : CardLimitSkill("#yhqupai-limit")
	{
		frequency = NotCompulsory;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		QString color;
		foreach (QString mark, target->getMarkNames()) {
			if (!mark.startsWith("&yhqupaicolor+") || !mark.endsWith("-PlayClear") || target->getMark(mark) <= 0) continue;
			QStringList marks = mark.split("+");
			if (marks.length() != 2) continue;
			QStringList colors = marks.last().split("-");
			color = colors.first();
			break;
		}
		if (!color.isEmpty()) {
			QStringList colors;
			colors << "red" << "black" << "no_color";
			colors.removeOne(color);
			return ".|" + colors.join(",");
		}
		return "";
	}
};

class YHYange : public TriggerSkillV2
{
public:
    YHYange() : TriggerSkillV2("yhyange") { events << CardFinished << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Turn; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && !player->hasFlag("CurrentPlayer")
                && room->hasCurrent() && use.card && !use.card->isKindOf("SkillCard") && use.card->hasSuit()
            ? availableSources(player) : TriggerList();
    }

    TriggerList availableSources(ServerPlayer *player) const
    {
        TriggerList result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext candidate; candidate.owner = candidate.invoker = player;
            candidate.skill_name = objectName(); candidate.instanceID = id;
            candidate.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(candidate)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets = room->getOtherPlayers(ctx.owner);
        ctx.targets.prepend(ctx.owner);
        ctx.manual_effect = true;
        return true;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.invoker->peiyin(this);
        const Card *used = ctx.original_data->value<CardUseStruct>().card;
        const Card::Suit suit = used->getSuit();
        QList<int> revealed;
        int match = -1;
        try {
            while (ctx.invoker->isAlive()) {
                const QList<int> ids = room->getNCards(1, false);
                if (ids.isEmpty()) break;
                const int id = ids.first();
                // Track every revealed ID before nested moves can interrupt this effect.
                revealed << id;
                CardsMoveStruct move(id, nullptr, Player::PlaceTable,
                    CardMoveReason(CardMoveReason::S_REASON_TURNOVER, ctx.invoker->objectName(), objectName(), ""));
                room->moveCardsAtomic(move, true);
                if (Sanguosha->getCard(id)->getSuit() == suit) { match = id; break; }
            }
            if (match >= 0 && ctx.invoker->isAlive() && room->getCardPlace(match) == Player::PlaceTable) {
                room->obtainCard(ctx.invoker, match);
                revealed.removeOne(match);
                QList<int> available;
                for (int id : revealed)
                    if (room->getCardPlace(id) == Player::PlaceTable) available << id;
                ctx.extra_data = ListI2V(available);
                if (!available.isEmpty()) {
                    room->fillAG(available);
                    // Distribution order and each recipient remain visible to V2 target interception.
                    for (ServerPlayer *target : ctx.targets) {
                        if (ctx.extra_data.toList().isEmpty()) break;
                        skillEffect(event, room, actor, ctx, target);
                    }
                    room->getThread()->delay();
                }
            }
            clearRestCards(room, revealed);
        } catch (TriggerEvent) {
            clearRestCards(room, revealed);
            throw;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> available;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (room->getCardPlace(id) == Player::PlaceTable) available << id;
        }
        if (available.isEmpty()) { ctx.extra_data = QVariantList(); return false; }
        const int id = room->askForAG(target, available, false, objectName());
        if (available.contains(id) && room->getCardPlace(id) == Player::PlaceTable) {
            available.removeOne(id);
            ctx.extra_data = ListI2V(available);
            room->takeAG(target, id);
        }
        return false;
    }

private:
    void clearRestCards(Room *room, const QList<int> &ids) const
    {
        room->clearAG();
        QList<int> remaining;
        for (int id : ids)
            if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
        if (remaining.isEmpty()) return;
        DummyCard cards(remaining);
        room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, "", objectName(), ""), nullptr);
    }
};

YinhuPackage::YinhuPackage()
	: Package("yinhu")
{
	General *yh_zhangwenyuan = new General(this, "yh_zhangwenyuan", "wei", 4);
	yh_zhangwenyuan->addSkill(new YHShecuo);
	yh_zhangwenyuan->addSkill("#fulinbf");
	yh_zhangwenyuan->addSkill(new YHShecuoliLimit);
	related_skills.insert("yhshecuo", "#yhshecuo-limit");
	related_skills.insert("yhshecuo", "#fulinbf");

	General *yh_sunquan = new General(this, "yh_sunquan", "wei", 4);
	yh_sunquan->setStartHp(3);
	yh_sunquan->addSkill(new YHYingfu);
	yh_sunquan->addSkill(new YHNabi);
	yh_sunquan->addSkill(new YHNabiTransfer);
	yh_sunquan->addSkill(new YHHuanglong);
	related_skills.insert("yhnabi", "#yhnabi");

	General *yh_liuguanzhang = new General(this, "yh_liuguanzhang", "shu", 4);
	yh_liuguanzhang->addSkill(new YHYijie);
	yh_liuguanzhang->addSkill(new YHXinghan);
	yh_liuguanzhang->addSkill(new YHXinghanDraw);
	yh_liuguanzhang->addSkill(new YHXinghanTarget);
	related_skills.insert("yhxinghan", "#yhxinghan-draw");
	related_skills.insert("yhxinghan", "#yhxinghan-target");

	General *yh_chenshou = new General(this, "yh_chenshou", "shu", 3);
	yh_chenshou->addSkill(new YHZhushi);
	yh_chenshou->addSkill(new YHZhushiPut);
	yh_chenshou->addSkill(new YHQubi);
	//yh_chenshou->addSkill(new Skill("yhqubi", Skill::Compulsory)); //耦合进了Room::drawCards
	yh_chenshou->addSkill(new YHShijin);
	related_skills.insert("yhzhushi", "#yhzhushi");

	General *yh_zhugeliang = new General(this, "yh_zhugeliang", "wu", 3);
	yh_zhugeliang->addSkill(new YHBianzhan);
	yh_zhugeliang->addSkill(new YHJifeng);
	yh_zhugeliang->addSkill(new YHJifengEffect);

	General *yh_wangfan = new General(this, "yh_wangfan", "wu", 3);
	yh_wangfan->addSkill(new YHHuntian);
	yh_wangfan->addSkill(new YHCeri);

	General *yh_caocao = new General(this, "yh_caocao", "qun", 4);
	yh_caocao->addSkill(new YHSancai);
	yh_caocao->addSkill(new YHSancaiAttackRange);
	yh_caocao->addSkill(new YHJuyi);
	related_skills.insert("yhsancai", "#yhsancai");

	General *yh_xunyu = new General(this, "yh_xunyu", "qun", 3);
	yh_xunyu->addSkill(new YHHanjie);
	yh_xunyu->addSkill(new YHJuxian);

	General *yh_zhanghua = new General(this, "yh_zhanghua", "jin", 3);
	yh_zhanghua->addSkill(new YHDujian);
	yh_zhanghua->addSkill(new YHDujianTarget);
	yh_zhanghua->addSkill(new YHBuque);
	yh_zhanghua->addSkill(new YHBuquePindian);
	yh_zhanghua->addSkill(new YHZhanghua("yhbuque"));
	yh_zhanghua->addSkill(new YHChenzhen);
	yh_zhanghua->addSkill(new YHZhanghua("yhchenzhen"));
	related_skills.insert("yhdujian", "#yhdujian");
	related_skills.insert("yhbuque", "#yhbuque");
	related_skills.insert("yhbuque", "#yhbuque-move");
	related_skills.insert("yhchenzhen", "#yhchenzhen-move");

	General *yh_liushan = new General(this, "yh_liushan", "jin", 3);
	yh_liushan->addSkill(new YHSigong);
	yh_liushan->addSkill(new YHSigongEffect);
	yh_liushan->addSkill(new YHXijian);
	yh_liushan->addSkill(new YHXijianEffect);
	related_skills.insert("yhsigong", "#yhsigong");
	related_skills.insert("yhxijian", "#yhxijian");

	General *yh_shenxuchu = new General(this, "yh_shenxuchu", "god", 4);
	yh_shenxuchu->addSkill(new YHBoben);
	yh_shenxuchu->addSkill(new YHBobenDamage);
	yh_shenxuchu->addSkill(new YHHankai);
	yh_shenxuchu->addSkill(new YHHankaiEffect);
	yh_shenxuchu->addSkill(new YHHankaiLimit);
	related_skills.insert("yhboben", "#yhboben");
	related_skills.insert("yhhankai", "#yhhankai");
	related_skills.insert("yhhankai", "#yhhankai-limit");

	General *yh_shenmachao = new General(this, "yh_shenmachao", "god", 4);
	yh_shenmachao->addSkill(new YHFeisha);
	yh_shenmachao->addSkill(new YHJuantu);
	yh_shenmachao->addSkill(new YHJuantuNumber);
	yh_shenmachao->addSkill(new YHJuantuClear);
	yh_shenmachao->addSkill(new YHQuanwang);
	yh_shenmachao->addRelateSkill("yhchouxi");
	related_skills.insert("yhjuantu", "#yhjuantu");
	related_skills.insert("yhjuantu", "#yhjuantu-clear");

	General *yh_nvzhuangsimayi = new General(this, "yh_nvzhuangsimayi", "wei", 4, false);
	yh_nvzhuangsimayi->setStartHp(3);
	yh_nvzhuangsimayi->addSkill(new YHChenwen);
	yh_nvzhuangsimayi->addSkill(new YHShouzhuang);
	yh_nvzhuangsimayi->addSkill(new YHPodi);
	yh_nvzhuangsimayi->addSkill(new YHPodiInvalidity);
	yh_nvzhuangsimayi->addSkill(new YHPodiTargetMod);
	related_skills.insert("yhpodi", "#yhpodi-inv");
	related_skills.insert("yhpodi", "#yhpodi-target");

	General *yh_dingfuren = new General(this, "yh_dingfuren", "wei", 3, false);
	yh_dingfuren->addSkill(new YHDuwei);
	yh_dingfuren->addSkill(new YHDuweiTargetMod);
	yh_dingfuren->addSkill("#choulve-record");
	yh_dingfuren->addSkill(new YHSiku);
	related_skills.insert("yhduwei", "#yhduwei");
	related_skills.insert("yhduwei", "#choulve-record");

	General *yh_cuifuren = new General(this, "yh_cuifuren", "shu", 3, false);
	yh_cuifuren->addSkill(new YHQingsi);
	yh_cuifuren->addSkill(new YHChuzhu);
	yh_cuifuren->addSkill(new YHChuzhuMark);
	related_skills.insert("yhchuzhu", "#yhchuzhu");

	General *yh_liumu = new General(this, "yh_liumu", "shu", 3, false);
	yh_liumu->addSkill(new YHHuairen);
	yh_liumu->addSkill(new YHHuairenEffect);
	yh_liumu->addSkill(new YHHuairenLimit);
	yh_liumu->addSkill(new YHYuren);
	related_skills.insert("yhhuairen", "#yhhuairen");
	related_skills.insert("yhhuairen", "#yhhuairen-limit");

	General *yh_zhupeilan = new General(this, "yh_zhupeilan", "wu", 3, false);
	yh_zhupeilan->addSkill(new YHRangwei);
	yh_zhupeilan->addSkill(new YHPosi);

	General *yh_sunlubansunluyu = new General(this, "yh_sunlubansunluyu", "wu", 3, false);
	yh_sunlubansunluyu->addSkill(new YHXiaoyun);
	yh_sunlubansunluyu->addSkill(new YHMeiying);

	General *yh_liufuren = new General(this, "yh_liufuren", "qun", 3, false);
	yh_liufuren->addSkill(new YHKudu);
	yh_liufuren->addSkill(new YHKuduSkip);
	yh_liufuren->addSkill(new YHKunmo);
	yh_liufuren->addSkill(new YHTanyou);
	yh_liufuren->addSkill(new YHTanyouBuff);
	related_skills.insert("yhtanyou", "#yhtanyou");

	General *yh_zhenji = new General(this, "yh_zhenji", "qun", 3, false);
	yh_zhenji->addSkill(new YHBozhi);
	yh_zhenji->addSkill(new YHBozhiRecord);
	yh_zhenji->addSkill(new YHJixiang);
	related_skills.insert("yhbozhi", "#yhbozhi");

	General *yh_jiananfeng = new General(this, "yh_jiananfeng", "jin", 3, false);
	yh_jiananfeng->addSkill(new YHGuidu);
	yh_jiananfeng->addSkill(new YHDaobi);
	yh_jiananfeng->addSkill(new YHShanhai);
	yh_jiananfeng->addSkill(new YHShanhaiTargetMod);
	related_skills.insert("yhshanhai", "#yhshanhai");

	General *yh_weihuacun = new General(this, "yh_weihuacun", "jin", 3, false);
	yh_weihuacun->addSkill(new YHYanglian);
	yh_weihuacun->addSkill(new YHYanglianRemove);
	yh_weihuacun->addSkill(new YHXiandao);
	related_skills.insert("yhyanglian", "#yhyanglian");

	General *yh_shenerqiao = new General(this, "yh_shenerqiao", "god", 3, false);
	yh_shenerqiao->addSkill(new YHQizu);
	yh_shenerqiao->addSkill(new YHShoulu);

	General *yh_shencaiwenji = new General(this, "yh_shencaiwenji", "god", 3, false);
	yh_shencaiwenji->addSkill(new YHQupai);
	yh_shencaiwenji->addSkill(new YHQupaiEffect);
	yh_shencaiwenji->addSkill(new YHQupaiLimit);
	yh_shencaiwenji->addSkill(new YHYange);
	related_skills.insert("yhqupai", "#yhqupai");
	related_skills.insert("yhqupai", "#yhqupai-limit");

	addMetaObject<YHShecuoCard>();
	addMetaObject<YHYijieCard>();
	addMetaObject<YHZhushiCard>();
	addMetaObject<YHBianzhanCard>();
	addMetaObject<YHHuntianCard>();
	addMetaObject<YHJuxianCard>();
	addMetaObject<YHBuquePutCard>();
	addMetaObject<YHBuqueCard>();
	addMetaObject<YHXijianGiveCard>();
	addMetaObject<YHQuanwangCard>();
	addMetaObject<YHPodiCard>();
	addMetaObject<YHYurenCard>();
	addMetaObject<YHMeiyingCard>();
	addMetaObject<YHKunmoCard>();

	skills << new YHBuquePut << new YHXijianGive << new YHChouxi << new YHChouxiDamage;
	related_skills.insert("yhchouxi", "#yhchouxi");
}
ADD_PACKAGE(Yinhu)
