// GU-audit2026 2026-08c: 势小乔 / 势贺齐 / 幻周瑜.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-08 (translations in lang/zh_CN/Package/Mobile.lua).
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "roomthread.h"

// Visible, non-attached skills of a character.
static QStringList gu08cSkillNames(const Player *player)
{
	QStringList names;
	if (!player) return names;
	foreach (const Skill *skill, player->getVisibleSkillList()) {
		if (!skill || skill->isAttachedLordSkill()) continue;
		if (!names.contains(skill->objectName())) names << skill->objectName();
	}
	return names;
}

// Cards entering the player's hand from anywhere but that same hand.
static QList<int> gu08cGainedIds(const CardsMoveOneTimeStruct &move, const Player *player)
{
	QList<int> ids;
	if (!player || move.to != player || move.to_place != Player::PlaceHand) return ids;
	for (int i = 0; i < move.card_ids.size(); ++i) {
		const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
		if (move.from == player && from == Player::PlaceHand) continue;
		ids << move.card_ids.at(i);
	}
	return ids;
}

// Cards leaving the player's hand or equip area.
static bool gu08cLoss(const CardsMoveOneTimeStruct &move, const Player *player)
{
	if (!player || move.from != player) return false;
	for (int i = 0; i < move.card_ids.size(); ++i) {
		const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
		if (from != Player::PlaceHand && from != Player::PlaceEquip) continue;
		if (move.to == player && move.to_place == from) continue;
		return true;
	}
	return false;
}

// ---------------- 势小乔 合韵 ----------------
// "与你有相同技能" compares visible skill names; the chosen character is another one.
// The skill you lose is any one of your visible skills, chosen by you.

class Gu08cHeyunCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu08cHeyunCard()
	{
		setSkillName(QStringLiteral("gu_heyun"));
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		if (!targets.isEmpty() || !Self || !to_select || to_select == Self) return false;
		const QStringList mine = gu08cSkillNames(Self);
		foreach (const QString &name, gu08cSkillNames(to_select))
			if (mine.contains(name)) return true;
		return false;
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		if (!room || !source || !source->isAlive() || targets.isEmpty()) return;
		ServerPlayer *target = targets.first();
		const QStringList mine = gu08cSkillNames(source);
		if (mine.isEmpty()) return;
		const QString lose = room->askForChoice(source, QStringLiteral("gu_heyun"), mine.join(QLatin1Char('+')),
			QVariant::fromValue(target));
		if (!lose.isEmpty() && source->hasSkill(lose))
			room->handleAcquireDetachSkills(source, QStringLiteral("-") + lose);
		if (target && target->isAlive()) target->drawCards(2, QStringLiteral("gu_heyun"));
	}
};

class Gu08cHeyun : public ZeroCardViewAsSkill
{
public:
	Gu08cHeyun() : ZeroCardViewAsSkill("gu_heyun") {}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->usedTimes(QStringLiteral("Gu08cHeyunCard")) < 2;
	}

	const Card *viewAs() const override
	{
		Gu08cHeyunCard *card = new Gu08cHeyunCard;
		card->setSkillName(objectName());
		return card;
	}
};

// ---------------- 势小乔 音洄 ----------------
// The whole effect is optional. Only a skill still held is lost; the new skill is
// any visible skill of another character that you do not already have.

static QStringList gu08cYinhuiPool(const ServerPlayer *self, const ServerPlayer *other)
{
	QStringList pool;
	foreach (const QString &name, gu08cSkillNames(other))
		if (!self->hasSkill(name, true)) pool << name;
	return pool;
}

class Gu08cYinhui : public TriggerSkill
{
public:
	Gu08cYinhui() : TriggerSkill("gu_yinhui")
	{
		events << RoundStart;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive()) return false;
		const QString old = player->getTag(QStringLiteral("gu_yinhui_skill")).toString();
		const bool hasOld = !old.isEmpty() && player->hasSkill(old, true);
		QList<ServerPlayer *> candidates;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (!gu08cYinhuiPool(player, p).isEmpty()) candidates << p;
		if (!hasOld && candidates.isEmpty()) return false;
		if (!player->askForSkillInvoke(this)) return false;
		player->removeTag(QStringLiteral("gu_yinhui_skill"));
		if (hasOld) room->handleAcquireDetachSkills(player, QStringLiteral("-") + old);
		if (!player->isAlive()) return false;
		candidates.clear();
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (!gu08cYinhuiPool(player, p).isEmpty()) candidates << p;
		if (candidates.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, candidates, objectName(), "@gu_yinhui", false, true);
		if (!target) return false;
		const QStringList pool = gu08cYinhuiPool(player, target);
		if (pool.isEmpty()) return false;
		const QString gain = room->askForChoice(player, objectName(), pool.join(QLatin1Char('+')),
			QVariant::fromValue(target));
		if (gain.isEmpty() || !pool.contains(gain)) return false;
		room->handleAcquireDetachSkills(player, gain);
		player->setTag(QStringLiteral("gu_yinhui_skill"), gain);
		return false;
	}
};

