// GU-audit2026 months 2026-03, 2026-04 and 2026-05.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-03 / ### 2026-04 / ### 2026-05.
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "serverplayer.h"
#include "util.h"
#include "hegemony-mode.h"
#include <QSet>
#include <QHash>

static bool guHasGeneral(const ServerPlayer *player, const QString &name)
{
	return player && (player->getGeneralName() == name || player->getGeneral2Name() == name);
}

static bool guCampNamed(ServerPlayer *owner, const QString &name)
{
	if (!owner) return false;
	foreach (ServerPlayer *p, owner->getRoom()->getAlivePlayers()) {
		if (p == owner || !guHasGeneral(p, name)) continue;
		if (owner->isFriendWith(p)) return true;
	}
	return false;
}

static bool guNotIdentity(Room *room)
{
	return room && !isNormalGameMode(room->getMode(), HegemonyMode::enabled());
}

// 合御：骥乐进 / 骥李典同阵营在场。骥乐进本月未实装，仍按武将 id 检测。
static bool guZlHeyu(ServerPlayer *owner)
{
	return owner && owner->hasSkill("gu_heyu")
		&& (guCampNamed(owner, "gu_jiyuejin") || guCampNamed(owner, "gu_jilidian"));
}

static int guChongleiLimit(ServerPlayer *owner)
{
	if (guZlHeyu(owner)) return 3;
	return qMax(1, owner->getMark("gu_heji_choice_lun"));
}

static bool guLdYuejin(ServerPlayer *owner)
{
	return owner && owner->hasSkill("gu_ldheyu") && guNotIdentity(owner->getRoom())
		&& guCampNamed(owner, "gu_jiyuejin");
}

static bool guLdZhang(ServerPlayer *owner)
{
	return owner && owner->hasSkill("gu_ldheyu") && guNotIdentity(owner->getRoom())
		&& guCampNamed(owner, "gu_jizhangliao");
}

static bool guHasGaigongFlag(const Card *card)
{
	if (!card) return false;
	if (card->hasFlag("gu_gaigong_use")) return true;
	int id = card->getEffectiveId();
	if (id >= 0) {
		const Card *real = Sanguosha->getCard(id);
		if (real && real->hasFlag("gu_gaigong_use")) return true;
	}
	foreach (int sub, card->getSubcards()) {
		const Card *real = Sanguosha->getCard(sub);
		if (real && real->hasFlag("gu_gaigong_use")) return true;
	}
	return false;
}

static int guRealSuitCount(const QList<int> &ids)
{
	QSet<int> suits;
	foreach (int id, ids) {
		const Card *card = Sanguosha->getCard(id);
		if (!card) continue;
		Card::Suit suit = card->getSuit();
		if (suit == Card::Spade || suit == Card::Club || suit == Card::Heart || suit == Card::Diamond)
			suits.insert(int(suit));
	}
	return suits.size();
}

class GuChongleiJink : public FilterSkill
{
public:
	GuChongleiJink() : FilterSkill("#gu_chonglei-jink") {}

	bool viewFilter(const Card *to_select) const override
	{
		int id = to_select->getEffectiveId();
		if (id < 0 || Sanguosha->getCardPlace(id) != Player::PlaceHand) return false;
		const Card *real = Sanguosha->getCard(id);
		return real && real->hasFlag("gu_chonglei_jink");
	}

	const Card *viewAs(const Card *originalCard) const override
	{
		Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
		jink->setSkillName("gu_chonglei");
		return jink;
	}
};

struct GuChongleiRefreshGuard {
	Room *room;
	explicit GuChongleiRefreshGuard(Room *r) : room(r) { room->setTag("gu_chonglei_refresh", true); }
	~GuChongleiRefreshGuard() { room->removeTag("gu_chonglei_refresh"); }
};

static void guChongleiRefresh(Room *room, bool inPlay)
{
	if (!room || room->getTag("gu_chonglei_refresh").toBool()) return;
	GuChongleiRefreshGuard guard(room);

	QList<ServerPlayer *> owners;
	foreach (ServerPlayer *p, room->getAlivePlayers())
		if (p->hasSkill("gu_chonglei")) owners << p;

	foreach (ServerPlayer *p, room->getAllPlayers(true)) {
		foreach (int id, p->handCards())
			room->setCardFlag(id, "-gu_chonglei_jink");
		if (p->hasSkill("#gu_chonglei-jink", true)) {
			room->detachSkillFromPlayer(p, "#gu_chonglei-jink", false, true, false);
			room->filterCards(p, p->getHandcards(), true);
		}
	}

	if (inPlay) {
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			bool affect = false;
			bool allHands = false;
			foreach (ServerPlayer *owner, owners) {
				if (owner == p) continue;
				affect = true;
				if (guZlHeyu(owner)) allHands = true;
			}
			if (!affect) continue;
			bool flagged = false;
			foreach (int id, p->handCards()) {
				const Card *card = Sanguosha->getCard(id);
				if (!card) continue;
				if (allHands || card->getTypeId() != Card::TypeBasic) {
					room->setCardFlag(id, "gu_chonglei_jink");
					flagged = true;
				}
			}
			if (!flagged) continue;
			if (!p->hasSkill("#gu_chonglei-jink"))
				room->attachSkillToPlayer(p, "#gu_chonglei-jink");
			room->filterCards(p, p->getHandcards(), true);
		}
	}
}

