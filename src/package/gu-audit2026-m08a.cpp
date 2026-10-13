// GU-audit2026 2026-08a: 威马腾 / 王越 / 傅干 / 木鹿大王（十周年新版）.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-08 (translations in lang/zh_CN/Package/Mobile.lua).
// Readings: effects the doc writes without 「可」 resolve without a prompt; 试锋② and
// 剑道's swap say 「可」 and ask. The OL muludawang (兽法/咒鳞) is a separate general and
// is not touched here.
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "skill-dialog-info.h"
#include "card-lifetime-manager.h"
#include "game-rng.h"

#include <QSet>

static const QString kGu08aQi = QStringLiteral("&gu_heqi_qi");

static QString gu08aId(int id)
{
	return QString::number(id);
}

static QList<int> gu08aCardIds(const Card *card)
{
	QList<int> ids;
	if (!card) return ids;
	if (card->isVirtualCard()) ids = card->getSubcards();
	else if (card->getEffectiveId() >= 0) ids << card->getEffectiveId();
	return ids;
}

static int gu08aRandomDamageCardInDrawPile(Room *room)
{
	QList<int> pool;
	foreach (int id, room->getDrawPile()) {
		const Card *card = Sanguosha->getCard(id);
		if (card && card->isDamageCard()) pool << id;
	}
	return pool.isEmpty() ? -1 : pool.at(qsanRandomBounded(pool.size()));
}

// ---------------- 威马腾 骑 ----------------
// 「移动任意一骑」: one 骑 from any character holding one to any other character.

static bool gu08aMoveQi(Room *room, ServerPlayer *player, const QString &skill)
{
	QList<ServerPlayer *> holders;
	foreach (ServerPlayer *p, room->getAlivePlayers())
		if (p->getMark(kGu08aQi) > 0) holders << p;
	if (holders.isEmpty() || room->getAlivePlayers().length() < 2) return false;
	ServerPlayer *from = room->askForPlayerChosen(player, holders, skill + "_from", "@gu_qi-from", false, false);
	if (!from) return false;
	QList<ServerPlayer *> tos = room->getOtherPlayers(from);
	if (tos.isEmpty()) return false;
	ServerPlayer *to = room->askForPlayerChosen(player, tos, skill + "_to", "@gu_qi-to:" + from->objectName(), false, false);
	if (!to) return false;
	room->doAnimate(1, from->objectName(), to->objectName());
	from->loseMark(kGu08aQi, 1);
	to->gainMark(kGu08aQi, 1);
	return true;
}

// ---------------- 威马腾 合骑 ----------------
// A 【杀】 cannot target its user, so the 合骑 slashes go to other characters with 骑.

class Gu08aHeqi : public TriggerSkill
{
public:
	Gu08aHeqi() : TriggerSkill("gu_heqi")
	{
		events << GameStart << CardFinished << ConfirmDamage;
	}

	int getPriority(TriggerEvent event) const override
	{
		return event == ConfirmDamage ? 5 : TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == GameStart) {
			if (!player->isAlive()) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->gainMark(kGu08aQi, 3);
			return false;
		}
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || !damage.card || damage.card->getSkillName() != objectName()) return false;
			damage.from = nullptr;
			data = QVariant::fromValue(damage);
			return false;
		}
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !player->isAlive() || !use.card || !use.card->isKindOf("Slash")) return false;
		if (use.card->getSkillName() == objectName()) return false;
		QList<ServerPlayer *> holders;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (p->getMark(kGu08aQi) > 0) holders << p;
		if (holders.isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, this);
		foreach (ServerPlayer *target, holders) {
			const int x = target->getMark(kGu08aQi);
			for (int i = 0; i < x; ++i) {
				if (!player->isAlive() || !target->isAlive()) break;
				Card *slash = Sanguosha->cloneCard("slash", Card::NoSuit, 0);
				if (!slash) break;
				slash->setSkillName(objectName());
				slash->deleteLater();
				if (room->isProhibited(player, target, slash)) break;
				room->useCard(CardUseStruct(slash, player, target), false);
			}
		}
		return false;
	}
};

class Gu08aHeqiMod : public TargetModSkill
{
public:
	Gu08aHeqiMod() : TargetModSkill("#gu_heqi-mod")
	{
		pattern = ".";
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *to) const override
	{
		if (from && to && from->hasSkill("gu_heqi") && to->getMark(kGu08aQi) > 0) return 1000;
		return 0;
	}
};