// ---------------- 势贺齐 应机牌 ----------------
// An 应机 card is a hand card carrying the card tip "gu_yingji". The engine drops
// every card tip when the card moves, so the status only lives while the card stays
// in that hand.

static bool gu08cIsYingji(const Card *card)
{
	return card && card->hasFlag(QStringLiteral("cardTip:gu_yingji"));
}

static void gu08cMarkYingji(Room *room, ServerPlayer *owner, const QList<int> &ids)
{
	if (!room || !owner) return;
	foreach (int id, ids)
		if (owner->handCards().contains(id)) room->setCardTip(id, QStringLiteral("gu_yingji"));
}

// ---------------- 势贺齐 闪袭 ----------------

class Gu08cShanxi : public OneCardViewAsSkill
{
public:
	Gu08cShanxi() : OneCardViewAsSkill("gu_shanxi") {}

	bool viewFilter(const Card *to_select) const override
	{
		return Self && to_select && Self->handCards().contains(to_select->getEffectiveId()) && gu08cIsYingji(to_select);
	}

	const Card *viewAs(const Card *originalCard) const override
	{
		Card *duel = Sanguosha->cloneCard(QStringLiteral("duel"), originalCard->getSuit(), originalCard->getNumber());
		if (!duel) return nullptr;
		duel->addSubcard(originalCard);
		duel->setSkillName(objectName());
		return duel;
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		if (!player) return false;
		foreach (const Card *card, player->getHandcards())
			if (gu08cIsYingji(card)) return true;
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &) const override
	{
		return false;
	}
};

static QString gu08cShanxiMark(const Player *from)
{
	return QStringLiteral("gu_shanxi_%1-PlayClear").arg(from ? from->objectName() : QString());
}

// "出牌阶段每名角色限一次" = each target once per play phase.
class Gu08cShanxiProhibit : public ProhibitSkill
{
public:
	Gu08cShanxiProhibit() : ProhibitSkill("#gu_shanxi-prohibit") {}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		return from && to && card && card->getSkillName() == QLatin1String("gu_shanxi")
			&& to->getMark(gu08cShanxiMark(from)) > 0;
	}
};

class Gu08cShanxiRecord : public TriggerSkill
{
public:
	Gu08cShanxiRecord() : TriggerSkill("#gu_shanxi-record")
	{
		events << CardUsed;
		global = true;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!player || use.from != player || !use.card || use.card->getSkillName() != QLatin1String("gu_shanxi"))
			return false;
		foreach (ServerPlayer *to, use.to)
			if (to) room->addPlayerMark(to, gu08cShanxiMark(player));
		return false;
	}
};

// ---------------- 势贺齐 绮胄 ----------------
// 场上 = equip area and judge area. A marked 【杀】 is remembered privately; the
// next marking by this skill then draws two 应机 cards. The draw-phase bonus is
// two more cards in the same draw; the last two drawn are the 应机 ones.

class Gu08cQizhou : public TriggerSkill
{
public:
	Gu08cQizhou() : TriggerSkill("gu_qizhou")
	{
		events << CardsMoveOneTime << DrawNCards << AfterDrawNCards;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.who != player || draw.reason != QLatin1String("draw_phase")) return false;
			room->sendCompulsoryTriggerLog(player, this);
			draw.num += 2;
			data = QVariant::fromValue(draw);
			room->setPlayerFlag(player, "gu_qizhou_draw");
			return false;
		}
		if (event == AfterDrawNCards) {
			const DrawStruct draw = data.value<DrawStruct>();
			if (draw.who != player || draw.reason != QLatin1String("draw_phase")
				|| !player->hasFlag("gu_qizhou_draw")) return false;
			room->setPlayerFlag(player, "-gu_qizhou_draw");
			gu08cMarkYingji(room, player, draw.card_ids.mid(qMax(0, draw.card_ids.size() - 2)));
			return false;
		}
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to != player || move.card_ids.isEmpty()) return false;
		if (move.to_place != Player::PlaceEquip && move.to_place != Player::PlaceDelayedTrick) return false;
		QList<ServerPlayer *> candidates;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			foreach (const Card *card, p->getHandcards()) {
				if (!gu08cIsYingji(card)) {
					candidates << p;
					break;
				}
			}
		}
		if (candidates.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, candidates, objectName(), "@gu_qizhou", true, true);
		if (!target || !target->isAlive()) return false;
		QList<int> disabled;
		foreach (const Card *card, target->getHandcards())
			if (gu08cIsYingji(card)) disabled << card->getEffectiveId();
		const int id = room->askForCardChosen(player, target, "h", objectName(), target == player,
			Card::MethodNone, disabled);
		if (id < 0 || !target->handCards().contains(id)) return false;
		room->setCardTip(id, QStringLiteral("gu_yingji"));
		const Card *marked = Sanguosha->getCard(id);
		const bool slash = marked && marked->isKindOf("Slash");
		if (player->getMark("gu_qizhou_slash") > 0) {
			room->setPlayerMark(player, "gu_qizhou_slash", 0);
			if (player->isAlive()) gu08cMarkYingji(room, player, player->drawCardsList(2, objectName()));
		}
		if (slash && player->isAlive()) room->setPlayerMark(player, "gu_qizhou_slash", 1);
		return false;
	}
};