static bool guPlayNow(Room *room)
{
	ServerPlayer *current = room->getCurrent();
	return current && current->getPhase() == Player::Play;
}

class GuChonglei : public TriggerSkill
{
public:
	GuChonglei() : TriggerSkill("gu_chonglei")
	{
		events << CardResponded << EventPhaseStart << EventPhaseChanging << CardsMoveOneTime
			<< Death << EventLoseSkill << EventAcquireSkill << KingdomChanged << Revived;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardResponded) {
			if (!player->isAlive()) return false;
			CardResponseStruct resp = data.value<CardResponseStruct>();
			if (resp.m_isRetrial || !resp.m_card) return false;
			QList<ServerPlayer *> owners;
			foreach (ServerPlayer *p, room->getAlivePlayers())
				if (p->hasSkill(objectName())) owners << p;
			room->sortByActionOrder(owners);
			foreach (ServerPlayer *owner, owners) {
				ServerPlayer *victim = nullptr;
				if (player == owner && resp.m_who && resp.m_who != owner)
					victim = resp.m_who;
				else if (resp.m_who == owner && player != owner)
					victim = player;
				if (!victim || !victim->isAlive() || victim == owner) continue;
				QList<int> hands = victim->handCards();
				if (hands.isEmpty()) continue;
				if (owner->getMark("gu_chonglei_got_lun") >= guChongleiLimit(owner)) continue;
				room->sendCompulsoryTriggerLog(owner, this);
				DummyCard *dummy = new DummyCard(hands);
				room->obtainCard(owner, dummy, objectName(), false);
				dummy->deleteLater();
				room->addPlayerMark(owner, "gu_chonglei_got_lun");
			}
			return false;
		}

		if (event == EventPhaseStart) {
			guChongleiRefresh(room, player->getPhase() == Player::Play);
		} else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::Play || change.from == Player::Play)
				guChongleiRefresh(room, change.to == Player::Play);
		} else if (event == CardsMoveOneTime) {
			if (!guPlayNow(room)) return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place == Player::PlaceHand || move.from_places.contains(Player::PlaceHand))
				guChongleiRefresh(room, true);
		} else if (event == EventLoseSkill || event == EventAcquireSkill) {
			SkillChangeStruct change;
			if (!change.tryParse(data)) return false;
			if (change.skillName != objectName() && change.skillName != "gu_heyu"
				&& change.skillName != "#gu_chonglei-jink")
				return false;
			guChongleiRefresh(room, guPlayNow(room));
		} else {
			guChongleiRefresh(room, guPlayNow(room));
		}
		return false;
	}
};

class GuDangshi : public TriggerSkill
{
public:
	GuDangshi() : TriggerSkill("gu_dangshi")
	{
		events << CardFinished;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->getTypeId() <= 0 || !use.card->isDamageCard())
			return false;
		QList<ServerPlayer *> victims;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (!use.to.contains(p)) victims << p;
		if (victims.isEmpty() || !player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		int x = guZlHeyu(player) ? 3 : qMax(1, player->getMark("gu_heji_choice_lun"));
		room->sortByActionOrder(victims);
		foreach (ServerPlayer *p, victims) {
			if (!p->isAlive()) continue;
			int can = 0;
			foreach (const Card *card, p->getCards("he"))
				if (p->canDiscard(p, card->getEffectiveId())) can++;
			QStringList choices;
			if (can >= x) choices << "discard=" + QString::number(x);
			choices << "damage";
			QString choice = room->askForChoice(p, objectName(), choices.join("+"), data);
			if (choice.startsWith("discard"))
				room->askForDiscard(p, objectName(), x, x, false, true);
			else if (p->isAlive())
				room->damage(DamageStruct(objectName(), player->isAlive() ? player : nullptr, p));
			room->addPlayerMark(player, "gu_heji_choice_lun");
		}
		return false;
	}
};

