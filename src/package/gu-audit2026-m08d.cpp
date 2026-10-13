// GU-audit2026 2026-08d: 界严颜（手杀） / 崔琰毛玠（手杀） / 界王基（手杀）.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-08 (translations in lang/zh_CN/Package/Mobile.lua).
// The repo yanyan / th_cuiyuanmaojie / wangji are other servers' versions and stay untouched.
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"

static int gu08dChargeMax(const Player *player)
{
	int n = 0;
	if (!player) return 0;
	foreach (const Skill *s, player->getVisibleSkillList()) {
		const QString cn = s->property("ChargeNum").toString();
		if (cn.contains("/")) n += cn.split("/").last().toInt();
	}
	return n;
}

static QList<int> gu08dPhysicalIds(const Card *card)
{
	QList<int> ids;
	if (!card) return ids;
	if (card->isVirtualCard()) ids = card->getSubcards();
	else if (card->getEffectiveId() >= 0) ids << card->getEffectiveId();
	return ids;
}

// ---------------- 界严颜 拒战 ----------------
// Literal reading of the card text:
//  * 阴阳 (conversion) skill; it switches state each time it is used, and the
//    "此状态下" counter restarts on every switch.
//  * 阳: triggers when you become a 【杀】 target, or when (in this state) a
//    character becomes a 【杀】 target for the third time. You and the user
//    each draw one; the user cannot use cards on you this turn.
//  * 阴: triggers when (in this state) a 【杀】 is used to specify targets for
//    the third time. You obtain one card of one of its targets (other than
//    you); you cannot use cards on that target this turn.
//  * 乘势 = both listed conditions hold at once (阳: you are the third target).
//    The 阴 text lists only one condition, so 乘势 never applies in 阴.
//  * Initial state 阴 when the kingdom chosen at game start is 蜀.

static const char *gu08dJuzhanCount = "gu_juzhan_count";

static QString gu08dJuzhanBan(const Player *to)
{
	return QString("gu_juzhan_ban_%1-Clear").arg(to ? to->objectName() : QString());
}

class Gu08dJuzhan : public TriggerSkill
{
public:
	Gu08dJuzhan() : TriggerSkill("gu_juzhan")
	{
		events << GameStart << TargetConfirmed << TargetSpecified;
		change_skill = true;
		global = true;
	}

	static void switchState(Room *room, ServerPlayer *owner, int to)
	{
		room->setPlayerMark(owner, gu08dJuzhanCount, 0);
		room->setChangeSkillState(owner, "gu_juzhan", to);
	}

