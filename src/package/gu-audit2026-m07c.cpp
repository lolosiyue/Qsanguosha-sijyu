// GU-audit2026 2026-07c: 界周妃 / 幻大乔 / 疑伏寿.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-07 (translations in lang/zh_CN/Package/Mobile.lua).
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "wrapped-card.h"

#include <QHash>
#include <QSet>
#include <memory>

// 除外区 = a named special pile on a general (PlaceSpecial, name not starting with '#'),
// the same zone the Lei 周妃 良姻 calls 移出游戏. '#' piles are client-only views.
static bool gu07cIsExilePile(const QString &name)
{
	return !name.isEmpty() && !name.startsWith(QLatin1Char('#'));
}

// ---------------- 界周妃 良姻 ----------------
// No 「可」 in the text: each half resolves whenever its timing happens and the owner must
// pick a character. Every character picked by either half this round counts as a target.

static QString gu07cLiangyinMark(const ServerPlayer *owner)
{
	return QStringLiteral("gu_liangyin_") + owner->objectName() + QStringLiteral("_lun");
}

class Gu07cLiangyin : public TriggerSkill
{
public:
	Gu07cLiangyin() : TriggerSkill("gu_liangyin")
	{
		events << CardsMoveOneTime << RoundEnd;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const QString mark = gu07cLiangyinMark(player);
		if (event == RoundEnd) {
			QList<ServerPlayer *> hit, miss;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getMark(mark) > 0) hit << p;
				else miss << p;
			}
			QStringList choices;
			if (hit.length() == 1) choices << QStringLiteral("recover");
			if (miss.length() == 1) choices << QStringLiteral("lose");
			if (choices.isEmpty()) return false;
			player->setTag("GuLiangyinRecover", hit.length() == 1 ? hit.first()->objectName() : QString());
			player->setTag("GuLiangyinLose", miss.length() == 1 ? miss.first()->objectName() : QString());
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			const QString choice = room->askForChoice(player, objectName(), choices.join(QLatin1Char('+')));
			player->removeTag("GuLiangyinRecover");
			player->removeTag("GuLiangyinLose");
			if (choice == QLatin1String("recover")) {
				ServerPlayer *to = hit.first();
				if (to->isAlive()) room->recover(to, RecoverStruct(objectName(), player));
			} else if (choice == QLatin1String("lose")) {
				ServerPlayer *to = miss.first();
				if (to->isAlive()) room->loseHp(to, 1, true, player, objectName());
			}
			return false;
		}

		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		bool exiled = false, regained = false;
		if (move.to_place == Player::PlaceSpecial && gu07cIsExilePile(move.to_pile_name)) {
			for (int i = 0; i < move.card_ids.length(); ++i) {
				if (i < move.from_places.length() && move.from_places.at(i) == Player::PlaceSpecial) continue;
				exiled = true;
				break;
			}
		} else if (move.to && move.to_place == Player::PlaceHand) {
			for (int i = 0; i < move.card_ids.length(); ++i) {
				if (i >= move.from_places.length() || move.from_places.at(i) != Player::PlaceSpecial) continue;
				if (i < move.from_pile_names.length() && !gu07cIsExilePile(move.from_pile_names.at(i))) continue;
				regained = true;
				break;
			}
		}
		if (!exiled && !regained) return false;

		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (exiled || p->canDiscard(p, "he")) targets << p;
		}
		if (targets.isEmpty()) return false;
		player->setTag("GuLiangyinMode", exiled ? QStringLiteral("draw") : QStringLiteral("discard"));
		ServerPlayer *to = room->askForPlayerChosen(player, targets, objectName(),
			exiled ? "@gu_liangyin-draw" : "@gu_liangyin-discard", false, true);
		player->removeTag("GuLiangyinMode");
		if (!to) return false;
		room->broadcastSkillInvoke(objectName());
		room->setPlayerMark(to, mark, 1);
		if (exiled) to->drawCards(1, objectName());
		else room->askForDiscard(to, objectName(), 1, 1, false, true);
		return false;
	}
};

