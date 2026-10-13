// GU-audit2025 follow-up 2026-10-13: generals from Tom's 2025 audit list.
// Authoritative skill text: qsanguosha-task-briefs/materials/gu-audit2025-tom-20261013/SOURCE.md
// Legacy TriggerSkill idioms only; no new AI round-56 metadata types are registered.
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

static QList<int> guRandomIds(QList<int> ids, int n)
{
	QList<int> out;
	while (n-- > 0 && !ids.isEmpty())
		out << ids.takeAt(qsanRandomBounded(ids.length()));
	return out;
}

static bool guAlive(const ServerPlayer *p)
{
	return p && p->isAlive();
}

static QList<ServerPlayer *> guOwners(Room *room, const QString &skill)
{
	QList<ServerPlayer *> out;
	foreach (ServerPlayer *p, room->getAllPlayers())
		if (p->hasSkill(skill)) out << p;
	return out;
}

static void guUseVirtual(Room *room, const QString &name, const QString &skill, ServerPlayer *from,
	QList<ServerPlayer *> tos, bool distanceFree, bool addHistory)
{
	if (!guAlive(from)) return;
	Card *card = Sanguosha->cloneCard(name);
	if (!card) return;
	card->setSkillName("_" + skill);
	card->deleteLater();
	QList<ServerPlayer *> valid;
	foreach (ServerPlayer *to, tos) {
		if (!guAlive(to) || from->isProhibited(to, card)) continue;
		if (card->isKindOf("Slash") && !from->canSlash(to, card, !distanceFree)) continue;
		valid << to;
	}
	if (valid.isEmpty() || from->isLocked(card)) return;
	room->useCard(CardUseStruct(card, from, valid), addHistory);
}

static int guRound(Room *room)
{
	return room->getTag("TurnLengthCount").toInt();
}

static void guRelate(Package *pkg, General *g, const QString &main, const Skill *status)
{
	pkg->addSkills(status);
	g->addSkill(status->objectName());
	pkg->insertRelatedSkills(main, status->objectName());
}

// ---------------- 孙霸 ----------------
class GuJiedang : public TriggerSkill
{
public:
	GuJiedang() : TriggerSkill("gu_jiedang")
	{
		events << EventPhaseStart << EnterDying;
	}
	static void removeOneType(Room *room, ServerPlayer *player)
	{
		QList<int> pile = player->getPile("gu_jiedang");
		if (pile.isEmpty()) return;
		QStringList types;
		foreach (int id, pile) {
			QString t = Sanguosha->getCard(id)->getType();
			if (!types.contains(t)) types << t;
		}
		room->sendCompulsoryTriggerLog(player, "gu_jiedang", true, true);
		QString choice = room->askForChoice(player, "gu_jiedang", types.join("+"));
		QList<int> ids;
		foreach (int id, pile)
			if (Sanguosha->getCard(id)->getType() == choice) ids << id;
		if (ids.isEmpty()) return;
		CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), "gu_jiedang", "");
		room->throwCard(ids, reason, nullptr);
		if (player->isAlive()) player->drawCards(ids.length(), "gu_jiedang");
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (event == EnterDying) {
			removeOneType(room, player);
			return false;
		}
		if (player->getPhase() == Player::RoundStart) {
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (player->isDead()) break;
				if (p->isNude()) continue;
				const Card *c = room->askForExchange(p, objectName(), p->getCardCount(), 1, true,
					"@gu_jiedang-put:" + player->objectName(), true);
				if (!c || c->subcardsLength() == 0) continue;
				player->addToPile("gu_jiedang", c->getSubcards(), true);
				if (p->isAlive()) p->drawCards(1, objectName());
			}
		} else if (player->getPhase() == Player::Play || player->getPhase() == Player::Finish) {
			removeOneType(room, player);
		}
		return false;
	}
};

class GuJidi : public TriggerSkill
{
public:
	GuJidi() : TriggerSkill("gu_jidi")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer *from = damage.from;
		if (!guAlive(from) || from == player) return false;
		bool hp = from->getHp() > player->getHp();
		bool hand = from->getHandcardNum() > player->getHandcardNum();
		if (!hp && !hand) return false;
		room->sendCompulsoryTriggerLog(player, this);
		if (hp) room->loseHp(from, 1, true, player, objectName());
		if (hand && from->isAlive()) {
			QList<int> ids;
			foreach (const Card *c, from->getHandcards())
				if (from->canDiscard(from, c->getEffectiveId())) ids << c->getEffectiveId();
			ids = guRandomIds(ids, 2);
			if (!ids.isEmpty()) room->throwCard(ids, objectName(), from, from);
		}
		return false;
	}
};

// ---------------- 张翼 ----------------
static void guDoMurui(Room *room, ServerPlayer *p, int idx)
{
	const QString del = "gu_murui_del" + QString::number(idx);
	if (!guAlive(p) || !p->hasSkill("gu_murui") || p->getMark(del) > 0) return;
	if (p->isKongcheng() && p->getHandPile().isEmpty()) return;
	int before = p->getMark("damage_point_round");
	const Card *c = room->askForUseCard(p, "$.|.|.|hand", "@gu_murui" + QString::number(idx));
	if (c && p->isAlive() && p->getMark("damage_point_round") > before) {
		p->drawCards(2, "gu_murui");
		room->setPlayerMark(p, del, 1);
		room->addPlayerMark(p, "@gu_murui_del");
	}
}

class GuMurui : public TriggerSkill
{
public:
	GuMurui() : TriggerSkill("gu_murui")
	{
		events << RoundStart << EventPhaseStart << EventPhaseChanging << Death;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Death) {
			room->setTag("GuMuruiDeath", true);
		} else if (event == RoundStart) {
			guDoMurui(room, player, 1);
		} else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive || !room->getTag("GuMuruiDeath").toBool()) return false;
			room->removeTag("GuMuruiDeath");
			foreach (ServerPlayer *p, room->getAllPlayers()) guDoMurui(room, p, 2);
		} else if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
			room->removeTag("GuMuruiDeath");
			guDoMurui(room, player, 3);
		}
		return false;
	}
};

class GuAoren : public TriggerSkill
{
public:
	GuAoren() : TriggerSkill("gu_aoren")
	{
		events << CardFinished;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.card->isKindOf("BasicCard")) return false;
		int x = 0;
		for (int i = 1; i <= 3; i++) if (player->getMark("gu_murui_del" + QString::number(i)) > 0) x++;
		if (player->getMark("gu_aoren_lun") >= x) return false;
		QList<int> ids = use.card->getSubcards();
		if (ids.isEmpty()) return false;
		foreach (int id, ids) if (room->getCardPlace(id) != Player::DiscardPile) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, "gu_aoren_lun");
		room->obtainCard(player, use.card, objectName(), true);
		return false;
	}
};

// ---------------- 侯成 ----------------
class GuXianniangJiu : public FilterSkill
{
public:
	GuXianniangJiu() : FilterSkill("gu_xianniang_jiu")
	{
	}
	bool viewFilter(const Card *to_select) const override
	{
		return to_select->hasFlag("gu_xianniang_jiu") && to_select->isKindOf("BasicCard");
	}
	const Card *viewAs(const Card *originalCard) const override
	{
		Analeptic *jiu = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
		jiu->setSkillName(objectName());
		WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
		card->takeOver(jiu);
		return card;
	}
};

static void guXianniangMark(Room *room, ServerPlayer *holder, const QList<int> &ids)
{
	bool any = false;
	foreach (int id, ids) {
		if (!Sanguosha->getCard(id)->isKindOf("BasicCard") || room->getCardOwner(id) != holder
			|| room->getCardPlace(id) != Player::PlaceHand) continue;
		room->setCardFlag(id, "gu_xianniang_jiu");
		any = true;
	}
	if (!any) return;
	if (!holder->hasSkill("gu_xianniang_jiu")) room->attachSkillToPlayer(holder, "gu_xianniang_jiu");
	room->filterCards(holder, holder->getCards("h"), true);
}

class GuXianniang : public TriggerSkill
{
public:
	GuXianniang() : TriggerSkill("gu_xianniang")
	{
		events << CardsMoveOneTime << Damaged;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	void doEffect(Room *room, ServerPlayer *player, const QString &timing) const
	{
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (p->getHandcardNum() >= player->getHandcardNum() && !p->isNude()) cands << p;
		if (cands.isEmpty()) return;
		ServerPlayer *t = room->askForPlayerChosen(player, cands, objectName(), "@gu_xianniang-get", true, true);
		if (!t) return;
		room->broadcastSkillInvoke(objectName());
		room->setPlayerMark(player, "gu_xianniang_" + timing + "_lun", 1);
		QList<int> got;
		for (int i = 0; i < 2 && t->isAlive() && !t->isNude(); i++) {
			int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodNone, QList<int>(), i > 0);
			if (id < 0) break;
			room->obtainCard(player, id, objectName(), false);
			got << id;
		}
		if (got.isEmpty()) return;
		guXianniangMark(room, player, got);
		room->addPlayerMark(player, "gu_xianniang_got_lun", got.length());
		QList<ServerPlayer *> others = room->getOtherPlayers(player);
		others.removeOne(t);
		if (!others.isEmpty() && !player->isNude()) {
			ServerPlayer *r = room->askForPlayerChosen(player, others, objectName() + "_give",
				"@gu_xianniang-give:::" + QString::number(got.length()), true);
			if (r) {
				const Card *c = room->askForExchange(player, objectName(), got.length(), 1, true,
					"@gu_xianniang-givecard:" + r->objectName(), true);
				if (c && c->subcardsLength() > 0) {
					QList<int> ids = c->getSubcards();
					room->giveCard(player, r, ids, objectName());
					guXianniangMark(room, r, ids);
				}
			}
		}
		if (player->isAlive() && player->getMark("gu_xianniang_got_lun") > 2)
			room->loseHp(player, 1, true, player, objectName());
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && move.from_places.contains(Player::PlaceHand)) {
				foreach (int id, move.card_ids) {
					const Card *c = Sanguosha->getCard(id);
					if (c->hasFlag("gu_xianniang_jiu") && room->getCardOwner(id) != player)
						room->setCardFlag(id, "-gu_xianniang_jiu");
				}
			}
			if (move.from != player || !player->hasSkill(this) || player->isDead()) return false;
			if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return false;
			if (move.reason.m_playerId == player->objectName() || move.reason.m_playerId.isEmpty()) return false;
			if (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip)) return false;
			if (player->getMark("gu_xianniang_dis_lun") > 0) return false;
			doEffect(room, player, "dis");
		} else if (event == Damaged) {
			if (!player->hasSkill(this) || player->isDead() || player->getMark("gu_xianniang_dmg_lun") > 0) return false;
			doEffect(room, player, "dmg");
		}
		return false;
	}
};

