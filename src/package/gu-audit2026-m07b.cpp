// GU-audit2026 2026-07b: 李昭仪 / 段煨 / 势周瑜.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-07 (translations in lang/zh_CN/Package/Mobile.lua).
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"

#include <QSet>

// ---------------- 李昭仪 明节 ----------------
// The partner is kept as a tag on the owner and shown as a mark on the partner.
// "互获": cards the owner discards during the owner's discard phase go to the
// partner, and the other way round, if they are still in the discard pile.
// "一方阶段被跳过": either side has a phase skipped. Both draw 2, then the owner
// (the skill holder) may give the side that was not skipped a shield that
// prevents the next damage it would take.

static ServerPlayer *gu07bMingjiePartner(Room *room, ServerPlayer *owner)
{
	if (!room || !owner) return nullptr;
	const QString name = owner->getTag(QStringLiteral("gu_mingjie_partner")).toString();
	if (name.isEmpty()) return nullptr;
	return room->findPlayerByObjectName(name, true);
}

static void gu07bObtainFromDiscardPile(Room *room, ServerPlayer *to, const QList<int> &ids, const QString &skill)
{
	QList<int> get;
	foreach (int id, ids)
		if (room->getCardPlace(id) == Player::DiscardPile && !get.contains(id)) get << id;
	if (get.isEmpty() || !to || !to->isAlive()) return;
	DummyCard dummy(get);
	room->obtainCard(to, &dummy, skill, true);
}

class Gu07bMingjie : public TriggerSkill
{
public:
	Gu07bMingjie() : TriggerSkill("gu_mingjie")
	{
		events << GameStart << CardsMoveOneTime << EventPhaseSkipping << DamageInflicted << Death;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == GameStart) {
			if (!player->isAlive() || !player->hasSkill(this)) return false;
			if (!player->getTag(QStringLiteral("gu_mingjie_partner")).toString().isEmpty()) return false;
			QList<ServerPlayer *> others = room->getOtherPlayers(player);
			if (others.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, others, objectName(), "@gu_mingjie-choose", false, true);
			if (!target) return false;
			player->setTag(QStringLiteral("gu_mingjie_partner"), target->objectName());
			room->addPlayerMark(target, "@gu_mingjie");
			return false;
		}
		if (event == DamageInflicted) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.to != player || player->getMark("@gu_mingjie_fang") <= 0) return false;
			room->removePlayerMark(player, "@gu_mingjie_fang");
			LogMessage log;
			log.type = "#GuMingjieFang";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			return true;
		}
		if (event == Death) {
			const DeathStruct death = data.value<DeathStruct>();
			if (!death.who || death.who != player) return false;
			foreach (ServerPlayer *owner, room->getAlivePlayers()) {
				if (owner == player || !owner->hasSkill(this)) continue;
				if (owner->getTag(QStringLiteral("gu_mingjie_partner")).toString() != player->objectName()) continue;
				room->sendCompulsoryTriggerLog(owner, this);
				room->killPlayer(owner);
			}
			return false;
		}
		if (event == CardsMoveOneTime) {
			const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			ServerPlayer *from = qobject_cast<ServerPlayer *>(move.from);
			if (!from || from != player || from->getPhase() != Player::Discard) return false;
			if (move.to_place != Player::DiscardPile) return false;
			if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
				return false;
			// "弃置的牌" = cards that side discarded itself (not dismantled by someone else).
			if (!move.reason.m_playerId.isEmpty() && move.reason.m_playerId != from->objectName()) return false;
			QList<int> ids;
			for (int i = 0; i < move.card_ids.size(); ++i) {
				const Player::Place place = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
				if (place == Player::PlaceHand || place == Player::PlaceEquip) ids << move.card_ids.at(i);
			}
			if (ids.isEmpty()) return false;
			foreach (ServerPlayer *owner, room->getAlivePlayers()) {
				if (!owner->hasSkill(this)) continue;
				ServerPlayer *partner = gu07bMingjiePartner(room, owner);
				if (!partner || !partner->isAlive()) continue;
				ServerPlayer *receiver = nullptr;
				if (owner == from) receiver = partner;
				else if (partner == from) receiver = owner;
				if (!receiver) continue;
				room->sendCompulsoryTriggerLog(owner, this);
				gu07bObtainFromDiscardPile(room, receiver, ids, objectName());
			}
			return false;
		}
		// EventPhaseSkipping: must return false or the skip is cancelled.
		if (!player->isAlive()) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!owner->hasSkill(this)) continue;
			ServerPlayer *partner = gu07bMingjiePartner(room, owner);
			if (!partner || !partner->isAlive()) continue;
			ServerPlayer *other = nullptr;
			if (owner == player) other = partner;
			else if (partner == player) other = owner;
			if (!other) continue;
			room->sendCompulsoryTriggerLog(owner, this);
			owner->drawCards(2, objectName());
			if (partner->isAlive()) partner->drawCards(2, objectName());
			if (!owner->isAlive() || !other->isAlive()) continue;
			if (owner->askForSkillInvoke(objectName(), QVariant::fromValue(other)))
				room->addPlayerMark(other, "@gu_mingjie_fang");
		}
		return false;
	}
};

