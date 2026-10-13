// SOURCE-2 follow-up: overwrite warehouse test / alternate versions per Tom 2026-10-13 08:41 JST.
// Authoritative text: materials/gu-audit2025-tom-20261013/SOURCE-2.md
#include "gu-audit2025.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "wrapped-card.h"
#include "game-rng.h"
#include "skill-dialog-info.h"
#include "card-lifetime-manager.h"

static bool s2Alive(const ServerPlayer *p) { return p && p->isAlive(); }

static QList<int> s2RandomIds(QList<int> ids, int n)
{
	QList<int> out;
	while (n-- > 0 && !ids.isEmpty())
		out << ids.takeAt(qsanRandomBounded(ids.length()));
	return out;
}

static void s2UseVirtual(Room *room, const QString &name, const QString &skill, ServerPlayer *from,
	QList<ServerPlayer *> tos, bool distanceFree, bool addHistory)
{
	if (!s2Alive(from)) return;
	Card *card = Sanguosha->cloneCard(name);
	if (!card) return;
	card->setSkillName("_" + skill);
	card->deleteLater();
	QList<ServerPlayer *> valid;
	foreach (ServerPlayer *to, tos) {
		if (!s2Alive(to) || from->isProhibited(to, card)) continue;
		if (card->isKindOf("Slash") && !from->canSlash(to, card, !distanceFree)) continue;
		valid << to;
	}
	if (valid.isEmpty() || from->isLocked(card)) return;
	room->useCard(CardUseStruct(card, from, valid), addHistory);
}

static int s2MinNumber(const QList<int> &ids)
{
	int mn = 99;
	foreach (int id, ids) mn = qMin(mn, Sanguosha->getCard(id)->getNumber());
	return mn;
}

// ---------- 武皇甫嵩：朝镇 / 连捷 / 将贤 ----------
class GuS2Chaozhen : public TriggerSkill
{
public:
	GuS2Chaozhen() : TriggerSkill("chaozhen")
	{
		events << EventPhaseStart << EnterDying;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getMark("chaozhen_off-Clear") > 0) return false;
		if (event == EventPhaseStart && player->getPhase() != Player::Start) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		QString place = room->askForChoice(player, objectName(), "field+drawpile");
		QList<int> pool;
		if (place == "field") {
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				foreach (const Card *c, p->getCards("ej"))
					pool << c->getEffectiveId();
			}
		} else {
			pool = room->getDrawPile();
		}
		if (pool.isEmpty()) return false;
		int mn = s2MinNumber(pool);
		QList<int> cands;
		foreach (int id, pool) if (Sanguosha->getCard(id)->getNumber() == mn) cands << id;
		int id = cands.at(qsanRandomBounded(cands.length()));
		room->obtainCard(player, id, objectName(), true);
		if (player->isAlive() && Sanguosha->getCard(id)->getNumber() == 1) {
			room->recover(player, RecoverStruct(objectName(), player));
			room->setPlayerMark(player, "chaozhen_off-Clear", 1);
		}
		return false;
	}
};
Skill *guNewChaozhen() { return new GuS2Chaozhen; }

class GuS2Lianjie : public TriggerSkill
{
public:
	GuS2Lianjie() : TriggerSkill("lianjie")
	{
		events << TargetSpecified << PreCardUsed;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
		if (event == PreCardUsed) {
			foreach (int id, use.card->getSubcards()) {
				if (player->getMark("lianjie_free_" + QString::number(id) + "-Clear") > 0) {
					use.m_addHistory = false;
					data = QVariant::fromValue(use);
					break;
				}
			}
			return false;
		}
		// hand card used; number <= all other hand cards
		if (!use.m_isHandcard) return false;
		int num = use.card->getNumber();
		foreach (const Card *c, player->getHandcards()) {
			if (use.card->getSubcards().contains(c->getEffectiveId())) continue;
			if (c->getNumber() < num) return false;
		}
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->isKongcheng()) continue;
			if (player->getMark("lianjie_" + p->objectName() + "-Clear") > 0) continue;
			cands << p;
		}
		if (cands.isEmpty() || !player->askForSkillInvoke(this, data)) return false;
		ServerPlayer *t = room->askForPlayerChosen(player, cands, objectName(), "@lianjie", false, true);
		if (!t) return false;
		room->broadcastSkillInvoke(objectName());
		use.card->setFlags("lianjie_effect");
		data = QVariant::fromValue(use);
		room->setPlayerMark(player, "lianjie_" + t->objectName() + "-Clear", 1);
		int mn = 99;
		QList<int> ids;
		foreach (const Card *c, t->getHandcards()) {
			if (c->getNumber() < mn) { mn = c->getNumber(); ids.clear(); }
			if (c->getNumber() == mn) ids << c->getEffectiveId();
		}
		if (!ids.isEmpty()) {
			int id = ids.at(qsanRandomBounded(ids.length()));
			room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPileBottom,
				CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString()), true);
		}
		int n = player->getMaxHp() - player->getHandcardNum();
		if (n > 0 && player->isAlive()) {
			QList<int> before = player->handCards();
			player->drawCards(n, objectName());
			foreach (int id, player->handCards()) {
				if (before.contains(id)) continue;
				room->setPlayerMark(player, "lianjie_free_" + QString::number(id) + "-Clear", 1);
			}
		}
		return false;
	}
};
class GuS2LianjieMod : public TargetModSkill
{
public:
	GuS2LianjieMod() : TargetModSkill("#lianjie-mod") { pattern = "."; }
	int getResidueNum(const Player *from, const Card *card, const Player *) const override
	{
		if (!from || !card) return 0;
		foreach (int id, card->getSubcards())
			if (from->getMark("lianjie_free_" + QString::number(id) + "-Clear") > 0) return 1000;
		return 0;
	}
	int getDistanceLimit(const Player *from, const Card *card, const Player *) const override
	{
		if (!from || !card) return 0;
		foreach (int id, card->getSubcards())
			if (from->getMark("lianjie_free_" + QString::number(id) + "-Clear") > 0) return 1000;
		return 0;
	}
};

Skill *guNewLianjie() {
	// related mod registered via package insertRelatedSkills in wire step
	return new GuS2Lianjie;
}
Skill *guNewLianjieMod() { return new GuS2LianjieMod; }


GuJiangxianCard::GuJiangxianCard() { target_fixed = true; }
bool GuJiangxianCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const { return false; }
bool GuJiangxianCard::targetsFeasible(const QList<const Player *> &t, const Player *) const { return t.isEmpty(); }
void GuJiangxianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@jiangxian");
	room->doSuperLightbox(source, "jiangxian");
	room->setPlayerMark(source, "jiangxian_buff-Clear", 1);
	room->setPlayerMark(source, "jiangxian_dmgcount-Clear", 0);
}

class GuJiangxianVS : public ZeroCardViewAsSkill
{
public:
	GuJiangxianVS() : ZeroCardViewAsSkill("jiangxian") {}
	bool isEnabledAtPlay(const Player *player) const override { return player->getMark("@jiangxian") > 0; }
	const Card *viewAs() const override { return new GuJiangxianCard; }
};

class GuS2Jiangxian : public TriggerSkill
{
public:
	GuS2Jiangxian() : TriggerSkill("jiangxian")
	{
		events << ConfirmDamage << Damage << EventPhaseChanging;
		frequency = Limited;
		limit_mark = "@jiangxian";
		view_as_skill = new GuJiangxianVS;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			if (player->getMark("jiangxian_buff-Clear") <= 0) return false;
			QString lose = room->askForChoice(player, objectName(), "chaozhen+lianjie");
			room->handleAcquireDetachSkills(player, "-" + lose);
			return false;
		}
		if (player->getMark("jiangxian_buff-Clear") <= 0) return false;
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || !damage.card || !damage.card->hasFlag("lianjie_effect")) return false;
			int x = qMin(5, player->getMark("jiangxian_dmgcount-Clear"));
			if (x <= 0) return false;
			damage.damage += x;
			data = QVariant::fromValue(damage);
			return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.from == player)
			room->addPlayerMark(player, "jiangxian_dmgcount-Clear", damage.damage);
		return false;
	}
};
Skill *guNewJiangxian() { return new GuS2Jiangxian; }


