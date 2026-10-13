// GU-audit2026 2026-07a: 星夏侯霸 / 夏侯恩 / 谋张任.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-07 (translations in lang/zh_CN/Package/Mobile.lua).
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "standard.h"
#include "maneuvering.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "roomthread.h"

#include <QMap>
#include <QRandomGenerator>

static bool gu07aIsHand(int id)
{
	return id >= 0 && Sanguosha->getCardPlace(id) == Player::PlaceHand;
}

static int gu07aPickRandom(const QList<int> &ids)
{
	if (ids.isEmpty()) return -1;
	return ids.at(QRandomGenerator::global()->bounded(ids.length()));
}

// ---------------- 星夏侯霸 维谷 ----------------
// "可指定自己的牌" = a card whose own target rules allow choosing its holder:
// non-fixed cards ask their targetFilter; fixed-target cards that land on the user or on
// everyone (peach, wine, equips, lightning, global effects) count, AOE and pure responses do not.
// The user side reacts at TargetSpecifying, the target side at TargetConfirming.
// "攻击范围内所有角色" is read off the skill owner's attack range; the card user and
// characters it cannot legally target are skipped.

static bool gu07aCanTargetSelf(const Player *player, const Card *card)
{
	if (!player || !card || card->getTypeId() == Card::TypeSkill) return false;
	if (card->isKindOf("AOE") || card->isKindOf("Jink") || card->isKindOf("Nullification")) return false;
	if (player->isProhibited(player, card)) return false;
	if (card->targetFixed()) return true;
	return card->targetFilter(QList<const Player *>(), player, player);
}

static QList<ServerPlayer *> gu07aWeiguExtra(Room *room, ServerPlayer *owner, const CardUseStruct &use)
{
	QList<ServerPlayer *> extra;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p == use.from || use.to.contains(p) || !owner->inMyAttackRange(p)) continue;
		if (use.from && room->isProhibited(use.from, p, use.card)) continue;
		extra << p;
	}
	return extra;
}

class Gu07aWeigu : public TriggerSkill
{
public:
	Gu07aWeigu() : TriggerSkill("gu_weigu")
	{
		events << TargetSpecifying << TargetConfirming;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.card->isDamageCard()) return false;
		if (use.to.length() != 1) return false;
		if (event == TargetSpecifying) {
			if (use.from != player) return false;
		} else {
			if (use.to.first() != player || use.from == player) return false;
		}
		const QString flag = "gu_weigu_" + player->objectName();
		if (use.card->hasFlag(flag)) return false;

		QStringList ids;
		foreach (const Card *card, player->getCards("he")) {
			if (!card || !player->canDiscard(player, card->getEffectiveId())) continue;
			if (gu07aCanTargetSelf(player, card)) ids << QString::number(card->getEffectiveId());
		}
		if (ids.isEmpty()) return false;

		const bool juefa = player->getMark("&gu_juefa") > 0;
		QStringList choices;
		if (juefa) choices << "damage";
		else if (room->canMoveField("ej")) choices << "move";
		if (!gu07aWeiguExtra(room, player, use).isEmpty()) choices << "target";
		if (choices.isEmpty()) return false;

		if (!player->askForSkillInvoke(this, data)) return false;
		if (!room->askForDiscard(player, objectName(), 1, 1, false, true, "@gu_weigu-discard", ids.join(",")))
			return false;
		room->setCardFlag(use.card, flag);
		if (!player->isAlive()) return false;

		const QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "move") {
			room->moveField(player, objectName(), false, "ej");
		} else if (choice == "damage") {
			ServerPlayer *to = room->askForPlayerChosen(player, room->getAlivePlayers(), "gu_weigu_damage",
				"@gu_weigu-damage", false, true);
			if (to && to->isAlive())
				room->damage(DamageStruct(objectName(), player, to, 2));
		} else if (choice == "target") {
			use = data.value<CardUseStruct>();
			const QList<ServerPlayer *> extra = gu07aWeiguExtra(room, player, use);
			if (extra.isEmpty()) return false;
			foreach (ServerPlayer *p, extra) {
				use.to << p;
				room->doAnimate(1, player->objectName(), p->objectName());
			}
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
			LogMessage log;
			log.type = "#gu_weigu-target";
			log.from = player;
			log.to = extra;
			log.card_str = use.card->toString();
			room->sendLog(log);
		}
		return false;
	}
};