// ---------------- 李昭仪 娴辅 ----------------
// "成为牌目标时" = TargetConfirming of a non-skill card. "令此牌无效" nullifies the
// whole card (all targets). Each use queues one skip, consumed by the next phase of
// yours that is judge/draw/play/discard and is not already skipped. Then you view
// the 明节 partner's hand and may take up to 3, then the partner does the same to you.

static bool gu07bSkippablePhase(Player::Phase phase)
{
	return phase == Player::Judge || phase == Player::Draw || phase == Player::Play || phase == Player::Discard;
}

class Gu07bXianfu : public TriggerSkill
{
public:
	Gu07bXianfu() : TriggerSkill("gu_xianfu")
	{
		events << TargetConfirming << EventPhaseChanging;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		if (event == EventPhaseChanging) {
			if (player->getMark("@gu_xianfu_skip") <= 0) return false;
			const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (!gu07bSkippablePhase(change.to) || player->isSkipped(change.to)) return false;
			room->removePlayerMark(player, "@gu_xianfu_skip");
			player->skip(change.to);
			return false;
		}
		if (!player->hasSkill(this) || player->getMark("gu_xianfu_used-Clear") > 0) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.to.contains(player)) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		room->setPlayerMark(player, "gu_xianfu_used-Clear", 1);
		room->addPlayerMark(player, "@gu_xianfu_skip");
		if (!use.nullified_list.contains(QStringLiteral("_ALL_TARGETS")))
			use.nullified_list << QStringLiteral("_ALL_TARGETS");
		data = QVariant::fromValue(use);

		ServerPlayer *partner = gu07bMingjiePartner(room, player);
		if (!partner || !partner->isAlive() || partner == player) return false;
		if (player->isAlive() && !partner->isKongcheng()) {
			const QList<int> ids = room->askForCardsChosen(player, partner, "h", objectName(), 0, 3, true);
			if (!ids.isEmpty()) {
				DummyCard dummy(ids);
				room->obtainCard(player, &dummy, objectName(), false);
			}
		}
		if (partner->isAlive() && player->isAlive() && !player->isKongcheng()) {
			const QList<int> ids = room->askForCardsChosen(partner, player, "h", objectName(), 0, 3, true);
			if (!ids.isEmpty()) {
				DummyCard dummy(ids);
				room->obtainCard(partner, &dummy, objectName(), false);
			}
		}
		return false;
	}
};

// ---------------- 段煨 讨怀 ----------------
// Turn-over skill. Only a card wholly from your hand counts. 最大/最小 compares the
// used card's number with the hand cards you still hold (ties count). On a match you
// draw 1; otherwise you may discard 1 card. The state turns over whenever the skill
// takes effect, i.e. after the draw or after you choose to discard.

class Gu07bTaohuai : public TriggerSkill
{
public:
	Gu07bTaohuai() : TriggerSkill("gu_taohuai")
	{
		events << CardUsed;
		change_skill = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.m_isHandcard) return false;
		if (use.card->getTypeId() == Card::TypeSkill) return false;
		const QList<int> used = use.card->isVirtualCard() ? use.card->getSubcards() : QList<int>() << use.card->getId();
		if (used.isEmpty()) return false;
		const int state = player->getChangeSkillState(objectName());
		const int number = use.card->getNumber();
		bool match = true;
		foreach (const Card *card, player->getHandcards()) {
			if (!card || used.contains(card->getEffectiveId())) continue;
			if (state == 2 ? card->getNumber() < number : card->getNumber() > number) {
				match = false;
				break;
			}
		}
		if (match) {
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(1, objectName());
		} else {
			if (player->isNude()) return false;
			if (!room->askForDiscard(player, objectName(), 1, 1, true, true,
					QStringLiteral("@gu_taohuai-discard"), ".", objectName()))
				return false;
		}
		if (player->isAlive())
			room->setChangeSkillState(player, objectName(), state == 2 ? 1 : 2);
		return false;
	}
};