	void chengshi(Room *room, ServerPlayer *owner) const
	{
		if (!owner->isAlive()) return;
		const Card *slash = room->askForUseCard(owner, "slash", "@gu_juzhan-slash", -1, Card::MethodUse, false);
		if (!slash || !owner->isAlive()) return;
		QList<int> ids;
		foreach (int id, gu08dPhysicalIds(slash))
			if (room->getCardPlace(id) == Player::DiscardPile) ids << id;
		if (ids.isEmpty()) return;
		DummyCard dummy(ids);
		room->obtainCard(owner, &dummy, objectName());
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == GameStart) {
			if (player->isAlive() && player->hasSkill(this) && player->getKingdom() == "shu")
				switchState(room, player, 2);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash") || use.to.isEmpty()) return false;
		if (event == TargetConfirmed) {
			if (!use.to.contains(player)) return false;
			foreach (ServerPlayer *owner, room->getAlivePlayers()) {
				if (!owner->hasSkill(this) || owner->getChangeSkillState(objectName()) != 1) continue;
				room->addPlayerMark(owner, gu08dJuzhanCount);
				const bool third = owner->getMark(gu08dJuzhanCount) == 3;
				const bool self = owner == player;
				if (!third && !self) continue;
				if (!owner->askForSkillInvoke(this, QVariant::fromValue(use.from))) continue;
				room->broadcastSkillInvoke(objectName());
				switchState(room, owner, 2);
				QList<ServerPlayer *> drawers;
				drawers << owner;
				if (use.from && use.from != owner && use.from->isAlive()) drawers << use.from;
				room->sortByActionOrder(drawers);
				foreach (ServerPlayer *p, drawers)
					if (p->isAlive()) p->drawCards(1, objectName());
				if (use.from && use.from != owner)
					room->setPlayerMark(use.from, gu08dJuzhanBan(owner), 1);
				if (self && third) chengshi(room, owner);
			}
			return false;
		}
		// TargetSpecified: player is the user of the 【杀】.
		if (use.from != player) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!owner->hasSkill(this) || owner->getChangeSkillState(objectName()) != 2) continue;
			room->addPlayerMark(owner, gu08dJuzhanCount);
			if (owner->getMark(gu08dJuzhanCount) != 3) continue;
			QList<ServerPlayer *> victims;
			foreach (ServerPlayer *p, use.to)
				if (p != owner && p->isAlive() && !p->isNude() && !victims.contains(p)) victims << p;
			if (victims.isEmpty()) continue;
			ServerPlayer *victim = room->askForPlayerChosen(owner, victims, objectName(), "@gu_juzhan-obtain", true, true);
			if (!victim) continue;
			room->broadcastSkillInvoke(objectName());
			switchState(room, owner, 1);
			const int id = room->askForCardChosen(owner, victim, "he", objectName());
			if (id >= 0)
				room->obtainCard(owner, Sanguosha->getCard(id),
					CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, owner->objectName()),
					room->getCardPlace(id) != Player::PlaceHand);
			room->setPlayerMark(owner, gu08dJuzhanBan(victim), 1);
		}
		return false;
	}
};

class Gu08dJuzhanPro : public ProhibitSkill
{
public:
	Gu08dJuzhanPro() : ProhibitSkill("#gu_juzhan-pro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		return from && to && card && card->getTypeId() != Card::TypeSkill
			&& from->getMark(gu08dJuzhanBan(to)) > 0;
	}
};

// ---------------- 崔琰毛玠 征辟 ----------------
// Prepare phase: choose up to N other characters (N = 1 + times a target picked
// three), each character at most once per game. Each target picks 1..3; you
// obtain that many cards of its area (you choose them), then give it back the
// same number of your cards.

class Gu08dZhengpi : public TriggerSkill
{
public:
	Gu08dZhengpi() : TriggerSkill("gu_zhengpi")
	{
		events << EventPhaseStart;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::Start) return false;
		const QString used = "gu_zhengpi_used_" + player->objectName();
		QList<ServerPlayer *> candidates;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (p->getMark(used) < 1 && p->getCardCount(true, true) > 0) candidates << p;
		if (candidates.isEmpty()) return false;
		const int max = 1 + player->getMark("gu_zhengpi_extra");
		QList<ServerPlayer *> targets = room->askForPlayersChosen(player, candidates, objectName(), 0, max,
			"@gu_zhengpi:::" + QString::number(max), true);
		if (targets.isEmpty()) return false;
		room->broadcastSkillInvoke(objectName());
		foreach (ServerPlayer *t, targets) room->setPlayerMark(t, used, 1);
		foreach (ServerPlayer *t, targets) {
			if (!player->isAlive()) break;
			if (!t->isAlive()) continue;
			const int cap = qMin(3, t->getCardCount(true, true));
			if (cap < 1) continue;
			QStringList choices;
			for (int i = 1; i <= cap; ++i) choices << QString::number(i);
			const int n = room->askForChoice(t, objectName(), choices.join("+"), QVariant::fromValue(player)).toInt();
			if (n == 3) room->addPlayerMark(player, "gu_zhengpi_extra");
			QList<int> got;
			for (int i = 0; i < n && player->isAlive() && t->isAlive(); ++i) {
				if (t->getCardCount(true, true) < 1) break;
				const int id = room->askForCardChosen(player, t, "hej", objectName());
				if (id < 0) break;
				room->obtainCard(player, Sanguosha->getCard(id),
					CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, player->objectName()),
					room->getCardPlace(id) != Player::PlaceHand);
				got << id;
			}
			const int back = qMin(got.length(), player->getCardCount());
			if (back < 1 || !player->isAlive() || !t->isAlive()) continue;
			const Card *give = room->askForExchange(player, objectName(), back, back, true,
				"@gu_zhengpi-give:" + t->objectName() + "::" + QString::number(back));
			if (!give) continue;
			room->obtainCard(t, give,
				CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName(), t->objectName(), objectName(), ""),
				false);
		}
		return false;
	}
};