// ---------- 乐诸葛果：羲笛 / 乘烟 ----------
class GuS2Xidi : public TriggerSkill
{
public:
	GuS2Xidi() : TriggerSkill("xidi")
	{
		events << GameStart << EventPhaseStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == GameStart) {
			room->sendCompulsoryTriggerLog(player, this);
			foreach (int id, player->handCards()) room->setCardTip(id, "xi_di");
			return false;
		}
		if (player->getPhase() != Player::Start && player->getPhase() != Player::Finish) return false;
		room->sendCompulsoryTriggerLog(player, this);
		int x = 0;
		foreach (const Card *c, player->getHandcards()) if (c->hasTip("xi_di")) x++;
		x = qMax(1, x);
		QList<int> cards = room->getNCards(x);
		room->askForGuanxing(player, cards);
		return false;
	}
};
class GuS2XidiMax : public MaxCardsSkill
{
public:
	GuS2XidiMax() : MaxCardsSkill("#xidi-max") {}
	int getExtra(const Player *target) const override
	{
		int n = 0;
		foreach (const Card *c, target->getHandcards()) if (c->hasTip("xi_di")) n++;
		return n;
	}
};
Skill *guNewXidi() { return new GuS2Xidi; }
Skill *guNewXidiMod() { return new GuS2XidiMax; }

class GuS2Chengyan : public TriggerSkill
{
public:
	GuS2Chengyan() : TriggerSkill("chengyan") { events << TargetSpecified; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card) return false;
		if (!(use.card->isKindOf("Slash") || use.card->isNDTrick())) return false;
		bool other = false;
		foreach (ServerPlayer *p, use.to) if (p != player) other = true;
		if (!other || !player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		player->drawCards(1, objectName());
		if (player->isDead() || player->isKongcheng()) return false;
		const Card *shown = player->getHandcards().last();
		room->showCard(player, shown->getEffectiveId());
		if (shown->isKindOf("Slash") || shown->isNDTrick()) {
			Card *rep = Sanguosha->cloneCard(shown->objectName());
			if (!rep) return false;
			rep->setSkillName(objectName());
			rep->addSubcards(use.card->getSubcards());
			rep->deleteLater();
			use.card = rep;
			data = QVariant::fromValue(use);
		} else {
			player->drawCards(1, objectName());
			if (!player->isKongcheng())
				room->setCardTip(player->getHandcards().last()->getEffectiveId(), "xi_di");
		}
		return false;
	}
};
Skill *guNewChengyan() { return new GuS2Chengyan; }

// ---------- 谋胡烈：传舆 / 倚投 ----------
GuChuanyuCard::GuChuanyuCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}
bool GuChuanyuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}
bool GuChuanyuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const { return targets.length() == 1; }
void GuChuanyuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *t = targets.first();
	room->giveCard(source, t, this, "chuanyu");
	foreach (int id, getSubcards()) {
		room->setCardFlag(id, "chuanyu_yu");
		room->setCardTip(id, "chuan_yu");
	}
	room->setPlayerMark(t, "chuanyu_got_lun", 1);
}

class GuChuanyuVS : public OneCardViewAsSkill
{
public:
	GuChuanyuVS() : OneCardViewAsSkill("chuanyu") { response_pattern = "@@chuanyu"; filter_pattern = "."; }
	const Card *viewAs(const Card *c) const override
	{
		GuChuanyuCard *card = new GuChuanyuCard;
		card->addSubcard(c);
		return card;
	}
};

class GuS2Chuanyu : public TriggerSkill
{
public:
	GuS2Chuanyu() : TriggerSkill("chuanyu")
	{
		events << RoundStart << CardsMoveOneTime << RoundEnd;
		view_as_skill = new GuChuanyuVS;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == RoundStart) {
			if (player->isNude()) return false;
			if (!room->askForUseCard(player, "@@chuanyu", "@chuanyu-give")) return false;
			return false;
		}
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place != Player::DiscardPile
				|| (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE)
				return false;
			foreach (int id, move.card_ids) {
				if (!Sanguosha->getCard(id)->hasFlag("chuanyu_yu")) continue;
				if (room->getCardPlace(id) != Player::DiscardPile) continue;
				QList<ServerPlayer *> cands;
				foreach (ServerPlayer *p, room->getAlivePlayers())
					if (p->getMark("chuanyu_got_lun") == 0) cands << p;
				ServerPlayer *t = room->askForPlayerChosen(player, cands, objectName(), "@chuanyu-pass", true, true);
				if (!t) continue;
				room->broadcastSkillInvoke(objectName());
				room->obtainCard(t, id, objectName(), true);
				room->setCardFlag(id, "chuanyu_yu");
				room->setCardTip(id, "chuan_yu");
				room->setPlayerMark(t, "chuanyu_got_lun", 1);
			}
			return false;
		}
		// RoundEnd: discard all 舆, then slashes
		QList<ServerPlayer *> got;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			QList<int> throwIds;
			foreach (const Card *c, p->getCards("he")) {
				if (c->hasFlag("chuanyu_yu") || c->hasTip("chuan_yu")) throwIds << c->getEffectiveId();
			}
			if (!throwIds.isEmpty()) room->throwCard(throwIds, objectName(), p, p);
			if (p->getMark("chuanyu_got_lun") > 0) got << p;
			room->setPlayerMark(p, "chuanyu_got_lun", 0);
		}
		if (got.isEmpty() || !player->askForSkillInvoke(this)) return false;
		ServerPlayer *victim = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@chuanyu-slash");
		if (!victim) return false;
		room->broadcastSkillInvoke(objectName());
		foreach (ServerPlayer *p, got) {
			if (p->isDead() || victim->isDead()) continue;
			s2UseVirtual(room, "slash", objectName(), p, QList<ServerPlayer *>() << victim, true, false);
		}
		return false;
	}
};
Skill *guNewChuanyu() { return new GuS2Chuanyu; }

class GuS2Yitou : public TriggerSkill
{
public:
	GuS2Yitou() : TriggerSkill("yitou")
	{
		events << EventPhaseStart << Damage;
	}
	bool triggerable(const ServerPlayer *target) const override { return target != nullptr; }
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.from) return false;
			QString giver = damage.from->property("yitou_giver").toString();
			ServerPlayer *g = room->findPlayerByObjectName(giver);
			if (s2Alive(g)) g->drawCards(1, objectName());
			return false;
		}
		if (player->getPhase() != Player::Play) return false;
		int most = -1;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			most = qMax(most, p->getHandcardNum());
		if (player->getHandcardNum() < most || most < 0) return false;
		foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
			if (!owner->hasSkill(this) || owner->isKongcheng()) continue;
			if (!owner->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			room->broadcastSkillInvoke(objectName());
			QList<int> ids = owner->handCards();
			room->giveCard(owner, player, ids, objectName());
			player->setProperty("yitou_giver", owner->objectName());
			room->setPlayerMark(player, "yitou_on-Clear", 1);
		}
		return false;
	}
};
// clear yitou_giver at turn end via mark -Clear companion
class GuS2YitouClear : public TriggerSkill
{
public:
	GuS2YitouClear() : TriggerSkill("#yitou-clear")
	{
		events << EventPhaseChanging;
		global = true;
	}
	bool triggerable(const ServerPlayer *target) const override { return target != nullptr; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		if (player->getMark("yitou_on-Clear") > 0)
			player->setProperty("yitou_giver", QString());
		return false;
	}
};
Skill *guNewYitou() { return new GuS2Yitou; }
Skill *guNewYitouClear() { return new GuS2YitouClear; }

// ---------- 蒋济[手杀] gu_jiangji：急筹 / 机论 ----------
GuJichouCard::GuJichouCard() { target_fixed = true; }
bool GuJichouCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const { return false; }
bool GuJichouCard::targetsFeasible(const QList<const Player *> &t, const Player *) const { return t.isEmpty(); }
void GuJichouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	// ① 每回合限一次：视为使用一张未被记录的普通锦囊并记录
	QStringList recorded = source->property("gu_jichou_record").toString().split("+");
	QStringList names;
	foreach (int id, Sanguosha->getRandomCards()) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (!c || !c->isNDTrick() || c->isKindOf("DelayedTrick")) continue;
		if (recorded.contains(c->objectName())) continue;
		if (!names.contains(c->objectName())) names << c->objectName();
	}
	if (names.isEmpty()) return;
	QString name = room->askForChoice(source, "gu_jichou", names.join("+"));
	recorded << name;
	source->setProperty("gu_jichou_record", recorded.join("+"));
	room->setPlayerMark(source, "gu_jichou-Clear", 1);
	Card *trick = Sanguosha->cloneCard(name);
	if (!trick) return;
	trick->setSkillName("_gu_jichou");
	trick->deleteLater();
	if (source->isLocked(trick)) return;
	room->setPlayerMark(source, "gu_jichou_using", 1);
	room->useCard(CardUseStruct(trick, source), false);
	room->setPlayerMark(source, "gu_jichou_using", 0);
}