// After the marked card resolves without the card itself dealing damage: lose 1 Hp, draw 2.
// Also records 绝伐 kills: only deaths caused by 维谷's own 2-point damage count as "经维谷杀死".
class Gu07aWeiguFinish : public TriggerSkill
{
public:
	Gu07aWeiguFinish() : TriggerSkill("#gu_weigu-finish")
	{
		events << CardFinished << Death;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Death) {
			const DeathStruct death = data.value<DeathStruct>();
			if (!player || death.who != player || !death.damage) return false;
			ServerPlayer *killer = death.damage->from;
			if (killer && death.damage->reason == QLatin1String("gu_weigu") && killer->getMark("&gu_juefa") > 0)
				room->addPlayerMark(killer, "gu_juefa_kill");
			return false;
		}
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			const QString flag = "gu_weigu_" + p->objectName();
			if (!use.card->hasFlag(flag)) continue;
			room->setCardFlag(use.card, "-" + flag);
			if (!p->isAlive() || use.card->hasFlag("DamageDone")) continue;
			room->sendCompulsoryTriggerLog(p, "gu_weigu");
			room->loseHp(HpLostStruct(p, 1, "gu_weigu", p));
			if (p->isAlive()) p->drawCards(2, "gu_weigu");
		}
		return false;
	}
};

// ---------------- 星夏侯霸 绝伐 ----------------
// The doc gives no timing, so it is a play-phase activation. "下回合结束" = the end of the
// owner's next turn after the activating one (the counter starts at 2 and drops at each own
// turn end). 调整手牌体力至上限: hand to the max-hand-card count (draw or discard), then Hp to max Hp.

class Gu07aJuefaCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu07aJuefaCard()
	{
		target_fixed = true;
		mute = true;
		setSkillName(QStringLiteral("gu_juefa"));
	}

	bool targetFilter(const QList<const Player *> &, const Player *, const Player *) const override
	{
		return false;
	}

	bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override
	{
		return targets.isEmpty();
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const override
	{
		if (!room || !source) return;
		room->doSuperLightbox(source, "gu_juefa");
		room->removePlayerMark(source, "@gu_juefa");
		room->setPlayerMark(source, "gu_juefa_kill", 0);
		room->setPlayerMark(source, "&gu_juefa", 2);
	}
};

class Gu07aJuefa : public ZeroCardViewAsSkill
{
public:
	Gu07aJuefa() : ZeroCardViewAsSkill("gu_juefa")
	{
		frequency = Limited;
		limit_mark = "@gu_juefa";
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->getMark("@gu_juefa") > 0;
	}

	const Card *viewAs() const override
	{
		return new Gu07aJuefaCard;
	}
};

class Gu07aJuefaSettle : public TriggerSkill
{
public:
	Gu07aJuefaSettle() : TriggerSkill("#gu_juefa-settle")
	{
		events << EventPhaseChanging;
		global = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to != Player::NotActive) return false;
		const int left = player->getMark("&gu_juefa");
		if (left <= 0) return false;
		room->setPlayerMark(player, "&gu_juefa", left - 1);
		if (left - 1 > 0) return false;
		const bool killed = player->getMark("gu_juefa_kill") > 0;
		room->setPlayerMark(player, "gu_juefa_kill", 0);
		room->sendCompulsoryTriggerLog(player, "gu_juefa");
		if (killed) {
			const int limit = player->getMaxCards();
			const int hand = player->getHandcardNum();
			if (hand < limit)
				player->drawCards(limit - hand, "gu_juefa");
			else if (hand > limit)
				room->askForDiscard(player, "gu_juefa", hand - limit, hand - limit, false, false, "@gu_juefa-discard");
			if (player->isAlive() && player->getHp() < player->getMaxHp())
				room->recover(player, RecoverStruct("gu_juefa", player, player->getMaxHp() - player->getHp()));
		} else if (player->getHp() > 0) {
			room->loseHp(HpLostStruct(player, player->getHp(), "gu_juefa", player));
		}
		return false;
	}
};