// ---------------- 威吕布 ----------------
GuXiaowuCard::GuXiaowuCard()
{
	target_fixed = true;
	will_throw = true;
}
bool GuXiaowuCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
	return false;
}
bool GuXiaowuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.isEmpty();
}
void GuXiaowuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int n = source->getMark("gu_xiaowu_two-Clear") > 0 ? 2 : 1;
	QList<int> cands;
	foreach (int id, room->getDrawPile()) {
		const Card *c = Sanguosha->getCard(id);
		if (c->isKindOf("Slash") || Sanguosha->translate(":" + c->objectName()).contains("【杀】"))
			cands << id;
	}
	QList<int> ids = guRandomIds(cands, n);
	foreach (int id, ids) {
		room->setPlayerMark(source, "gu_xiaowu_free_" + QString::number(id), 1);
		room->obtainCard(source, id, "gu_xiaowu", false);
	}
	room->setPlayerMark(source, "gu_xiaowu_wait", 1);
}

class GuXiaowuVS : public OneCardViewAsSkill
{
public:
	GuXiaowuVS() : OneCardViewAsSkill("gu_xiaowu")
	{
		filter_pattern = ".!";
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return !player->hasUsed("GuXiaowuCard") && player->canDiscard(player, "he");
	}
	const Card *viewAs(const Card *originalCard) const override
	{
		GuXiaowuCard *card = new GuXiaowuCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class GuXiaowu : public TriggerSkill
{
public:
	GuXiaowu() : TriggerSkill("gu_xiaowu")
	{
		events << PreCardUsed << Damage;
		view_as_skill = new GuXiaowuVS;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
			bool hit = false;
			foreach (int id, use.card->getSubcards()) {
				QString m = "gu_xiaowu_free_" + QString::number(id);
				if (player->getMark(m) > 0) {
					room->setPlayerMark(player, m, 0);
					hit = true;
				}
			}
			if (hit && use.m_addHistory) {
				use.m_addHistory = false;
				data = QVariant::fromValue(use);
			}
		} else if (player->getMark("gu_xiaowu_wait") > 0) {
			room->setPlayerMark(player, "gu_xiaowu_wait", 0);
			room->addPlayerHistory(player, "GuXiaowuCard", 0);
			room->setPlayerMark(player, "gu_xiaowu_two-Clear", 1);
		}
		return false;
	}
};

class GuBaguanVS : public ViewAsSkill
{
public:
	GuBaguanVS() : ViewAsSkill("gu_baguan")
	{
		response_pattern = "@@gu_baguan";
	}
	bool isEnabledAtPlay(const Player *) const override
	{
		return false;
	}
	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		return !to_select->isEquipped() && Self->getHandcards().contains(to_select)
			&& selected.length() < Self->getMark("gu_baguan_x");
	}
	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (cards.isEmpty()) return nullptr;
		Slash *slash = new Slash(Card::SuitToBeDecided, -1);
		slash->addSubcards(cards);
		slash->setSkillName(objectName());
		return slash;
	}
};

class GuBaguan : public TriggerSkill
{
public:
	GuBaguan() : TriggerSkill("gu_baguan")
	{
		events << CardUsed << CardFinished << ConfirmDamage;
		view_as_skill = new GuBaguanVS;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || !damage.card || !damage.card->isKindOf("Slash")
				|| damage.card->getSkillName() != objectName()) return false;
			damage.damage = damage.card->subcardsLength();
			data = QVariant::fromValue(damage);
			return damage.damage <= 0;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.card->isKindOf("Weapon")) return false;
		if (event == CardUsed) {
			CardTagOwner prev = player->getTag("ComboMovesCard").value<CardTagOwner>();
			if (prev.card && prev.card->getTypeId() != Card::TypeEquip && prev.card->isSingleTargetCard()) {
				use.card->setFlags("ComboMoves");
				player->setFlags("gu_baguan_ready");
			}
			return false;
		}
		if (!player->hasFlag("gu_baguan_ready")) return false;
		player->setFlags("-gu_baguan_ready");
		if (!player->getWeapon() || player->isKongcheng()) return false;
		int x = Sanguosha->translate(player->getWeapon()->objectName()).length();
		if (x <= 0) return false;
		room->setPlayerMark(player, "gu_baguan_x", x);
		room->askForUseCard(player, "@@gu_baguan", "@gu_baguan:::" + QString::number(x));
		room->setPlayerMark(player, "gu_baguan_x", 0);
		return false;
	}
};

// ---------------- 星丁奉 ----------------
class GuDangchen : public TriggerSkill
{
public:
	GuDangchen() : TriggerSkill("gu_dangchen")
	{
		events << EventPhaseStart << TargetSpecified;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Play) return false;
			QList<ServerPlayer *> cands;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) if (!p->isNude()) cands << p;
			ServerPlayer *t = room->askForPlayerChosen(player, cands, objectName(), "@gu_dangchen", true, true);
			if (!t) return false;
			room->broadcastSkillInvoke(objectName());
			const Card *c = room->askForExchange(t, objectName(), t->getCardCount(), 1, true,
				"@gu_dangchen-give:" + player->objectName(), false);
			int n = c ? c->subcardsLength() : 0;
			if (n > 0) room->giveCard(t, player, c->getSubcards(), objectName());
			int x = 0;
			if (n > 0 && player->canDiscard(player, "he")) {
				const Card *d = room->askForDiscard(player, objectName(), n, 1, true, true,
					"@gu_dangchen-discard:::" + QString::number(n));
				if (d) x = d->subcardsLength();
			}
			room->setPlayerMark(player, "gu_dangchen_x-PlayClear", x);
			room->setPlayerMark(t, "gu_dangchen_" + player->objectName() + "-PlayClear", 1);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		int x = player->getMark("gu_dangchen_x-PlayClear");
		if (use.from != player || x <= 0 || player->getPhase() != Player::Play) return false;
		if (!use.card || !(use.card->isKindOf("Slash") || use.card->isNDTrick())) return false;
		bool hit = false;
		foreach (ServerPlayer *p, use.to)
			if (p->getMark("gu_dangchen_" + player->objectName() + "-PlayClear") > 0) hit = true;
		if (!hit || !player->askForSkillInvoke(this, data)) return false;
		JudgeStruct judge;
		judge.who = player;
		judge.reason = objectName();
		judge.pattern = ".";
		judge.good = true;
		room->judge(judge);
		if (judge.card && judge.card->getNumber() % x == 0) {
			use.extra_use += 1;
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class GuJianyu : public TriggerSkill
{
public:
	GuJianyu() : TriggerSkill("gu_jianyu")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (room->getCurrent() != player || !move.from || move.from == player
			|| !move.from_places.contains(Player::PlaceEquip)) return false;
		room->sendCompulsoryTriggerLog(player, this);
		player->drawCards(1, objectName());
		return false;
	}
};

// ---------------- 界诸葛诞 ----------------
class GuGongao : public TriggerSkill
{
public:
	GuGongao() : TriggerSkill("gu_gongao")
	{
		events << EnterDying;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getMark("gu_gongao_dyed") > 0) return false;
		player->addMark("gu_gongao_dyed");
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->hasSkill(this)) continue;
			room->sendCompulsoryTriggerLog(p, this);
			room->gainMaxHp(p, 1, objectName());
			room->recover(p, RecoverStruct(objectName(), p));
		}
		return false;
	}
};

class GuJuyi : public TriggerSkill
{
public:
	GuJuyi() : TriggerSkill("gu_juyi")
	{
		events << EventPhaseStart;
		frequency = Wake;
		waked_skills = "benghuai,gu_weizhong";
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::Start || player->getMark(objectName()) > 0) return false;
		if (!player->isWounded() || player->getMaxHp() <= room->alivePlayerCount()) return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->doSuperLightbox(player, objectName());
		room->setPlayerMark(player, objectName(), 1);
		int n = player->getMaxHp() - player->getHandcardNum();
		if (n > 0) player->drawCards(n, objectName());
		room->handleAcquireDetachSkills(player, "benghuai|gu_weizhong");
		return false;
	}
};

class GuWeizhong : public TriggerSkill
{
public:
	GuWeizhong() : TriggerSkill("gu_weizhong")
	{
		events << MaxHpChanged;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		room->sendCompulsoryTriggerLog(player, this);
		player->drawCards(2, objectName());
		return false;
	}
};

// ---------------- 张奂 ----------------
class GuYiju : public TriggerSkill
{
public:
	GuYiju() : TriggerSkill("gu_yiju")
	{
		events << TargetConfirmed;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || use.from == player
			|| use.to.length() != 1 || use.to.first() != player) return false;
		if (!player->canDiscard(player, "he")) return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->askForDiscard(player, objectName(), 1, 1, false, true);
		return false;
	}
};

class GuShuguoVS : public OneCardViewAsSkill
{
public:
	GuShuguoVS() : OneCardViewAsSkill("gu_shuguo")
	{
		response_pattern = "@@gu_shuguo";
		expand_pile = "#gu_shuguo";
	}
	bool viewFilter(const Card *to_select) const override
	{
		return Self->getPile("#gu_shuguo").contains(to_select->getEffectiveId());
	}
	const Card *viewAs(const Card *originalCard) const override
	{
		return originalCard;
	}
};

class GuShuguo : public TriggerSkill
{
public:
	GuShuguo() : TriggerSkill("gu_shuguo")
	{
		events << CardsMoveOneTime << EventPhaseStart << EventPhaseChanging;
		view_as_skill = new GuShuguoVS;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() == Player::RoundStart) {
				room->removeTag("GuShuguoIds");
				room->removeTag("GuShuguoFrom");
			}
			return false;
		}
		if (event == CardsMoveOneTime) {
			if (player != room->getCurrent()) return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place != Player::DiscardPile
				|| (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return false;
			QVariantList ids = room->getTag("GuShuguoIds").toList();
			QStringList froms = room->getTag("GuShuguoFrom").toStringList();
			foreach (int id, move.card_ids) {
				ids << id;
				froms << (move.from ? move.from->objectName() : QString());
			}
			room->setTag("GuShuguoIds", ids);
			room->setTag("GuShuguoFrom", froms);
			return false;
		}
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to != Player::NotActive) return false;
		QVariantList ids = room->getTag("GuShuguoIds").toList();
		QStringList froms = room->getTag("GuShuguoFrom").toStringList();
		if (ids.isEmpty()) return false;
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			if (!owner->hasSkill(this) || !owner->askForSkillInvoke(this)) continue;
			room->broadcastSkillInvoke(objectName());
			int used = 0, total = 0;
			for (int i = 0; i < ids.length() && owner->isAlive(); i++) {
				int id = ids.at(i).toInt();
				if (room->getCardPlace(id) != Player::DiscardPile) continue;
				total++;
				if (!owner->canUse(Sanguosha->getCard(id))) continue;
				QList<int> one;
				one << id;
				room->notifyMoveToPile(owner, one, objectName(), Player::DiscardPile, true);
				const Card *c = room->askForUseCard(owner, "@@gu_shuguo", "@gu_shuguo");
				room->notifyMoveToPile(owner, one, objectName(), Player::DiscardPile, false);
				if (!c) continue;
				used++;
				if (froms.value(i) != owner->objectName()) break;
			}
			if (owner->isDead()) continue;
			bool uniqueMost = true;
			foreach (ServerPlayer *p, room->getOtherPlayers(owner))
				if (p->getHandcardNum() >= owner->getHandcardNum()) uniqueMost = false;
			int x = qMin(5, total - used);
			if (!uniqueMost && x > 0) owner->drawCards(x, objectName());
		}
		return false;
	}
};