class GuHeyu : public Skill
{
public:
	GuHeyu() : Skill("gu_heyu", Compulsory) {}
};

class GuDuanjin : public TriggerSkill
{
public:
	GuDuanjin() : TriggerSkill("gu_duanjin")
	{
		events << CardUsed << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const override
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() <= 0 || use.from != player) return false;
		if (event == CardUsed) {
			room->setPlayerMark(player, "gu_duanjin_used-Clear", 1);
			return false;
		}
		if (!player->hasSkill(objectName()) || use.card->getTypeId() != Card::TypeBasic) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->getMark("gu_duanjin_used-Clear") > 0 && player->canDiscard(p, "he"))
				targets << p;
		}
		if (targets.isEmpty() || !player->askForSkillInvoke(this, data)) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@gu_duanjin", false, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
		if (id < 0) return false;
		room->throwCard(id, objectName(), target, player);
		if (player->isAlive() && guLdYuejin(player) && room->getCardPlace(id) == Player::DiscardPile) {
			room->sendCompulsoryTriggerLog(player, "gu_ldheyu");
			room->obtainCard(player, id, objectName(), true);
		}
		return false;
	}
};

class GuGaigong : public TriggerSkill
{
public:
	GuGaigong() : TriggerSkill("gu_gaigong")
	{
		events << Damage << Damaged << PreCardUsed;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !guHasGaigongFlag(use.card) || !guLdZhang(player)) return false;
			room->sendCompulsoryTriggerLog(player, "gu_ldheyu");
			use.no_respond_list << "_ALL_TARGETS";
			data.setValue(use);
			return false;
		}

		if (player->getMark("gu_gaigong-Clear") > 0) return false;
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.damage <= 0) return false;
		ServerPlayer *other = nullptr;
		if (event == Damage) {
			if (damage.from != player || !damage.to || damage.to == player) return false;
			other = damage.to;
		} else {
			if (damage.to != player || !damage.from || damage.from == player) return false;
			other = damage.from;
		}
		bool selfOk = !player->isKongcheng();
		bool otherOk = other->isAlive() && !other->isKongcheng();
		if (!selfOk && !otherOk) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		room->setPlayerMark(player, "gu_gaigong-Clear", 1);

		QString who = "self";
		if (selfOk && otherOk)
			who = room->askForChoice(player, objectName(), "self+other", data);
		else if (otherOk)
			who = "other";
		ServerPlayer *holder = who == "other" ? other : player;
		if (!holder->isAlive() || holder->isKongcheng()) return false;
		int maxn = qMin(2, holder->getHandcardNum());
		QList<int> shown = room->askForCardsChosen(player, holder, "h", objectName(), 1, maxn, false,
			Card::MethodNone, QList<int>(), false);
		if (shown.isEmpty()) {
			room->setPlayerMark(player, "gu_gaigong-Clear", 0);
			return false;
		}
		int n = shown.length();
		if (room->getDrawPile().length() + room->getDiscardPile().length() < n) {
			room->setPlayerMark(player, "gu_gaigong-Clear", 0);
			return false;
		}
		room->showCard(holder, shown);
		QList<int> bottoms = room->getNCards(n, false, false);
		if (bottoms.length() < n) {
			if (!bottoms.isEmpty()) room->returnToEndDrawPile(bottoms);
			room->setPlayerMark(player, "gu_gaigong-Clear", 0);
			return false;
		}
		QList<int> exchanged = shown + bottoms;
		room->moveCardsToEndOfDrawpile(player, shown, objectName(), true, false);
		if (holder->isAlive()) {
			DummyCard *dummy = new DummyCard(bottoms);
			room->obtainCard(holder, dummy, objectName(), true);
			dummy->deleteLater();
		} else {
			room->returnToEndDrawPile(bottoms);
		}
		if (!player->isAlive() || guRealSuitCount(exchanged) < 3) return false;
		QList<int> usable;
		foreach (int id, bottoms) {
			if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand) continue;
			const Card *card = Sanguosha->getCard(id);
			if (card && card->isAvailable(player)) usable << id;
		}
		if (usable.isEmpty()) return false;
		room->fillAG(usable, player);
		int pick = room->askForAG(player, usable, true, objectName(), "@gu_gaigong-use");
		room->clearAG(player);
		if (pick < 0 || room->getCardOwner(pick) != player || room->getCardPlace(pick) != Player::PlaceHand)
			return false;
		room->setCardFlag(pick, "gu_gaigong_use");
		room->askForUseCard(player, QString::number(pick), "@gu_gaigong-use");
		room->setCardFlag(pick, "-gu_gaigong_use");
		return false;
	}
};