// 应机牌 rule: at the end of any character's turn, every 应机 card in any hand is
// put into the discard pile.
class Gu08cYingjiClear : public TriggerSkill
{
public:
	Gu08cYingjiClear() : TriggerSkill("#gu_qizhou-clear")
	{
		events << EventPhaseChanging;
		global = true;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		QList<CardsMoveStruct> moves;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			QList<int> ids;
			foreach (const Card *card, p->getHandcards())
				if (gu08cIsYingji(card)) ids << card->getEffectiveId();
			if (ids.isEmpty()) continue;
			moves << CardsMoveStruct(ids, p, nullptr, Player::PlaceHand, Player::DiscardPile,
				CardMoveReason(CardMoveReason::S_REASON_PUT, p->objectName(), QStringLiteral("gu_yingji"), QString()));
		}
		if (moves.isEmpty()) return false;
		LogMessage log;
		log.type = "#GuYingjiClear";
		log.from = player;
		room->sendLog(log);
		room->moveCardsAtomic(moves, true);
		return false;
	}
};

// ---------------- 幻周瑜 督鍪 ----------------
// "每阶段首次" is keyed by the current character, its turn count and phase; declining
// still spends that first time. Gaining = cards entering your hand; losing = cards
// leaving your hand or equip area. Option 2: you pick the three cards (hand and
// equip, all if three or fewer), that character recasts them. Cards got "因此" =
// the shown card for option 1, the recast draws for option 2.

static QString gu08cPhaseKey(Room *room)
{
	ServerPlayer *current = room ? room->getCurrent() : nullptr;
	if (!current) return QStringLiteral("none");
	return QStringLiteral("%1:%2:%3").arg(current->objectName())
		.arg(current->getMark("Global_TurnCount")).arg(int(current->getPhase()));
}

static QString gu08cDumouMark(const ServerPlayer *owner)
{
	return QStringLiteral("gu_dumou_got_%1-Clear").arg(owner ? owner->objectName() : QString());
}

class Gu08cDumou : public TriggerSkill
{
public:
	Gu08cDumou() : TriggerSkill("gu_dumou")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (gu08cGainedIds(move, player).isEmpty() && !gu08cLoss(move, player)) return false;
		const QString key = gu08cPhaseKey(room);
		if (player->getTag(QStringLiteral("gu_dumou_key")).toString() == key) return false;
		player->setTag(QStringLiteral("gu_dumou_key"), key);
		if (!player->askForSkillInvoke(this, data)) return false;

		QList<ServerPlayer *> withCards;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (p->getCardCount(true) > 0) withCards << p;
		QStringList choices;
		choices << QStringLiteral("obtain");
		if (!withCards.isEmpty()) choices << QStringLiteral("recast");
		const QString choice = room->askForChoice(player, objectName(), choices.join(QLatin1Char('+')));
		if (choice == QLatin1String("recast")) {
			ServerPlayer *target = room->askForPlayerChosen(player, withCards, "gu_dumou_recast",
				"@gu_dumou-recast", false, false);
			if (!target || !target->isAlive()) return false;
			QList<int> ids;
			const QList<const Card *> cards = target->getCards("he");
			if (cards.size() <= 3) {
				foreach (const Card *card, cards) ids << card->getEffectiveId();
			} else {
				ids = room->askForCardsChosen(player, target, "he", objectName(), 3, 3, target == player,
					Card::MethodNone, QList<int>(), false);
			}
			if (ids.isEmpty()) return false;
			room->recastCards(target, ids, objectName());
			if (target->isAlive()) room->addPlayerMark(target, gu08cDumouMark(player), ids.size());
			return false;
		}
		const QList<int> shown = room->showDrawPile(player, 1, objectName(), false);
		if (shown.isEmpty() || !player->isAlive()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), "gu_dumou_obtain",
			"@gu_dumou-obtain", false, false);
		if (!target || !target->isAlive()) return false;
		room->obtainCard(target, shown.first(), objectName());
		if (target->isAlive() && target->handCards().contains(shown.first()))
			room->addPlayerMark(target, gu08cDumouMark(player));
		return false;
	}
};