// ---------------- 界周妃 箜声 ----------------
// 准备阶段 = your own. The cards go face up to the current character's 除外区 (pile
// gu_kongsheng). At that character's next 结束阶段开始时 each source may use one of the
// cards it exiled, then the character obtains the rest.

class Gu07cKongshengVS : public OneCardViewAsSkill
{
public:
	Gu07cKongshengVS() : OneCardViewAsSkill("gu_kongsheng")
	{
		response_pattern = "@@gu_kongsheng";
		expand_pile = "#gu_kongsheng";
	}

	bool isEnabledAtPlay(const Player *) const override
	{
		return false;
	}

	bool viewFilter(const Card *to_select) const override
	{
		return Self && to_select && Self->getPile("#gu_kongsheng").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const Card *originalCard) const override
	{
		return originalCard;
	}
};

class Gu07cKongsheng : public TriggerSkill
{
public:
	Gu07cKongsheng() : TriggerSkill("gu_kongsheng")
	{
		events << EventPhaseStart << HpLost << HpRecover;
		view_as_skill = new Gu07cKongshengVS;
		global = true;
	}

	void exile(Room *room, ServerPlayer *owner, ServerPlayer *current) const
	{
		if (!owner->isAlive() || !current || !current->isAlive() || current->isNude()) return;
		if (!owner->askForSkillInvoke(this, QVariant::fromValue(current))) return;
		QList<int> ids;
		if (current == owner) {
			const Card *card = room->askForExchange(owner, objectName(), owner->getCardCount(true), 1, true,
				"@gu_kongsheng-self", true);
			if (card) ids = card->getSubcards();
		} else {
			ids = room->askForCardsChosen(owner, current, "he", objectName(), 1, current->getCardCount(true));
		}
		if (ids.isEmpty() || !current->isAlive()) return;
		room->broadcastSkillInvoke(objectName());
		QVariantMap src = current->getTag("GuKongshengSrc").toMap();
		QVariantList mine = src.value(owner->objectName()).toList();
		foreach (int id, ids) mine << id;
		src.insert(owner->objectName(), mine);
		current->setTag("GuKongshengSrc", src);
		current->addToPile(objectName(), ids, true);
	}

	void settle(Room *room, ServerPlayer *player) const
	{
		const QVariantMap src = player->getTag("GuKongshengSrc").toMap();
		if (src.isEmpty()) return;
		player->removeTag("GuKongshengSrc");
		foreach (ServerPlayer *owner, room->getAllPlayers(true)) {
			if (!src.contains(owner->objectName())) continue;
			QList<int> ids;
			foreach (const QVariant &v, src.value(owner->objectName()).toList()) {
				const int id = v.toInt();
				if (player->getPile(objectName()).contains(id)) ids << id;
			}
			if (ids.isEmpty()) continue;
			if (owner->isAlive() && player->isAlive()) {
				room->notifyMoveToPile(owner, ids, objectName(), Player::PlaceSpecial, true);
				room->askForUseCard(owner, "@@gu_kongsheng", "@gu_kongsheng-use");
				room->notifyMoveToPile(owner, ids, objectName(), Player::PlaceSpecial, false);
			}
			QList<int> rest;
			foreach (int id, ids)
				if (player->getPile(objectName()).contains(id)) rest << id;
			if (rest.isEmpty() || !player->isAlive()) continue;
			DummyCard dummy(rest);
			room->obtainCard(player, &dummy, objectName(), true);
		}
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player) return false;
		if (event == EventPhaseStart) {
			if (player->getPhase() == Player::Finish && player->isAlive()) {
				settle(room, player);
				return false;
			}
			if (player->getPhase() == Player::Start && player->isAlive() && player->hasSkill(this))
				exile(room, player, player);
			return false;
		}
		if (!player->isAlive() || !player->hasSkill(this)) return false;
		ServerPlayer *current = room->getCurrent();
		if (!current || !current->isAlive() || current->getPhase() == Player::NotActive) return false;
		exile(room, player, current);
		return false;
	}
};