class GuLdHeyu : public Skill
{
public:
	GuLdHeyu() : Skill("gu_ldheyu") {}
};

// ---------------- 2026-04 朱绩（→施绩）／谋曹操 ----------------

static int guClosestUniqueBelow(Room *room, ServerPlayer *owner)
{
	QHash<int, int> counts;
	foreach (ServerPlayer *p, room->getAlivePlayers())
		counts[p->getHandcardNum()]++;
	int hand = owner->getHandcardNum();
	int best = -1;
	int bestDist = 0;
	foreach (int count, counts.keys()) {
		if (counts.value(count) != 1 || count >= hand) continue;
		int dist = hand - count;
		if (best < 0 || dist < bestDist) {
			best = count;
			bestDist = dist;
		}
	}
	return best;
}

static QString guTipValue(const QStringList &tips, const QString &prefix)
{
	foreach (const QString &tip, tips) {
		if (tip.startsWith(prefix)) return tip.mid(prefix.length());
	}
	return QString();
}

class GuJiezhui : public TriggerSkill
{
public:
	GuJiezhui() : TriggerSkill("gu_jiezhui")
	{
		events << EventPhaseStart << Damage;
	}
	bool triggerable(const ServerPlayer *target, Room *room, TriggerEvent event) const override
	{
		if (!target || !target->isAlive() || !room) return false;
		if (event == Damage) return true;
		if (event != EventPhaseStart || target->getPhase() != Player::RoundStart) return false;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (p->hasSkill(this)) return true;
		return false;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.to) return false;
			const QString prefix = QStringLiteral("gu_jiezhui_");
			foreach (const QString &flag, damage.card->getFlags()) {
				if (!flag.startsWith(prefix)) continue;
				const QString key = QStringLiteral("gu_jiezhui_hit_") + flag.mid(prefix.length());
				QStringList hit = room->getTag(key).toStringList();
				if (!hit.contains(damage.to->objectName())) {
					hit << damage.to->objectName();
					room->setTag(key, hit);
				}
			}
			return false;
		}
		if (player->getPhase() != Player::RoundStart) return false;
		QList<ServerPlayer *> owners;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->hasSkill(this) && p->getMark("gu_jiezhui-Clear") == 0)
				owners << p;
		}
		room->sortByActionOrder(owners);
		foreach (ServerPlayer *owner, owners) {
			if (!owner->isAlive() || owner->getMark("gu_jiezhui-Clear") > 0 || !owner->hasSkill(this))
				continue;
			int unique = guClosestUniqueBelow(room, owner);
			if (unique < 0) continue;
			int n = owner->getHandcardNum() - unique;
			if (n <= 0) continue;
			int can = 0;
			foreach (const Card *card, owner->getHandcards())
				if (owner->canDiscard(owner, card->getEffectiveId())) can++;
			if (can < n) continue;
			if (!room->askForDiscard(owner, objectName(), n, n, true, false,
				"@gu_jiezhui:" + QString::number(n)))
				continue;
			room->setPlayerMark(owner, "gu_jiezhui-Clear", 1);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(owner, objectName());
			if (!owner->isAlive()) continue;
			Slash *slash = new Slash(Card::NoSuit, 0);
			slash->setSkillName(objectName());
			slash->deleteLater();
			QList<ServerPlayer *> cands;
			foreach (ServerPlayer *p, room->getOtherPlayers(owner))
				if (owner->canSlash(p, slash, false) && !owner->isProhibited(p, slash))
					cands << p;
			if (cands.isEmpty()) continue;
			int maxn = qMin(n, cands.length());
			QList<ServerPlayer *> tos = room->askForPlayersChosen(owner, cands, objectName(), 1, maxn,
				"@gu_jiezhui-slash", true);
			if (tos.isEmpty() || !owner->isAlive()) continue;
			int seq = room->getTag("gu_jiezhui_seq").toInt() + 1;
			room->setTag("gu_jiezhui_seq", seq);
			const QString token = owner->objectName() + "_" + QString::number(seq);
			const QString flag = QStringLiteral("gu_jiezhui_") + token;
			const QString hitKey = QStringLiteral("gu_jiezhui_hit_") + token;
			slash->setFlags(flag);
			room->setTag(hitKey, QStringList());
			room->useCard(CardUseStruct(slash, owner, tos), true);
			QStringList hit = room->getTag(hitKey).toStringList();
			room->removeTag(hitKey);
			if (!owner->isAlive()) continue;
			bool all = true;
			foreach (ServerPlayer *t, tos)
				if (!hit.contains(t->objectName())) all = false;
			int need = unique - owner->getHandcardNum();
			if (all && need > 0) owner->drawCards(need, objectName());
		}
		return false;
	}
};