// ---------------- 张燕 ----------------
class GuQiaolue : public TriggerSkill
{
public:
	GuQiaolue() : TriggerSkill("gu_qiaolue")
	{
		events << TargetSpecified;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill || use.from != player) return false;
		const QString key = "gu_qiaolue_" + use.card->objectName();
		QList<ServerPlayer *> firsts;
		foreach (ServerPlayer *p, use.to) {
			if (p->getMark(key) > 0) continue;
			p->addMark(key);
			if (p != player && !firsts.contains(p)) firsts << p;
		}
		if (!player->hasSkill(this) || player->isDead() || firsts.isEmpty()) return false;
		bool any = false;
		foreach (ServerPlayer *p, firsts) if (!p->isNude()) any = true;
		if (!any || !player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		foreach (ServerPlayer *p, firsts) {
			if (player->isDead()) break;
			if (p->isDead() || p->isNude()) continue;
			int id = room->askForCardChosen(player, p, "he", objectName());
			if (id >= 0) room->obtainCard(player, id, objectName(), false);
		}
		return false;
	}
};

class GuHanjie : public TriggerSkill
{
public:
	GuHanjie() : TriggerSkill("gu_hanjie")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr && target->isAlive();
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::RoundStart) return false;
		foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
			if (!owner->hasSkill(this) || player->getHp() < owner->getHp() || owner->isKongcheng()) continue;
			if (player->isDead()) break;
			QStringList names;
			foreach (int id, Sanguosha->getRandomCards()) {
				const Card *c = Sanguosha->getEngineCard(id);
				if (!c || !c->isDamageCard() || c->getTypeId() == Card::TypeEquip) continue;
				if (!names.contains(c->objectName())) names << c->objectName();
			}
			if (names.isEmpty() || !owner->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			const Card *black = room->askForCard(owner, ".|black|.|hand", "@gu_hanjie:" + player->objectName(),
				QVariant::fromValue(player), Card::MethodNone);
			if (!black) continue;
			QString name = room->askForChoice(owner, objectName(), names.join("+"), QVariant::fromValue(player));
			Card *card = Sanguosha->cloneCard(name);
			if (!card) continue;
			card->setSkillName(objectName());
			card->addSubcard(black);
			card->deleteLater();
			if (owner->isLocked(card) || owner->isProhibited(player, card)) continue;
			room->broadcastSkillInvoke(objectName());
			room->useCard(CardUseStruct(card, owner, player), false);
		}
		return false;
	}
};

// ---------------- 伍孚 ----------------
GuZhongeCard::GuZhongeCard()
{
}
bool GuZhongeCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}
bool GuZhongeCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 1;
}
static void guZhongeStep(Room *room, const QString &choice, ServerPlayer *source, ServerPlayer *target, bool differ)
{
	for (int k = 0; k < 2; k++) {
		ServerPlayer *a = k == 0 ? source : target;
		ServerPlayer *b = k == 0 ? target : source;
		int times = (k == 0 && differ) ? 2 : 1;
		for (int i = 0; i < times; i++) {
			if (!guAlive(a)) break;
			if (choice == "draw") a->drawCards(1, "gu_zhonge");
			else if (guAlive(b)) guUseVirtual(room, "slash", "gu_zhonge", a, QList<ServerPlayer *>() << b, true, false);
		}
	}
}
void GuZhongeCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *target = targets.first();
	int diff = source->getMaxHp() - source->getHandcardNum();
	if (diff > 0) source->drawCards(diff, "gu_zhonge");
	else if (diff < 0) room->askForDiscard(source, "gu_zhonge", -diff, -diff);
	if (source->isDead() || target->isDead()) return;
	QString mine = room->askForChoice(source, "gu_zhonge", "draw+slash", QVariant::fromValue(target));
	QString his = room->askForChoice(target, "gu_zhonge", "draw+slash", QVariant::fromValue(source));
	LogMessage log;
	log.type = "#GuZhongeChoice";
	log.from = source;
	log.arg = "gu_zhonge:" + mine;
	log.arg2 = "gu_zhonge:" + his;
	room->sendLog(log);
	bool differ = mine != his;
	guZhongeStep(room, mine, source, target, differ);
	guZhongeStep(room, his, source, target, differ);
}

class GuZhonge : public ZeroCardViewAsSkill
{
public:
	GuZhonge() : ZeroCardViewAsSkill("gu_zhonge")
	{
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return !player->hasUsed("GuZhongeCard");
	}
	const Card *viewAs() const override
	{
		return new GuZhongeCard;
	}
};

GuJuekaiCard::GuJuekaiCard()
{
	target_fixed = true;
}
bool GuJuekaiCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
	return false;
}
bool GuJuekaiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.isEmpty();
}
void GuJuekaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@gu_juekai");
	room->doSuperLightbox(source, "gu_juekai");
	room->addPlayerHistory(source, "GuZhongeCard", 0);
	room->setTag("GuJuekaiOn", true);
	foreach (ServerPlayer *p, room->getAlivePlayers())
		if (p->getMark("gu_juekai_got") > 0) room->setPlayerMark(p, "&gu_juekai_ban-PlayClear", 1);
}

class GuJuekaiVS : public ZeroCardViewAsSkill
{
public:
	GuJuekaiVS() : ZeroCardViewAsSkill("gu_juekai")
	{
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return player->getMark("@gu_juekai") > 0;
	}
	const Card *viewAs() const override
	{
		return new GuJuekaiCard;
	}
};

class GuJuekai : public TriggerSkill
{
public:
	GuJuekai() : TriggerSkill("gu_juekai")
	{
		events << CardsMoveOneTime << EventPhaseStart << EventPhaseEnd;
		frequency = Limited;
		limit_mark = "@gu_juekai";
		view_as_skill = new GuJuekaiVS;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart || event == EventPhaseEnd) {
			if (player->getPhase() != Player::Play) return false;
			room->removeTag("GuJuekaiOn");
			foreach (ServerPlayer *p, room->getAllPlayers(true)) p->setMark("gu_juekai_got", 0);
			return false;
		}
		ServerPlayer *current = room->getCurrent();
		if (!current || current->getPhase() != Player::Play) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to != player || move.to_place != Player::PlaceHand) return false;
		player->setMark("gu_juekai_got", 1);
		if (room->getTag("GuJuekaiOn").toBool())
			room->setPlayerMark(player, "&gu_juekai_ban-PlayClear", 1);
		return false;
	}
};

class GuJuekaiLimit : public CardLimitSkill
{
public:
	GuJuekaiLimit() : CardLimitSkill("#gu_juekai-limit")
	{
	}
	QString limitList(const Player *target) const override
	{
		return target->getMark("&gu_juekai_ban-PlayClear") > 0 ? "use" : "";
	}
	QString limitPattern(const Player *target) const override
	{
		return target->getMark("&gu_juekai_ban-PlayClear") > 0 ? ".|.|.|hand" : "";
	}
};

void guAudit2025Tenyear(Package *pkg)
{
	General *sunba = new General(pkg, "gu_sunba", "wu", 4);
	sunba->addSkill(new GuJiedang);
	sunba->addSkill(new GuJidi);

	General *zhangyi = new General(pkg, "gu_zhangyi", "shu", 4);
	zhangyi->addSkill(new GuMurui);
	zhangyi->addSkill(new GuAoren);

	General *houcheng = new General(pkg, "gu_houcheng", "qun", 5);
	houcheng->addSkill(new GuXianniang);
	pkg->addSkills(new GuXianniangJiu);

	General *weilvbu = new General(pkg, "gu_weilvbu", "qun", 5);
	weilvbu->addSkill(new GuXiaowu);
	weilvbu->addSkill(new GuBaguan);
	pkg->addMetaObject<GuXiaowuCard>();

	General *xingdingfeng = new General(pkg, "gu_xingdingfeng", "wu", 4);
	xingdingfeng->addSkill(new GuDangchen);
	xingdingfeng->addSkill(new GuJianyu);

	General *jiezhugedan = new General(pkg, "gu_jiezhugedan", "wei", 4);
	jiezhugedan->addSkill(new GuGongao);
	jiezhugedan->addSkill(new GuJuyi);
	jiezhugedan->addRelateSkill("benghuai");
	jiezhugedan->addRelateSkill("gu_weizhong");
	pkg->addSkills(new GuWeizhong);

	General *zhanghuan = new General(pkg, "gu_zhanghuan", "qun", 4);
	zhanghuan->addSkill(new GuYiju);
	zhanghuan->addSkill(new GuShuguo);

	General *zhangyan = new General(pkg, "gu_zhangyan", "qun", 4);
	zhangyan->addSkill(new GuQiaolue);
	zhangyan->addSkill(new GuHanjie);

	General *wufu = new General(pkg, "gu_wufu", "qun", 3);
	wufu->addSkill(new GuZhonge);
	wufu->addSkill(new GuJuekai);
	guRelate(pkg, wufu, "gu_juekai", new GuJuekaiLimit);
	pkg->addMetaObject<GuZhongeCard>();
	pkg->addMetaObject<GuJuekaiCard>();
}