// ---------------- 幻大乔 帼色 ----------------
// 受伤状态 = isWounded(). Each character's last seen state is kept in a server tag,
// seeded at game start and at every phase start, and compared on every Hp/MaxHp change.
// 【乐】判定生效后 = any 【乐不思蜀】 whose judgement takes effect on the current character:
// its play-phase skip is cancelled and the discard phase is skipped instead.

static int gu07cWoundState(const ServerPlayer *p)
{
	return p->isWounded() ? 1 : 2;
}

class Gu07cGuose : public TriggerSkill
{
public:
	Gu07cGuose() : TriggerSkill("gu_guose")
	{
		events << GameStart << EventPhaseStart << HpChanged << MaxHpChanged << FinishJudge << EventPhaseSkipping;
		global = true;
	}

	static void seed(Room *room)
	{
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (!p->getTag("GuGuoseWound").isValid()) p->setTag("GuGuoseWound", gu07cWoundState(p));
	}

	void place(Room *room, ServerPlayer *owner, ServerPlayer *target) const
	{
		if (!owner->isAlive() || !target->isAlive() || !target->hasJudgeArea() || target->containsTrick("indulgence"))
			return;
		const bool mine = !owner->isNude();
		const bool theirs = target != owner && !target->isNude();
		if (!mine && !theirs) return;
		if (!owner->askForSkillInvoke(this, QVariant::fromValue(target))) return;
		int id = -1;
		QString pick = mine ? QStringLiteral("self") : QStringLiteral("target");
		if (mine && theirs)
			pick = room->askForChoice(owner, objectName(), QStringLiteral("self+target"), QVariant::fromValue(target));
		if (pick == QLatin1String("self")) {
			const Card *card = room->askForExchange(owner, objectName(), 1, 1, true, "@gu_guose-card:" + target->objectName());
			if (card && !card->getSubcards().isEmpty()) id = card->getSubcards().first();
		} else {
			id = room->askForCardChosen(owner, target, "he", objectName());
		}
		if (id < 0 || !target->isAlive() || target->containsTrick("indulgence")) return;
		room->broadcastSkillInvoke(objectName());
		const Card *card = Sanguosha->getCard(id);
		ServerPlayer *from = room->getCardOwner(id);
		Indulgence *trick = new Indulgence(card->getSuit(), card->getNumber());
		trick->setSkillName(objectName());
		WrappedCard *wrapped = Sanguosha->getWrappedCard(id);
		wrapped->takeOver(trick);
		room->broadcastUpdateCard(room->getAllPlayers(true), id, wrapped);
		CardMoveReason reason(CardMoveReason::S_REASON_PUT, owner->objectName(), target->objectName(), objectName(), QString());
		room->moveCardTo(wrapped, from, target, Player::PlaceDelayedTrick, reason, true);
		if (owner->isAlive()) owner->drawCards(2, objectName());
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == GameStart || event == EventPhaseStart) {
			seed(room);
			return false;
		}
		if (!player) return false;
		if (event == EventPhaseSkipping) {
			if (player->getPhase() != Player::Play || player->getMark("gu_guose_play-Clear") <= 0) return false;
			room->setPlayerMark(player, "gu_guose_play-Clear", 0);
			LogMessage log;
			log.type = "#GuGuoseUnskip";
			log.from = player;
			room->sendLog(log);
			return true;
		}
		if (event == FinishJudge) {
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (!judge || judge->reason != QLatin1String("indulgence") || !judge->isEffected()) return false;
			ServerPlayer *who = judge->who;
			if (!who || !who->isAlive() || who != room->getCurrent() || who->getMark("gu_guose_play-Clear") > 0)
				return false;
			foreach (ServerPlayer *owner, room->getAlivePlayers()) {
				if (!owner->hasSkill(this)) continue;
				const QString choice = room->askForChoice(owner, objectName(), QStringLiteral("swap+cancel"),
					QVariant::fromValue(who));
				if (choice != QLatin1String("swap")) continue;
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(owner, objectName());
				room->setPlayerMark(who, "gu_guose_play-Clear", 1);
				who->skip(Player::Discard);
				break;
			}
			return false;
		}
		// HpChanged / MaxHpChanged
		if (!player->isAlive()) return false;
		const QVariant last = player->getTag("GuGuoseWound");
		const int now = gu07cWoundState(player);
		player->setTag("GuGuoseWound", now);
		if (!last.isValid() || last.toInt() == now) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!player->isAlive()) break;
			if (owner->hasSkill(this)) place(room, owner, player);
		}
		return false;
	}
};