// ---------------- 崔琰毛玠 奉迎 ----------------
// The text gives no timing; as an active 限定技 it is used in the play phase.
// The target may be any character. Every character in its attack range that has
// a hand card uses one hand card as 【无中生有】 targeting it. If its max HP is
// the greatest (ties included), you take an extra turn after this turn.

class Gu08dFengyingCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu08dFengyingCard()
	{
		mute = true;
		setSkillName("gu_fengying");
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const override
	{
		return targets.isEmpty();
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		room->doSuperLightbox(source, "gu_fengying");
		room->removePlayerMark(source, "@gu_fengying");
		ServerPlayer *target = targets.value(0);
		if (!target) return;
		QList<ServerPlayer *> users;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (p != target && target->inMyAttackRange(p)) users << p;
		foreach (ServerPlayer *p, users) {
			if (!target->isAlive()) break;
			if (!p->isAlive() || p->isKongcheng()) continue;
			const Card *hand = room->askForExchange(p, "gu_fengying", 1, 1, false,
				"@gu_fengying-use:" + target->objectName());
			if (!hand || hand->subcardsLength() < 1) continue;
			Card *exn = Sanguosha->cloneCard("ex_nihilo");
			exn->addSubcard(hand->getSubcards().first());
			exn->setSkillName("_gu_fengying");
			if (!p->isCardLimited(exn, Card::MethodUse) && !p->isProhibited(target, exn))
				room->useCard(CardUseStruct(exn, p, target), false);
			exn->deleteLater();
		}
		if (!source->isAlive() || !target->isAlive()) return;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (p->getMaxHp() > target->getMaxHp()) return;
		room->scheduleExtraTurn(source, "gu_fengying");
	}
};

class Gu08dFengying : public ZeroCardViewAsSkill
{
public:
	Gu08dFengying() : ZeroCardViewAsSkill("gu_fengying")
	{
		frequency = Limited;
		limit_mark = "@gu_fengying";
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->getMark("@gu_fengying") > 0;
	}

	const Card *viewAs() const override
	{
		return new Gu08dFengyingCard;
	}
};

// ---------------- 界王基 奇制 ----------------
// 蓄力技（1/2）. Play phase, each character once per phase: spend 1 charge, it
// draws two. Afterwards in this phase, each time you use a basic card or an
// ordinary trick and specify targets, view the hand of every character that drew
// this way (seat order) and discard one card of it.

static QString gu08dQizhiMark(const Player *source)
{
	return QString("gu_qizhi_%1-PlayClear").arg(source ? source->objectName() : QString());
}

class Gu08dQizhiCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu08dQizhiCard()
	{
		setSkillName("gu_qizhi");
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		return targets.isEmpty() && to_select && to_select->getMark(gu08dQizhiMark(Self)) < 1;
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		ServerPlayer *target = targets.value(0);
		if (!target || source->getMark("&charge_num") < 1) return;
		source->loseMark("&charge_num");
		room->setPlayerMark(target, gu08dQizhiMark(source), 1);
		target->drawCards(2, "gu_qizhi");
	}
};

class Gu08dQizhiVS : public ZeroCardViewAsSkill
{
public:
	Gu08dQizhiVS() : ZeroCardViewAsSkill("gu_qizhi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->getMark("&charge_num") > 0;
	}

	const Card *viewAs() const override
	{
		return new Gu08dQizhiCard;
	}
};