// ---------------- 夏侯恩 持剑 ----------------
// Same view-as-equip mechanism as thdaojian (no weapon in an open weapon slot).

class Gu07aChijian : public ViewAsEquipSkill
{
public:
	Gu07aChijian() : ViewAsEquipSkill("gu_chijian")
	{
		frequency = Compulsory;
	}

	QString viewAsEquip(const Player *target) const override
	{
		if (target && target->hasSkill(objectName()) && target->hasEquipArea(0) && !target->getWeapon())
			return "qinggang_sword";
		return QString();
	}
};

class Gu07aChijianMod : public TargetModSkill
{
public:
	Gu07aChijianMod() : TargetModSkill("#gu_chijian-mod")
	{
		pattern = "Slash";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const override
	{
		if (from && card && card->isKindOf("Slash") && from->getPhase() == Player::Play && from->hasSkill("gu_chijian"))
			return 1;
		return 0;
	}
};

// ---------------- 夏侯恩 恃武 ----------------
// "至其回合结束" covers both halves of the swap: the other character holds 持剑 and you are
// without it until the end of that character's turn (the turn whose start fired 恃武).
// 杀死你的角色 keeps 持剑 permanently. The damage card is a random one from the draw pile.

static void gu07aShiwuRestore(Room *room, ServerPlayer *p)
{
	if (!room || !p) return;
	if (p->getTag("gu_shiwu_gave").toBool()) {
		p->removeTag("gu_shiwu_gave");
		if (p->isAlive() && p->hasSkill("gu_chijian", true))
			room->handleAcquireDetachSkills(p, "-gu_chijian");
	}
	const QStringList lent = p->getTag("gu_shiwu_lent").toStringList();
	p->removeTag("gu_shiwu_lent");
	foreach (const QString &name, lent) {
		ServerPlayer *owner = room->findPlayerByObjectName(name);
		if (owner && owner->isAlive() && !owner->hasSkill("gu_chijian", true))
			room->handleAcquireDetachSkills(owner, "gu_chijian");
	}
}

class Gu07aShiwu : public TriggerSkill
{
public:
	Gu07aShiwu() : TriggerSkill("gu_shiwu")
	{
		events << EventPhaseStart << DamageDone << EventPhaseChanging << Death;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == DamageDone) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.from && damage.card && damage.card->isKindOf("Duel")
				&& damage.card->getSkillName() == objectName())
				room->setPlayerFlag(damage.from, "gu_shiwu_dealt");
			return false;
		}
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to == Player::NotActive)
				gu07aShiwuRestore(room, player);
			return false;
		}
		if (event == Death) {
			const DeathStruct death = data.value<DeathStruct>();
			if (death.who != player) return false;
			gu07aShiwuRestore(room, player);
			if (!player->hasSkill(this, true) || !death.damage) return false;
			ServerPlayer *killer = death.damage->from;
			if (!killer || killer == player || !killer->isAlive()) return false;
			room->sendCompulsoryTriggerLog(player, objectName());
			if (killer->getTag("gu_shiwu_gave").toBool())
				killer->removeTag("gu_shiwu_gave");
			else if (!killer->hasSkill("gu_chijian", true))
				room->handleAcquireDetachSkills(killer, "gu_chijian");
			return false;
		}
		if (player->getPhase() != Player::RoundStart || !player->isAlive()) return false;
		foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
			if (!player->isAlive()) break;
			if (!owner->isAlive() || !owner->hasSkill(this)) continue;
			Duel *duel = new Duel(Card::NoSuit, 0);
			duel->setSkillName(objectName());
			duel->deleteLater();
			if (owner->isCardLimited(duel, Card::MethodUse) || room->isProhibited(owner, player, duel)) continue;
			if (!owner->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			room->setPlayerFlag(owner, "-gu_shiwu_dealt");
			room->setPlayerFlag(player, "-gu_shiwu_dealt");
			room->useCard(CardUseStruct(duel, owner, player), false);
			const bool ownerDealt = owner->hasFlag("gu_shiwu_dealt");
			const bool targetDealt = player->hasFlag("gu_shiwu_dealt");
			room->setPlayerFlag(owner, "-gu_shiwu_dealt");
			room->setPlayerFlag(player, "-gu_shiwu_dealt");
			if (ownerDealt && owner->isAlive()) {
				QList<int> pool;
				foreach (int id, room->getDrawPile()) {
					const Card *card = Sanguosha->getCard(id);
					if (card && card->isDamageCard()) pool << id;
				}
				const int id = gu07aPickRandom(pool);
				if (id >= 0) room->obtainCard(owner, id, objectName(), false);
			}
			if (targetDealt && player->isAlive()) {
				if (!player->hasSkill("gu_chijian", true)) {
					room->handleAcquireDetachSkills(player, "gu_chijian");
					player->setTag("gu_shiwu_gave", true);
				}
				if (owner->isAlive() && owner->hasSkill("gu_chijian", true)) {
					room->handleAcquireDetachSkills(owner, "-gu_chijian");
					QStringList lent = player->getTag("gu_shiwu_lent").toStringList();
					if (!lent.contains(owner->objectName())) lent << owner->objectName();
					player->setTag("gu_shiwu_lent", lent);
				}
			}
		}
		return false;
	}
};