// ---------------- 威马腾 挥锐 ----------------

class Gu08aHuirui : public TriggerSkill
{
public:
	Gu08aHuirui() : TriggerSkill("gu_huirui")
	{
		events << EventPhaseStart << Damaged;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		if (event == EventPhaseStart && player->getPhase() != Player::Play) return false;
		if (event == Damaged && data.value<DamageStruct>().to != player) return false;

		QStringList choices;
		choices << "gain";
		bool anyQi = false;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (p->getMark(kGu08aQi) > 0) anyQi = true;
		if (anyQi && room->getAlivePlayers().length() > 1) choices << "move";
		Card *slash = Sanguosha->cloneCard("slash", Card::NoSuit, 0);
		if (!slash) return false;
		slash->setSkillName(objectName());
		slash->deleteLater();
		QList<ServerPlayer *> slashTargets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (player->canSlash(p, slash, true)) slashTargets << p;
		if (!slashTargets.isEmpty()) choices << "slash";

		room->sendCompulsoryTriggerLog(player, this);
		const QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "move") {
			gu08aMoveQi(room, player, objectName());
		} else if (choice == "slash") {
			ServerPlayer *to = room->askForPlayerChosen(player, slashTargets, objectName(), "@gu_huirui-slash", false, false);
			if (to && to->isAlive() && player->isAlive())
				room->useCard(CardUseStruct(slash, player, to), false);
		} else {
			player->gainMark(kGu08aQi, 2);
		}
		return false;
	}
};

// ---------------- 威马腾 骁贲 ----------------
// Combo: previous card a damage card, this card a non-basic card. The doc gives no damage
// source for 「受 1 伤」, so each lost 骑 is one sourceless 1-point damage.

class Gu08aXiaoben : public TriggerSkill
{
public:
	Gu08aXiaoben() : TriggerSkill("gu_xiaoben")
	{
		events << CardUsed;
		setProperty("ComboMoves", true);
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->hasFlag("ComboMoves")) return false;
		const Card::CardType type = use.card->getTypeId();
		if (type == Card::TypeSkill || type == Card::TypeBasic) return false;
		const CardTagOwner prev = player->getTag("ComboMovesCard").value<CardTagOwner>();
		if (!prev.card || !prev.card->isDamageCard()) return false;
		use.card->setFlags("ComboMoves");
		room->sendCompulsoryTriggerLog(player, this);
		gu08aMoveQi(room, player, objectName());

		QList<ServerPlayer *> losers;
		QList<int> counts;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			const int n = p->getMark(kGu08aQi);
			if (n <= 0) continue;
			p->loseMark(kGu08aQi, n);
			losers << p;
			counts << n;
		}
		for (int i = 0; i < losers.length(); ++i) {
			ServerPlayer *p = losers.at(i);
			for (int k = 0; k < counts.at(i) && p->isAlive(); ++k)
				room->damage(DamageStruct(objectName(), nullptr, p, 1));
		}
		return false;
	}
};

// ---------------- 王越 试锋 ----------------
// ① has no 「可」 and resolves automatically; ② says 「可」 and asks. 「他」 = another character.

class Gu08aShifeng : public TriggerSkill
{
public:
	Gu08aShifeng() : TriggerSkill("gu_shifeng")
	{
		events << CardFinished << TargetSpecifying;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.from || use.card->getTypeId() == Card::TypeSkill) return false;
		if (event == CardFinished) {
			if (!use.card->isDamageCard()) return false;
			foreach (ServerPlayer *owner, room->getAlivePlayers()) {
				if (!owner->hasSkill(this) || owner == use.from || !use.to.contains(owner)) continue;
				if (owner->getMark("gu_shifeng_1-Clear") > 0 || !use.from->isAlive()) continue;
				Card *duel = Sanguosha->cloneCard("duel", Card::NoSuit, 0);
				if (!duel) continue;
				duel->setSkillName(objectName());
				duel->deleteLater();
				if (room->isProhibited(owner, use.from, duel)) continue;
				room->setPlayerMark(owner, "gu_shifeng_1-Clear", 1);
				room->sendCompulsoryTriggerLog(owner, this);
				room->useCard(CardUseStruct(duel, owner, use.from), false);
			}
			return false;
		}
		if (!use.card->isKindOf("Duel")) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!owner->hasSkill(this) || owner == use.from || use.to.contains(owner)) continue;
			if (owner->getMark("gu_shifeng_2-Clear") > 0) continue;
			if (room->isProhibited(use.from, owner, use.card)) continue;
			if (!owner->askForSkillInvoke(this, data)) continue;
			room->setPlayerMark(owner, "gu_shifeng_2-Clear", 1);
			QList<int> ids;
			foreach (int id, gu08aCardIds(use.card))
				if (room->getCardPlace(id) == Player::PlaceTable) ids << id;
			if (!ids.isEmpty()) {
				DummyCard dummy(ids);
				room->obtainCard(owner, &dummy, objectName(), true);
			}
			use.to.clear();
			use.to << owner;
			data = QVariant::fromValue(use);
			break;
		}
		return false;
	}
};