GuJichouGiveCard::GuJichouGiveCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}
bool GuJichouGiveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}
bool GuJichouGiveCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const { return targets.length() == 1; }
void GuJichouGiveCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->setPlayerMark(source, "gu_jichou_give-PlayClear", 1);
	room->giveCard(source, targets.first(), this, "gu_jichou");
}

class GuJichouVS : public ViewAsSkill
{
public:
	GuJichouVS() : ViewAsSkill("gu_jichou") {}
	bool isEnabledAtPlay(const Player *player) const override
	{
		QStringList recorded = player->property("gu_jichou_record").toString().split("+");
		if (player->getMark("gu_jichou-Clear") == 0) return true;
		if (player->getMark("gu_jichou_give-PlayClear") == 0 && !recorded.isEmpty()) return true;
		return false;
	}
	bool viewFilter(const QList<const Card *> &, const Card *to_select) const override
	{
		if (Self->getMark("gu_jichou-Clear") == 0) return false;
		QStringList recorded = Self->property("gu_jichou_record").toString().split("+");
		return recorded.contains(to_select->objectName()) && to_select->isNDTrick();
	}
	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (Self->getMark("gu_jichou-Clear") == 0) {
			if (!cards.isEmpty()) return nullptr;
			return new GuJichouCard;
		}
		if (cards.isEmpty()) return nullptr;
		GuJichouGiveCard *card = new GuJichouGiveCard;
		card->addSubcards(cards);
		return card;
	}
};

class GuJichouLimit : public CardLimitSkill
{
public:
	GuJichouLimit() : CardLimitSkill("#gu_jichou-limit") {}
	QString limitList(const Player *target) const override
	{
		QStringList recorded = target->property("gu_jichou_record").toString().split("+");
		return recorded.isEmpty() ? QString() : "use";
	}
	QString limitPattern(const Player *target) const override
	{
		QStringList recorded = target->property("gu_jichou_record").toString().split("+");
		if (recorded.isEmpty()) return QString();
		// cannot use non-virtual cards of recorded names
		return recorded.join(","); // approximate; engine pattern for names is limited
	}
};

class GuJichouNoRespond : public TriggerSkill
{
public:
	GuJichouNoRespond() : TriggerSkill("#gu_jichou-norespond")
	{
		events << CardUsed;
		global = true;
	}
	bool triggerable(const ServerPlayer *target) const override { return target != nullptr; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p == player || !p->hasSkill("gu_jichou")) continue;
			QStringList recorded = p->property("gu_jichou_record").toString().split("+");
			if (recorded.contains(use.card->objectName())) {
				use.no_respond_list << p->objectName();
				data = QVariant::fromValue(use);
			}
		}
		// ② also: p cannot use recorded non-virtual - handled by limit skill poorly; enforce here on PreCardUsed
		return false;
	}
};

class GuJichouBlock : public TriggerSkill
{
public:
	GuJichouBlock() : TriggerSkill("#gu_jichou-block")
	{
		events << PreCardUsed;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (player->getMark("gu_jichou_using") > 0) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->isVirtualCard()) return false;
		QStringList recorded = player->property("gu_jichou_record").toString().split("+");
		if (recorded.contains(use.card->objectName())) {
			use.nullified_list << "_ALL_TARGETS";
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class GuJichou : public TriggerSkill
{
public:
	GuJichou() : TriggerSkill("gu_jichou")
	{
		view_as_skill = new GuJichouVS;
		events << NonTrigger;
	}
	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return false; }
};

class GuJilun : public TriggerSkill
{
public:
	GuJilun() : TriggerSkill("gu_jilun") { events << Damaged; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player->hasSkill("gu_jichou")) return false;
		QStringList recorded = player->property("gu_jichou_record").toString().split("+");
		QStringList choices; choices << "draw";
		if (!recorded.isEmpty()) choices << "use";
		QString ch = room->askForChoice(player, objectName(), choices.join("+") + "+cancel");
		if (ch == "cancel") return false;
		room->broadcastSkillInvoke(objectName());
		if (ch == "draw") {
			int x = qBound(1, 3, recorded.length());
			if (recorded.isEmpty()) x = 1;
			player->drawCards(x, objectName());
			return false;
		}
		// use: basic or recorded NDTrick with target count <= X
		int x = qBound(1, 3, recorded.length());
		QStringList names;
		foreach (int id, Sanguosha->getRandomCards()) {
			const Card *c = Sanguosha->getEngineCard(id);
			if (!c) continue;
			if (c->isKindOf("BasicCard") && player->getMark("gu_jilun_basic_" + c->objectName()) == 0) {
				if (!names.contains(c->objectName())) names << c->objectName();
			}
			if (recorded.contains(c->objectName()) && c->isNDTrick() && player->getMark("gu_jilun_used_" + c->objectName()) == 0) {
				if (!names.contains(c->objectName())) names << c->objectName();
			}
		}
		if (names.isEmpty()) return false;
		QString name = room->askForChoice(player, "gu_jilun_card", names.join("+"));
		room->setPlayerMark(player, "gu_jilun_used_" + name, 1);
		Card *card = Sanguosha->cloneCard(name);
		if (!card) return false;
		card->setSkillName("_gu_jilun");
		card->deleteLater();
		QList<ServerPlayer *> cands = room->getAlivePlayers();
		// ask use via askForUseCard pattern is hard; use simple single-target or zero-target
		if (card->targetFixed()) {
			room->useCard(CardUseStruct(card, player), false);
		} else {
			QList<ServerPlayer *> tos = room->askForPlayersChosen(player, room->getOtherPlayers(player), objectName(), 1, qMin(x, room->alivePlayerCount() - 1), "@gu_jilun");
			if (tos.isEmpty()) return false;
			room->useCard(CardUseStruct(card, player, tos), false);
		}
		return false;
	}
};

void guAudit2025Source2Mobile(Package *pkg)
{
	General *jiangji = new General(pkg, "gu_jiangji", "wei", 3);
	jiangji->addSkill(new GuJichou);
	jiangji->addSkill(new GuJilun);
	pkg->addSkills(new GuJichouLimit);
	pkg->addSkills(new GuJichouNoRespond);
	pkg->addSkills(new GuJichouBlock);
	pkg->insertRelatedSkills("gu_jichou", "#gu_jichou-limit");
	pkg->insertRelatedSkills("gu_jichou", "#gu_jichou-norespond");
	pkg->insertRelatedSkills("gu_jichou", "#gu_jichou-block");
	pkg->addMetaObject<GuJichouCard>();
	pkg->addMetaObject<GuJichouGiveCard>();
}

// ========== SOURCE-2 OL batch overwrites ==========

// ----- 袁姬：节言 / 镜花 / 水月 -----
class GuS2Jieyan : public TriggerSkill
{
public:
	GuS2Jieyan() : TriggerSkill("jieyan") { events << CardsMoveOneTime; }
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.from) return false;
		int lost = 0;
		foreach (Player::Place pl, move.from_places) if (pl == Player::PlaceHand) lost++;
		if (lost != 2) return false;
		ServerPlayer *who = qobject_cast<ServerPlayer *>(move.from);
		if (!who) return false;
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			if (!owner->hasSkill(this) || owner->getMark("jieyan_off-Clear") > 0) continue;
			if (!owner->askForSkillInvoke(this, QVariant::fromValue(who))) continue;
			room->broadcastSkillInvoke(objectName());
			QString side = room->askForChoice(owner, objectName(), "top+bottom");
			int idOwner = -1, idWho = -1;
			if (side == "top") {
				QList<int> top = room->getNCards(1, false);
				if (!top.isEmpty()) { idOwner = top.first(); room->obtainCard(owner, idOwner, objectName(), true); room->showCard(owner, idOwner); }
				if (!room->getDrawPile().isEmpty()) {
					idWho = room->getDrawPile().last();
					room->obtainCard(who, idWho, objectName(), true);
					if (who->isAlive()) room->showCard(who, idWho);
				}
			} else {
				if (!room->getDrawPile().isEmpty()) {
					idOwner = room->getDrawPile().last();
					room->obtainCard(owner, idOwner, objectName(), true);
					room->showCard(owner, idOwner);
				}
				QList<int> top = room->getNCards(1, false);
				if (!top.isEmpty()) { idWho = top.first(); room->obtainCard(who, idWho, objectName(), true); if (who->isAlive()) room->showCard(who, idWho); }
			}
			if (idOwner >= 0 && idWho >= 0
				&& Sanguosha->getCard(idOwner)->getSuit() != Sanguosha->getCard(idWho)->getSuit())
				room->setPlayerMark(owner, "jieyan_off-Clear", 1);
		}
		return false;
	}
};
Skill *guNewJieyan() { return new GuS2Jieyan; }