// ---------------- 田忌 ----------------
class GuWeiji : public TriggerSkill
{
public:
	GuWeiji() : TriggerSkill("gu_weiji")
	{
		events << TargetSpecified;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
		QList<ServerPlayer *> others;
		foreach (ServerPlayer *p, use.to) if (p != player && p->isAlive() && !others.contains(p)) others << p;
		if (others.isEmpty() || !player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		QString num = room->askForChoice(player, objectName(), "1+2+3", data);
		foreach (ServerPlayer *p, others) {
			if (player->isDead()) break;
			if (p->isDead()) continue;
			QString guess = room->askForChoice(p, "gu_weiji_guess", "1+2+3", QVariant::fromValue(player));
			LogMessage log;
			log.type = "#GuWeijiGuess";
			log.from = p;
			log.arg = guess;
			log.arg2 = num;
			room->sendLog(log);
			if (guess != num) player->drawCards(num.toInt(), objectName());
		}
		return false;
	}
};

class GuSaima : public TriggerSkill
{
public:
	GuSaima() : TriggerSkill("gu_saima")
	{
		events << CardUsed;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.card->isKindOf("Horse")) return false;
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) if (player->canPindian(p)) cands << p;
		ServerPlayer *t = room->askForPlayerChosen(player, cands, objectName(), "@gu_saima", true, true);
		if (!t) return false;
		room->broadcastSkillInvoke(objectName());
		int wins = 0;
		for (int i = 0; i < 3; i++) {
			if (player->isDead() || t->isDead() || !player->canPindian(t)) break;
			PindianStruct *pd = player->PinDian(t, objectName());
			if (pd && pd->success) wins++;
		}
		if (wins >= 2 && t->isAlive())
			room->damage(DamageStruct(objectName(), player->isAlive() ? player : nullptr, t));
		return false;
	}
};

// ---------------- 忍姜维 ----------------
GuHuoluanCard::GuHuoluanCard()
{
}
bool GuHuoluanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < 2 && to_select != Self && Self->canPindian(to_select);
}
bool GuHuoluanCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return !targets.isEmpty() && targets.length() <= 2;
}
void GuHuoluanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	// Joint pindian follows the manual multi-target pindian precedent of GusheCard (ol.cpp).
	QList<ServerPlayer *> all;
	all << source << targets;
	QMap<ServerPlayer *, int> nums;
	QList<int> shown;
	foreach (ServerPlayer *p, all) {
		if (p->isDead() || p->isKongcheng()) continue;
		const Card *c = room->askForExchange(p, "gu_huoluan", 1, 1, false, "@gu_huoluan-pindian:" + source->objectName());
		if (!c || c->subcardsLength() == 0) continue;
		int id = c->getSubcards().first();
		int number = Sanguosha->getCard(id)->getNumber();
		if (p == source) {
			source->setTag("GuHuoluanRecord", id);
			room->setPlayerMark(source, "gu_huoluan_record", id + 1);
		}
		CardMoveReason reason(CardMoveReason::S_REASON_PINDIAN, p->objectName(), "gu_huoluan", "");
		room->moveCardTo(Sanguosha->getCard(id), p, nullptr, Player::PlaceTable, reason, true);
		shown << id;
		if (p == source) {
			QStringList ch;
			for (int i = 1; i <= 13; i++) ch << QString::number(i);
			number = room->askForChoice(source, "gu_huoluan_number", ch.join("+"), QVariant(number)).toInt();
		}
		nums[p] = number;
		LogMessage log;
		log.type = "#GuHuoluanNumber";
		log.from = p;
		log.arg = QString::number(number);
		room->sendLog(log);
	}
	if (!shown.isEmpty()) {
		CardMoveReason reason(CardMoveReason::S_REASON_PINDIAN, source->objectName(), "gu_huoluan", "");
		QList<int> left;
		foreach (int id, shown) if (room->getCardPlace(id) == Player::PlaceTable) left << id;
		if (!left.isEmpty()) room->throwCard(left, reason, nullptr);
	}
	if (nums.size() < 2) return;
	int mn = 99, mx = -1;
	foreach (int v, nums.values()) { mn = qMin(mn, v); mx = qMax(mx, v); }
	ServerPlayer *low = nullptr, *high = nullptr;
	if (nums.values().count(mn) == 1) low = nums.key(mn);
	if (nums.values().count(mx) == 1) high = nums.key(mx);
	if (low) {
		foreach (ServerPlayer *p, nums.keys())
			if (p != low && guAlive(p) && guAlive(low))
				guUseVirtual(room, "slash", "gu_huoluan", p, QList<ServerPlayer *>() << low, true, false);
	}
	if (guAlive(high) && high->getHp() > 0) high->drawCards(high->getHp(), "gu_huoluan");
	if (low != source && high != source) room->addPlayerHistory(source, "GuHuoluanCard", 0);
}

class GuHuoluan : public ZeroCardViewAsSkill
{
public:
	GuHuoluan() : ZeroCardViewAsSkill("gu_huoluan")
	{
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return !player->hasUsed("GuHuoluanCard") && !player->isKongcheng();
	}
	const Card *viewAs() const override
	{
		return new GuHuoluanCard;
	}
};

class GuGuxing : public TriggerSkill
{
public:
	GuGuxing() : TriggerSkill("gu_guxing")
	{
		events << EventPhaseStart;
		frequency = Limited;
		limit_mark = "@gu_guxing";
	}
	static QList<int> pool(Room *room)
	{
		QList<int> ids = room->getDrawPile();
		ids << room->getDiscardPile();
		return ids;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::RoundStart || player->getMark("@gu_guxing") <= 0) return false;
		int rec = player->getMark("gu_huoluan_record") - 1;
		if (rec < 0 || !player->askForSkillInvoke(this)) return false;
		room->removePlayerMark(player, "@gu_guxing");
		room->doSuperLightbox(player, objectName());
		const Card *record = Sanguosha->getCard(rec);
		int x = qMin(3, guRound(room));
		for (int step = 1; step <= x && player->isAlive(); step++) {
			QList<int> get;
			if (step == 1) {
				if (pool(room).contains(rec)) get << rec;
			} else if (step == 2) {
				foreach (int id, pool(room)) {
					const Card *c = Sanguosha->getCard(id);
					if (id != rec && c->getSuit() == record->getSuit() && c->getNumber() == record->getNumber()) get << id;
				}
			} else {
				foreach (int id, pool(room)) {
					if (get.length() >= 5) break;
					if (id != rec && Sanguosha->getCard(id)->objectName() == record->objectName()) get << id;
				}
			}
			if (get.isEmpty()) continue;
			DummyCard dummy(get);
			room->obtainCard(player, &dummy, objectName(), true);
		}
		return false;
	}
};

// ---------------- 忍邓艾 ----------------
class GuNeyan : public TriggerSkill
{
public:
	GuNeyan() : TriggerSkill("gu_neyan")
	{
		events << CardUsed;
		frequency = Compulsory;
		change_skill = true;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill
			|| use.card->getTypeId() == Card::TypeEquip) return false;
		room->sendCompulsoryTriggerLog(player, this);
		if (player->getMark("gu_neyan_yin") == 0) {
			room->setPlayerMark(player, "gu_neyan_yin", 1);
			QString pattern = use.card->getTypeId() == Card::TypeBasic ? "BasicCard" : "TrickCard";
			if (room->askForCard(player, pattern, "@gu_neyan-discard:::" + use.card->getType(), data, objectName())) {
				use.extra_use += 1;
			} else {
				use.nullified_list << "_ALL_TARGETS";
			}
			data = QVariant::fromValue(use);
		} else {
			room->setPlayerMark(player, "gu_neyan_yin", 0);
			if (use.m_addHistory) room->addPlayerHistory(player, use.card->getClassName(), -1);
		}
		return false;
	}
};

class GuNeyanMod : public TargetModSkill
{
public:
	GuNeyanMod() : TargetModSkill("#gu_neyan-mod")
	{
		pattern = "^EquipCard";
	}
	int getResidueNum(const Player *from, const Card *, const Player *) const override
	{
		return from->hasSkill("gu_neyan") && from->getMark("gu_neyan_yin") > 0 ? 1000 : 0;
	}
};

class GuQianyao : public TriggerSkill
{
public:
	GuQianyao() : TriggerSkill("gu_qianyao")
	{
		events << EventPhaseStart << ConfirmDamage;
		frequency = Limited;
		limit_mark = "@gu_qianyao";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->hasFlag("gu_qianyao_dmg")) {
				damage.damage++;
				data = QVariant::fromValue(damage);
			}
			return false;
		}
		if (player->getPhase() != Player::RoundStart || player->getMark("@gu_qianyao") <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->removePlayerMark(player, "@gu_qianyao");
		room->doSuperLightbox(player, objectName());
		int x = qMax(1, qMin(3, guRound(room)));
		player->drawCards(x, objectName());
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_gu_qianyao");
		slash->deleteLater();
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (player->canSlash(p, slash, true) && !player->isProhibited(p, slash)) cands << p;
		if (cands.isEmpty() || player->isDead()) return false;
		QList<ServerPlayer *> tos = room->askForPlayersChosen(player, cands, objectName(), 1, 2, "@gu_qianyao");
		if (tos.isEmpty()) return false;
		if (x >= 2) slash->setFlags("gu_qianyao_dmg");
		CardUseStruct use(slash, player, tos);
		if (x >= 3) use.no_respond_list << "_ALL_TARGETS";
		room->useCard(use, false);
		return false;
	}
};

// ---------------- 魔麴义 ----------------
class GuTieji : public TriggerSkill
{
public:
	GuTieji() : TriggerSkill("gu_tieji")
	{
		events << TargetSpecified << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers(true)) {
				int n = p->getMark("gu_tieji");
				if (n <= 0) continue;
				p->setMark("gu_tieji", 0);
				room->removePlayerMark(p, "@skill_invalidity", n);
			}
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !player->hasSkill(this) || !use.card || !use.card->isKindOf("Slash")) return false;
		for (int i = 0; i < use.to.length(); i++) {
			ServerPlayer *t = use.to.at(i);
			if (player->isDead()) break;
			if (t->isDead() || !player->askForSkillInvoke(this, QVariant::fromValue(t))) continue;
			room->broadcastSkillInvoke(objectName());
			t->addMark("gu_tieji");
			room->addPlayerMark(t, "@skill_invalidity");
			foreach (ServerPlayer *p, room->getAllPlayers()) room->filterCards(p, p->getCards("he"), true);
			JudgeStruct judge;
			judge.pattern = ".";
			judge.good = true;
			judge.reason = objectName();
			judge.who = player;
			room->judge(judge);
			QString suit = judge.card ? judge.card->getSuitString() : QString();
			if (t->isAlive() && (!t->canDiscard(t, "he")
				|| !room->askForCard(t, ".|" + suit, "@gu_tieji-discard:::" + suit, data, Card::MethodDiscard))) {
				QVariantList jinks = player->getTag("Jink_" + use.card->toString()).toList();
				if (i < jinks.size()) jinks[i] = 0;
				player->setTag("Jink_" + use.card->toString(), jinks);
			}
		}
		return false;
	}
};