// ---------------- 谋张任 慑道 ----------------
// Optional, during your own turn. "其他角色的牌": cards leaving another character's areas, or
// cards that character put on the table (use/response); judge cards are not theirs.
// Option 1: the gained red cards ignore the hand limit this turn; then discard one black card if able.
// Option 2: the gained 【杀】 are filtered into thunder 【杀】 until they leave your hand.

static ServerPlayer *gu07aMoveOwner(Room *room, const CardsMoveOneTimeStruct &move, int index)
{
	const Player::Place place = index < move.from_places.size() ? move.from_places.at(index) : Player::PlaceUnknown;
	const Player *from = nullptr;
	if (place == Player::PlaceHand || place == Player::PlaceEquip || place == Player::PlaceDelayedTrick
		|| place == Player::PlaceSpecial) {
		from = move.from;
	} else if (place == Player::PlaceTable) {
		from = move.from ? move.from : move.origin_from;
		if (!from && !move.reason.m_playerId.isEmpty())
			from = room->findPlayerByObjectName(move.reason.m_playerId, true);
	}
	return from ? room->findPlayerByObjectName(from->objectName(), true) : nullptr;
}

class Gu07aShedaoFilter : public FilterSkill
{
public:
	Gu07aShedaoFilter() : FilterSkill("#gu_shedao-filter") {}

	bool viewFilter(const Card *to_select) const override
	{
		const int id = to_select->getEffectiveId();
		if (!gu07aIsHand(id)) return false;
		const Card *real = Sanguosha->getCard(id);
		return real && real->hasFlag("gu_shedao_thunder") && to_select->isKindOf("Slash");
	}

	const Card *viewAs(const Card *originalCard) const override
	{
		ThunderSlash *slash = new ThunderSlash(originalCard->getSuit(), originalCard->getNumber());
		slash->setSkillName("gu_shedao");
		return slash;
	}
};