// ---------------- 王越 剑道 ----------------
// 「摸一张伤害牌」 takes a random damage card from the draw pile. 「替换」 shuffles the chosen
// non-damage hand card into the draw pile and takes a random damage card from it.

class Gu08aJiandaoFilter : public FilterSkill
{
public:
	Gu08aJiandaoFilter() : FilterSkill("#gu_jiandao-filter") {}

	bool viewFilter(const Card *to_select) const override
	{
		if (!to_select || to_select->getEffectiveId() < 0) return false;
		if (Sanguosha->getCardPlace(to_select->getEffectiveId()) != Player::PlaceHand) return false;
		return to_select->isKindOf("TrickCard") && to_select->isDamageCard() && !to_select->isKindOf("Duel");
	}

	const Card *viewAs(const Card *originalCard) const override
	{
		Duel *duel = new Duel(originalCard->getSuit(), originalCard->getNumber());
		duel->setSkillName("gu_jiandao");
		return duel;
	}
};

class Gu08aJiandao : public TriggerSkill
{
public:
	Gu08aJiandao() : TriggerSkill("gu_jiandao")
	{
		events << PreCardUsed << CardFinished << Damage << ConfirmDamage << TargetSpecified;
		frequency = Compulsory;
		global = true;
	}

	static void drawDamageCard(Room *room, ServerPlayer *owner, const QString &skill)
	{
		const int id = gu08aRandomDamageCardInDrawPile(room);
		if (id < 0) return;
		room->setPlayerMark(owner, "gu_jiandao_free_" + gu08aId(id), 1);
		room->obtainCard(owner, Sanguosha->getCard(id),
			CardMoveReason(CardMoveReason::S_REASON_DRAW, owner->objectName(), skill, QString()), false);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardFinished) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || use.card->getTypeId() == Card::TypeSkill) return false;
			foreach (ServerPlayer *owner, room->getAlivePlayers()) {
				if (!owner->hasSkill(this)) continue;
				const bool usedDuel = use.from == owner && use.card->isKindOf("Duel");
				const bool targeted = use.to.contains(owner) && use.card->isDamageCard();
				if (!usedDuel && !targeted) continue;
				room->sendCompulsoryTriggerLog(owner, this);
				drawDamageCard(room, owner, objectName());
			}
			return false;
		}
		if (!player || !player->isAlive() || !player->hasSkill(this)) return false;
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
			bool free = false;
			foreach (int id, gu08aCardIds(use.card)) {
				if (player->getMark("gu_jiandao_free_" + gu08aId(id)) > 0) {
					room->setPlayerMark(player, "gu_jiandao_free_" + gu08aId(id), 0);
					free = true;
				}
			}
			if (free && use.m_addHistory) {
				use.m_addHistory = false;
				data = QVariant::fromValue(use);
			}
			return false;
		}
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			const int bonus = player->getMark("&gu_jiandao_bonus");
			if (damage.from != player || bonus <= 0 || !damage.card || !damage.card->isKindOf("Slash")) return false;
			room->sendCompulsoryTriggerLog(player, this);
			damage.damage += bonus;
			data = QVariant::fromValue(damage);
			return false;
		}
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || !use.card->isKindOf("Slash")) return false;
			if (player->getMark("&gu_jiandao_duel") <= 0) return false;
			room->setPlayerMark(player, "&gu_jiandao_duel", 0);
			room->sendCompulsoryTriggerLog(player, this);
			foreach (ServerPlayer *p, use.to) {
				if (!p->isAlive()) continue;
				if (room->askForCard(p, "BasicCard", "@gu_jiandao-basic:" + player->objectName(), data, objectName()))
					continue;
				if (!use.no_respond_list.contains(p->objectName())) use.no_respond_list << p->objectName();
			}
			data = QVariant::fromValue(use);
			return false;
		}
		// Damage
		const DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player) return false;
		if (damage.card && damage.card->isKindOf("Duel"))
			room->setPlayerMark(player, "&gu_jiandao_duel", 1);
		room->addPlayerMark(player, "gu_jiandao_count");
		if (player->getMark("gu_jiandao_count") % 2 != 0) return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->addPlayerMark(player, "&gu_jiandao_bonus");
		QList<int> disabled;
		bool any = false;
		foreach (const Card *card, player->getHandcards()) {
			if (card->isDamageCard()) disabled << card->getEffectiveId();
			else any = true;
		}
		if (!any || gu08aRandomDamageCardInDrawPile(room) < 0) return false;
		if (!player->askForSkillInvoke("gu_jiandao_swap", data, false)) return false;
		const int pick = room->askForCardChosen(player, player, "h", objectName(), true, Card::MethodNone, disabled, true);
		if (pick < 0 || room->getCardPlace(pick) != Player::PlaceHand) return false;
		const int gain = gu08aRandomDamageCardInDrawPile(room);
		if (gain < 0) return false;
		room->shuffleIntoDrawPile(player, QList<int>() << pick, objectName(), false);
		if (player->isAlive() && room->getCardPlace(gain) == Player::DrawPile)
			room->obtainCard(player, Sanguosha->getCard(gain),
				CardMoveReason(CardMoveReason::S_REASON_GOTCARD, player->objectName(), objectName(), QString()), false);
		return false;
	}
};