class GuChongqi : public TriggerSkill
{
public:
	GuChongqi() : TriggerSkill("gu_chongqi")
	{
		events << TargetSpecified << ConfirmDamage;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from == player && damage.card && damage.to
				&& damage.card->hasFlag("gu_chongqi_" + damage.to->objectName())) {
				damage.damage++;
				data = QVariant::fromValue(damage);
			}
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.card->isKindOf("Slash") || player->getMark("gu_chongqi-Clear") > 0) return false;
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, use.to) if (player->canDiscard(p, "he") && !cands.contains(p)) cands << p;
		ServerPlayer *t = room->askForPlayerChosen(player, cands, objectName(), "@gu_chongqi", true, true);
		if (!t) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, "gu_chongqi-Clear");
		int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodDiscard);
		if (id >= 0) room->throwCard(id, objectName(), t, player);
		// 游击: target in your attack range while you are not in its attack range.
		if (t->isAlive() && player->inMyAttackRange(t) && !t->inMyAttackRange(player)) {
			LogMessage log;
			log.type = "#GuYouji";
			log.from = player;
			log.to << t;
			log.arg = objectName();
			room->sendLog(log);
			use.card->setFlags("gu_chongqi_" + t->objectName());
		}
		return false;
	}
};

class GuFanquan : public TriggerSkill
{
public:
	GuFanquan() : TriggerSkill("gu_fanquan")
	{
		events << Damaged;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@gu_fanquan", true, true);
		if (!t) return false;
		room->broadcastSkillInvoke(objectName());
		// 搏击: both players are in each other's attack range.
		bool boji = player->inMyAttackRange(t) && t->inMyAttackRange(player);
		int n = 1;
		if (boji) {
			n = qMax(1, damage.damage);
			room->addPlayerMark(player, "gu_fanquan_dist-Clear");
			LogMessage log;
			log.type = "#GuBoji";
			log.from = player;
			log.to << t;
			log.arg = objectName();
			room->sendLog(log);
		}
		room->damage(DamageStruct(objectName(), player, t, n));
		return false;
	}
};

class GuFanquanDist : public DistanceSkill
{
public:
	GuFanquanDist() : DistanceSkill("#gu_fanquan-dist")
	{
	}
	int getCorrect(const Player *from, const Player *) const override
	{
		return from->getMark("gu_fanquan_dist-Clear");
	}
};

void guAudit2025Doudizhu(Package *pkg)
{
	// 田忌 has no kingdom in SOURCE.md; non-Three-Kingdoms 地主 figures use qun here, e.g. ddz_libai.
	General *tianji = new General(pkg, "gu_tianji", "qun", 3);
	tianji->addSkill(new GuWeiji);
	tianji->addSkill(new GuSaima);

	General *renjiangwei = new General(pkg, "gu_renjiangwei", "shu", 4);
	renjiangwei->addSkill(new GuHuoluan);
	renjiangwei->addSkill(new GuGuxing);
	pkg->addMetaObject<GuHuoluanCard>();

	General *rendengai = new General(pkg, "gu_rendengai", "wei", 4);
	rendengai->addSkill(new GuNeyan);
	rendengai->addSkill(new GuQianyao);
	guRelate(pkg, rendengai, "gu_neyan", new GuNeyanMod);

	General *moquyi = new General(pkg, "gu_moquyi", "qun", 4);
	moquyi->addSkill(new GuTieji);
	moquyi->addSkill(new GuChongqi);
	moquyi->addSkill(new GuFanquan);
	guRelate(pkg, moquyi, "gu_fanquan", new GuFanquanDist);
}


// ---------------- 闪刘宏 ----------------
class GuChaozheng : public TriggerSkill
{
public:
	GuChaozheng() : TriggerSkill("gu_chaozheng")
	{
		events << EventPhaseStart << YishiBeforeReveal;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == YishiBeforeReveal) {
			YishiStruct *yishi = data.value<YishiStruct *>();
			if (!yishi || !yishi->containsParticipant(player)) return false;
			QString color;
			foreach (const QString &op, yishi->getOpinions(player)) {
				if (op == "red" || op == "black") { color = op; break; }
			}
			// setOpinions only rewrites the shown cards. Append one extra opinion so the tally counts +1.
			if (color.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player, this);
			yishi->opinions << color;
			return false;
		}
		if (player->getPhase() != Player::Play || !player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		YishiStruct yishi = room->askForYishi(player, room->getAlivePlayers(), objectName());
		if (!yishi.started) return false;
		QList<ServerPlayer *> reds;
		foreach (ServerPlayer *p, yishi.participants) if (yishi.hasOpinion(p, "red")) reds << p;
		if (yishi.result == "red") {
			foreach (ServerPlayer *p, reds)
				if (p->isAlive() && p->isWounded()) room->recover(p, RecoverStruct(objectName(), player));
		} else if (yishi.result == "black") {
			foreach (ServerPlayer *p, reds)
				if (p != player && p->isAlive()) room->loseHp(p, 1, true, player, objectName());
		}
		if (player->isDead()) return false;
		int same = 0;
		foreach (ServerPlayer *p, yishi.participants)
			if (yishi.sharesOpinion(p, player)) same++;
		same = qMin(2, same);
		if (same > 0) player->drawCards(same, objectName());
		return false;
	}
};

class GuShenchong : public TriggerSkill
{
public:
	GuShenchong() : TriggerSkill("gu_shenchong")
	{
		events << EventPhaseStart << Death;
		frequency = Limited;
		limit_mark = "@gu_shenchong";
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr && target->hasSkill(this);
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player) return false;
			const QString targetName = player->getTag("GuShenchongTarget").toString();
			if (targetName.isEmpty()) return false;
			ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
			if (guAlive(killer)) {
				QList<int> ids;
				foreach (const Card *c, killer->getCards("he")) ids << c->getEffectiveId();
				if (!ids.isEmpty()) room->throwCard(ids, objectName(), killer, killer);
			}
			ServerPlayer *t = room->findPlayerByObjectName(targetName);
			if (guAlive(t)) {
				QStringList detach;
				foreach (const Skill *s, t->getVisibleSkillList()) {
					if (!s || s->objectName().startsWith("#")) continue;
					detach << "-" + s->objectName();
				}
				if (t->hasSkill("bahu") && !detach.contains("-bahu")) detach << "-bahu";
				if (t->hasSkill("gu_feiyang") && !detach.contains("-gu_feiyang")) detach << "-gu_feiyang";
				if (!detach.isEmpty()) room->handleAcquireDetachSkills(t, detach.join("|"));
			}
			return false;
		}
		if (player->isDead() || player->getPhase() != Player::Start || player->getMark("@gu_shenchong") <= 0) return false;
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@gu_shenchong", true, true);
		if (!t) return false;
		room->removePlayerMark(player, "@gu_shenchong");
		room->doSuperLightbox(player, objectName());
		player->setTag("GuShenchongTarget", t->objectName());
		room->handleAcquireDetachSkills(t, "gu_feiyang|bahu");
		return false;
	}
};

class GuJulian : public TriggerSkill
{
public:
	GuJulian() : TriggerSkill("gu_julian$")
	{
		events << CardsMoveOneTime << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Finish || !player->hasLordSkill(objectName())) return false;
			QList<ServerPlayer *> qun;
			foreach (ServerPlayer *p, room->getOtherPlayers(player))
				if (p->getKingdom() == "qun" && !p->isKongcheng()) qun << p;
			if (qun.isEmpty() || !player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			foreach (ServerPlayer *p, qun) {
				if (player->isDead()) break;
				if (p->isDead() || p->isKongcheng()) continue;
				int id = room->askForCardChosen(player, p, "h", objectName());
				if (id >= 0) room->obtainCard(player, id, objectName(), false);
			}
			return false;
		}
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to != player || move.to_place != Player::PlaceHand) return false;
		if (move.reason.m_reason != CardMoveReason::S_REASON_DRAW || move.reason.m_skillName == objectName()) return false;
		if (player->getPhase() == Player::Draw || player->getKingdom() != "qun" || player->getMark("gu_julian-Clear") > 0) return false;
		bool lord = false;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) if (p->hasLordSkill(objectName())) lord = true;
		if (!lord || !room->askForSkillInvoke(player, "gu_julian_draw", data)) return false;
		room->addPlayerMark(player, "gu_julian-Clear");
		player->drawCards(1, objectName());
		return false;
	}
};

class GuFeiyang : public TriggerSkill
{
public:
	GuFeiyang() : TriggerSkill("gu_feiyang")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::Judge || player->getJudgingArea().isEmpty() || player->getCardCount() < 2) return false;
		if (!room->askForDiscard(player, objectName(), 2, 2, true, true, "@gu_feiyang")) return false;
		room->broadcastSkillInvoke(objectName());
		QList<int> ids = player->getJudgingAreaID();
		if (!ids.isEmpty()) room->throwCard(ids, objectName(), player, player);
		return false;
	}
};

// ---------------- 闪赵云 ----------------
class GuLonglin : public TriggerSkill
{
public:
	GuLonglin() : TriggerSkill("gu_longlin")
	{
		events << TargetSpecified << Damage << CardUsed;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from == player && use.card && use.card->getTypeId() != Card::TypeSkill
				&& player->getMark("gu_longlin_self") > 0)
				room->setPlayerMark(player, "gu_longlin_self", 0);
			return false;
		}
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || !damage.card || damage.card->getSkillName() != objectName()) return false;
			ServerPlayer *user = room->findPlayerByObjectName(player->getTag("GuLonglinUser").toString());
			if (guAlive(user) && user->getPhase() == Player::Play) room->setPlayerMark(user, "gu_longlin_self", 1);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.card->isKindOf("Slash") || player->getPhase() != Player::Play) return false;
		if (player->getMark("gu_longlin_first-PlayClear") > 0) return false;
		player->addMark("gu_longlin_first-PlayClear");
		foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
			if (!owner->hasSkill(this) || !owner->canDiscard(owner, "he")) continue;
			if (!room->askForDiscard(owner, objectName(), 1, 1, true, true, "@gu_longlin:" + player->objectName())) continue;
			room->broadcastSkillInvoke(objectName());
			use.nullified_list << "_ALL_TARGETS";
			data = QVariant::fromValue(use);
			if (player->isAlive() && owner->isAlive() && room->askForSkillInvoke(player, "gu_longlin_duel", QVariant::fromValue(owner))) {
				owner->setTag("GuLonglinUser", player->objectName());
				Duel *duel = new Duel(Card::NoSuit, 0);
				duel->setSkillName("_gu_longlin");
				duel->deleteLater();
				if (!player->isProhibited(owner, duel) && !player->isLocked(duel))
					room->useCard(CardUseStruct(duel, player, owner), false);
			}
			break;
		}
		return false;
	}
};