// ---------------- 幻大乔 流俪 ----------------
// 场上 = equip and judging areas. "则摸两张" has no subject, so you draw; the check is
// made after the discard.

class Gu07cLiuli : public TriggerSkill
{
public:
	Gu07cLiuli() : TriggerSkill("gu_liuli")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.card->isDamageCard()) return false;
		foreach (ServerPlayer *to, use.to) {
			if (!player->isAlive()) break;
			if (!to->isAlive() || use.nullified_list.contains(to->objectName())) continue;
			QList<int> disabled;
			bool any = false;
			foreach (const Card *c, to->getCards("ej")) {
				if (c->isRed() && player->canDiscard(to, c->getEffectiveId())) any = true;
				else disabled << c->getEffectiveId();
			}
			if (!any) continue;
			player->setTag("GuLiuliFrom", use.from ? use.from->objectName() : QString());
			const bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(to));
			player->removeTag("GuLiuliFrom");
			if (!invoke) continue;
			const int id = room->askForCardChosen(player, to, "ej", objectName(), false, Card::MethodDiscard, disabled);
			if (id < 0) continue;
			room->broadcastSkillInvoke(objectName());
			room->throwCard(id, objectName(), to, player);
			use.nullified_list << to->objectName();
			data = QVariant::fromValue(use);
			if (player->isAlive() && to->isWounded() == player->isWounded())
				player->drawCards(2, objectName());
		}
		return false;
	}
};

// ---------------- 疑伏寿 密图 ----------------
// Your own 准备阶段. Choosing nobody = not using it. All chosen characters draw and show
// first, then in action order each 拼点s (as initiator) a character you name for it.
// X counts the chosen characters whose 拼点 card was not one of their shown cards,
// including those that could not 拼点.