class Gu08aJiandaoMod : public TargetModSkill
{
public:
	Gu08aJiandaoMod() : TargetModSkill("#gu_jiandao-mod")
	{
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const override
	{
		if (!from || !card) return 0;
		foreach (int id, gu08aCardIds(card))
			if (from->getMark("gu_jiandao_free_" + gu08aId(id)) > 0) return 1000;
		return 0;
	}
};

// ---------------- 傅干 巧谏 ----------------
// Both sides pick unseen (you first, then the target) and the cards are thrown in one
// move. You choose which of the two effects the target carries out on its next turn.
// A delayed trick still counts as its first 锦囊 but gets no extra resolution.

class Gu08aQiaojianCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu08aQiaojianCard()
	{
		mute = true;
		setSkillName(QStringLiteral("gu_qiaojian"));
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		return targets.isEmpty() && to_select && Self && to_select != Self
			&& Self->getMark("gu_qiaojian_" + to_select->objectName() + "-PlayClear") == 0;
	}

	void onEffect(CardEffectStruct &effect) const override
	{
		ServerPlayer *source = effect.from;
		ServerPlayer *target = effect.to;
		if (!source || !target) return;
		Room *room = source->getRoom();
		room->setPlayerMark(source, "gu_qiaojian_" + target->objectName() + "-PlayClear", 1);

		QList<int> mine, theirs;
		if (source->isAlive()) {
			const Card *c = room->askForExchange(source, "gu_qiaojian", source->getCardCount(true), 0, true,
				"@gu_qiaojian-discard:" + target->objectName(), true);
			if (c) mine = c->getSubcards();
		}
		if (target->isAlive()) {
			const Card *c = room->askForExchange(target, "gu_qiaojian_target", target->getCardCount(true), 0, true,
				"@gu_qiaojian-discard:" + source->objectName(), true);
			if (c) theirs = c->getSubcards();
		}
		QList<CardsMoveStruct> moves;
		if (!mine.isEmpty())
			moves << CardsMoveStruct(mine, nullptr, Player::DiscardPile,
				CardMoveReason(CardMoveReason::S_REASON_THROW, source->objectName(), "gu_qiaojian", QString()));
		if (!theirs.isEmpty())
			moves << CardsMoveStruct(theirs, nullptr, Player::DiscardPile,
				CardMoveReason(CardMoveReason::S_REASON_THROW, target->objectName(), "gu_qiaojian", QString()));
		if (!moves.isEmpty()) room->moveCardsAtomic(moves, true);

		int mySum = 0, theirSum = 0;
		foreach (int id, mine) mySum += Sanguosha->getCard(id)->getNumber();
		foreach (int id, theirs) theirSum += Sanguosha->getCard(id)->getNumber();
		LogMessage log;
		log.type = "#Gu08aQiaojianSum";
		log.from = source;
		log.to << target;
		log.arg = QString::number(mySum);
		log.arg2 = QString::number(theirSum);
		room->sendLog(log);

		if (mySum > theirSum) {
			if (source->isAlive())
				room->recover(source, RecoverStruct("gu_qiaojian", source, 1));
			if (!source->isAlive() || !target->isAlive()) return;
			const QString choice = room->askForChoice(source, "gu_qiaojian", "extra+ban", QVariant::fromValue(target));
			if (choice == "ban") room->setPlayerMark(target, "&gu_qiaojian_ban", 1);
			else room->setPlayerMark(target, "&gu_qiaojian_extra", 1);
			return;
		}
		if (!source->isAlive()) return;
		QList<int> gain;
		foreach (int id, mine + theirs)
			if (room->getCardPlace(id) == Player::DiscardPile) gain << id;
		if (gain.isEmpty()) return;
		DummyCard dummy(gain);
		room->obtainCard(source, &dummy, "gu_qiaojian", true);
		QList<int> held;
		foreach (int id, gain)
			if (room->getCardOwner(id) == source && room->getCardPlace(id) == Player::PlaceHand) held << id;
		if (!held.isEmpty()) room->ignoreCards(source, held);
	}
};