// ---------------- 势周瑜 雄姿 helpers ----------------
// After 雄姿: the general's skills (炽沄/焰洄/焚涛) only work in your own turn, and
// every "1./2." choice in them is fixed to the option picked here.

static bool gu07bXiongziBlocked(Room *room, ServerPlayer *owner)
{
	return owner && owner->getMark("gu_xiongzi_opt") > 0 && room->getCurrent() != owner;
}

// 0 = not fixed, 1 or 2 = the fixed option.
static int gu07bXiongziOption(ServerPlayer *owner)
{
	return owner ? owner->getMark("gu_xiongzi_opt") : 0;
}

// ---------------- 势周瑜 炽沄 ----------------
// "每阶段首次获得牌后": only the first gain of each phase is checked; the marker is
// cleared at every phase change. Giving is optional (not a 锁定技). "同色" = the
// colours of the cards you gave; the receiver shows its hand cards of those colours.

static bool gu07bGainToHand(const CardsMoveOneTimeStruct &move, ServerPlayer *player)
{
	if (!player || move.to != player || move.to_place != Player::PlaceHand) return false;
	for (int i = 0; i < move.card_ids.size(); ++i) {
		const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
		if (move.from == player && (from == Player::PlaceHand || from == Player::PlaceEquip
			|| from == Player::PlaceJudge || from == Player::PlaceDelayedTrick || from == Player::PlaceSpecial))
			continue;
		return true;
	}
	return false;
}

class Gu07bChiyun : public TriggerSkill
{
public:
	Gu07bChiyun() : TriggerSkill("gu_chiyun")
	{
		events << EventPhaseChanging << CardsMoveOneTime;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging) {
			foreach (ServerPlayer *p, room->getAllPlayers(true))
				if (p->getMark("gu_chiyun_got") > 0) room->setPlayerMark(p, "gu_chiyun_got", 0);
			return false;
		}
		if (!player || !player->isAlive() || !player->hasSkill(this)) return false;
		ServerPlayer *current = room->getCurrent();
		if (!current || current->getPhase() == Player::NotActive || current->getPhase() == Player::PhaseNone) return false;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!gu07bGainToHand(move, player) || player->getMark("gu_chiyun_got") > 0) return false;
		room->setPlayerMark(player, "gu_chiyun_got", 1);
		if (gu07bXiongziBlocked(room, player) || player->isKongcheng()) return false;
		QList<ServerPlayer *> others = room->getOtherPlayers(player);
		if (others.isEmpty()) return false;
		QList<int> hand = player->handCards();
		const CardsMoveStruct given = room->askForYijiStruct(player, hand, objectName(), false, false, true, -1,
			others, CardMoveReason(), QStringLiteral("@gu_chiyun-give"), false, true);
		if (!given.to || given.card_ids.isEmpty()) return false;
		ServerPlayer *target = room->findPlayerByObjectName(given.to->objectName());
		room->notifySkillInvoked(player, objectName());
		if (!target || !target->isAlive()) return false;
		QSet<int> colors;
		foreach (int id, given.card_ids) {
			const Card *card = Sanguosha->getCard(id);
			if (card) colors.insert(int(card->getColor()));
		}
		QString choice;
		const int fixed = gu07bXiongziOption(player);
		if (fixed == 1) choice = QStringLiteral("show");
		else if (fixed == 2) choice = QStringLiteral("chain");
		else choice = room->askForChoice(target, objectName(), QStringLiteral("show+chain"), QVariant::fromValue(player));
		if (choice == QLatin1String("show")) {
			QList<int> show;
			foreach (const Card *card, target->getHandcards())
				if (card && colors.contains(int(card->getColor()))) show << card->getEffectiveId();
			if (!show.isEmpty()) room->showCard(target, show);
			if (player->isAlive() && target->isAlive())
				room->damage(DamageStruct(objectName(), player, target, 1, DamageStruct::Fire));
		} else {
			if (player->isAlive()) player->drawCards(2, objectName());
			if (target->isAlive() && !target->isChained()) room->setPlayerChained(target, true, player);
		}
		return false;
	}
};