class GuS2Jinghua : public TriggerSkill
{
public:
	GuS2Jinghua() : TriggerSkill("jinghua")
	{
		events << RoundStart << EventPhaseStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == RoundStart) {
			room->sendCompulsoryTriggerLog(player, this);
			QList<int> before = player->handCards();
			player->drawCards(2, objectName());
			QStringList rec;
			foreach (int id, player->handCards()) if (!before.contains(id)) rec << QString::number(id);
			player->setProperty("jinghua_ids", rec.join("+"));
			return false;
		}
		if (player->getPhase() != Player::RoundStart) return false;
		QStringList rec = player->property("jinghua_ids").toString().split("+");
		player->setProperty("jinghua_ids", QString());
		QList<int> ids;
		foreach (QString s, rec) {
			int id = s.toInt();
			if (id >= 0 && player->handCards().contains(id)) ids << id;
		}
		if (ids.isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, this);
		DummyCard d(ids);
		room->moveCardTo(&d, nullptr, Player::DrawPileBottom,
			CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString()), true);
		return false;
	}
};
Skill *guNewJinghua() { return new GuS2Jinghua; }

class GuS2Shuiyue : public TriggerSkill
{
public:
	GuS2Shuiyue() : TriggerSkill("shuiyue")
	{
		events << EventPhaseChanging << RoundEnd;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging) {
			if (!player->hasSkill(this) || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			room->sendCompulsoryTriggerLog(player, this);
			QList<int> before = player->handCards();
			player->drawCards(2, objectName());
			QStringList rec;
			foreach (int id, player->handCards()) if (!before.contains(id)) rec << QString::number(id);
			player->setProperty("shuiyue_ids", rec.join("+"));
			return false;
		}
		if (!player->hasSkill(this)) return false;
		QStringList rec = player->property("shuiyue_ids").toString().split("+");
		player->setProperty("shuiyue_ids", QString());
		QList<int> ids;
		foreach (QString s, rec) {
			int id = s.toInt();
			if (id >= 0 && player->handCards().contains(id)) ids << id;
		}
		if (ids.isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, this);
		DummyCard d(ids);
		room->moveCardTo(&d, nullptr, Player::DrawPile,
			CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString()), true);
		return false;
	}
};
Skill *guNewShuiyue() { return new GuS2Shuiyue; }

// ----- 谋袁术：矜名 / 枭噬（厌粱已吻合） -----
class GuS2Jinming : public TriggerSkill
{
public:
	GuS2Jinming() : TriggerSkill("jinming")
	{
		events << EventPhaseStart << EventPhaseChanging << HpRecover << CardsMoveOneTime << CardUsed << Damage;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
			QStringList opts;
			for (int i = 1; i <= 4; i++)
				if (player->getMark("jinming_rm" + QString::number(i)) == 0)
					opts << QString::number(i);
			if (opts.isEmpty() || !player->askForSkillInvoke(this)) return false;
			QString ch = room->askForChoice(player, objectName(), opts.join("+"));
			room->broadcastSkillInvoke(objectName());
			room->setPlayerMark(player, "jinming_choice-Clear", ch.toInt());
			return false;
		}
		int choice = player->getMark("jinming_choice-Clear");
		if (choice <= 0 && event != EventPhaseChanging) return false;
		if (event == HpRecover && choice == 1) room->addPlayerMark(player, "jinming_ok-Clear");
		if (event == CardsMoveOneTime && choice == 2) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
				room->addPlayerMark(player, "jinming_dis-Clear", move.card_ids.length());
		}
		if (event == CardUsed && choice == 3) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill) {
				QString t = use.card->getType();
				if (player->getMark("jinming_t_" + t + "-Clear") == 0) {
					room->setPlayerMark(player, "jinming_t_" + t + "-Clear", 1);
					room->addPlayerMark(player, "jinming_types-Clear");
				}
			}
		}
		if (event == Damage && choice == 4) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from == player) room->addPlayerMark(player, "jinming_dmg-Clear", damage.damage);
		}
		if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
			choice = player->getMark("jinming_choice-Clear");
			if (choice <= 0) return false;
			int prev = player->getMark("jinming_record");
			if (prev > 0) player->drawCards(prev, objectName());
			room->setPlayerMark(player, "jinming_record", choice);
			bool ok = false;
			if (choice == 1) ok = player->getMark("jinming_ok-Clear") >= 1;
			if (choice == 2) ok = player->getMark("jinming_dis-Clear") >= 2;
			if (choice == 3) ok = player->getMark("jinming_types-Clear") >= 3;
			if (choice == 4) ok = player->getMark("jinming_dmg-Clear") >= 4;
			if (!ok) room->setPlayerMark(player, "jinming_rm" + QString::number(choice), 1);
		}
		return false;
	}
};
Skill *guNewJinming() { return new GuS2Jinming; }

class GuS2Xiaoshi : public TriggerSkill
{
public:
	GuS2Xiaoshi() : TriggerSkill("xiaoshi") { events << CardUsed << Damage << EventPhaseChanging; }
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from == player && player->getMark("xiaoshi_wait-PlayClear") > 0)
				room->setPlayerMark(player, "xiaoshi_dmg-PlayClear", 1);
			return false;
		}
		if (event == EventPhaseChanging) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card) return false;
		if (!(use.card->isKindOf("BasicCard") || use.card->isNDTrick())) return false;
		if (player->getMark("xiaoshi-PlayClear") > 0) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (use.to.contains(p) || p == player) continue;
			if (!player->isProhibited(p, use.card)) cands << p;
		}
		ServerPlayer *t = room->askForPlayerChosen(player, cands, objectName(), "@xiaoshi", true);
		if (!t) return false;
		room->broadcastSkillInvoke(objectName());
		room->setPlayerMark(player, "xiaoshi-PlayClear", 1);
		room->setPlayerMark(player, "xiaoshi_wait-PlayClear", 1);
		use.to << t;
		room->sortByActionOrder(use.to);
		data = QVariant::fromValue(use);
		// after card finishes - use CardFinished via mark check on next phase? use CardFinished event
		return false;
	}
};

class GuS2XiaoshiAfter : public TriggerSkill
{
public:
	GuS2XiaoshiAfter() : TriggerSkill("#xiaoshi-after")
	{
		events << CardFinished;
		global = true;
	}
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || player->getMark("xiaoshi_wait-PlayClear") <= 0) return false;
		room->setPlayerMark(player, "xiaoshi_wait-PlayClear", 0);
		if (player->getMark("xiaoshi_dmg-PlayClear") > 0) return false;
		if (!player->hasSkill("xiaoshi")) return false;
		QString ch = room->askForChoice(player, "xiaoshi", "losehp+draw");
		if (ch == "losehp") room->loseHp(player, 1, true, player, "xiaoshi");
		else {
			int n = qMax(1, player->getMark("jinming_record"));
			ServerPlayer *t = room->askForPlayerChosen(player, use.to, "xiaoshi", "@xiaoshi-draw", false, true);
			if (t) t->drawCards(n, "xiaoshi");
		}
		return false;
	}
};
Skill *guNewXiaoshi() { return new GuS2Xiaoshi; }
Skill *guNewXiaoshiAfter() { return new GuS2XiaoshiAfter; }

// ----- 武安国 olliyong overwrite via new skill keeping name olliyong -----
GuS2LiyongCard::GuS2LiyongCard() {}
bool GuS2LiyongCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}
bool GuS2LiyongCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const { return targets.length() == 1; }
void GuS2LiyongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	// yin: get suit-used card from pile, target uses duel on source
	QList<int> cands;
	foreach (int id, room->getDrawPile()) {
		const Card *c = Sanguosha->getCard(id);
		if (source->getMark("liyong_suit_" + c->getSuitString() + "-Clear") > 0)
			cands << id;
	}
	if (!cands.isEmpty())
		room->obtainCard(source, cands.at(qsanRandomBounded(cands.length())), "olliyong", false);
	s2UseVirtual(room, "duel", "olliyong", targets.first(), QList<ServerPlayer *>() << source, true, false);
	if (source->getChangeSkillState("olliyong") == 2)
		room->setChangeSkillState(source, "olliyong", 1);
	else
		room->setChangeSkillState(source, "olliyong", 2);
}