class Gu08aQiaojianVS : public ZeroCardViewAsSkill
{
public:
	Gu08aQiaojianVS() : ZeroCardViewAsSkill("gu_qiaojian") {}

	bool isEnabledAtPlay(const Player *player) const override
	{
		if (!player) return false;
		foreach (const Player *p, player->getAliveSiblings())
			if (player->getMark("gu_qiaojian_" + p->objectName() + "-PlayClear") == 0) return true;
		return false;
	}

	const Card *viewAs() const override
	{
		Gu08aQiaojianCard *card = new Gu08aQiaojianCard;
		card->setSkillName(objectName());
		return card;
	}
};

class Gu08aQiaojian : public TriggerSkill
{
public:
	Gu08aQiaojian() : TriggerSkill("gu_qiaojian")
	{
		events << EventPhaseStart << CardUsed;
		view_as_skill = new Gu08aQiaojianVS;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			if (player->getMark("&gu_qiaojian_extra") > 0) {
				room->setPlayerMark(player, "&gu_qiaojian_extra", 0);
				room->setPlayerMark(player, "&gu_qiaojian_extra-Clear", 1);
			}
			if (player->getMark("&gu_qiaojian_ban") > 0) {
				room->setPlayerMark(player, "&gu_qiaojian_ban", 0);
				room->setPlayerMark(player, "&gu_qiaojian_ban-Clear", 1);
			}
			return false;
		}
		if (player->getMark("&gu_qiaojian_extra-Clear") <= 0) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card) return false;
		bool changed = false;
		if (use.card->isKindOf("Slash") && player->getMark("gu_qiaojian_slash_done-Clear") == 0) {
			room->setPlayerMark(player, "gu_qiaojian_slash_done-Clear", 1);
			use.extra_use += 1;
			changed = true;
		} else if (use.card->isKindOf("TrickCard") && player->getMark("gu_qiaojian_trick_done-Clear") == 0) {
			room->setPlayerMark(player, "gu_qiaojian_trick_done-Clear", 1);
			if (!use.card->isKindOf("DelayedTrick")) {
				use.extra_use += 1;
				changed = true;
			}
		}
		if (changed) {
			LogMessage log;
			log.type = "#Gu08aQiaojianExtra";
			log.from = player;
			log.card_str = use.card->toString();
			room->sendLog(log);
			data = QVariant::fromValue(use);
		}
		if (player->getMark("gu_qiaojian_slash_done-Clear") > 0 && player->getMark("gu_qiaojian_trick_done-Clear") > 0)
			room->setPlayerMark(player, "&gu_qiaojian_extra-Clear", 0);
		return false;
	}
};

class Gu08aQiaojianLimit : public CardLimitSkill
{
public:
	Gu08aQiaojianLimit() : CardLimitSkill("#gu_qiaojian-limit") {}

	QString limitList(const Player *) const override
	{
		return "use";
	}