static void guHuanshiRename(Room *room, ServerPlayer *player)
{
	if (!player || player->getMark("gu_huanshi_named") > 0 || !player->hasSkill("gu_huanshi"))
		return;
	room->setPlayerMark(player, "gu_huanshi_named", 1);
	room->sendCompulsoryTriggerLog(player, "gu_huanshi");
	if (player->getGeneralName() == "gu_zhuji")
		room->changePlayerGeneral(player, "gu_shiji");
	else if (player->getGeneral2Name() == "gu_zhuji")
		room->changePlayerGeneral2(player, "gu_shiji");
	if (!player->hasSkill("gu_jianlv"))
		room->acquireSkill(player, "gu_jianlv");
}

static void guHuanshiRecast(Room *room, ServerPlayer *player)
{
	if (!player || !player->isAlive() || player->getPhase() != Player::Play) return;
	if (room->getCurrent() != player) return;
	if (room->getTag("gu_huanshi_recast_busy").toBool()) return;
	room->setTag("gu_huanshi_recast_busy", true);
	while (player->isAlive() && player->getPhase() == Player::Play) {
		QList<int> ids;
		foreach (const Card *card, player->getHandcards()) {
			if (!card->isKindOf("Analeptic") || player->isCardLimited(card, Card::MethodRecast))
				continue;
			ids << card->getEffectiveId();
		}
		if (ids.isEmpty()) break;
		room->fillAG(ids, player);
		int id = room->askForAG(player, ids, true, "gu_huanshi", "@gu_huanshi-recast");
		room->clearAG(player);
		if (!ids.contains(id) || room->getCardOwner(id) != player
			|| room->getCardPlace(id) != Player::PlaceHand)
			break;
		room->recastCard(player, id, "gu_huanshi");
	}
	room->removeTag("gu_huanshi_recast_busy");
}

class GuHuanshi : public TriggerSkill
{
public:
	GuHuanshi() : TriggerSkill("gu_huanshi")
	{
		events << CardUsed << ConfirmDamage << DamageInflicted << Damage << Damaged
			<< EventPhaseStart << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target && target->hasSkill(objectName());
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || !use.card->isKindOf("Slash")) return false;
			if (player->getMark("gu_huanshi_slash-Clear") > 0) return false;
			room->addPlayerMark(player, "gu_huanshi_slash-Clear");
			room->setCardFlag(use.card, "gu_huanshi_plus");
			return false;
		}
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player) return false;
			bool changed = false;
			if (damage.card && damage.card->hasFlag("gu_huanshi_plus")
				&& guTipValue(damage.tips, QStringLiteral("gu_huanshi_plus_done:")).isEmpty()) {
				damage.damage++;
				damage.tips << QStringLiteral("gu_huanshi_plus_done:1");
				changed = true;
			}
			if (guTipValue(damage.tips, QStringLiteral("gu_huanshi_src:")).isEmpty()) {
				damage.tips << QStringLiteral("gu_huanshi_src:") + QString::number(player->getHp());
				changed = true;
			}
			if (changed) data = QVariant::fromValue(damage);
			return false;
		}
		if (event == DamageInflicted) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.to != player) return false;
			if (guTipValue(damage.tips, QStringLiteral("gu_huanshi_hp:")).isEmpty()) {
				damage.tips << QStringLiteral("gu_huanshi_hp:") + QString::number(player->getHp());
				data = QVariant::fromValue(damage);
			}
			return false;
		}
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || damage.damage <= 0) return false;
			QString snap = guTipValue(damage.tips, QStringLiteral("gu_huanshi_src:"));
			int hp = snap.isEmpty() ? player->getHp() : snap.toInt();
			if (hp > 0 && damage.damage == hp) guHuanshiRename(room, player);
			return false;
		}
		if (event == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.to != player || damage.damage <= 0) return false;
			QString snap = guTipValue(damage.tips, QStringLiteral("gu_huanshi_hp:"));
			int hp = snap.isEmpty() ? -1 : snap.toInt();
			if (hp > 0 && damage.damage == hp) guHuanshiRename(room, player);
			return false;
		}
		if (event == EventPhaseStart) {
			if (player->isAlive() && player->getPhase() == Player::Play)
				guHuanshiRecast(room, player);
			return false;
		}
		if (!player->isAlive() || player->getPhase() != Player::Play || room->getCurrent() != player)
			return false;
		if (room->getTag("gu_huanshi_recast_busy").toBool()) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to != player || move.to_place != Player::PlaceHand) return false;
		bool wine = false;
		foreach (int id, move.card_ids) {
			const Card *card = Sanguosha->getCard(id);
			if (card && card->isKindOf("Analeptic") && room->getCardOwner(id) == player
				&& room->getCardPlace(id) == Player::PlaceHand)
				wine = true;
		}
		if (wine) guHuanshiRecast(room, player);
		return false;
	}
};