class Gu08cDumouEnd : public TriggerSkill
{
public:
	Gu08cDumouEnd() : TriggerSkill("#gu_dumou-end")
	{
		events << EventPhaseChanging;
		global = true;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!owner->isAlive() || !owner->hasSkill("gu_dumou")) continue;
			const QString mark = gu08cDumouMark(owner);
			int best = 0;
			ServerPlayer *top = nullptr;
			bool unique = false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				const int n = p->getMark(mark);
				if (n <= 0) continue;
				if (n > best) {
					best = n;
					top = p;
					unique = true;
				} else if (n == best) {
					unique = false;
				}
			}
			if (!top || !unique || !top->isAlive()) continue;
			room->sendCompulsoryTriggerLog(owner, "gu_dumou");
			room->doAnimate(1, owner->objectName(), top->objectName());
			room->loseHp(top, 1, true, owner, "gu_dumou");
		}
		return false;
	}
};

// ---------------- 幻周瑜 撼天 ----------------
// "获得牌" counts every move of cards into a hand from outside it (draws included);
// one move = one time. If the target holds cards it got this turn, it must show one
// of them; the rest follows 【火攻】. Fire damage you cause is +X.

static QList<int> gu08cHantianHeld(ServerPlayer *target)
{
	QList<int> held;
	if (!target) return held;
	foreach (const QVariant &v, target->getTag(QStringLiteral("gu_hantian_ids")).toList()) {
		const int id = v.toInt();
		if (target->handCards().contains(id) && !held.contains(id)) held << id;
	}
	return held;
}

static void gu08cHantianResolve(Room *room, const CardEffectStruct &effect)
{
	ServerPlayer *from = effect.from;
	ServerPlayer *target = effect.to;
	if (!target || !target->isAlive() || target->isKongcheng()) return;
	const QList<int> held = gu08cHantianHeld(target);
	int id = -1;
	if (!held.isEmpty()) {
		QStringList names;
		foreach (int h, held) names << QString::number(h);
		const QList<int> pick = room->askForExchangeCards(target, QStringLiteral("gu_hantian"), 1, 1,
			QStringLiteral("@gu_hantian-show:%1").arg(from ? from->objectName() : QString()), QString(),
			names.join(QLatin1Char(',')) + QStringLiteral("|.|.|hand"));
		id = (!pick.isEmpty() && held.contains(pick.first())) ? pick.first() : held.first();
	} else {
		const Card *card = room->askForCardShow(target, from ? from : target, QStringLiteral("fire_attack"));
		if (!card) return;
		id = card->getEffectiveId();
	}
	room->showCard(target, id);
	const Card *card = Sanguosha->getCard(id);
	if (!card || !from || !from->isAlive()) return;
	const QString suit_str = card->getSuitString();
	QString suit_str_png = suit_str;
	if (card->hasSuit()) suit_str_png = "<img src='image/system/cardsuit/" + suit_str + ".png' height=17/>";
	const QString prompt = QString("@fire-attack:%1::%2").arg(target->objectName()).arg(suit_str_png);
	const QString pattern = QString(".%1").arg(suit_str.at(0).toUpper());
	if (room->askForCard(from, pattern, prompt, QVariant::fromValue(effect)))
		room->damage(DamageStruct(effect.card, from, target, 1, DamageStruct::Fire));
	else
		from->setFlags("FireAttackFailed_" + target->objectName());
}

class Gu08cHantianVS : public ZeroCardViewAsSkill
{
public:
	Gu08cHantianVS() : ZeroCardViewAsSkill("gu_hantian") {}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->getMark("gu_hantian-PlayClear") < 1;
	}

	const Card *viewAs() const override
	{
		Card *card = Sanguosha->cloneCard(QStringLiteral("fire_attack"));
		if (card) card->setSkillName(objectName());
		return card;
	}
};