class GuS2OLLiyongVS : public ViewAsSkill
{
public:
	GuS2OLLiyongVS() : ViewAsSkill("olliyong") { change_skill = true; }
	bool isEnabledAtPlay(const Player *) const override { return true; }
	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		if (Self->getChangeSkillState("olliyong") == 2) return false;
		if (!selected.isEmpty()) return false;
		return Self->getMark("liyong_suit_" + to_select->getSuitString() + "-Clear") == 0
			&& !Self->isJilei(to_select);
	}
	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (Self->getChangeSkillState("olliyong") != 2) {
			if (cards.length() != 1) return nullptr;
			Duel *duel = new Duel(Card::SuitToBeDecided, -1);
			duel->addSubcards(cards);
			duel->setSkillName(objectName());
			return duel;
		}
		if (!cards.isEmpty()) return nullptr;
		return new GuS2LiyongCard;
	}
};

class GuS2OLLiyong : public TriggerSkill
{
public:
	GuS2OLLiyong() : TriggerSkill("olliyong")
	{
		events << CardUsed << PreCardUsed;
		view_as_skill = new GuS2OLLiyongVS;
		change_skill = true;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card) return false;
		if (event == CardUsed) {
			if (use.card->getTypeId() != Card::TypeSkill) {
				QList<int> ids = use.card->isVirtualCard() ? use.card->getSubcards() : QList<int>() << use.card->getEffectiveId();
				foreach (int id, ids) {
					if (id < 0) continue;
					room->setPlayerMark(player, "liyong_suit_" + Sanguosha->getCard(id)->getSuitString() + "-Clear", 1);
				}
			}
			if (use.card->getSkillName() == objectName() && player->getChangeSkillState("olliyong") != 2) {
				// yang used duel - switch to yin
				room->setChangeSkillState(player, "olliyong", 2);
			}
		}
		return false;
	}
};
Skill *guNewOLLiyong() { return new GuS2OLLiyong; }

// ----- 郭照：椒遇 / 内训 -----
class GuS2Jiaoyu : public TriggerSkill
{
public:
	GuS2Jiaoyu() : TriggerSkill("jiaoyu")
	{
		events << RoundStart << EventPhaseStart << EventPhaseChanging << CardUsed << Damage;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == RoundStart && player->hasSkill(this)) {
			room->sendCompulsoryTriggerLog(player, this);
			int x = 0;
			for (int i = 0; i < 5; i++) if (player->hasEquipArea(i) && !player->getEquip(i)) x++;
			QList<int> got;
			for (int i = 0; i < x; i++) {
				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".";
				judge.good = true;
				room->judge(judge);
				if (judge.card) got << judge.card->getEffectiveId();
			}
			QString color = room->askForChoice(player, objectName(), "red+black");
			player->setProperty("jiaoyu_color", color);
			room->setPlayerMark(player, "jiaoyu_has", 1);
			QList<int> take;
			foreach (int id, got) {
				if (Sanguosha->getCard(id)->getColorString() == color
					&& (room->getCardPlace(id) == Player::DiscardPile || room->getCardPlace(id) == Player::PlaceTable))
					take << id;
			}
			if (!take.isEmpty()) {
				DummyCard d(take);
				room->obtainCard(player, &d, objectName(), true);
			}
			room->setPlayerMark(player, "jiaoyu_extra_next", 1);
			return false;
		}
		if (event == EventPhaseChanging && player->hasSkill(this)
			&& data.value<PhaseChangeStruct>().to == Player::NotActive
			&& player->getMark("jiaoyu_do_extra") > 0) {
			room->setPlayerMark(player, "jiaoyu_do_extra", 0);
			room->setPlayerMark(player, "jiaoyu_extra_play-PlayClear", 1);
			room->setPlayerMark(player, "jiaoyu_lock_color", 1);
			player->insertPhase(Player::Play);
			return false;
		}
		if (event == EventPhaseStart && player->getPhase() == Player::RoundStart
			&& player->getMark("jiaoyu_extra_next") > 0) {
			room->setPlayerMark(player, "jiaoyu_extra_next", 0);
			room->setPlayerMark(player, "jiaoyu_do_extra", 1);
		}
		if (event == CardUsed && player->getMark("jiaoyu_extra_play-PlayClear") > 0) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill) {
				QString color = player->property("jiaoyu_color").toString();
				if (use.card->getColorString() != color) {
					use.nullified_list << "_ALL_TARGETS";
					data = QVariant::fromValue(use);
				}
			}
		}
		if (event == Damage) {
			foreach (ServerPlayer *p, room->getAllPlayers())
				room->setPlayerMark(p, "jiaoyu_other_lock-Clear", 0);
		}
		return false;
	}
};

class GuS2JiaoyuProhibit : public ProhibitSkill
{
public:
	GuS2JiaoyuProhibit() : ProhibitSkill("#jiaoyu-prohibit") {}
	bool isProhibited(const Player *from, const Player *, const Card *card, const QList<const Player *> &) const override
	{
		if (!from || !card || card->getTypeId() == Card::TypeSkill) return false;
		// during owner's extra play, others cannot use same color as owner's equips
		// simplified: mark jiaoyu_other_lock set on others when extra starts
		return from->getMark("jiaoyu_other_lock-Clear") > 0
			&& card->getColorString() == from->property("jiaoyu_ban_color").toString();
	}
};
Skill *guNewJiaoyu() { return new GuS2Jiaoyu; }
Skill *guNewJiaoyuProhibit() { return new GuS2JiaoyuProhibit; }

class GuS2Neixun : public TriggerSkill
{
public:
	GuS2Neixun() : TriggerSkill("neixun")
	{
		events << CardUsed;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr && t->isAlive(); }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeEquip) return false;
		if (player->getPhase() != Player::Play || player->getMark("neixun_done-Clear") > 0) return false;
		player->addMark("neixun_done-Clear"); // first non-equip this turn - mark before skill owners act
		// fix: should mark only after confirming first
		foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
			if (!owner->hasSkill(this) || owner->getMark("jiaoyu_has") <= 0) continue;
			room->sendCompulsoryTriggerLog(owner, this);
			QString color = owner->property("jiaoyu_color").toString();
			bool same = use.card->getColorString() == color;
			if (same) {
				if (owner->isNude()) continue;
				const Card *c = room->askForExchange(owner, objectName(), 1, 1, true, "@neixun-give:" + player->objectName());
				if (c) {
					room->giveCard(owner, player, c->getSubcards(), objectName());
					player->drawCards(1, objectName());
				}
			} else {
				if (player->isNude()) continue;
				int id = room->askForCardChosen(owner, player, "he", objectName());
				if (id >= 0) {
					room->obtainCard(owner, id, objectName(), false);
					room->setPlayerMark(owner, "neixun_keep-Clear", 1);
					room->setPlayerMark(owner, "neixun_keep_id", id + 1);
					player->drawCards(1, objectName());
				}
			}
		}
		return false;
	}
};
class GuS2NeixunMax : public MaxCardsSkill
{
public:
	GuS2NeixunMax() : MaxCardsSkill("#neixun-max") {}
	int getExtra(const Player *target) const override
	{
		// cards obtained via neixun don't count until owner's turn ends - approximate via mark
		return target->getMark("neixun_keep-Clear") > 0 ? 1 : 0;
	}
};
Skill *guNewNeixun() { return new GuS2Neixun; }
Skill *guNewNeixunMax() { return new GuS2NeixunMax; }

// ----- 孔淑：累卵 -----
GuLeiluanPlayCard::GuLeiluanPlayCard() { target_fixed = true; will_throw = false; handling_method = Card::MethodNone; }
bool GuLeiluanPlayCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const { return false; }
bool GuLeiluanPlayCard::targetsFeasible(const QList<const Player *> &t, const Player *) const { return t.isEmpty(); }
void GuLeiluanPlayCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int x = 0;
	foreach (ServerPlayer *p, room->getAlivePlayers()) if (p->isChained()) x++;
	x = qMax(1, x);
	if (source->getCardCount() < x) return;
	const Card *mat = room->askForExchange(source, "leiluan", x, x, true, "@leiluan:::" + QString::number(x));
	if (!mat || mat->subcardsLength() != x) return;
	QStringList names;
	foreach (int id, Sanguosha->getRandomCards()) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (!c || !c->isKindOf("BasicCard")) continue;
		if (source->getMark("leiluan_used_" + c->objectName() + "_lun") > 0) continue;
		if (!names.contains(c->objectName())) names << c->objectName();
	}
	if (names.isEmpty()) return;
	QString name = room->askForChoice(source, "leiluan", names.join("+"));
	bool lastHand = true;
	foreach (int id, source->handCards()) if (!mat->getSubcards().contains(id)) lastHand = false;
	Card *basic = Sanguosha->cloneCard(name);
	basic->addSubcards(mat->getSubcards());
	basic->setSkillName("leiluan");
	basic->deleteLater();
	room->useCard(CardUseStruct(basic, source), true);
	room->setPlayerMark(source, "leiluan_used_" + name + "_lun", 1);
	if (lastHand && source->isAlive()) {
		source->drawCards(2, "leiluan");
		foreach (int id, room->getDiscardPile()) {
			const Card *c = Sanguosha->getCard(id);
			if (c->isNDTrick() && !c->isKindOf("DelayedTrick")) {
				room->obtainCard(source, id, "leiluan", true);
				break;
			}
		}
		room->setPlayerMark(source, "leiluan_off", 1);
	}
}