	QString limitPattern(const Player *target, const Card *card) const override
	{
		if (target && card && target->getMark("&gu_qiaojian_ban-Clear") > 0 && card->isDamageCard())
			return card->toString();
		return QString();
	}
};

// ---------------- 傅干 析察 ----------------
// Each point of damage is one view. Each picked card stays marked until its holder uses it
// (as itself or as part of a converted card): that use is nullified and the owner gets the card.

class Gu08aXicha : public TriggerSkill
{
public:
	Gu08aXicha() : TriggerSkill("gu_xicha")
	{
		events << Damaged << CardUsed << CardFinished;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == Damaged) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.to != player || !player->hasSkill(this)) return false;
			for (int i = 0; i < damage.damage; ++i) {
				ServerPlayer *from = damage.from;
				if (!player->isAlive() || !from || !from->isAlive() || from->isKongcheng()) break;
				room->sendCompulsoryTriggerLog(player, this);
				room->doAnimate(1, player->objectName(), from->objectName());
				const int n = qMin(2, from->getHandcardNum());
				const QList<int> ids = room->askForCardsChosen(player, from, "h", objectName(), n, n, true,
					Card::MethodNone, QList<int>(), false);
				QStringList marks = from->getTag("gu_xicha").toStringList();
				foreach (int id, ids) {
					const QString entry = player->objectName() + ":" + gu08aId(id);
					if (!marks.contains(entry)) marks << entry;
				}
				from->setTag("gu_xicha", marks);
			}
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.from != player || use.card->getTypeId() == Card::TypeSkill) return false;
		const QString hitKey = "gu_xicha_hit_" + use.card->toString();
		if (event == CardFinished) {
			const QString ownerName = room->getTag(hitKey).toString();
			if (ownerName.isEmpty()) return false;
			room->removeTag(hitKey);
			ServerPlayer *owner = room->findPlayerByObjectName(ownerName);
			if (!owner || !owner->isAlive()) return false;
			QList<int> ids;
			foreach (int id, gu08aCardIds(use.card)) {
				const Player::Place place = room->getCardPlace(id);
				if (place == Player::PlaceTable || (place == Player::PlaceEquip && room->getCardOwner(id) == use.from))
					ids << id;
			}
			if (ids.isEmpty()) return false;
			DummyCard dummy(ids);
			room->obtainCard(owner, &dummy, objectName(), true);
			return false;
		}
		QStringList marks = player->getTag("gu_xicha").toStringList();
		if (marks.isEmpty()) return false;
		const QList<int> ids = gu08aCardIds(use.card);
		QString ownerName;
		foreach (const QString &entry, QStringList(marks)) {
			const int sep = entry.lastIndexOf(':');
			if (sep < 0 || !ids.contains(entry.mid(sep + 1).toInt())) continue;
			marks.removeAll(entry);
			if (ownerName.isEmpty()) ownerName = entry.left(sep);
		}
		if (ownerName.isEmpty()) return false;
		player->setTag("gu_xicha", marks);
		ServerPlayer *owner = room->findPlayerByObjectName(ownerName);
		LogMessage log;
		log.type = "#Gu08aXichaHit";
		log.from = player;
		if (owner) log.to << owner;
		log.card_str = use.card->toString();
		room->sendLog(log);
		if (owner) room->notifySkillInvoked(owner, objectName());
		if (!use.nullified_list.contains("_ALL_TARGETS")) use.nullified_list << "_ALL_TARGETS";
		data = QVariant::fromValue(use);
		room->setTag(hitKey, ownerName);
		return false;
	}
};

// ---------------- 木鹿大王 咒法 ----------------
// Wording per the official announcement (2026-08 坑⑤): 「伤害为雷电伤害」.
// The card list is every basic or non-delayed trick damage card.

static QStringList gu08aZhoufaPool()
{
	static QStringList cached;
	if (!cached.isEmpty()) return cached;
	QStringList names;
	QSet<QString> seen;
	QStringList raw = Sanguosha->getCardNames(QStringLiteral("BasicCard"));
	raw << Sanguosha->getCardNames(QStringLiteral("TrickCard"));
	foreach (const QString &name, raw) {
		if (name.startsWith(QLatin1Char('_')) || seen.contains(name)) continue;
		Card *card = Sanguosha->cloneCard(name);
		if (!card) continue;
		const bool ok = card->isDamageCard() && !card->isKindOf("DelayedTrick");
		delete card;
		if (!ok) continue;
		seen.insert(name);
		names << name;
	}
	if (!names.isEmpty()) cached = names;
	return names;
}