class Gu07aShedao : public TriggerSkill
{
public:
	Gu07aShedao() : TriggerSkill("gu_shedao")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from == player) {
			for (int i = 0; i < move.card_ids.size(); ++i) {
				if (i >= move.from_places.size() || move.from_places.at(i) != Player::PlaceHand) continue;
				const int id = move.card_ids.at(i);
				const Card *card = Sanguosha->getCard(id);
				if (card && card->hasFlag("gu_shedao_thunder"))
					room->setCardFlag(id, "-gu_shedao_thunder");
			}
		}
		if (move.to_place != Player::DiscardPile || room->getCurrent() != player
			|| player->getPhase() == Player::NotActive)
			return false;
		QList<int> reds, slashes;
		for (int i = 0; i < move.card_ids.size(); ++i) {
			const int id = move.card_ids.at(i);
			if (room->getCardPlace(id) != Player::DiscardPile) continue;
			ServerPlayer *owner = gu07aMoveOwner(room, move, i);
			if (!owner || owner == player) continue;
			const Card *card = Sanguosha->getCard(id);
			if (!card) continue;
			if (card->isRed()) reds << id;
			if (card->isKindOf("Slash")) slashes << id;
		}
		QStringList choices;
		if (!reds.isEmpty()) choices << "red";
		if (!slashes.isEmpty()) choices << "slash";
		if (choices.isEmpty()) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		const QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		const QList<int> picked = choice == QLatin1String("red") ? reds : slashes;
		QList<int> valid;
		foreach (int id, picked)
			if (room->getCardPlace(id) == Player::DiscardPile) valid << id;
		if (valid.isEmpty()) return false;
		DummyCard *dummy = new DummyCard(valid);
		room->obtainCard(player, dummy, objectName());
		dummy->deleteLater();
		if (!player->isAlive()) return false;
		QList<int> got;
		foreach (int id, valid)
			if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand) got << id;
		if (choice == QLatin1String("red")) {
			if (!got.isEmpty()) room->ignoreCards(player, got);
			bool hasBlack = false;
			foreach (const Card *card, player->getCards("he")) {
				if (card && card->isBlack() && player->canDiscard(player, card->getEffectiveId())) {
					hasBlack = true;
					break;
				}
			}
			if (hasBlack)
				room->askForDiscard(player, objectName(), 1, 1, false, true, "@gu_shedao-discard", ".|black");
		} else if (!got.isEmpty()) {
			foreach (int id, got) room->setCardFlag(id, "gu_shedao_thunder");
			room->filterCards(player, player->getHandcards(), true);
		}
		return false;
	}
};

// ---------------- 谋张任 迅矢 ----------------
// The damage clause reads with the thunder 【杀】 as subject: your thunder 【杀】 deals damage.

class Gu07aXunshiMod : public TargetModSkill
{
public:
	Gu07aXunshiMod() : TargetModSkill("#gu_xunshi-mod")
	{
		pattern = "Slash";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const override
	{
		if (from && card && card->isKindOf("ThunderSlash") && from->hasSkill("gu_xunshi")) return 1000;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const override
	{
		if (from && card && card->isKindOf("ThunderSlash") && from->hasSkill("gu_xunshi")) return 1000;
		return 0;
	}
};

class Gu07aXunshi : public TriggerSkill
{
public:
	Gu07aXunshi() : TriggerSkill("gu_xunshi")
	{
		events << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player || !damage.card || !damage.card->isKindOf("ThunderSlash")) return false;
		room->sendCompulsoryTriggerLog(player, this);
		ServerPlayer *victim = damage.to;
		if (victim && victim->isAlive()) {
			QList<int> pool;
			foreach (const Card *card, victim->getCards("he"))
				if (card && victim->canDiscard(victim, card->getEffectiveId())) pool << card->getEffectiveId();
			const int id = gu07aPickRandom(pool);
			if (id >= 0) room->throwCard(id, objectName(), victim);
		}
		if (player->isAlive()) player->drawCards(2, objectName());
		return false;
	}
};

// ---------------- 谋张任 震弓 ----------------
// Each chosen target takes at least 1; the remaining points are assigned one at a time.

class Gu07aZhengongCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu07aZhengongCard()
	{
		mute = true;
		setSkillName(QStringLiteral("gu_zhengong"));
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		return Self && to_select && to_select != Self && !targets.contains(to_select)
			&& targets.length() < subcardsLength();
	}

	bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override
	{
		return !targets.isEmpty() && targets.length() <= subcardsLength();
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		if (!room || !source) return;
		room->doSuperLightbox(source, "gu_zhengong");
		room->removePlayerMark(source, "@gu_zhengong");
		QList<ServerPlayer *> alive;
		foreach (ServerPlayer *p, targets)
			if (p && p->isAlive()) alive << p;
		if (alive.isEmpty()) return;
		QMap<ServerPlayer *, int> amount;
		foreach (ServerPlayer *p, alive) amount[p] = 1;
		int extra = subcardsLength() - alive.length();
		while (extra > 0 && source->isAlive()) {
			ServerPlayer *p = alive.length() == 1 ? alive.first()
				: room->askForPlayerChosen(source, alive, "gu_zhengong",
					"@gu_zhengong-assign:::" + QString::number(extra), false, false);
			if (!p) p = alive.first();
			amount[p] += 1;
			--extra;
		}
		room->sortByActionOrder(alive);
		foreach (ServerPlayer *p, alive) {
			if (!p->isAlive()) continue;
			ServerPlayer *from = source->isAlive() ? source : nullptr;
			room->damage(DamageStruct("gu_zhengong", from, p, amount.value(p, 1)));
		}
	}
};