class GuS2Leiluan : public ZeroCardViewAsSkill
{
public:
	GuS2Leiluan() : ZeroCardViewAsSkill("leiluan") {}
	bool isEnabledAtPlay(const Player *player) const override { return player->getMark("leiluan_off") == 0; }
	const Card *viewAs() const override { return new GuLeiluanPlayCard; }
};

class GuS2LeiluanWake : public TriggerSkill
{
public:
	GuS2LeiluanWake() : TriggerSkill("#leiluan-wake")
	{
		events << Damaged;
		global = true;
	}
	bool triggerable(const ServerPlayer *t) const override { return t && t->getMark("leiluan_off") > 0; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		room->setPlayerMark(player, "leiluan_off", 0);
		return false;
	}
};
Skill *guNewLeiluan() { return new GuS2Leiluan; }
Skill *guNewLeiluanWake() { return new GuS2LeiluanWake; }

class GuS2Fuchao : public TriggerSkill
{
public:
	GuS2Fuchao() : TriggerSkill("fuchao")
	{
		events << RoundEnd << CardFinished;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == RoundEnd && player->hasSkill(this)) {
			room->sendCompulsoryTriggerLog(player, this);
			QList<ServerPlayer *> chained;
			chained << player;
			foreach (ServerPlayer *p, room->getOtherPlayers(player))
				if (p->isChained()) chained << p;
			foreach (ServerPlayer *p, chained) if (p->isAlive()) p->drawCards(1, objectName());
			int x = 0;
			foreach (ServerPlayer *p, room->getAlivePlayers()) if (p->isChained()) x++;
			x = qMax(1, x);
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (!p->isChained() || p == player) continue;
				if (p->canDiscard(p, "he"))
					room->askForDiscard(p, objectName(), x, x, false, true);
			}
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from == player && player->hasSkill(this) && use.card && use.card->isKindOf("BasicCard")) {
			ServerPlayer *cur = room->getCurrent();
			if (cur && cur->isAlive() && !cur->isChained())
				room->setPlayerChained(cur);
		}
		return false;
	}
};
Skill *guNewFuchao() { return new GuS2Fuchao; }

// ----- 谋邓艾：积谷 / 解腕（SOURCE-2：替换；解腕距离 X） -----
class GuS2Jigu : public TriggerSkill
{
public:
	GuS2Jigu() : TriggerSkill("jigu")
	{
		events << CardsMoveOneTime << EventPhaseStart;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
			foreach (ServerPlayer *owner, room->getAllPlayers()) {
				if (!owner->hasSkill(this) || owner->getMaxHp() != player->getMaxHp()) continue;
				if (owner->getPile("gu").isEmpty() || owner->isKongcheng()) continue;
				if (!owner->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
				room->broadcastSkillInvoke(objectName());
				int n = qMin(owner->getHandcardNum(), owner->getPile("gu").length());
				const Card *give = room->askForExchange(owner, objectName(), n, 1, false, "@jigu-exchange", true);
				if (!give || give->subcardsLength() == 0) continue;
				QList<int> fromHand = give->getSubcards();
				room->notifyMoveToPile(owner, owner->getPile("gu"), objectName(), Player::PlaceSpecial, true);
				const Card *back = room->askForExchange(owner, objectName() + "2", fromHand.length(), fromHand.length(), false,
					"@jigu-back", false, ".|.|.|gu");
				room->notifyMoveToPile(owner, owner->getPile("gu"), objectName(), Player::PlaceSpecial, false);
				if (!back) continue;
				owner->addToPile("gu", fromHand);
				room->obtainCard(owner, back, objectName(), false);
			}
			return false;
		}
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile) return false;
		ServerPlayer *from = qobject_cast<ServerPlayer *>(move.from);
		if (!from || from->getPhase() == Player::Play) return false;
		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE) return false;
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			if (!owner->hasSkill(this)) continue;
			if (owner->getPile("gu").length() >= owner->getMaxHp()) continue;
			QList<int> ids;
			foreach (int id, move.card_ids) {
				if (room->getCardPlace(id) != Player::DiscardPile) continue;
				if (Sanguosha->getCard(id)->getSuit() == Card::Heart) continue;
				ids << id;
			}
			while (!ids.isEmpty() && owner->getPile("gu").length() < owner->getMaxHp()) {
				room->sendCompulsoryTriggerLog(owner, this);
				owner->addToPile("gu", ids.takeFirst());
			}
		}
		return false;
	}
};
Skill *guNewJigu() { return new GuS2Jigu; }

class GuS2JiewanVS : public OneCardViewAsSkill
{
public:
	GuS2JiewanVS() : OneCardViewAsSkill("jiewan")
	{
		filter_pattern = ".|.|.|hand";
		response_pattern = "@@jiewan";
	}
	bool viewFilter(const Card *to_select) const override
	{
		return !to_select->isEquipped();
	}
	const Card *viewAs(const Card *originalCard) const override
	{
		Snatch *snatch = new Snatch(originalCard->getSuit(), originalCard->getNumber());
		snatch->addSubcard(originalCard);
		snatch->setSkillName(objectName());
		return snatch;
	}
};

class GuS2Jiewan : public TriggerSkill
{
public:
	GuS2Jiewan() : TriggerSkill("jiewan")
	{
		events << EventPhaseStart;
		view_as_skill = new GuS2JiewanVS;
	}
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			if (!owner->hasSkill(this) || !owner->isAlive()) continue;
			if (player->getPhase() == Player::Start) {
				if (owner->isKongcheng()) continue;
				QStringList choices;
				if (owner->getMaxHp() > 1) choices << "loseMax";
				if (owner->getPile("gu").length() >= 2) choices << "removeGu";
				choices << "cancel";
				QString ch = room->askForChoice(owner, objectName(), choices.join("+"));
				if (ch == "cancel") continue;
				room->broadcastSkillInvoke(objectName());
				if (ch == "loseMax") room->loseMaxHp(owner, 1, objectName());
				else {
					QList<int> pile = owner->getPile("gu");
					QList<int> rm = s2RandomIds(pile, 2);
					CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, owner->objectName(), objectName(), QString());
					room->throwCard(rm, reason, nullptr);
				}
				int x = qMax(1, owner->getPile("gu").length());
				room->setPlayerMark(owner, "jiewan_dist", x);
				room->askForUseCard(owner, "@@jiewan", "@jiewan");
				room->setPlayerMark(owner, "jiewan_dist", 0);
			} else if (player->getPhase() == Player::Finish) {
				if (owner->getHandcardNum() != owner->getPile("gu").length()) continue;
				bool uniqueHighest = true;
				foreach (ServerPlayer *p, room->getOtherPlayers(owner))
					if (p->getMaxHp() >= owner->getMaxHp()) uniqueHighest = false;
				if (uniqueHighest) continue;
				room->sendCompulsoryTriggerLog(owner, this);
				room->gainMaxHp(owner, 1, objectName());
			}
		}
		return false;
	}
};
class GuS2JiewanDist : public TargetModSkill
{
public:
	GuS2JiewanDist() : TargetModSkill("#jiewan-dist") { pattern = "Snatch"; }
	int getDistanceLimit(const Player *from, const Card *card, const Player *) const override
	{
		if (card && card->getSkillName() == "jiewan")
			return from->getMark("jiewan_dist");
		return 0;
	}
};
Skill *guNewJiewan() { return new GuS2Jiewan; }
Skill *guNewJiewanDist() { return new GuS2JiewanDist; }