class GuLonglinProhibit : public ProhibitSkill
{
public:
	GuLonglinProhibit() : ProhibitSkill("#gu_longlin-prohibit")
	{
	}
	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
	{
		return from && to && card && card->getTypeId() != Card::TypeSkill && from->getMark("gu_longlin_self") > 0 && from != to;
	}
};

static bool guZhendanReplay(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request)
{
	ActiveSkillRequest prefix = request;
	prefix.selectedCardIds.clear();
	foreach (int id, request.selectedCardIds) {
		const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
		if (!card || !skill->canSelectCard(prefix, card)) return false;
		prefix.selectedCardIds << id;
	}
	return true;
}

class GuZhendanVS : public ViewAsSkillV2
{
public:
	GuZhendanVS() : ViewAsSkillV2("gu_zhendan", 1)
	{
		response_or_use = true;
	}
	SkillDialogInfo getDialogInfo() const override
	{
		return SkillDialogInfo::guhuo("gu_zhendan", true, false);
	}
	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || player->getMark("gu_zhendan_off_lun") > 0) return false;
		if (request.pattern.startsWith(".") || request.pattern.startsWith("@")) return false;
		if (request.pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		return request.reason == CardUseStruct::CARD_USE_REASON_PLAY || !usableNames(request).isEmpty();
	}
	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return request.initiator && request.selectedCardIds.isEmpty() && card && !card->isKindOf("BasicCard")
			&& request.initiator->handCards().contains(card->getEffectiveId());
	}
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.size() == 1 && guZhendanReplay(this, request);
	}
protected:
	bool allowDeclaration(const Player *player, const QString &name) const override
	{
		return player && player->getMark("gu_zhendan_used_" + name + "_lun") == 0;
	}
};

class GuZhendan : public TriggerSkill
{
public:
	GuZhendan() : TriggerSkill("gu_zhendan")
	{
		events << CardUsed << Damaged << RoundEnd << EventPhaseStart << RoundStart;
		view_as_skill = new GuZhendanVS;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	void doDraw(Room *room, ServerPlayer *player) const
	{
		if (player->getMark("gu_zhendan_off_lun") > 0) return;
		room->sendCompulsoryTriggerLog(player, this);
		int x = qMin(5, room->getTag("GuZhendanTurns").toInt());
		if (x > 0) player->drawCards(x, objectName());
		room->setPlayerMark(player, "gu_zhendan_off_lun", 1);
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == RoundStart) {
			room->setTag("GuZhendanTurns", 0);
		} else if (event == EventPhaseStart) {
			if (player->getPhase() == Player::RoundStart)
				room->setTag("GuZhendanTurns", room->getTag("GuZhendanTurns").toInt() + 1);
		} else if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from == player && player->hasSkill(this) && use.card && use.card->isKindOf("BasicCard"))
				room->setPlayerMark(player, "gu_zhendan_used_" + use.card->objectName() + "_lun", 1);
		} else if ((event == Damaged || event == RoundEnd) && player->isAlive() && player->hasSkill(this)) {
			doDraw(room, player);
		}
		return false;
	}
};

// ---------------- 向宠[OL] ----------------
class GuGuying : public TriggerSkill
{
public:
	GuGuying() : TriggerSkill("gu_guying")
	{
		events << CardsMoveOneTime << EventPhaseStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart) {
			int n = player->getMark("gu_guying_dis");
			if (player->getPhase() != Player::Start || n <= 0) return false;
			room->setPlayerMark(player, "gu_guying_dis", 0);
			room->sendCompulsoryTriggerLog(player, this);
			for (int i = 0; i < n && player->canDiscard(player, "he"); i++)
				room->askForDiscard(player, objectName(), 1, 1, false, true);
			return false;
		}
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player || move.to == player || move.card_ids.length() != 1) return false;
		if (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip)) return false;
		ServerPlayer *current = room->getCurrent();
		if (!guAlive(current) || current == player || current->getMark("gu_guying-Clear") > 0) return false;
		room->addPlayerMark(current, "gu_guying-Clear");
		room->sendCompulsoryTriggerLog(player, this);
		int id = move.card_ids.first();
		QStringList choices;
		if (!current->isNude()) choices << "give";
		choices << "return";
		QString ch = room->askForChoice(current, objectName(), choices.join("+"), QVariant::fromValue(player));
		if (ch == "give") {
			QList<int> all;
			foreach (const Card *c, current->getCards("he")) all << c->getEffectiveId();
			room->giveCard(current, player, guRandomIds(all, 1), objectName());
		} else {
			const Card *card = Sanguosha->getCard(id);
			if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand)
				room->obtainCard(player, id, objectName(), true);
			if (card->getTypeId() == Card::TypeEquip && room->getCardOwner(id) == player
				&& room->getCardPlace(id) == Player::PlaceHand && player->canUse(card))
				room->useCard(CardUseStruct(card, player, player));
		}
		room->addPlayerMark(player, "gu_guying_dis");
		return false;
	}
};

GuMuzhenCard::GuMuzhenCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}
bool GuMuzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (!targets.isEmpty() || to_select == Self) return false;
	if (subcardsLength() == 2) return Self->getMark("gu_muzhen1-PlayClear") == 0 && !to_select->getEquips().isEmpty();
	if (subcardsLength() != 1 || Self->getMark("gu_muzhen2-PlayClear") > 0) return false;
	const Card *c = Sanguosha->getCard(getSubcards().first());
	const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
	if (!equip) return false;
	int loc = int(equip->location());
	return to_select->hasEquipArea(loc) && !to_select->getEquip(loc);
}
bool GuMuzhenCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 1;
}
void GuMuzhenCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *t = targets.first();
	if (subcardsLength() == 2) {
		room->setPlayerMark(source, "gu_muzhen1-PlayClear", 1);
		room->giveCard(source, t, this, "gu_muzhen");
		if (source->isAlive() && t->isAlive() && !t->getEquips().isEmpty()) {
			int id = room->askForCardChosen(source, t, "e", "gu_muzhen");
			if (id >= 0) room->obtainCard(source, id, "gu_muzhen", true);
		}
	} else {
		room->setPlayerMark(source, "gu_muzhen2-PlayClear", 1);
		CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "gu_muzhen", "");
		room->moveCardTo(this, source, t, Player::PlaceEquip, reason, true);
		if (source->isAlive() && t->isAlive() && !t->isKongcheng()) {
			int id = room->askForCardChosen(source, t, "h", "gu_muzhen");
			if (id >= 0) room->obtainCard(source, id, "gu_muzhen", false);
		}
	}
}

class GuMuzhen : public ViewAsSkill
{
public:
	GuMuzhen() : ViewAsSkill("gu_muzhen")
	{
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return player->getMark("gu_muzhen1-PlayClear") == 0 || player->getMark("gu_muzhen2-PlayClear") == 0;
	}
	bool viewFilter(const QList<const Card *> &selected, const Card *) const override
	{
		return selected.length() < 2;
	}
	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (cards.isEmpty() || cards.length() > 2) return nullptr;
		if (cards.length() == 1 && (cards.first()->getTypeId() != Card::TypeEquip || Self->getMark("gu_muzhen2-PlayClear") > 0)) return nullptr;
		if (cards.length() == 2 && Self->getMark("gu_muzhen1-PlayClear") > 0) return nullptr;
		GuMuzhenCard *card = new GuMuzhenCard;
		card->addSubcards(cards);
		return card;
	}
};

// ---------------- 闪张辽 ----------------
GuZhengbingCard::GuZhengbingCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}
bool GuZhengbingCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
	return false;
}
bool GuZhengbingCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.isEmpty();
}
void GuZhengbingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	const Card *c = Sanguosha->getCard(getSubcards().first());
	room->recastCard(source, c->getEffectiveId(), "gu_zhengbing");
	if (source->isDead()) return;
	if (c->isKindOf("Slash")) {
		room->addPlayerMark(source, "&gu_zhengbing_max");
		room->setPlayerMark(source, "gu_zhengbing_o1", 1);
	} else if (c->isKindOf("Jink")) {
		source->drawCards(1, "gu_zhengbing");
		room->setPlayerMark(source, "gu_zhengbing_o2", 1);
	} else if (c->isKindOf("Peach") || c->isKindOf("Analeptic")) {
		room->addPlayerMark(source, "gu_zhengbing_extra-Clear");
		room->setPlayerMark(source, "gu_zhengbing_o3", 1);
	}
	if (source->isAlive() && source->getMark("gu_zhengbing_o1") > 0 && source->getMark("gu_zhengbing_o2") > 0
		&& source->getMark("gu_zhengbing_o3") > 0 && source->getKingdom() != "wei") {
		room->changeKingdom(source, "wei");
	}
}

class GuZhengbingVS : public OneCardViewAsSkill
{
public:
	GuZhengbingVS() : OneCardViewAsSkill("gu_zhengbing")
	{
		filter_pattern = ".";
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return player->getKingdom() == "qun" && player->usedTimes("GuZhengbingCard") < 3;
	}
	const Card *viewAs(const Card *originalCard) const override
	{
		GuZhengbingCard *card = new GuZhengbingCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class GuZhengbing : public TriggerSkill
{
public:
	GuZhengbing() : TriggerSkill("gu_zhengbing")
	{
		events << EventPhaseStart;
		view_as_skill = new GuZhengbingVS;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::Finish || player->getMark("gu_zhengbing_extra-Clear") <= 0) return false;
		const int extra = player->getMark("gu_zhengbing_extra-Clear");
		room->setPlayerMark(player, "gu_zhengbing_extra-Clear", 0);
		room->sendCompulsoryTriggerLog(player, this);
		for (int i = 0; i < extra; ++i) player->insertPhase(Player::Play);
		return false;
	}
};

class GuZhengbingMax : public MaxCardsSkill
{
public:
	GuZhengbingMax() : MaxCardsSkill("#gu_zhengbing-max")
	{
	}
	int getExtra(const Player *target) const override
	{
		return target->getMark("&gu_zhengbing_max");
	}
};

class GuTuwei : public TriggerSkill
{
public:
	GuTuwei() : TriggerSkill("gu_tuwei")
	{
		events << EventPhaseStart << EventPhaseChanging << Damaged;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Damaged) {
			player->setMark("gu_tuwei_hurt", 1);
			return false;
		}
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			QStringList names = player->getTag("GuTuweiTargets").toStringList();
			player->removeTag("GuTuweiTargets");
			foreach (QString name, names) {
				ServerPlayer *p = room->findPlayerByObjectName(name);
				if (!guAlive(p) || p->getMark("gu_tuwei_hurt") > 0 || player->isDead() || player->isNude()) continue;
				int id = room->askForCardChosen(p, player, "he", objectName());
				if (id >= 0) room->obtainCard(p, id, objectName(), false);
			}
			foreach (ServerPlayer *p, room->getAllPlayers(true)) p->setMark("gu_tuwei_hurt", 0);
			return false;
		}
		if (player->getPhase() == Player::RoundStart) {
			foreach (ServerPlayer *p, room->getAllPlayers(true)) p->setMark("gu_tuwei_hurt", 0);
			return false;
		}
		if (player->getPhase() != Player::Play || !player->hasSkill(this) || player->getKingdom() != "wei") return false;
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (player->inMyAttackRange(p) && !p->isNude()) cands << p;
		if (cands.isEmpty()) return false;
		QList<ServerPlayer *> tos = room->askForPlayersChosen(player, cands, objectName(), 0, cands.length(), "@gu_tuwei", true);
		if (tos.isEmpty()) return false;
		room->broadcastSkillInvoke(objectName());
		QStringList names;
		foreach (ServerPlayer *p, tos) {
			if (player->isDead()) break;
			if (p->isDead() || p->isNude()) continue;
			int id = room->askForCardChosen(player, p, "he", objectName());
			if (id < 0) continue;
			room->obtainCard(player, id, objectName(), false);
			names << p->objectName();
		}
		player->setTag("GuTuweiTargets", QVariant(names));
		return false;
	}
};