// ---------------- 势周瑜 焰洄 ----------------
// Per target of your card (TargetSpecified, each target with hand cards in turn) you
// may pick one of its hand cards and show it; if that card was already shown this turn
// (any public show, tracked from ShowCards) it is discarded. X counts characters whose
// cards were shown publicly this turn. At the end of every phase of the turn you choose
// 1 or 2 (options without effect are not offered; you may decline).

static QStringList gu07bShownIds(Room *room)
{
	return room->getTag(QStringLiteral("gu_yanhui_shown")).toStringList();
}

class Gu07bYanhui : public TriggerSkill
{
public:
	Gu07bYanhui() : TriggerSkill("gu_yanhui")
	{
		events << ShowCards << EventPhaseChanging << TargetSpecified << EventPhaseEnd;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == EventPhaseChanging) {
			const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive || change.to == Player::RoundStart)
				room->removeTag(QStringLiteral("gu_yanhui_shown"));
			return false;
		}
		if (room->findPlayersBySkillName(objectName()).isEmpty()) return false;
		if (event == ShowCards) {
			if (!room->getCurrent()) return false;
			QStringList shown = gu07bShownIds(room);
			bool any = false;
			foreach (const QString &s, data.toString().split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
				bool ok = false;
				s.toInt(&ok);
				if (!ok) continue;
				any = true;
				if (!shown.contains(s)) shown << s;
			}
			if (!any) return false;
			room->setTag(QStringLiteral("gu_yanhui_shown"), shown);
			room->setPlayerMark(player, "gu_yanhui_shown-Clear", 1);
			return false;
		}
		if (event == TargetSpecified) {
			if (!player->isAlive() || !player->hasSkill(this) || gu07bXiongziBlocked(room, player)) return false;
			const CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
			QList<ServerPlayer *> done;
			foreach (ServerPlayer *target, use.to) {
				if (!player->isAlive()) break;
				if (!target || done.contains(target) || !target->isAlive() || target->isKongcheng()) continue;
				done << target;
				if (!player->askForSkillInvoke(this, QVariant::fromValue(target))) continue;
				const int id = room->askForCardChosen(player, target, "h", objectName());
				if (id < 0) continue;
				const bool before = gu07bShownIds(room).contains(QString::number(id));
				room->showCard(target, id);
				if (before && target->isAlive() && room->getCardOwner(id) == target
					&& room->getCardPlace(id) == Player::PlaceHand && player->canDiscard(target, id)) {
					room->throwCard(id, objectName(), target, player);
					room->setPlayerMark(target, "gu_yanhui_lost-Clear", 1);
				}
			}
			return false;
		}
		// EventPhaseEnd of the current character.
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!owner->hasSkill(this) || gu07bXiongziBlocked(room, owner)) continue;
			QList<ServerPlayer *> victims;
			int x = 0;
			foreach (ServerPlayer *p, room->getAllPlayers(true)) {
				if (p->getMark("gu_yanhui_shown-Clear") > 0) ++x;
				if (p->isAlive() && p->getMark("gu_yanhui_lost-Clear") > 0) victims << p;
			}
			const int fixed = gu07bXiongziOption(owner);
			QStringList choices;
			if (!victims.isEmpty() && fixed != 2) choices << QStringLiteral("damage");
			if (x > 0 && fixed != 1) choices << QStringLiteral("draw");
			if (choices.isEmpty()) continue;
			choices << QStringLiteral("cancel");
			const QString choice = room->askForChoice(owner, objectName(), choices.join(QLatin1Char('+')),
				QVariant::fromValue(x));
			if (choice == QLatin1String("damage")) {
				ServerPlayer *to = room->askForPlayerChosen(owner, victims, objectName(), "@gu_yanhui-damage", false, true);
				if (to && to->isAlive())
					room->damage(DamageStruct(objectName(), owner, to, 1, DamageStruct::Fire));
			} else if (choice == QLatin1String("draw")) {
				room->notifySkillInvoked(owner, objectName());
				owner->drawCards(x, objectName());
			}
		}
		return false;
	}
};