class GuHuanshiProhibit : public ProhibitSkill
{
public:
	GuHuanshiProhibit() : ProhibitSkill("#gu_huanshi-prohibit") {}
	bool isProhibited(const Player *from, const Player *, const Card *card, const QList<const Player *> &) const override
	{
		return from && card && card->isKindOf("Analeptic") && from->hasSkill("gu_huanshi")
			&& !from->hasFlag("Global_Dying");
	}
};

class GuJianlv : public TriggerSkill
{
public:
	GuJianlv() : TriggerSkill("gu_jianlv")
	{
		events << CardsMoveOneTime;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player || move.to_place != Player::DiscardPile) return false;
		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
			return false;
		int n = 0;
		for (int i = 0; i < move.card_ids.length(); i++) {
			Player::Place place = i < move.from_places.length() ? move.from_places.at(i) : Player::PlaceUnknown;
			if (place == Player::PlaceHand || place == Player::PlaceEquip) n++;
		}
		int need = player->getMark("gu_jianlv_times") + 1;
		if (n < need) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) targets << p;
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@gu_jianlv", true, true);
		if (!target || !player->isAlive()) return false;
		int before = player->getMark("damage_point_round");
		room->damage(DamageStruct(objectName(), player, target));
		if (!player->isAlive() || player->getMark("damage_point_round") <= before) return false;
		room->addPlayerMark(player, "gu_jianlv_times");
		if (target->isAlive()) return false;
		QList<ServerPlayer *> others;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (p != target) others << p;
		QStringList choices;
		choices << "reset";
		if (!others.isEmpty()) choices << "again";
		choices << "cancel";
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "reset")
			room->setPlayerMark(player, "gu_jianlv_times", 0);
		else if (choice == "again" && !others.isEmpty()) {
			ServerPlayer *second = room->askForPlayerChosen(player, others, objectName(), "@gu_jianlv-again", true, true);
			if (second && player->isAlive())
				room->damage(DamageStruct(objectName(), player, second));
		}
		return false;
	}
};

static bool guObtainDamageCard(Room *room, int id)
{
	if (id < 0) return false;
	Player::Place place = room->getCardPlace(id);
	return room->getCardOwner(id) == nullptr || place == Player::DiscardPile || place == Player::PlaceTable;
}

class GuMxJianxiong : public TriggerSkill
{
public:
	GuMxJianxiong() : TriggerSkill("gu_mxjianxiong")
	{
		events << GameStart << Damaged;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == GameStart) {
			int n = room->askForChoice(player, objectName(), "0+1+2").toInt();
			if (n > 0) player->gainMark("&gu_zhishi", qMin(2, n));
			return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to != player || damage.damage <= 0) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		int x = player->getMark("&gu_zhishi");
		if (damage.card) {
			QList<int> ids = damage.card->getSubcards();
			if (ids.isEmpty()) {
				int id = damage.card->getEffectiveId();
				if (id >= 0) ids << id;
			}
			foreach (int id, ids) {
				if (player->isAlive() && guObtainDamageCard(room, id))
					room->obtainCard(player, id, objectName(), true);
			}
		}
		int draw = qMax(0, 3 - x);
		if (draw > 0 && player->isAlive()) player->drawCards(draw, objectName());
		if (x > 0 && player->isAlive() && player->getMark("&gu_zhishi") > 0
			&& room->askForChoice(player, objectName(), "discard+keep", data) == "discard")
			player->loseMark("&gu_zhishi");
		return false;
	}
};