void guAudit2025OLSp(Package *pkg)
{
	General *liuhong = new General(pkg, "gu_shanliuhong", "qun", 4);
	liuhong->addSkill(new GuChaozheng);
	liuhong->addSkill(new GuShenchong);
	liuhong->addSkill(new GuJulian);
	liuhong->addRelateSkill("gu_feiyang");
	liuhong->addRelateSkill("bahu");
	pkg->addSkills(new GuFeiyang);

	General *zhaoyun = new General(pkg, "gu_shanzhaoyun", "shu", 4);
	zhaoyun->addSkill(new GuLonglin);
	zhaoyun->addSkill(new GuZhendan);
	guRelate(pkg, zhaoyun, "gu_longlin", new GuLonglinProhibit);

	General *xiangchong = new General(pkg, "gu_olxiangchong", "shu", 4);
	xiangchong->addSkill(new GuGuying);
	xiangchong->addSkill(new GuMuzhen);
	pkg->addMetaObject<GuMuzhenCard>();

	// 群/魏 dual faction: starts as qun; 整兵 switches the kingdom property to wei.
	General *zhangliao = new General(pkg, "gu_shanzhangliao", "qun", 4);
	zhangliao->addSkill(new GuZhengbing);
	zhangliao->addSkill(new GuTuwei);
	guRelate(pkg, zhangliao, "gu_zhengbing", new GuZhengbingMax);
	pkg->addMetaObject<GuZhengbingCard>();
}


// ---------------- 黄权[海外] ----------------
GuQuanjianCard::GuQuanjianCard()
{
}
bool GuQuanjianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}
bool GuQuanjianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 1;
}
void GuQuanjianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *t = targets.first();
	QStringList choices;
	if (source->getMark("gu_quanjian1-PlayClear") == 0) choices << "damage";
	if (source->getMark("gu_quanjian2-PlayClear") == 0) choices << "adjust";
	if (choices.isEmpty()) return;
	QString ch = room->askForChoice(source, "gu_quanjian", choices.join("+"), QVariant::fromValue(t));
	room->setPlayerMark(source, ch == "damage" ? "gu_quanjian1-PlayClear" : "gu_quanjian2-PlayClear", 1);
	bool done = false;
	if (ch == "damage") {
		QList<ServerPlayer *> victims;
		foreach (ServerPlayer *p, room->getAlivePlayers()) if (t->inMyAttackRange(p)) victims << p;
		ServerPlayer *v = victims.isEmpty() ? nullptr
			: room->askForPlayerChosen(source, victims, "gu_quanjian_victim", "@gu_quanjian-victim:" + t->objectName());
		if (v && room->askForChoice(t, "gu_quanjian_damage", "yes+no", QVariant::fromValue(v)) == "yes") {
			done = true;
			room->damage(DamageStruct("gu_quanjian", t, v));
		}
	} else if (room->askForChoice(t, "gu_quanjian_adjust", "yes+no", QVariant::fromValue(source)) == "yes") {
		done = true;
		int max = t->getMaxCards();
		int hand = t->getHandcardNum();
		if (hand < max) {
			int to = qMin(max, 5);
			if (to > hand) t->drawCards(to - hand, "gu_quanjian");
		} else if (hand > max) {
			room->askForDiscard(t, "gu_quanjian", hand - max, hand - max);
		}
		room->setPlayerMark(t, "&gu_quanjian_lock-Clear", 1);
	}
	if (!done && t->isAlive()) room->addPlayerMark(t, "&gu_quanjian_dmg-Clear");
}

class GuQuanjianVS : public ZeroCardViewAsSkill
{
public:
	GuQuanjianVS() : ZeroCardViewAsSkill("gu_quanjian")
	{
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return player->getMark("gu_quanjian1-PlayClear") == 0 || player->getMark("gu_quanjian2-PlayClear") == 0;
	}
	const Card *viewAs() const override
	{
		return new GuQuanjianCard;
	}
};

class GuQuanjian : public TriggerSkill
{
public:
	GuQuanjian() : TriggerSkill("gu_quanjian")
	{
		events << DamageInflicted;
		view_as_skill = new GuQuanjianVS;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr && target->getMark("&gu_quanjian_dmg-Clear") > 0;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		room->setPlayerMark(player, "&gu_quanjian_dmg-Clear", 0);
		DamageStruct damage = data.value<DamageStruct>();
		damage.damage++;
		data = QVariant::fromValue(damage);
		return false;
	}
};

class GuQuanjianLimit : public CardLimitSkill
{
public:
	GuQuanjianLimit() : CardLimitSkill("#gu_quanjian-limit")
	{
	}
	QString limitList(const Player *target) const override
	{
		return target->getMark("&gu_quanjian_lock-Clear") > 0 ? "use" : "";
	}
	QString limitPattern(const Player *target) const override
	{
		return target->getMark("&gu_quanjian_lock-Clear") > 0 ? ".|.|.|hand" : "";
	}
};

class GuTujue : public TriggerSkill
{
public:
	GuTujue() : TriggerSkill("gu_tujue")
	{
		events << Dying;
		frequency = Limited;
		limit_mark = "@gu_tujue";
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.who != player || player->getMark("@gu_tujue") <= 0 || player->isNude()) return false;
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@gu_tujue", true, true);
		if (!t) return false;
		room->removePlayerMark(player, "@gu_tujue");
		room->doSuperLightbox(player, objectName());
		QList<int> ids;
		foreach (const Card *c, player->getCards("he")) ids << c->getEffectiveId();
		room->giveCard(player, t, ids, objectName());
		// SOURCE.md: X is the number of cards given, and at least 5.
		int x = qMax(5, ids.length());
		if (player->isAlive()) room->recover(player, RecoverStruct(objectName(), player, x));
		if (player->isAlive()) player->drawCards(x, objectName());
		return false;
	}
};

// ---------------- 起孙坚[海外] ----------------
GuPingtaoCard::GuPingtaoCard()
{
}
bool GuPingtaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}
bool GuPingtaoCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 1;
}
void GuPingtaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *t = targets.first();
	QStringList choices;
	if (!t->isNude()) choices << "give";
	choices << "slash";
	QString ch = room->askForChoice(t, "gu_pingtao", choices.join("+"), QVariant::fromValue(source));
	if (ch == "give") {
		const Card *c = room->askForExchange(t, "gu_pingtao", 1, 1, true, "@gu_pingtao-give:" + source->objectName());
		if (c && c->subcardsLength() > 0) room->giveCard(t, source, c->getSubcards(), "gu_pingtao");
		room->addPlayerMark(source, "&gu_pingtao_extra-PlayClear");
	} else {
		guUseVirtual(room, "slash", "gu_pingtao", source, QList<ServerPlayer *>() << t, true, false);
	}
}

class GuPingtao : public ZeroCardViewAsSkill
{
public:
	GuPingtao() : ZeroCardViewAsSkill("gu_pingtao")
	{
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		return !player->hasUsed("GuPingtaoCard");
	}
	const Card *viewAs() const override
	{
		return new GuPingtaoCard;
	}
};

class GuPingtaoMod : public TargetModSkill
{
public:
	GuPingtaoMod() : TargetModSkill("#gu_pingtao-mod")
	{
	}
	int getResidueNum(const Player *from, const Card *, const Player *) const override
	{
		return from->getMark("&gu_pingtao_extra-PlayClear");
	}
};

class GuJuelie : public TriggerSkill
{
public:
	GuJuelie() : TriggerSkill("gu_juelie")
	{
		events << DamageCaused << TargetSpecified;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || !damage.card || !damage.card->isKindOf("Slash")) return false;
			bool minHand = true, minHp = true;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getHandcardNum() < player->getHandcardNum()) minHand = false;
				if (p->getHp() < player->getHp()) minHp = false;
			}
			if (!minHand && !minHp) return false;
			room->sendCompulsoryTriggerLog(player, this);
			damage.damage++;
			data = QVariant::fromValue(damage);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *t, use.to) {
			if (player->isDead() || !player->canDiscard(player, "he")) break;
			if (t->isDead() || !player->canDiscard(t, "he")) continue;
			const Card *d = room->askForDiscard(player, objectName(), player->getCardCount(), 1, true, true,
				"@gu_juelie:" + t->objectName());
			if (!d) continue;
			int n = d->subcardsLength();
			for (int i = 0; i < n && t->isAlive() && player->canDiscard(t, "he"); i++) {
				int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodDiscard);
				if (id < 0) break;
				room->throwCard(id, objectName(), t, player);
			}
		}
		return false;
	}
};