class Gu07cMitu : public TriggerSkill
{
public:
	Gu07cMitu() : TriggerSkill("gu_mitu")
	{
		events << EventPhaseStart;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || player->getPhase() != Player::Start) return false;
		QList<ServerPlayer *> chosen = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 3,
			"@gu_mitu-choose", true);
		if (chosen.isEmpty()) return false;
		room->broadcastSkillInvoke(objectName());
		room->sortByActionOrder(chosen);
		QHash<QString, QList<int> > shown;
		foreach (ServerPlayer *p, chosen) {
			if (!p->isAlive()) continue;
			const QList<int> before = p->handCards();
			p->drawCards(2, objectName());
			QList<int> drawn;
			foreach (int id, p->handCards())
				if (!before.contains(id)) drawn << id;
			if (drawn.isEmpty()) continue;
			room->showCard(p, drawn);
			foreach (int id, drawn) room->setCardFlag(id, "gu_mitu_shown");
			shown.insert(p->objectName(), drawn);
		}
		int x = 0;
		foreach (ServerPlayer *p, chosen) {
			const QList<int> mine = shown.value(p->objectName());
			bool usedShown = false;
			if (p->isAlive() && player->isAlive()) {
				QList<ServerPlayer *> rivals;
				foreach (ServerPlayer *q, room->getAlivePlayers())
					if (q != p && p->canPindian(q, false)) rivals << q;
				if (!rivals.isEmpty()) {
					player->setTag("GuMituFrom", p->objectName());
					ServerPlayer *rival = room->askForPlayerChosen(player, rivals, objectName(),
						"@gu_mitu-rival:" + p->objectName(), false, true);
					player->removeTag("GuMituFrom");
					std::unique_ptr<PindianStruct> pd(rival ? p->PinDian(rival, objectName()) : nullptr);
					if (pd && pd->from_card && pd->to_card) {
						usedShown = mine.contains(pd->from_card->getEffectiveId());
						ServerPlayer *winner = nullptr, *loser = nullptr;
						if (pd->from_number > pd->to_number) {
							winner = pd->from;
							loser = pd->to;
						} else if (pd->to_number > pd->from_number) {
							winner = pd->to;
							loser = pd->from;
						}
						if (winner && loser && winner->isAlive() && loser->isAlive()) {
							Card *slash = Sanguosha->cloneCard("slash");
							slash->setSkillName("_gu_mitu");
							slash->deleteLater();
							if (!winner->isProhibited(loser, slash))
								room->useCard(CardUseStruct(slash, winner, loser), false);
						}
					}
				}
			}
			foreach (int id, mine) room->setCardFlag(id, "-gu_mitu_shown");
			if (!usedShown) ++x;
		}
		if (x > 0 && player->isAlive()) room->loseMaxHp(player, x, objectName());
		return false;
	}
};

// ---------------- 疑伏寿 潜流 ----------------
// 距离 1 以内 = your distance to it is at most 1 (you yourself count). No 「可」 for the
// look; the gain needs all three bottom cards to share one suit or to have three suits.

class Gu07cQianliu : public TriggerSkill
{
public:
	Gu07cQianliu() : TriggerSkill("gu_qianliu")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *to, use.to) {
			if (!player->isAlive()) break;
			if (to != player && (!to->isAlive() || player->distanceTo(to) > 1)) continue;
			QList<int> ids = room->getNCards(3, true, false);
			if (ids.isEmpty()) continue;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			QSet<int> suits;
			foreach (int id, ids) suits.insert(int(Sanguosha->getCard(id)->getSuit()));
			const bool match = ids.length() == 3 && (suits.size() == 1 || suits.size() == 3);
			const QList<int> seen = ids;
			room->askForGuanxing(player, ids, Room::GuanxingBothSides);
			if (!match || !player->isAlive()) continue;
			if (room->askForChoice(player, objectName(), QStringLiteral("obtain+cancel"), data) != QLatin1String("obtain"))
				continue;
			QList<int> gain;
			foreach (int id, seen)
				if (room->getCardPlace(id) == Player::DrawPile) gain << id;
			if (gain.isEmpty()) continue;
			LogMessage log;
			log.type = "$GuQianliuShow";
			log.from = player;
			log.card_str = ListI2S(gain).join(QLatin1Char('+'));
			room->sendLog(log);
			DummyCard dummy(gain);
			room->obtainCard(player, &dummy, objectName(), true);
		}
		return false;
	}
};

void guAudit2026M07c(Package *pkg)
{
	General *zhoufei = new General(pkg, "gu_jiezhoufei", "wu", 3, false);
	zhoufei->addSkill(new Gu07cLiangyin);
	zhoufei->addSkill(new Gu07cKongsheng);

	General *daqiao = new General(pkg, "gu_huandaqiao", "wu", 3, false);
	daqiao->addSkill(new Gu07cGuose);
	daqiao->addSkill(new Gu07cLiuli);

	General *fushou = new General(pkg, "gu_yifushou", "qun", 4, false);
	fushou->setStartHp(3);
	fushou->addSkill(new Gu07cMitu);
	fushou->addSkill(new Gu07cQianliu);
}