class Gu08dQizhi : public TriggerSkill
{
public:
	Gu08dQizhi() : TriggerSkill("gu_qizhi")
	{
		events << TargetSpecified;
		view_as_skill = new Gu08dQizhiVS;
		setProperty("ChargeNum", "1/2");
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (player->getPhase() != Player::Play) return false;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !(use.card->isKindOf("BasicCard") || use.card->isNDTrick())) return false;
		bool logged = false;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (!player->isAlive()) break;
			if (p->getMark(gu08dQizhiMark(player)) < 1 || !player->canDiscard(p, "h")) continue;
			if (!logged) {
				room->sendCompulsoryTriggerLog(player, this);
				logged = true;
			}
			const int id = room->askForCardChosen(player, p, "h", objectName(), true, Card::MethodDiscard);
			if (id >= 0 && player->canDiscard(p, id)) room->throwCard(id, objectName(), p, player);
		}
		return false;
	}
};

// ---------------- 界王基 进趋 ----------------
// 锁定技. In your turn, every second basic card / ordinary trick you use: choose
// a character, it gains 1 charge (capped by its 蓄力技 maximum). Finish phase:
// draw X, X = the highest charge value you had during this turn.

class Gu08dJinqu : public TriggerSkill
{
public:
	Gu08dJinqu() : TriggerSkill("gu_jinqu")
	{
		events << CardUsed << EventPhaseStart << MarkChanged;
		frequency = Compulsory;
	}

	static void noteMax(Room *room, ServerPlayer *player)
	{
		const int now = player->getMark("&charge_num");
		if (now > player->getMark("gu_jinqu_max-Clear")) room->setPlayerMark(player, "gu_jinqu_max-Clear", now);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const bool myTurn = room->getCurrent() == player && player->getPhase() != Player::NotActive;
		if (event == MarkChanged) {
			if (myTurn && data.value<MarkStruct>().name == "&charge_num") noteMax(room, player);
			return false;
		}
		if (event == EventPhaseStart) {
			if (player->getPhase() == Player::RoundStart) {
				noteMax(room, player);
			} else if (player->getPhase() == Player::Finish) {
				noteMax(room, player);
				const int x = player->getMark("gu_jinqu_max-Clear");
				if (x > 0) {
					room->sendCompulsoryTriggerLog(player, this);
					player->drawCards(x, objectName());
				}
			}
			return false;
		}
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!myTurn || use.from != player || !use.card || !(use.card->isKindOf("BasicCard") || use.card->isNDTrick()))
			return false;
		room->addPlayerMark(player, "gu_jinqu_count-Clear");
		if (player->getMark("gu_jinqu_count-Clear") % 2 != 0) return false;
		room->sendCompulsoryTriggerLog(player, this);
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@gu_jinqu");
		if (!target) return false;
		room->doAnimate(1, player->objectName(), target->objectName());
		if (target->getMark("&charge_num") < gu08dChargeMax(target)) target->gainMark("&charge_num");
		return false;
	}
};

void guAudit2026M08d(Package *pkg)
{
	pkg->addMetaObject<Gu08dFengyingCard>();
	pkg->addMetaObject<Gu08dQizhiCard>();

	General *yanyan = new General(pkg, "gu_jieyanyan", "qun+shu", 4, true);
	yanyan->addSkill(new Gu08dJuzhan);
	pkg->addSkills(new Gu08dJuzhanPro);
	pkg->insertRelatedSkills("gu_juzhan", "#gu_juzhan-pro");

	General *cuimao = new General(pkg, "gu_cuiyanmaojie", "wei", 3, true);
	cuimao->addSkill(new Gu08dZhengpi);
	cuimao->addSkill(new Gu08dFengying);

	General *wangji = new General(pkg, "gu_jiewangji", "wei", 3, true);
	wangji->addSkill(new Gu08dQizhi);
	wangji->addSkill(new Gu08dJinqu);
}

#include "gu-audit2026-m08d.moc"