// ---------------- 界凌统[台服] ----------------
static void guXuanfengDiscard(Room *room, ServerPlayer *player)
{
	QList<ServerPlayer *> picked;
	for (int i = 0; i < 2 && player->isAlive(); i++) {
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!player->canDiscard(p, "he")) continue;
			if (picked.length() >= 2 && !picked.contains(p)) continue;
			cands << p;
		}
		ServerPlayer *t = room->askForPlayerChosen(player, cands, "gu_xuanfeng_dis", "@gu_xuanfeng-dis", i > 0);
		if (!t) break;
		if (!picked.contains(t)) picked << t;
		int id = room->askForCardChosen(player, t, "he", "gu_xuanfeng", false, Card::MethodDiscard);
		if (id >= 0) room->throwCard(id, "gu_xuanfeng", t, player);
	}
}
static void guXuanfengMove(Room *room, ServerPlayer *player)
{
	QList<ServerPlayer *> froms;
	foreach (ServerPlayer *p, room->getOtherPlayers(player)) if (!p->getEquips().isEmpty()) froms << p;
	ServerPlayer *from = room->askForPlayerChosen(player, froms, "gu_xuanfeng_from", "@gu_xuanfeng-from", true);
	if (!from) return;
	QList<int> disabled;
	foreach (const Card *e, from->getEquips()) {
		const EquipCard *equip = qobject_cast<const EquipCard *>(e->getRealCard());
		if (!equip) continue;
		int loc = int(equip->location());
		bool ok = false;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (p != from && p->hasEquipArea(loc) && !p->getEquip(loc)) ok = true;
		if (!ok) disabled << e->getEffectiveId();
	}
	if (disabled.length() >= from->getEquips().length()) return;
	int id = room->askForCardChosen(player, from, "e", "gu_xuanfeng", false, Card::MethodNone, disabled);
	if (id < 0) return;
	const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
	int loc = int(equip->location());
	QList<ServerPlayer *> tos;
	foreach (ServerPlayer *p, room->getOtherPlayers(player))
		if (p != from && p->hasEquipArea(loc) && !p->getEquip(loc)) tos << p;
	ServerPlayer *to = room->askForPlayerChosen(player, tos, "gu_xuanfeng_to", "@gu_xuanfeng-to");
	if (!to) return;
	CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), "gu_xuanfeng", "");
	room->moveCardTo(Sanguosha->getCard(id), from, to, Player::PlaceEquip, reason, true);
}

class GuXuanfeng : public TriggerSkill
{
public:
	GuXuanfeng() : TriggerSkill("gu_xuanfeng")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
	}
	void doEffect(Room *room, ServerPlayer *player) const
	{
		if (!player->askForSkillInvoke(this)) return;
		room->broadcastSkillInvoke(objectName());
		QStringList choices;
		choices << "dis" << "move";
		bool area = false;
		for (int i = 0; i < 5; i++) if (player->hasEquipArea(i)) area = true;
		if (area) choices << "beishui";
		QString ch = room->askForChoice(player, objectName(), choices.join("+"));
		if (ch == "beishui") {
			QStringList slotList;
			for (int i = 0; i < 5; i++) if (player->hasEquipArea(i)) slotList << QString("EquipArea%1").arg(i);
			QString slot = room->askForChoice(player, "gu_xuanfeng_area", slotList.join("+"));
			player->throwEquipArea(slot.mid(9).toInt());
		}
		if (ch == "dis" || ch == "beishui") guXuanfengDiscard(room, player);
		if ((ch == "move" || ch == "beishui") && player->isAlive()) guXuanfengMove(room, player);
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseEnd) {
			if (player->getPhase() == Player::Discard && player->getMark("gu_xuanfeng_dis-Clear") >= 2) doEffect(room, player);
			return false;
		}
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player) return false;
		if (player->getPhase() == Player::Discard
			&& (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
			player->addMark("gu_xuanfeng_dis-Clear", move.card_ids.length());
		if (move.from_places.contains(Player::PlaceEquip)) doEffect(room, player);
		return false;
	}
};

class GuYongjin : public TriggerSkill
{
public:
	GuYongjin() : TriggerSkill("gu_yongjin")
	{
		events << EventPhaseStart << CardsMoveOneTime << EventPhaseEnd << Damage;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getAllPlayers(true)) p->setMark("gu_yongjin_lost", 0);
			return false;
		}
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from != player || move.to == player) return false;
			int n = 0;
			foreach (Player::Place place, move.from_places)
				if (place == Player::PlaceHand || place == Player::PlaceEquip) n++;
			if (n > 0) player->addMark("gu_yongjin_lost", n);
			return false;
		}
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || !damage.card || damage.card->getSkillName() != objectName()) return false;
			QList<int> thrown;
			for (int i = 0; i < 5; i++) if (!player->hasEquipArea(i)) thrown << i;
			if (!thrown.isEmpty()) {
				player->obtainEquipArea(thrown.at(qsanRandomBounded(thrown.length())));
				return false;
			}
			QList<int> ids;
			foreach (int id, room->getDiscardPile()) {
				const EquipCard *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(id)->getRealCard());
				if (equip && player->hasEquipArea(int(equip->location())) && !player->getEquip(int(equip->location()))) ids << id;
			}
			if (ids.isEmpty()) return false;
			int id = ids.at(qsanRandomBounded(ids.length()));
			CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), "");
			room->moveCardTo(Sanguosha->getCard(id), nullptr, player, Player::PlaceEquip, reason, true);
			return false;
		}
		// EventPhaseEnd of the current player's phase.
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			int lost = owner->getMark("gu_yongjin_lost");
			if (!owner->hasSkill(this) || lost <= 0) continue;
			QList<ServerPlayer *> cands;
			foreach (ServerPlayer *p, room->getOtherPlayers(owner))
				if (p->getMark("gu_yongjin_lost") == lost) cands << p;
			if (cands.isEmpty()) continue;
			QList<ServerPlayer *> tos = room->askForPlayersChosen(owner, cands, objectName(), 0, cands.length(), "@gu_yongjin", true);
			if (tos.isEmpty()) continue;
			room->broadcastSkillInvoke(objectName());
			guUseVirtual(room, "slash", objectName(), owner, tos, true, false);
		}
		foreach (ServerPlayer *p, room->getAllPlayers(true)) p->setMark("gu_yongjin_lost", 0);
		return false;
	}
};

// ---------------- 群曹操[手杀] ----------------
class GuLingfa : public TriggerSkill
{
public:
	GuLingfa() : TriggerSkill("gu_lingfa")
	{
		events << RoundStart << CardUsed << CardFinished;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == RoundStart) {
			if (!player->hasSkill(this) || player->isDead()) return false;
			int x = guRound(room);
			if (x == 1 || x == 2) {
				if (player->askForSkillInvoke(this, QString::number(x))) {
					room->broadcastSkillInvoke(objectName());
					room->setPlayerMark(player, QString("&gu_lingfa%1_lun").arg(x), 1);
				}
			}
			if (x > 2) {
				room->sendCompulsoryTriggerLog(player, this);
				room->handleAcquireDetachSkills(player, "-gu_lingfa|gu_zhian|gu_jianxiong");
			}
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card) return false;
		int item = 0;
		if (event == CardUsed && use.card->isKindOf("Slash")) item = 1;
		else if (event == CardFinished && use.card->isKindOf("Peach")) item = 2;
		if (item == 0) return false;
		foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
			if (player->isDead()) break;
			if (owner->getMark(QString("&gu_lingfa%1_lun").arg(item)) <= 0) continue;
			bool done = false;
			if (item == 1) {
				done = player->canDiscard(player, "he") && room->askForDiscard(player, objectName(), 1, 1, true, true,
					"@gu_lingfa-discard:" + owner->objectName());
			} else if (!player->isNude() && owner->isAlive()) {
				const Card *c = room->askForExchange(player, objectName(), 1, 1, true,
					"@gu_lingfa-give:" + owner->objectName(), true);
				if (c && c->subcardsLength() > 0) {
					room->giveCard(player, owner, c->getSubcards(), objectName());
					done = true;
				}
			}
			if (!done && owner->isAlive()) room->damage(DamageStruct(objectName(), owner, player));
		}
		return false;
	}
};

class GuZhian : public TriggerSkill
{
public:
	GuZhian() : TriggerSkill("gu_zhian")
	{
		events << CardFinished;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !(use.card->getTypeId() == Card::TypeEquip || use.card->isKindOf("DelayedTrick"))) return false;
		foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
			if (!owner->hasSkill(this) || owner->getMark("gu_zhian-Clear") > 0) continue;
			QStringList choices;
			int id = use.card->getEffectiveId();
			Player::Place place = id >= 0 ? room->getCardPlace(id) : Player::PlaceUnknown;
			bool onField = place == Player::PlaceEquip || place == Player::PlaceDelayedTrick;
			if (onField) choices << "discard";
			if (onField && owner->canDiscard(owner, "h")) choices << "obtain";
			if (player->isAlive()) choices << "damage";
			choices << "cancel";
			QString ch = room->askForChoice(owner, objectName(), choices.join("+"), data);
			if (ch == "cancel") continue;
			room->addPlayerMark(owner, "gu_zhian-Clear");
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(owner, objectName());
			if (ch == "discard") {
				ServerPlayer *holder = room->getCardOwner(id);
				room->throwCard(id, objectName(), holder, owner);
			} else if (ch == "obtain") {
				if (room->askForDiscard(owner, objectName(), 1, 1, false, false))
					room->obtainCard(owner, id, objectName(), true);
			} else {
				room->damage(DamageStruct(objectName(), owner, player));
			}
			break;
		}
		return false;
	}
};

class GuJianxiong : public TriggerSkill
{
public:
	GuJianxiong() : TriggerSkill("gu_jianxiong")
	{
		events << Damaged;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card) return false;
		QList<int> ids = damage.card->isVirtualCard() ? damage.card->getSubcards() : QList<int>() << damage.card->getEffectiveId();
		if (ids.isEmpty()) return false;
		foreach (int id, ids) if (room->getCardPlace(id) != Player::PlaceTable) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		room->obtainCard(player, damage.card, objectName(), true);
		return false;
	}
};

void guAudit2025Mobile(Package *pkg)
{
	General *huangquan = new General(pkg, "gu_ovhuangquan", "shu", 4);
	huangquan->addSkill(new GuQuanjian);
	huangquan->addSkill(new GuTujue);
	guRelate(pkg, huangquan, "gu_quanjian", new GuQuanjianLimit);
	pkg->addMetaObject<GuQuanjianCard>();

	General *sunjian = new General(pkg, "gu_qisunjian", "qun", 3);
	sunjian->addSkill(new GuPingtao);
	sunjian->addSkill(new GuJuelie);
	guRelate(pkg, sunjian, "gu_pingtao", new GuPingtaoMod);
	pkg->addMetaObject<GuPingtaoCard>();

	General *lingtong = new General(pkg, "gu_jielingtong", "wu", 4);
	lingtong->addSkill(new GuXuanfeng);
	lingtong->addSkill(new GuYongjin);

	General *caocao = new General(pkg, "gu_quncaocao", "qun", 4);
	caocao->addSkill(new GuLingfa);
	caocao->addRelateSkill("gu_zhian");
	caocao->addRelateSkill("gu_jianxiong");
	pkg->addSkills(new GuZhian);
	pkg->addSkills(new GuJianxiong);
}