class Gu07aZhengong : public ViewAsSkill
{
public:
	Gu07aZhengong() : ViewAsSkill("gu_zhengong")
	{
		frequency = Limited;
		limit_mark = "@gu_zhengong";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		if (!Self || !to_select || Self->isJilei(to_select)) return false;
		return selected.isEmpty() || to_select->sameNameWith(selected.first());
	}

	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (cards.isEmpty()) return nullptr;
		Gu07aZhengongCard *card = new Gu07aZhengongCard;
		card->addSubcards(cards);
		card->setSkillName(objectName());
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->getMark("@gu_zhengong") > 0 && !player->isNude();
	}
};

class Gu07aZhengongRecord : public TriggerSkill
{
public:
	Gu07aZhengongRecord() : TriggerSkill("#gu_zhengong-record")
	{
		events << DamageDone;
		global = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		const DamageStruct damage = data.value<DamageStruct>();
		if (damage.reason == QLatin1String("gu_zhengong") && damage.to)
			room->setPlayerMark(damage.to, "&gu_zhengong-Clear", 1);
		return false;
	}
};

class Gu07aZhengongInvalidity : public InvaliditySkill
{
public:
	Gu07aZhengongInvalidity() : InvaliditySkill("#gu_zhengong-invalidity") {}

	bool isSkillValid(const Player *player, const Skill *skill) const override
	{
		if (!player || player->getMark("&gu_zhengong-Clear") <= 0) return true;
		if (!skill || skill->inherits("InvaliditySkill")) return true;
		return skill->getFrequency(player) == Skill::Compulsory;
	}
};

void guAudit2026M07a(Package *pkg)
{
	pkg->addMetaObject<Gu07aJuefaCard>();
	pkg->addMetaObject<Gu07aZhengongCard>();

	pkg->addSkills(new Gu07aWeiguFinish);
	pkg->insertRelatedSkills("gu_weigu", "#gu_weigu-finish");
	pkg->addSkills(new Gu07aJuefaSettle);
	pkg->insertRelatedSkills("gu_juefa", "#gu_juefa-settle");
	pkg->addSkills(new Gu07aChijianMod);
	pkg->insertRelatedSkills("gu_chijian", "#gu_chijian-mod");
	pkg->addSkills(new Gu07aShedaoFilter);
	pkg->insertRelatedSkills("gu_shedao", "#gu_shedao-filter");
	pkg->addSkills(new Gu07aXunshiMod);
	pkg->insertRelatedSkills("gu_xunshi", "#gu_xunshi-mod");
	pkg->addSkills(new Gu07aZhengongRecord);
	pkg->insertRelatedSkills("gu_zhengong", "#gu_zhengong-record");
	pkg->addSkills(new Gu07aZhengongInvalidity);
	pkg->insertRelatedSkills("gu_zhengong", "#gu_zhengong-invalidity");

	General *xiahouba = new General(pkg, "gu_xingxiahouba", "shu", 4, true);
	xiahouba->addSkill(new Gu07aWeigu);
	xiahouba->addSkill(new Gu07aJuefa);

	General *xiahouen = new General(pkg, "gu_xiahouen", "wei", 5, true);
	xiahouen->addSkill(new Gu07aChijian);
	xiahouen->addSkill(new Gu07aShiwu);

	General *zhangren = new General(pkg, "gu_mouzhangren", "qun", 5, true);
	zhangren->addSkill(new Gu07aShedao);
	zhangren->addSkill(new Gu07aXunshi);
	zhangren->addSkill(new Gu07aZhengong);
}

#include "gu-audit2026-m07a.moc"