class GuQingzheng : public TriggerSkill
{
public:
	GuQingzheng() : TriggerSkill("gu_qingzheng")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::Play) return false;
		room->sendCompulsoryTriggerLog(player, this);
		int x = player->getMark("&gu_zhishi");
		int need = qMax(0, 3 - x);
		QHash<QString, QList<int>> bySuit;
		foreach (const Card *card, player->getHandcards()) {
			if (!player->canDiscard(player, card->getEffectiveId())) continue;
			QString suit = card->getSuitString();
			if (suit != "spade" && suit != "heart" && suit != "club" && suit != "diamond") continue;
			bySuit[suit] << card->getEffectiveId();
		}
		QStringList left = bySuit.keys();
		int kinds = qMin(need, left.length());
		QList<int> thrown;
		for (int i = 0; i < kinds && !left.isEmpty() && player->isAlive(); i++) {
			QString suit = left.length() == 1 ? left.first()
				: room->askForChoice(player, objectName(), left.join("+"));
			if (!left.contains(suit)) suit = left.first();
			thrown << bySuit.value(suit);
			left.removeAll(suit);
		}
		if (!thrown.isEmpty() && player->isAlive())
			room->throwCard(thrown, objectName(), player, player);
		int mine = thrown.length();
		QList<ServerPlayer *> watch;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (!p->isKongcheng()) watch << p;
		if (player->isAlive() && !watch.isEmpty()) {
			ServerPlayer *target = room->askForPlayerChosen(player, watch, objectName(), "@gu_qingzheng", false, true);
			if (target && target->isAlive() && !target->isKongcheng()) {
				int id = room->doGongxin(player, target, target->handCards(), objectName());
				player->removeTag(objectName());
				QString suit;
				if (id >= 0 && Sanguosha->getCard(id)) suit = Sanguosha->getCard(id)->getSuitString();
				if (suit != "spade" && suit != "heart" && suit != "club" && suit != "diamond") {
					suit.clear();
					foreach (const Card *card, target->getHandcards()) {
						QString s = card->getSuitString();
						if (s == "spade" || s == "heart" || s == "club" || s == "diamond") {
							suit = s;
							break;
						}
					}
				}
				QList<int> drop;
				if (!suit.isEmpty()) {
					foreach (const Card *card, target->getHandcards())
						if (card->getSuitString() == suit && player->canDiscard(target, card->getEffectiveId()))
							drop << card->getEffectiveId();
				}
				if (!drop.isEmpty())
					room->throwCard(drop, objectName(), target, player);
				if (drop.length() < mine && player->isAlive() && target->isAlive())
					room->damage(DamageStruct(objectName(), player, target));
			}
		}
		if (x < 2 && player->isAlive() && player->hasSkill("gu_mxjianxiong")
			&& room->askForChoice(player, objectName(), "gain+cancel") == "gain")
			player->gainMark("&gu_zhishi");
		return false;
	}
};

static bool guCardFlag(const Card *card, const QString &flag)
{
	if (!card) return false;
	if (card->hasFlag(flag)) return true;
	const int id = card->getEffectiveId();
	if (id < 0) return false;
	const Card *real = Sanguosha->getCard(id);
	return real && real->hasFlag(flag);
}

// 驱乘：杀仅指定你的上家或下家、未造成伤害、且其沿该方向的邻座不是你。
// 不计次数（仅当此次计入了次数）并视为对那个邻座出杀。无「可以」，按锁定技。
class GuQucheng : public TriggerSkill
{
public:
	GuQucheng() : TriggerSkill("gu_qucheng")
	{
		events << Damage << CardFinished;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from != player || !damage.card || !damage.card->isKindOf("Slash")) return false;
			if (!guCardFlag(damage.card, "gu_qucheng_hit"))
				room->setCardFlag(damage.card, "gu_qucheng_hit");
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || !use.card->isKindOf("Slash") || use.to.length() != 1)
			return false;
		if (guCardFlag(use.card, "gu_qucheng_hit")) return false;
		ServerPlayer *target = use.to.first();
		if (!target) return false;
		ServerPlayer *down = qobject_cast<ServerPlayer *>(player->getNextAlive());
		ServerPlayer *up = qobject_cast<ServerPlayer *>(player->getLastAlive());
		ServerPlayer *chain = nullptr;
		if (target == down && target != up)
			chain = qobject_cast<ServerPlayer *>(target->getNextAlive());
		else if (target == up && target != down)
			chain = qobject_cast<ServerPlayer *>(target->getLastAlive());
		if (!chain || !chain->isAlive() || chain == player) return false;
		room->sendCompulsoryTriggerLog(player, this);
		if (use.m_addHistory)
			room->addPlayerHistory(player, use.card->getClassName(), -1);
		if (room->getTag("gu_qucheng_depth").toInt() > room->alivePlayerCount()) return false;
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName(objectName());
		slash->deleteLater();
		if (!player->isAlive() || !player->canSlash(chain, slash, true)) return false;
		const int depth = room->getTag("gu_qucheng_depth").toInt();
		room->setTag("gu_qucheng_depth", depth + 1);
		try {
			room->useCard(CardUseStruct(slash, player, chain), true);
		} catch (...) {
			room->setTag("gu_qucheng_depth", depth);
			throw;
		}
		room->setTag("gu_qucheng_depth", depth);
		return false;
	}
};