// ---------------- 势周瑜 焚涛 ----------------
// 锁定技. Any chained character taking fire damage (DamageInflicted): you draw 1, then
// that character chooses. 1: every damage conducted by the iron chain from this damage
// gets +1 (no effect if this damage is itself a conducted one). 2: it discards half of
// its cards (hand+equip, rounded up) and is chained again once this damage completes.

class Gu07bFentao : public TriggerSkill
{
public:
	Gu07bFentao() : TriggerSkill("gu_fentao")
	{
		events << DamageInflicted << DamageComplete;
		frequency = Compulsory;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer *victim = damage.to;
		if (!victim) return false;
		const QString rechain = QStringLiteral("gu_fentao_rechain_") + victim->objectName();
		if (event == DamageComplete) {
			if (!damage.tips.contains(rechain)) return false;
			damage.tips.removeAll(rechain);
			data = QVariant::fromValue(damage);
			if (victim->isAlive() && !victim->isChained()) room->setPlayerChained(victim, true);
			return false;
		}
		if (damage.chain && damage.tips.contains(QStringLiteral("gu_fentao_plus"))) {
			damage.tips.removeAll(QStringLiteral("gu_fentao_plus"));
			damage.damage += 1;
			LogMessage log;
			log.type = "#GuFentaoPlus";
			log.from = victim;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			data = QVariant::fromValue(damage);
		}
		if (damage.nature != DamageStruct::Fire || !victim->isAlive() || !victim->isChained()) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!owner->hasSkill(this) || gu07bXiongziBlocked(room, owner)) continue;
			if (!victim->isAlive()) break;
			room->sendCompulsoryTriggerLog(owner, this);
			owner->drawCards(1, objectName());
			if (!victim->isAlive()) break;
			const int fixed = gu07bXiongziOption(owner);
			QString choice;
			if (fixed == 1) choice = QStringLiteral("conduct");
			else if (fixed == 2) choice = QStringLiteral("discard");
			else choice = room->askForChoice(victim, objectName(), QStringLiteral("conduct+discard"), data);
			if (choice == QLatin1String("conduct")) {
				if (!damage.tips.contains(QStringLiteral("gu_fentao_plus")))
					damage.tips << QStringLiteral("gu_fentao_plus");
			} else {
				const int n = (victim->getCardCount(true) + 1) / 2;
				if (n > 0)
					room->askForDiscard(victim, objectName(), n, n, false, true,
						QStringLiteral("@gu_fentao-discard:::%1").arg(n));
				if (!damage.tips.contains(rechain)) damage.tips << rechain;
			}
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

// ---------------- 势周瑜 雄姿 ----------------

class Gu07bXiongzi : public PhaseChangeSkill
{
public:
	Gu07bXiongzi() : PhaseChangeSkill("gu_xiongzi")
	{
		frequency = Limited;
		limit_mark = "@gu_xiongzi";
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const override
	{
		if (player->getPhase() != Player::Start || player->getMark("@gu_xiongzi") <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->doSuperLightbox(player, objectName());
		room->removePlayerMark(player, "@gu_xiongzi");
		const QString choice = room->askForChoice(player, objectName(), QStringLiteral("one+two"));
		const int opt = choice == QLatin1String("two") ? 2 : 1;
		room->setPlayerMark(player, "gu_xiongzi_opt", opt);
		room->setPlayerMark(player, opt == 2 ? "@gu_xiongzi_two" : "@gu_xiongzi_one", 1);
		player->drawCards(2, objectName());
		return false;
	}
};

void guAudit2026M07b(Package *pkg)
{
	General *lizhaoyi = new General(pkg, "gu_lizhaoyi", "shu", 3, false);
	lizhaoyi->addSkill(new Gu07bMingjie);
	lizhaoyi->addSkill(new Gu07bXianfu);

	General *duanwei = new General(pkg, "gu_duanwei", "qun", 4, true);
	duanwei->addSkill(new Gu07bTaohuai);

	General *zhouyu = new General(pkg, "gu_shizhouyu", "wu", 4, true);
	zhouyu->addSkill(new Gu07bChiyun);
	zhouyu->addSkill(new Gu07bYanhui);
	zhouyu->addSkill(new Gu07bFentao);
	zhouyu->addSkill(new Gu07bXiongzi);
}