class Gu08cHantian : public TriggerSkill
{
public:
	Gu08cHantian() : TriggerSkill("gu_hantian")
	{
		events << CardUsed << ConfirmDamage;
		view_as_skill = new Gu08cHantianVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == CardUsed) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (use.from == player && use.card && use.card->isKindOf("FireAttack")
				&& use.card->getSkillName() == QLatin1String("gu_hantian"))
				room->addPlayerMark(player, "gu_hantian-PlayClear");
			return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player || damage.nature != DamageStruct::Fire || !damage.to || damage.damage < 1)
			return false;
		const int x = damage.to->getMark("gu_hantian_gain-Clear");
		if (x <= 0) return false;
		room->sendCompulsoryTriggerLog(player, this);
		damage.damage += x;
		data = QVariant::fromValue(damage);
		return false;
	}
};

class Gu08cHantianTrack : public TriggerSkill
{
public:
	Gu08cHantianTrack() : TriggerSkill("#gu_hantian-track")
	{
		events << CardsMoveOneTime << EventPhaseChanging << CardOnEffect << EventSkillEffectTarget << PostCardEffected;
		global = true;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventSkillEffectTarget) {
			// Skip the stock 【火攻】 effect for a target already resolved by 撼天.
			if (!player || !player->hasFlag("gu_hantian_skip")) return false;
			room->setPlayerFlag(player, "-gu_hantian_skip");
			return true;
		}
		if (event == PostCardEffected) {
			if (player && player->hasFlag("gu_hantian_skip")) room->setPlayerFlag(player, "-gu_hantian_skip");
			return false;
		}
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers(true))
				p->removeTag(QStringLiteral("gu_hantian_ids"));
			return false;
		}
		if (!player || !room->findPlayerBySkillName("gu_hantian")) return false;
		if (event == CardsMoveOneTime) {
			const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			const QList<int> ids = gu08cGainedIds(move, player);
			if (ids.isEmpty()) return false;
			room->addPlayerMark(player, "gu_hantian_gain-Clear");
			QVariantList list = player->getTag(QStringLiteral("gu_hantian_ids")).toList();
			foreach (int id, ids) list << id;
			player->setTag(QStringLiteral("gu_hantian_ids"), list);
			return false;
		}
		// CardOnEffect runs after nullification. The stock effect is then skipped through
		// EventSkillEffectTarget, which the game rule raises for unaccepted virtual
		// view-as cards only, so the custom path is limited to exactly those cards.
		const CardEffectStruct effect = data.value<CardEffectStruct>();
		if (!effect.card || !effect.card->isKindOf("FireAttack")
			|| effect.card->getSkillName() != QLatin1String("gu_hantian")) return false;
		if (effect.isAcceptedSkillEffectCard() || !effect.card->isVirtualCard()) return false;
		if (!effect.to || !effect.to->isAlive() || effect.to->isKongcheng()) return false;
		if (gu08cHantianHeld(effect.to).isEmpty()) return false;
		for (int i = 0; i <= effect.extra_effect; ++i) {
			if (!effect.to->isAlive()) break;
			gu08cHantianResolve(room, effect);
		}
		// Set after resolving so nested effects cannot consume the skip early.
		room->setPlayerFlag(effect.to, "gu_hantian_skip");
		return false;
	}
};

void guAudit2026M08c(Package *pkg)
{
	pkg->addMetaObject<Gu08cHeyunCard>();

	General *xiaoqiao = new General(pkg, "gu_shixiaoqiao", "wu", 3, false);
	xiaoqiao->addSkill(new Gu08cHeyun);
	xiaoqiao->addSkill(new Gu08cYinhui);

	pkg->addSkills(new Gu08cShanxiProhibit);
	pkg->addSkills(new Gu08cShanxiRecord);
	pkg->insertRelatedSkills("gu_shanxi", "#gu_shanxi-prohibit");
	pkg->insertRelatedSkills("gu_shanxi", "#gu_shanxi-record");
	pkg->addSkills(new Gu08cYingjiClear);
	pkg->insertRelatedSkills("gu_qizhou", "#gu_qizhou-clear");

	General *heqi = new General(pkg, "gu_shiheqi", "wu", 4, true);
	heqi->addSkill(new Gu08cShanxi);
	heqi->addSkill(new Gu08cQizhou);

	pkg->addSkills(new Gu08cDumouEnd);
	pkg->insertRelatedSkills("gu_dumou", "#gu_dumou-end");
	pkg->addSkills(new Gu08cHantianTrack);
	pkg->insertRelatedSkills("gu_hantian", "#gu_hantian-track");

	General *zhouyu = new General(pkg, "gu_huanzhouyu", "wu", 4, true);
	zhouyu->addSkill(new Gu08cDumou);
	zhouyu->addSkill(new Gu08cHantian);
}

#include "gu-audit2026-m08c.moc"