// 羌勇：杀造成伤害时弃置受伤角色 X 张牌（手牌+装备，X＝本回合出杀数，不足则全弃）。
// 其无手牌则该次伤害+1，判定在弃牌之前。无「可以」，按锁定技。
class GuQiangyong : public TriggerSkill
{
public:
	GuQiangyong() : TriggerSkill("gu_qiangyong")
	{
		events << ConfirmDamage << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player || !damage.to || !damage.card || !damage.card->isKindOf("Slash"))
			return false;
		if (event == ConfirmDamage) {
			if (!damage.to->isAlive() || !damage.to->isKongcheng()) return false;
			if (damage.tips.contains(QStringLiteral("gu_qiangyong_plus"))) return false;
			damage.tips << QStringLiteral("gu_qiangyong_plus");
			damage.damage += 1;
			data = QVariant::fromValue(damage);
			room->sendCompulsoryTriggerLog(player, this);
			return false;
		}
		if (!damage.to->isAlive() || damage.tips.contains(QStringLiteral("gu_qiangyong_discarded")))
			return false;
		damage.tips << QStringLiteral("gu_qiangyong_discarded");
		data = QVariant::fromValue(damage);
		const int x = player->getSlashCount();
		int can = 0;
		foreach (const Card *card, damage.to->getCards("he"))
			if (player->canDiscard(damage.to, card->getEffectiveId())) can++;
		const int n = qMin(x, can);
		if (n <= 0) return false;
		room->sendCompulsoryTriggerLog(player, this);
		const QList<int> ids = room->askForCardsChosen(player, damage.to, "he", objectName(), n, n,
			false, Card::MethodDiscard, QList<int>(), false);
		if (!ids.isEmpty())
			room->throwCard(ids, objectName(), damage.to, player);
		return false;
	}
};

// 灼魂黄：标记在变色角色身上。「其本回合非锁定技失效」。
// 标记沿用现成的 &zhuohun2-Clear（当前回合结束清除，不改施加时机）。
class GuZhuohunInvalidity : public InvaliditySkill
{
public:
	GuZhuohunInvalidity() : InvaliditySkill("#gu_zhuohun-invalidity")
	{
	}

	bool isSkillValid(const Player *player, const Skill *skill) const override
	{
		if (!player || player->getMark("&zhuohun2-Clear") <= 0) return true;
		if (!skill || skill->inherits("InvaliditySkill")) return true;
		if (skill->getFrequency(player) == Skill::Compulsory) return true;
		return false;
	}
};

void guAudit2026Mobile(Package *pkg)
{
	General *zhangliao = new General(pkg, "gu_jizhangliao", "wei", 4);
	zhangliao->addSkill(new GuChonglei);
	zhangliao->addSkill(new GuDangshi);
	zhangliao->addSkill(new GuHeyu);

	General *lidian = new General(pkg, "gu_jilidian", "wei", 3);
	lidian->addSkill(new GuDuanjin);
	lidian->addSkill(new GuGaigong);
	lidian->addSkill(new GuLdHeyu);

	pkg->addSkills(new GuChongleiJink);

	General *zhuji = new General(pkg, "gu_zhuji", "wu", 4);
	zhuji->addSkill(new GuJiezhui);
	zhuji->addSkill(new GuHuanshi);
	pkg->addSkills(new GuHuanshiProhibit);
	pkg->insertRelatedSkills("gu_huanshi", "#gu_huanshi-prohibit");

	General *shiji = new General(pkg, "gu_shiji", "wu", 4, true, true);
	shiji->addSkill("gu_jiezhui");
	shiji->addSkill("gu_huanshi");
	shiji->addSkill(new GuJianlv);

	General *caocao = new General(pkg, "gu_moucaocao", "wei", 4);
	caocao->addSkill(new GuMxJianxiong);
	caocao->addSkill(new GuQingzheng);

	pkg->addSkills(new GuZhuohunInvalidity);

	General *cheliji = new General(pkg, "gu_cheliji", "qun", 4);
	cheliji->addSkill(new GuQucheng);
	cheliji->addSkill(new GuQiangyong);

	guAudit2026June(pkg);
	guAudit2026JuneB(pkg);
	guAudit2026M07a(pkg);
	guAudit2026M07b(pkg);
	guAudit2026M07c(pkg);
	guAudit2026M07d(pkg);
	guAudit2026M08a(pkg);
	guAudit2026M08b(pkg);
	guAudit2026M08c(pkg);
	guAudit2026M08d(pkg);
}