static QString gu08aZhoufaResponseName(const Player *player, const QString &pattern)
{
	foreach (const QString &name, gu08aZhoufaPool()) {
		Card *card = Sanguosha->cloneCard(name);
		if (!card) continue;
		card->setSkillName("gu_zhoufa");
		const bool ok = Sanguosha->matchExpPattern(pattern, player, card);
		delete card;
		if (ok) return name;
	}
	return QString();
}

class Gu08aZhoufaVS : public OneCardViewAsSkill
{
public:
	Gu08aZhoufaVS() : OneCardViewAsSkill("gu_zhoufa") {}

	bool viewFilter(const Card *to_select) const override
	{
		if (!to_select) return false;
		const Card::CardType type = to_select->getTypeId();
		return type != Card::TypeBasic && type != Card::TypeSkill;
	}

	const Card *viewAs(const Card *originalCard) const override
	{
		QString name;
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			name = gu08aZhoufaResponseName(Self, Sanguosha->getCurrentCardUsePattern());
		} else {
			const Card *picked = Self ? Self->getTag(QStringLiteral("gu_zhoufa")).value<const Card *>() : nullptr;
			if (picked) name = picked->objectName();
		}
		if (name.isEmpty()) return nullptr;
		Card *card = Sanguosha->cloneCard(name, originalCard->getSuit(), originalCard->getNumber());
		if (!card) return nullptr;
		card->addSubcard(originalCard);
		card->setSkillName(objectName());
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->getCardCount(true, false) > 0;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const override
	{
		if (Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		return !gu08aZhoufaResponseName(player, pattern).isEmpty();
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::juguan(QStringLiteral("gu_zhoufa"), gu08aZhoufaPool().join(QStringLiteral(",")));
	}
};

class Gu08aZhoufa : public TriggerSkill
{
public:
	Gu08aZhoufa() : TriggerSkill("gu_zhoufa")
	{
		events << ConfirmDamage;
		view_as_skill = new Gu08aZhoufaVS;
		global = true;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::juguan(QStringLiteral("gu_zhoufa"), gu08aZhoufaPool().join(QStringLiteral(",")));
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || damage.card->getSkillName() != objectName()) return false;
		if (damage.nature == DamageStruct::Thunder) return false;
		damage.nature = DamageStruct::Thunder;
		data = QVariant::fromValue(damage);
		return false;
	}
};

// ---------------- 木鹿大王 兽群 / 驭象 ----------------
// 「不超初始」 = the max Hp recorded at game start. The fire-damage clause of ② runs for the
// same period as 驭象 (until the start of your next turn).

class Gu08aShouqun : public TriggerSkill
{
public:
	Gu08aShouqun() : TriggerSkill("gu_shouqun")
	{
		events << GameStart << EventPhaseStart << Damaged;
		waked_skills = "gu_yuxiang";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		if (event == GameStart) {
			if (player->getMark("gu_shouqun_init") == 0)
				room->setPlayerMark(player, "gu_shouqun_init", player->getMaxHp());
			return false;
		}
		if (event == Damaged) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.to != player || damage.nature != DamageStruct::Fire) return false;
			if (player->getMark("&gu_shouqun_fire") <= 0) return false;
			room->sendCompulsoryTriggerLog(player, this);
			room->loseMaxHp(player, 1, objectName());
			return false;
		}
		if (player->getPhase() == Player::RoundStart) {
			if (player->getMark("&gu_shouqun_fire") <= 0) return false;
			room->setPlayerMark(player, "&gu_shouqun_fire", 0);
			if (player->getMark("gu_shouqun_granted") > 0) {
				room->setPlayerMark(player, "gu_shouqun_granted", 0);
				room->handleAcquireDetachSkills(player, "-gu_yuxiang");
			}
			return false;
		}
		if (player->getPhase() != Player::Draw) return false;
		const int x = player->getMaxHp();
		if (x <= 0) return false;
		room->sendCompulsoryTriggerLog(player, this);
		const QList<int> ids = room->getNCards(x, false);
		if (ids.isEmpty()) return true;
		room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable,
			CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString())), true);
		room->getThread()->delay();
		const QString choice = room->askForChoice(player, objectName(), "pick+all", data);
		QList<int> gain;
		foreach (int id, ids) {
			if (room->getCardPlace(id) != Player::PlaceTable) continue;
			const Card *card = Sanguosha->getCard(id);
			if (choice == "all" || card->isKindOf("Horse") || card->isKindOf("TrickCard")
				|| card->isKindOf("Slash") || card->isKindOf("Analeptic"))
				gain << id;
		}
		if (!gain.isEmpty()) {
			DummyCard dummy(gain);
			room->obtainCard(player, &dummy, objectName(), true);
		}
		QList<int> rest;
		foreach (int id, ids)
			if (room->getCardPlace(id) == Player::PlaceTable) rest << id;
		if (!rest.isEmpty()) {
			DummyCard dummy(rest);
			room->throwCard(&dummy, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
				player->objectName(), objectName(), QString()), nullptr);
		}
		if (!player->isAlive()) return true;
		if (choice == "all") {
			room->setPlayerMark(player, "&gu_shouqun_fire", 1);
			if (!player->hasSkill("gu_yuxiang")) {
				room->setPlayerMark(player, "gu_shouqun_granted", 1);
				room->handleAcquireDetachSkills(player, "gu_yuxiang");
			}
		} else {
			int init = player->getMark("gu_shouqun_init");
			if (init <= 0 && player->getGeneral()) init = player->getGeneral()->getMaxHp();
			if (player->getMaxHp() < init) room->gainMaxHp(player, 1, objectName());
		}
		return true;
	}
};