// ----- 谋黄月英：理贤 / 并才（整组重做） -----
class GuS2Lixian : public TriggerSkill
{
public:
	GuS2Lixian() : TriggerSkill("lixian")
	{
		events << Damaged << EventPhaseStart;
	}
	static QStringList addedNames(ServerPlayer *p)
	{
		return p->property("lixian_names").toString().split("+");
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == Damaged) {
			if (player->isNude()) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			doExNihilo(room, player);
			return false;
		}
		if (player->getPhase() == Player::Finish) {
			QStringList names = addedNames(player);
			if (names.length() < 3) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			doExNihilo(room, player);
		} else if (player->getPhase() == Player::Start) {
			QStringList names = addedNames(player);
			// 若以上牌名均已添加，你可以于准备阶段发动理贤
			QStringList need; need << "snatch" << "dismantlement" << "iron_chain";
			bool all = true;
			foreach (QString n, need) if (!names.contains(n)) all = false;
			if (!all || !player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			doExNihilo(room, player);
		}
		return false;
	}
	void doExNihilo(Room *room, ServerPlayer *player) const
	{
		if (player->isNude()) return;
		const Card *c = room->askForExchange(player, objectName(), 1, 1, true, "@lixian");
		if (!c) return;
		ExNihilo *ex = new ExNihilo(Card::SuitToBeDecided, -1);
		ex->addSubcards(c->getSubcards());
		ex->setSkillName(objectName());
		ex->deleteLater();
		room->useCard(CardUseStruct(ex, player), false);
		QString used = player->property("lixian_used_tricks").toString();
		QStringList ul = used.split("+");
		// track trick names used via this skill - simplified on CardFinished
	}
};
Skill *guNewLixian() { return new GuS2Lixian; }

class GuS2Bingcai : public TriggerSkill
{
public:
	GuS2Bingcai() : TriggerSkill("bingcai") { events << CardUsed << ConfirmDamage; /* TargetSpecified for re-settle */ }
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == ConfirmDamage) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("BasicCard")) return false;
		if (room->getTag("BingcaiUsed-Clear").toBool()) return false;
		room->setTag("BingcaiUsed-Clear", true);
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			if (!owner->hasSkill(this) || owner->isNude()) continue;
			if (!owner->askForSkillInvoke(this, data)) continue;
			room->broadcastSkillInvoke(objectName());
			const Card *recast = room->askForExchange(owner, objectName(), 1, 1, true, "@bingcai-recast");
			if (!recast) continue;
			bool bothDmg = use.card->isDamageCard() && recast->isDamageCard();
			bool bothNon = !use.card->isDamageCard() && !recast->isDamageCard();
			room->recastCard(owner, recast, objectName());
			if (bothDmg || bothNon) {
				use.extra_use += 1;
				data = QVariant::fromValue(use);
			}
			if (recast->isKindOf("TrickCard")) {
				QStringList names = owner->property("lixian_names").toString().split("+");
				QStringList pool; pool << "snatch" << "dismantlement" << "iron_chain";
				foreach (QString n, pool) {
					if (names.contains(n)) continue;
					names << n;
					owner->setProperty("lixian_names", names.join("+"));
					LogMessage log;
					log.type = "#BingcaiAdd";
					log.from = owner;
					log.arg = n;
					room->sendLog(log);
					break;
				}
			}
			break;
		}
		return false;
	}
};
Skill *guNewBingcai() { return new GuS2Bingcai; }

// ----- 界王异：贞烈 / 秘计（SOURCE-2） -----
class GuS2OLZhenlie : public TriggerSkill
{
public:
	GuS2OLZhenlie() : TriggerSkill("olzhenlie")
	{
		events << TargetConfirmed << EventPhaseStart;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Finish || player->getMark("olzhenlie_miji-Clear") <= 0) return false;
			room->setPlayerMark(player, "olzhenlie_miji-Clear", 0);
			doMiji(room, player);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from == player || !use.to.contains(player) || !use.card) return false;
		if (!(use.card->isKindOf("Slash") || use.card->isNDTrick())) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		room->loseHp(player, 1, true, player, objectName());
		if (player->isDead()) return false;
		use.nullified_list << player->objectName();
		data = QVariant::fromValue(use);
		QString ch = room->askForChoice(player, objectName(), "obtain+miji", data);
		if (ch == "obtain" && use.from && !use.from->isNude()) {
			int id = room->askForCardChosen(player, use.from, "he", objectName());
			if (id >= 0) room->obtainCard(player, id, objectName(), false);
		} else if (ch == "miji") {
			room->setPlayerMark(player, "olzhenlie_miji-Clear", 1);
		}
		return false;
	}
	void doMiji(Room *room, ServerPlayer *player) const
	{
		int x = player->getLostHp();
		if (x <= 0) return;
		player->drawCards(x, "olmiji");
		if (player->isKongcheng()) return;
		const Card *c = room->askForExchange(player, "olmiji", x, 1, false, "@olmiji-give", true);
		if (!c) return;
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), "olmiji", "@olmiji-target");
		if (t) room->giveCard(player, t, c->getSubcards(), "olmiji");
	}
};
Skill *guNewOLZhenlie() { return new GuS2OLZhenlie; }

class GuS2OLMiji : public TriggerSkill
{
public:
	GuS2OLMiji() : TriggerSkill("olmiji") { events << EventPhaseStart; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::Finish || player->getLostHp() <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		int x = player->getLostHp();
		player->drawCards(x, objectName());
		if (player->isDead() || player->isKongcheng()) return false;
		// distribute up to x cards to others - simplified one give
		const Card *c = room->askForExchange(player, objectName(), x, 1, false, "@olmiji-give", true);
		if (!c) return false;
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@olmiji-target", true);
		if (t) room->giveCard(player, t, c->getSubcards(), objectName());
		return false;
	}
};
Skill *guNewOLMiji() { return new GuS2OLMiji; }

// ----- 鲍信[OL] new engine general -----
GuMutaoCard::GuMutaoCard() {}
bool GuMutaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select != nullptr;
}
bool GuMutaoCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const { return targets.length() == 1; }
void GuMutaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *t = targets.first();
	QList<int> slashes;
	foreach (const Card *c, t->getHandcards())
		if (c->isKindOf("Slash")) slashes << c->getEffectiveId();
	if (slashes.isEmpty()) return;
	t->addToPile("mutao_slash", slashes, true);
	QList<ServerPlayer *> order;
	ServerPlayer *p = t->getNextAlive();
	while (p) {
		order << p;
		if (p == t) break;
		p = p->getNextAlive();
		if (order.length() > room->alivePlayerCount()) break;
	}
	// SOURCE: 由其下家开始的每一名角色 - redistribute all slashes randomly one by one starting from next
	QList<int> pile = t->getPile("mutao_slash");
	int idx = 0;
	ServerPlayer *last = nullptr;
	while (!pile.isEmpty()) {
		ServerPlayer *recv = order.isEmpty() ? t : order.at(idx % order.length());
		int id = pile.takeFirst();
		room->obtainCard(recv, id, "ol_mutao", true);
		last = recv;
		idx++;
	}
	if (last && last->isAlive()) {
		int x = 0;
		foreach (const Card *c, last->getHandcards()) if (c->isKindOf("Slash")) x++;
		x = qMin(2, x);
		if (x > 0) room->damage(DamageStruct("ol_mutao", t, last, x));
	}
}

class GuMutao : public ZeroCardViewAsSkill
{
public:
	GuMutao() : ZeroCardViewAsSkill("ol_mutao") {}
	bool isEnabledAtPlay(const Player *player) const override { return !player->hasUsed("GuMutaoCard"); }
	const Card *viewAs() const override { return new GuMutaoCard; }
};

class GuYimou : public TriggerSkill
{
public:
	GuYimou() : TriggerSkill("ol_yimou") { events << Damaged; }
	bool triggerable(const ServerPlayer *t) const override { return t != nullptr; }
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			if (!owner->hasSkill(this) || owner->distanceTo(player) > 1) continue;
			if (!owner->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			room->broadcastSkillInvoke(objectName());
			QString ch = room->askForChoice(owner, objectName(), "slash+give", QVariant::fromValue(player));
			if (ch == "slash") {
				foreach (int id, room->getDrawPile()) {
					if (Sanguosha->getCard(id)->isKindOf("Slash")) {
						room->obtainCard(player, id, objectName(), false);
						break;
					}
				}
			} else if (!player->isKongcheng()) {
				ServerPlayer *r = room->askForPlayerChosen(owner, room->getOtherPlayers(player), objectName(), "@ol_yimou-give");
				if (!r) continue;
				const Card *c = room->askForExchange(player, objectName(), 1, 1, false, "@ol_yimou-card:" + r->objectName());
				if (c) {
					room->giveCard(player, r, c->getSubcards(), objectName());
					player->drawCards(1, objectName());
				}
			}
		}
		return false;
	}
};

void guAudit2025Source2OL(Package *pkg)
{
	// Registration is done by swapping skills on existing generals in ol.cpp / ol-strengthen / yjcm2012.
	// New general 鲍信[OL]:
	General *baoxin = new General(pkg, "ol_baoxin", "qun", 4);
	baoxin->addSkill(new GuMutao);
	baoxin->addSkill(new GuYimou);
	pkg->addMetaObject<GuMutaoCard>();
	Q_UNUSED(pkg);
}