class Gu08aYuxiang : public TriggerSkill
{
public:
	Gu08aYuxiang() : TriggerSkill("gu_yuxiang")
	{
		events << TargetSpecified;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!player || use.from != player || !use.card || !use.card->isKindOf("Slash")) return false;
		bool any = false;
		foreach (ServerPlayer *p, use.to) {
			const int d = player->distanceTo(p);
			if (d < 0 || d > 2 || use.no_respond_list.contains(p->objectName())) continue;
			use.no_respond_list << p->objectName();
			any = true;
		}
		if (!any) return false;
		room->sendCompulsoryTriggerLog(player, this);
		data = QVariant::fromValue(use);
		return false;
	}
};

class Gu08aYuxiangDist : public DistanceSkill
{
public:
	Gu08aYuxiangDist() : DistanceSkill("#gu_yuxiang-dist") {}

	int getCorrect(const Player *from, const Player *) const override
	{
		return from && from->hasSkill("gu_yuxiang") ? -2 : 0;
	}
};

void guAudit2026M08a(Package *pkg)
{
	pkg->addMetaObject<Gu08aQiaojianCard>();

	pkg->addSkills(new Gu08aHeqiMod);
	pkg->addSkills(new Gu08aJiandaoMod);
	pkg->addSkills(new Gu08aQiaojianLimit);
	pkg->addSkills(new Gu08aYuxiang);
	pkg->addSkills(new Gu08aYuxiangDist);
	pkg->insertRelatedSkills("gu_heqi", "#gu_heqi-mod");
	pkg->insertRelatedSkills("gu_jiandao", "#gu_jiandao-mod");
	pkg->insertRelatedSkills("gu_qiaojian", "#gu_qiaojian-limit");
	pkg->insertRelatedSkills("gu_yuxiang", "#gu_yuxiang-dist");

	General *mateng = new General(pkg, "gu_weimateng", "qun", 4, true);
	mateng->addSkill(new Gu08aHeqi);
	mateng->addSkill(new Gu08aHuirui);
	mateng->addSkill(new Gu08aXiaoben);

	General *wangyue = new General(pkg, "gu_wangyue", "qun", 4, true);
	wangyue->addSkill(new Gu08aShifeng);
	wangyue->addSkill(new Gu08aJiandao);
	wangyue->addSkill(new Gu08aJiandaoFilter);
	pkg->insertRelatedSkills("gu_jiandao", "#gu_jiandao-filter");

	General *fugan = new General(pkg, "gu_fugan", "qun", 3, true);
	fugan->addSkill(new Gu08aQiaojian);
	fugan->addSkill(new Gu08aXicha);

	General *mulu = new General(pkg, "gu_tymuludawang", "qun", 5, true);
	mulu->addSkill(new Gu08aZhoufa);
	mulu->addSkill(new Gu08aShouqun);
}

#include "gu-audit2026-m08a.moc"