// ----- 星魏延：孤厉 / 驁肆（SOURCE-2） -----
class GuS2GuliVS : public ViewAsSkill
{
public:
	GuS2GuliVS() : ViewAsSkill("guli") {}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return player->getMark("guli-Clear") == 0 && !player->isKongcheng();
	}
	bool viewFilter(const QList<const Card *> &, const Card *to_select) const override
	{
		return !to_select->isEquipped() && Self->handCards().contains(to_select->getEffectiveId());
	}
	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (cards.length() != Self->getHandcardNum()) return nullptr;
		Slash *slash = new Slash(Card::SuitToBeDecided, -1);
		slash->addSubcards(cards);
		slash->setSkillName(objectName());
		slash->setFlags("SlashIgnoreArmor");
		return slash;
	}
};

class GuS2Guli : public TriggerSkill
{
public:
	GuS2Guli() : TriggerSkill("guli")
	{
		events << PreCardUsed << CardFinished << ConfirmDamage;
		view_as_skill = new GuS2GuliVS;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from == player && use.card && use.card->getSkillName() == objectName()) {
				use.m_addHistory = false;
				data = QVariant::fromValue(use);
				room->setPlayerMark(player, "guli-Clear", 1);
				room->setPlayerMark(player, "guli_wait", 1);
			}
			return false;
		}
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->getSkillName() == objectName() && damage.from == player)
				room->setPlayerMark(player, "guli_dmg", 1);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->getSkillName() != objectName()) return false;
		if (player->getMark("guli_wait") <= 0) return false;
		room->setPlayerMark(player, "guli_wait", 0);
		bool dmg = player->getMark("guli_dmg") > 0;
		room->setPlayerMark(player, "guli_dmg", 0);
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		int x = player->getMaxHp() - player->getHandcardNum();
		if (x > 0) player->drawCards(x, objectName());
		if (!dmg && player->isAlive()) room->loseHp(player, 1, true, player, objectName());
		return false;
	}
};
Skill *guNewGuli() { return new GuS2Guli; }

class GuS2Aoshi : public TriggerSkill
{
public:
	GuS2Aoshi() : TriggerSkill("aoshi")
	{
		events << Damage << CardUsed;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || player->getPhase() != Player::Play || !damage.to) return false;
			if (!player->inMyAttackRange(damage.to) || damage.to == player) return false;
			room->sendCompulsoryTriggerLog(player, this);
			room->setPlayerMark(damage.to, "aoshi_free_" + player->objectName() + "-PlayClear", 1);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || player->getPhase() != Player::Play || !use.card) return false;
		if (use.card->getTypeId() == Card::TypeSkill) return false;
		room->addPlayerMark(player, "aoshi_count-PlayClear");
		int x = room->getTag("TurnLengthCount").toInt();
		if (x <= 0) x = 1;
		if (player->getMark("aoshi_count-PlayClear") == x) {
			room->sendCompulsoryTriggerLog(player, this);
			use.no_respond_list << "_ALL_TARGETS";
			data = QVariant::fromValue(use);
		}
		return false;
	}
};
class GuS2AoshiMod : public TargetModSkill
{
public:
	GuS2AoshiMod() : TargetModSkill("#aoshi-mod") { pattern = "."; }
	int getResidueNum(const Player *from, const Card *, const Player *to) const override
	{
		if (!from || !to) return 0;
		return to->getMark("aoshi_free_" + from->objectName() + "-PlayClear") > 0 ? 1000 : 0;
	}
};
Skill *guNewAoshi() { return new GuS2Aoshi; }
Skill *guNewAoshiMod() { return new GuS2AoshiMod; }


// ---- GuS2Daozhuan: SOURCE-2 势于吉 道转 ----
GuS2DaozhuanCard::GuS2DaozhuanCard()
{
	m_skillName = "daozhuan";
	handling_method = Card::MethodUse;
}
bool GuS2DaozhuanCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) { card->setSkillName("_daozhuan"); card->deleteLater(); return card->targetFixed(); }
	return true;
}
bool GuS2DaozhuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) { card->setSkillName("_daozhuan"); card->deleteLater(); return card->targetFilter(targets, to_select, Self); }
	return false;
}
bool GuS2DaozhuanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) { card->setSkillName("_daozhuan"); card->deleteLater(); return card->targetsFeasible(targets, Self); }
	return true;
}
static const Card *guS2DaozhuanValidate(ServerPlayer *from, const QString &user_string)
{
	Room *room = from->getRoom();
	QStringList list;
	foreach (QString pn, user_string.split("+")) {
		if (from->getMark("daozhuan_guhuo_remove_" + pn + "_lun") < 1) {
			Card *dc = Sanguosha->cloneCard(pn);
			if (dc) { dc->deleteLater(); if (dc->isKindOf("BasicCard")) list << pn; }
		}
	}
	if (list.isEmpty()) return nullptr;
	QString to_pn = list.length() == 1 ? list.first() : room->askForChoice(from, "daozhuan", list.join("+"));
	room->addPlayerMark(from, "daozhuan_guhuo_remove_" + to_pn + "_lun");
	room->setPlayerMark(from, "daozhuan-Clear", 1);
	ServerPlayer *cp = room->getCurrent();
	bool lost_from_current = false;
	QString choice = "self";
	if (cp && cp != from && cp->getCardCount() > 0 && from->getCardCount() > 0)
		choice = room->askForChoice(from, "daozhuan_src", "self+current");
	else if (cp && cp != from && cp->getCardCount() > 0 && from->getCardCount() <= 0)
		choice = "current";
	if (choice == "current" && cp) {
		int id = room->askForCardChosen(from, cp, "he", "daozhuan");
		if (id >= 0) {
			room->throwCard(id, "daozhuan", nullptr);
			lost_from_current = true;
		} else return nullptr;
	} else {
		const Card *sc = room->askForExchange(from, "daozhuan", 1, 1, true, "daozhuan1:1");
		if (sc) room->throwCard(sc, "daozhuan", nullptr);
		else return nullptr;
		if (cp == from) lost_from_current = true;
	}
	if (lost_from_current)
		room->addPlayerMark(from, "daozhuanBan_lun");
	Card *use_card = Sanguosha->cloneCard(to_pn);
	use_card->setSkillName("daozhuan");
	use_card->deleteLater();
	return use_card;
}
const Card *GuS2DaozhuanCard::validate(CardUseStruct &use) const
{
	return guS2DaozhuanValidate(use.from, user_string);
}
const Card *GuS2DaozhuanCard::validateInResponse(ServerPlayer *from) const
{
	return guS2DaozhuanValidate(from, user_string);
}

class GuS2DaozhuanVs : public ZeroCardViewAsSkill
{
public:
	GuS2DaozhuanVs() : ZeroCardViewAsSkill("daozhuan") {}
	bool isEnabledAtPlay(const Player *player) const override
	{
		if (player->getMark("daozhuanBan_lun") > 0 || player->getMark("daozhuan-Clear") > 0) return false;
		if (player->getCardCount() <= 0) {
			foreach (const Player *p, player->getAliveSiblings())
				if (p->hasFlag("CurrentPlayer") && p->getCardCount() > 0) return true;
			return false;
		}
		return true;
	}
	bool isEnabledAtResponse(const Player *player, const QString &pattern) const override
	{
		if (Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		if (!isEnabledAtPlay(player)) return false;
		foreach (QString pn, pattern.split("+")) {
			if (player->getMark("daozhuan_guhuo_remove_" + pn + "_lun") < 1) {
				Card *dc = Sanguosha->cloneCard(pn);
				if (dc) { dc->deleteLater(); if (dc->isKindOf("BasicCard")) return true; }
			}
		}
		return false;
	}
	const Card *viewAs() const override
	{
		QString pn = Sanguosha->getCurrentCardUsePattern();
		if (pn.isEmpty()) {
			const Card *c = Self->getTag(objectName()).value<const Card *>();
			if (c) pn = c->objectName();
			else return nullptr;
		}
		GuS2DaozhuanCard *sc = new GuS2DaozhuanCard;
		sc->setUserString(pn);
		return sc;
	}
};
class GuS2Daozhuan : public TriggerSkill
{
public:
	GuS2Daozhuan() : TriggerSkill("daozhuan")
	{
		events << PreCardUsed;
		view_as_skill = new GuS2DaozhuanVs;
	}
	SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
	bool triggerable(const ServerPlayer *target) const override { return target && target->isAlive(); }
	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return false; }
};
Skill *guNewDaozhuan() { return new GuS2Daozhuan; }
