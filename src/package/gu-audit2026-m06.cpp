// GU-audit2026 month 2026-06: 荀勖 / 史阿 / 威张星彩 / 武张飞.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-06 (verbatim in Mobile.lua).
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "game-rng.h"
#include "skill-dialog-info.h"

#include <QSet>

static bool guPlayerZone(Player::Place place)
{
	return place == Player::PlaceHand || place == Player::PlaceEquip
		|| place == Player::PlaceDelayedTrick || place == Player::PlaceJudge;
}

static bool guLeftZone(const CardsMoveOneTimeStruct &move, ServerPlayer *player)
{
	if (!player || move.from != player) return false;
	for (int i = 0; i < move.card_ids.size(); ++i) {
		const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
		if (!guPlayerZone(from)) continue;
		if (!(move.to == player && guPlayerZone(move.to_place))) return true;
	}
	return false;
}

static int guForemostDiscard(Room *room, const QList<int> &ids)
{
	const QList<int> pile = room->getDiscardPile();
	int best = -1;
	int bestIndex = pile.size();
	foreach (int id, ids) {
		const int idx = pile.indexOf(id);
		if (idx >= 0 && idx < bestIndex) {
			bestIndex = idx;
			best = id;
		}
	}
	return best;
}

static QList<int> guCardIds(const Card *card)
{
	QList<int> ids;
	if (!card) return ids;
	if (card->isVirtualCard() && !card->getSubcards().isEmpty()) {
		foreach (int id, card->getSubcards())
			if (id >= 0) ids << id;
	} else {
		const int id = card->getEffectiveId();
		if (id >= 0) ids << id;
	}
	return ids;
}

static int guAbolishedAreas(const Player *player)
{
	int n = 0;
	if (!player) return 5;
	for (int i = 0; i < 5; ++i)
		if (!player->hasEquipArea(i)) ++n;
	return n;
}

// ---------------- 荀勖 笛音 ----------------
// One move that leaves hand/equip/judge counts as one 次. The armed obtain
// takes the foremost card of the next discard-pile entry, not the 7th loss itself.

class GuDiyin : public TriggerSkill
{
public:
	GuDiyin() : TriggerSkill("gu_diyin")
	{
		events << CardsMoveOneTime << Damaged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Damaged) {
			if (!player->isAlive()) return false;
			room->sendCompulsoryTriggerLog(player, this);
			QList<ServerPlayer *> same;
			foreach (ServerPlayer *p, room->getAlivePlayers())
				if (p->getHp() == player->getHp()) same << p;
			room->sortByActionOrder(same);
			foreach (ServerPlayer *p, same)
				if (p->isAlive()) p->drawCards(1, objectName());
			return false;
		}
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (player->getMark("gu_diyin_arm") > 0 && move.to_place == Player::DiscardPile) {
			const int id = guForemostDiscard(room, move.card_ids);
			if (id >= 0) {
				room->removePlayerMark(player, "gu_diyin_arm");
				room->sendCompulsoryTriggerLog(player, this);
				room->obtainCard(player, id, objectName());
			}
		}
		if (!guLeftZone(move, player)) return false;
		const int n = player->getMark("gu_diyin_loss") + 1;
		if (n >= 7) {
			room->setPlayerMark(player, "gu_diyin_loss", 0);
			room->addPlayerMark(player, "gu_diyin_arm");
		} else {
			room->setPlayerMark(player, "gu_diyin_loss", n);
		}
		return false;
	}
};

// ---------------- 荀勖 博洽 ----------------
// Play-phase view-as only. Success = three real suits differ OR three types differ.

static QStringList guBoqiaPool()
{
	static QStringList cached;
	if (!cached.isEmpty()) return cached;
	QStringList names;
	QSet<QString> seen;
	const QStringList basics = Sanguosha->getCardNames("BasicCard");
	const QStringList tricks = Sanguosha->getCardNames("TrickCard");
	QStringList all = basics;
	all << tricks;
	foreach (const QString &name, all) {
		if (name.startsWith(QLatin1Char('_')) || seen.contains(name)) continue;
		Card *card = Sanguosha->cloneCard(name);
		if (!card) continue;
		const bool ok = card->isKindOf("BasicCard") || (card->isNDTrick() && !card->isDamageCard());
		delete card;
		if (!ok) continue;
		seen.insert(name);
		names << name;
	}
	if (!names.isEmpty()) cached = names;
	return names;
}

static SkillDialogInfo guBoqiaDialog()
{
	QStringList avail;
	foreach (const QString &name, guBoqiaPool()) {
		if (Self && Self->getMark(QStringLiteral("gu_boqia_u_%1-Clear").arg(name)) > 0) continue;
		avail << name;
	}
	return SkillDialogInfo::juguan(QStringLiteral("gu_boqia"), avail.join(QStringLiteral(",")));
}

static bool guBoqiaPlayable(const Player *player)
{
	if (!player || player->getMark("gu_boqia_off-Clear") > 0 || player->getCardCount(true, false) < 3)
		return false;
	foreach (const QString &name, guBoqiaPool()) {
		if (player->getMark(QStringLiteral("gu_boqia_u_%1-Clear").arg(name)) > 0) continue;
		Card *card = Sanguosha->cloneCard(name);
		if (!card) continue;
		card->setSkillName(QStringLiteral("gu_boqia"));
		const bool ok = card->isAvailable(player);
		delete card;
		if (ok) return true;
	}
	return false;
}

class GuBoqiaVS : public ViewAsSkill
{
public:
	GuBoqiaVS() : ViewAsSkill("gu_boqia") {}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		return Self && selected.length() < 3 && to_select && Self->hasCard(to_select) && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (!Self || cards.length() != 3 || Self->getMark("gu_boqia_off-Clear") > 0) return nullptr;
		const Card *picked = Self->getTag(QStringLiteral("gu_boqia")).value<const Card *>();
		if (!picked) return nullptr;
		QString name = picked->objectName();
		if (name.isEmpty()) name = picked->getClassName();
		if (Self->getMark(QStringLiteral("gu_boqia_u_%1-Clear").arg(name)) > 0) return nullptr;
		Card *card = Sanguosha->cloneCard(name, Card::SuitToBeDecided, -1);
		if (!card) return nullptr;
		card->setSkillName(objectName());
		card->addSubcards(cards);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return guBoqiaPlayable(player);
	}

	bool isEnabledAtResponse(const Player *, const QString &) const override
	{
		return false;
	}

	bool isEnabledAtNullification(const ServerPlayer *) const override
	{
		return false;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return guBoqiaDialog();
	}
};

class GuBoqia : public TriggerSkill
{
public:
	GuBoqia() : TriggerSkill("gu_boqia")
	{
		events << PreCardUsed << CardFinished;
		view_as_skill = new GuBoqiaVS;
	}

	SkillDialogInfo getDialogInfo() const override
	{
		return guBoqiaDialog();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.from != player || use.card->getSkillName() != objectName()) return false;
		QString name = use.card->objectName();
		if (name.isEmpty()) name = use.card->getClassName();
		const QString mark = QStringLiteral("gu_boqia_u_%1-Clear").arg(name);
		if (event == PreCardUsed) {
			room->setPlayerMark(player, mark, 1);
			return false;
		}
		bool different = false;
		const QList<int> ids = use.card->getSubcards();
		if (ids.size() == 3) {
			QSet<int> suits, types;
			foreach (int id, ids) {
				const Card *card = Sanguosha->getCard(id);
				if (!card) continue;
				const Card::Suit suit = card->getSuit();
				if (suit == Card::Spade || suit == Card::Club || suit == Card::Heart || suit == Card::Diamond)
					suits.insert(int(suit));
				types.insert(int(card->getTypeId()));
			}
			different = suits.size() == 3 || types.size() == 3;
		}
		if (different) {
			room->setPlayerMark(player, mark, 0);
			QList<ServerPlayer *> alive = room->getAlivePlayers();
			if (!alive.isEmpty() && player->isAlive()) {
				ServerPlayer *target = room->askForPlayerChosen(player, alive, objectName(), "@gu_boqia-adjust", false);
				if (target && target->isAlive()) {
					const int gap = target->getMaxHp() - target->getHandcardNum();
					if (gap > 0) target->drawCards(gap, objectName());
					else if (gap < 0)
						room->askForDiscard(target, objectName(), -gap, -gap, false, false, "@gu_boqia-discard");
				}
			}
		} else {
			room->setPlayerMark(player, "gu_boqia_off-Clear", 1);
		}
		return false;
	}
};

// ---------------- 史阿 砺刃 ----------------

class GuLirenRange : public AttackRangeSkill
{
public:
	GuLirenRange() : AttackRangeSkill("#gu_liren-range") {}

	int getFixed(const Player *target, bool) const override
	{
		return target && target->hasSkill("gu_liren") ? 2 : -1;
	}

	int getExtra(const Player *target, bool include_weapon) const override
	{
		if (!include_weapon || !target || !target->hasSkill("gu_liren")) return 0;
		const Weapon *weapon = qobject_cast<const Weapon *>(target->getWeapon());
		if (!weapon) return 0;
		const int range = weapon->getRange(target);
		return range > 2 ? 2 - range : 0;
	}
};

class GuLiren : public TriggerSkill
{
public:
	GuLiren() : TriggerSkill("gu_liren")
	{
		events << Damage << CardOffset;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const int got = player->getMark("gu_liren_got-Clear");
		if (got >= 3) return false;
		if (event == CardOffset) {
			const CardEffectStruct effect = data.value<CardEffectStruct>();
			if (effect.from != player || !effect.card || !effect.card->isKindOf("Slash")) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(1, objectName());
			room->addPlayerMark(player, "gu_liren_got-Clear");
			return false;
		}
		const DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player || !damage.card || !damage.card->isKindOf("Slash")) return false;
		QList<int> take;
		foreach (int id, guCardIds(damage.card)) {
			if (take.size() >= 3 - got) break;
			if (room->getCardPlace(id) == Player::DiscardPile) take << id;
		}
		if (take.isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, this);
		DummyCard *dummy = new DummyCard(take);
		room->obtainCard(player, dummy, objectName());
		dummy->deleteLater();
		room->addPlayerMark(player, "gu_liren_got-Clear", take.size());
		return false;
	}
};

// ---------------- 史阿 剑出 / 击格 ----------------

class GuJianchuJink : public FilterSkill
{
public:
	GuJianchuJink() : FilterSkill("#gu_jianchu-jink") {}

	bool viewFilter(const Card *to_select) const override
	{
		if (!to_select || !to_select->isKindOf("Jink")) return false;
		Room *room = Sanguosha->currentRoom();
		if (!room) return false;
		const int id = to_select->getEffectiveId();
		if (id < 0 || room->getCardPlace(id) != Player::PlaceHand) return false;
		const ServerPlayer *owner = room->getCardOwner(id);
		return owner && owner->hasSkill("gu_jianchu") && owner->getMark("gu_jianchu") > 0;
	}

	const Card *viewAs(const Card *original) const override
	{
		Slash *slash = new Slash(original->getSuit(), original->getNumber());
		slash->setSkillName(objectName());
		return slash;
	}
};

class GuJige : public TriggerSkill
{
public:
	GuJige() : TriggerSkill("gu_jige")
	{
		events << CardEffected << ConfirmDamage;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (effect.to != player || !effect.card || !effect.card->isKindOf("Slash")) return false;
			if (effect.nullified || effect.no_offset) return false;
			const int number = effect.card->getNumber();
			if (number >= 13) return false;
			const QString pattern = QStringLiteral("Slash|.|%1~13").arg(number + 1);
			const QString prompt = QStringLiteral("@gu_jige-offset:%1").arg(effect.from ? effect.from->objectName() : QString());
			const Card *offset = room->askForCard(player, pattern, prompt, data, Card::MethodResponse, effect.from, false, objectName());
			if (!offset) return false;
			effect.offset_card = offset;
			data = QVariant::fromValue(effect);
			return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player || !damage.to || !damage.card || !damage.card->isKindOf("Slash")) return false;
		if (damage.tips.contains(QStringLiteral("gu_jige")) || player->hasFlag("gu_jige_busy")) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		damage.tips << QStringLiteral("gu_jige");
		data = QVariant::fromValue(damage);
		room->setPlayerFlag(player, "gu_jige_busy");
		QSet<int> busy;
		foreach (int id, guCardIds(damage.card)) busy.insert(id);
		const int fromN = guThrowSlashes(room, player, player, busy);
		const int toN = damage.to == player ? fromN : guThrowSlashes(room, damage.to, damage.to, busy);
		room->setPlayerFlag(player, "-gu_jige_busy");
		damage = data.value<DamageStruct>();
		if (!damage.tips.contains(QStringLiteral("gu_jige"))) damage.tips << QStringLiteral("gu_jige");
		damage.damage += qAbs(fromN - toN);
		data = QVariant::fromValue(damage);
		return false;
	}

private:
	static int guThrowSlashes(Room *room, ServerPlayer *who, ServerPlayer *thrower, const QSet<int> &busy)
	{
		if (!who || !who->isAlive()) return 0;
		QList<int> ids;
		foreach (const Card *card, who->getCards("he")) {
			if (!card || !card->isKindOf("Slash")) continue;
			const int id = card->getEffectiveId();
			if (id < 0 || busy.contains(id) || !thrower->canDiscard(who, id)) continue;
			ids << id;
		}
		if (ids.isEmpty()) return 0;
		room->throwCard(ids, "gu_jige", who, thrower);
		return ids.size();
	}
};

class GuJianchu : public TriggerSkill
{
public:
	GuJianchu() : TriggerSkill("gu_jianchu")
	{
		events << CardsMoveOneTime << Death;
		frequency = Wake;
	}

	bool triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant data) const override
	{
		if (event == Death) {
			if (!target || !data.canConvert<DeathStruct>()) return false;
			const DeathStruct death = data.value<DeathStruct>();
			return death.who == target && target->hasSkill(objectName());
		}
		return TriggerSkill::triggerable(target, room, event, owner, data);
	}

	bool canWake(TriggerEvent event, ServerPlayer *player, QVariant, Room *) const override
	{
		if (!player) return false;
		if (event == Death) return player->getMark(objectName()) > 0;
		return player->getMark(objectName()) < 1;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Death) {
			const DeathStruct death = data.value<DeathStruct>();
			if (!death.who || death.who != player || player->getMark(objectName()) <= 0) return false;
			QList<ServerPlayer *> targets = room->getOtherPlayers(player);
			if (targets.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@gu_jianchu-death", true);
			if (target) room->acquireSkill(target, "gu_liren");
			return false;
		}
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to != player || move.to_place != Player::PlaceHand) return false;
		const ServerPlayer *current = room->getCurrent();
		if (current && current->getPhase() == Player::Draw) return false;
		int gained = 0;
		for (int i = 0; i < move.card_ids.size(); ++i) {
			const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
			if (move.from == player && from == Player::PlaceHand) continue;
			++gained;
		}
		if (gained <= 0) return false;
		const int total = player->getMark("gu_jianchu_got") + gained;
		room->setPlayerMark(player, "gu_jianchu_got", total);
		if (total < 5 || player->getMark(objectName()) > 0) return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->setPlayerMark(player, objectName(), 1);
		room->doSuperLightbox(player, objectName());
		if (player->isAlive()) room->recover(player, RecoverStruct(objectName(), player, 1));
		if (player->isAlive()) {
			room->acquireSkill(player, "gu_jige");
			room->filterCards(player, player->getCards("h"), true);
		}
		return false;
	}
};

// ---------------- 威张星彩 煌怒 ----------------

static void guHuangnuAct(Room *room, ServerPlayer *source);

class GuHuangnuCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE GuHuangnuCard()
	{
		target_fixed = true;
		will_throw = false;
		mute = true;
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
		guHuangnuAct(room, source);
	}
};

class GuHuangnuVS : public ZeroCardViewAsSkill
{
public:
	GuHuangnuVS() : ZeroCardViewAsSkill("gu_huangnu") {}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->hasSkill("gu_huangnu") && player->getMark("gu_huangnu_off-Clear") <= 0;
	}

	const Card *viewAs() const override
	{
		return new GuHuangnuCard;
	}
};

class GuHuangnuMod : public TargetModSkill
{
public:
	GuHuangnuMod() : TargetModSkill("#gu_huangnu-mod")
	{
		pattern = "Slash";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const override
	{
		if (from && card && from->hasSkill("gu_huangnu") && card->getSkillName() == "gu_huangnu") return -1;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const override
	{
		if (from && card && from->hasSkill("gu_huangnu") && card->getSkillName() == "gu_huangnu") return 1000;
		return 0;
	}
};

class GuHuangnuNull : public TriggerSkill
{
public:
	GuHuangnuNull() : TriggerSkill("#gu_huangnu-null")
	{
		events << CardUsed;
		global = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.from || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
		if (use.from->getMark("&gu_huangnu_null-Clear") <= 0) return false;
		use.nullified_list << QStringLiteral("_ALL_TARGETS");
		data = QVariant::fromValue(use);
		room->setPlayerMark(use.from, "&gu_huangnu_null-Clear", 0);
		return false;
	}
};

struct GuHuangnuDepth
{
	Room *room;
	int prev;
	explicit GuHuangnuDepth(Room *r)
		: room(r), prev(r->getTag(QStringLiteral("gu_huangnu_depth")).toInt())
	{
		room->setTag(QStringLiteral("gu_huangnu_depth"), prev + 1);
	}
	~GuHuangnuDepth()
	{
		room->setTag(QStringLiteral("gu_huangnu_depth"), prev);
	}
};

static void guHuangnuAct(Room *room, ServerPlayer *source)
{
	if (!room || !source || !source->isAlive()) return;
	if (source->getMark("gu_huangnu_off-Clear") > 0) return;
	if (room->getTag(QStringLiteral("gu_huangnu_depth")).toInt() >= 8) return;
	GuHuangnuDepth depth(room);
	room->addPlayerMark(source, "gu_huangnu_times-Clear");
	const QString choice = room->askForChoice(source, "gu_huangnu", "discard+slash+null");
	if (choice == "discard") {
		const QString cat = room->askForChoice(source, "gu_huangnu_cat", "basic+trick+equip");
		QList<ServerPlayer *> alive = room->getAlivePlayers();
		ServerPlayer *target = alive.isEmpty() ? nullptr
			: room->askForPlayerChosen(source, alive, "gu_huangnu", "@gu_huangnu-discard", false);
		if (target) {
			QList<int> ids;
			foreach (const Card *card, target->getCards("hej")) {
				if (!card) continue;
				bool match = false;
				if (cat == "basic") match = card->getTypeId() == Card::TypeBasic;
				else if (cat == "trick") match = card->getTypeId() == Card::TypeTrick;
				else match = card->getTypeId() == Card::TypeEquip;
				if (!match) continue;
				const int id = card->getEffectiveId();
				if (id >= 0 && source->canDiscard(target, id)) ids << id;
			}
			if (!ids.isEmpty())
				room->throwCard(ids.at(qsanRandomBounded(ids.size())), "gu_huangnu", target, source);
		}
	} else if (choice == "slash") {
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("gu_huangnu");
		slash->deleteLater();
		QList<ServerPlayer *> victims;
		foreach (ServerPlayer *p, room->getOtherPlayers(source))
			if (source->canSlash(p, slash, false)) victims << p;
		if (!victims.isEmpty()) {
			ServerPlayer *target = room->askForPlayerChosen(source, victims, "gu_huangnu", "@gu_huangnu-slash", false);
			if (target) room->useCard(CardUseStruct(slash, source, target), false);
		}
	} else {
		QList<ServerPlayer *> alive = room->getAlivePlayers();
		ServerPlayer *target = alive.isEmpty() ? nullptr
			: room->askForPlayerChosen(source, alive, "gu_huangnu", "@gu_huangnu-null", false);
		if (target) room->setPlayerMark(target, "&gu_huangnu_null-Clear", 1);
	}
	QList<int> gone;
	for (int i = 0; i < 5; ++i)
		if (!source->hasEquipArea(i)) gone << i;
	const int x = source->getMark("gu_huangnu_times-Clear");
	if (gone.size() >= x && x > 0) {
		for (int n = 0; n < x && !gone.isEmpty(); ++n)
			source->obtainEquipArea(gone.takeAt(qsanRandomBounded(gone.size())));
	} else {
		room->setPlayerMark(source, "gu_huangnu_off-Clear", 1);
	}
}

class GuHuangnu : public TriggerSkill
{
public:
	GuHuangnu() : TriggerSkill("gu_huangnu")
	{
		events << Damaged;
		view_as_skill = new GuHuangnuVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player->isAlive() || player->getMark("gu_huangnu_off-Clear") > 0) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		guHuangnuAct(room, player);
		return false;
	}
};

// ---------------- 威张星彩 贤贶 ----------------

class GuXiankuang : public TriggerSkill
{
public:
	GuXiankuang() : TriggerSkill("gu_xiankuang")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player->isAlive() || player->getMark("gu_xiankuang_off-Clear") > 0) return false;
		if (player->hasFlag("gu_xiankuang_busy")) return false;
		if (guAbolishedAreas(player) >= 5) return false;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile) return false;
		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE)
			return false;
		ServerPlayer *loser = qobject_cast<ServerPlayer *>(move.from);
		if (!loser) return false;
		QList<int> basics;
		for (int i = 0; i < move.card_ids.size(); ++i) {
			const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
			if (from == Player::DrawPile || from == Player::DiscardPile) continue;
			const Card *card = Sanguosha->getCard(move.card_ids.at(i));
			if (card && card->getTypeId() == Card::TypeBasic) basics << move.card_ids.at(i);
		}
		if (basics.isEmpty() || guForemostDiscard(room, basics) < 0) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		QStringList areas;
		for (int i = 0; i < 5; ++i)
			if (player->hasEquipArea(i)) areas << QString::number(i);
		if (areas.isEmpty()) return false;
		room->setPlayerFlag(player, "gu_xiankuang_busy");
		const QString area = areas.size() == 1 ? areas.first()
			: room->askForChoice(player, "gu_xiankuang", areas.join("+"));
		player->throwEquipArea(area.toInt());
		const int abolished = guAbolishedAreas(player);
		QString decision = "obtain";
		if (loser->isAlive())
			decision = room->askForChoice(player, "gu_xiankuang", "obtain+draw");
		int got = 0;
		if (decision == "draw") {
			if (loser->isAlive() && abolished > 0) {
				loser->drawCards(abolished, objectName());
				if (loser == player) got = abolished;
			}
		} else {
			const int id = guForemostDiscard(room, basics);
			if (id >= 0) {
				room->obtainCard(player, id, objectName());
				got = 1;
			}
		}
		room->setPlayerFlag(player, "-gu_xiankuang_busy");
		if (player->isAlive()) room->setPlayerMark(player, "gu_xiankuang_off-Clear", 1);
		if (got > 0) room->addPlayerMark(player, "gu_xiankuang_got-Clear", got);
		if (player->getMark("gu_xiankuang_got-Clear") > 1)
			room->addSkillInvalidity(player, objectName(), player->objectName(), objectName());
		return false;
	}
};

// ---------------- 武张飞 恣胜 ----------------

static int guZishengPoint(const Card *card)
{
	if (!card) return 0;
	const QList<int> subs = card->getSubcards();
	if (card->isVirtualCard() && subs.size() > 1) {
		int sum = 0;
		foreach (int id, subs) {
			const Card *real = Sanguosha->getCard(id);
			if (real) sum += real->getNumber();
		}
		return sum;
	}
	return card->getNumber();
}

class GuZisheng : public TriggerSkill
{
public:
	GuZisheng() : TriggerSkill("gu_zisheng")
	{
		events << CardUsed << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardUsed) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
			const int point = guZishengPoint(use.card);
			if (point <= 3 || point % 3 != 0) return false;
			QList<int> pool;
			foreach (int id, room->getDrawPile()) {
				const Card *card = Sanguosha->getCard(id);
				if (card && card->getNumber() == 3) pool << id;
			}
			if (pool.isEmpty()) return false;
			if (!player->askForSkillInvoke(this, data)) return false;
			QList<int> pick;
			while (pick.size() < 3 && !pool.isEmpty())
				pick << pool.takeAt(qsanRandomBounded(pool.size()));
			int id = pick.first();
			if (pick.size() > 1) {
				room->fillAG(pick, player);
				const int chosen = room->askForAG(player, pick, false, objectName());
				room->clearAG(player);
				if (pick.contains(chosen)) id = chosen;
			}
			if (room->getCardPlace(id) == Player::DrawPile)
				room->obtainCard(player, id, objectName());
			return false;
		}
		if (player->hasFlag("gu_zisheng_busy")) return false;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		ServerPlayer *other = nullptr;
		int count = 0;
		if (move.to == player && move.to_place == Player::PlaceHand && move.from && move.from != player) {
			other = qobject_cast<ServerPlayer *>(move.from);
			for (int i = 0; i < move.card_ids.size(); ++i) {
				const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
				if (guPlayerZone(from)) ++count;
			}
		} else if (move.from && move.from != player
			&& move.reason.m_playerId == player->objectName()
			&& (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
			other = qobject_cast<ServerPlayer *>(move.from);
			for (int i = 0; i < move.card_ids.size(); ++i) {
				const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
				if (guPlayerZone(from)) ++count;
			}
		}
		if (!other || !other->isAlive() || count <= 0 || other->getHp() <= 0) return false;
		if (!player->askForSkillInvoke(this, other)) return false;
		const int damage = qMin(count, other->getHp());
		if (damage <= 0 || !other->isAlive()) return false;
		room->setPlayerFlag(player, "gu_zisheng_busy");
		room->damage(DamageStruct(objectName(), player, other, damage));
		room->setPlayerFlag(player, "-gu_zisheng_busy");
		return false;
	}
};

// ---------------- 武张飞 显略 / 豪贤 ----------------

static void guXianlueClear(Room *room, ServerPlayer *source)
{
	if (!source) return;
	source->removeTag("gu_xianlue_nums");
	room->setPlayerMark(source, "gu_xianlue_th3", 0);
	room->setPlayerMark(source, "gu_xianlue_th6", 0);
	room->setPlayerMark(source, "gu_xianlue_th9", 0);
	if (source->hasSkill("gu_haoxian", true) && source->getMark("@gu_haoxian") < 1)
		room->setPlayerMark(source, "@gu_haoxian", 1);
}

static int guXianlueCap(Room *room, ServerPlayer *source)
{
	if (source->getMark("gu_xianlue_cap_ready") <= 0) {
		room->setPlayerMark(source, "gu_xianlue_cap", 13);
		room->setPlayerMark(source, "gu_xianlue_cap_ready", 1);
	}
	return source->getMark("gu_xianlue_cap");
}

static void guXianlueRecord(Room *room, ServerPlayer *source, ServerPlayer *target)
{
	QStringList nums = source->getTag(QStringLiteral("gu_xianlue_nums")).toStringList();
	const int before = nums.size();
	QList<int> ids = target->handCards();
	if (!ids.isEmpty()) room->showCard(target, ids, source);
	foreach (int id, ids) {
		const Card *card = Sanguosha->getCard(id);
		nums << QString::number(card ? card->getNumber() : 0);
	}
	source->setTag("gu_xianlue_nums", nums);
	const int after = nums.size();
	LogMessage log;
	log.type = "#GuXianlueRecord";
	log.from = source;
	log.to << target;
	log.arg = nums.join(",");
	log.arg2 = QString::number(after);
	log.arg3 = QString::number(guXianlueCap(room, source));
	room->sendLog(log, source);
	const int thresholds[] = {3, 6, 9};
	for (int th : thresholds) {
		const QString mark = QStringLiteral("gu_xianlue_th%1").arg(th);
		if (before <= th && after > th && source->getMark(mark) <= 0) {
			room->setPlayerMark(source, mark, 1);
			if (source->isAlive()) source->drawCards(3, "gu_xianlue");
		}
	}
	if (source->getTag(QStringLiteral("gu_xianlue_nums")).toStringList().size() >= guXianlueCap(room, source))
		guXianlueClear(room, source);
}

class GuXianlueCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE GuXianlueCard()
	{
		will_throw = false;
		mute = true;
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		if (!Self || !to_select || !targets.isEmpty() || to_select == Self) return false;
		if (Self->getMark(QStringLiteral("gu_xianlue_on_%1-Clear").arg(to_select->objectName())) > 0) return false;
		return to_select->getMark("&gu_xianlue_hurt-Clear") > 0 || to_select->getMark("&gu_xianlue_lost-Clear") > 0;
	}

	bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override
	{
		return targets.length() == 1;
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		if (!source || targets.isEmpty() || !targets.first()) return;
		ServerPlayer *target = targets.first();
		room->setPlayerMark(source, QStringLiteral("gu_xianlue_on_%1-Clear").arg(target->objectName()), 1);
		guXianlueRecord(room, source, target);
	}
};

class GuXianlueVS : public ZeroCardViewAsSkill
{
public:
	GuXianlueVS() : ZeroCardViewAsSkill("gu_xianlue") {}

	bool isEnabledAtPlay(const Player *player) const override
	{
		if (!player) return false;
		foreach (const Player *p, player->getAliveSiblings()) {
			if (player->getMark(QStringLiteral("gu_xianlue_on_%1-Clear").arg(p->objectName())) > 0) continue;
			if (p->getMark("&gu_xianlue_hurt-Clear") > 0 || p->getMark("&gu_xianlue_lost-Clear") > 0) return true;
		}
		return false;
	}

	const Card *viewAs() const override
	{
		return new GuXianlueCard;
	}
};

class GuXianlue : public TriggerSkill
{
public:
	GuXianlue() : TriggerSkill("gu_xianlue")
	{
		events << GameStart;
		view_as_skill = new GuXianlueVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		guXianlueCap(room, player);
		return false;
	}
};

class GuXianlueRecordSkill : public TriggerSkill
{
public:
	GuXianlueRecordSkill() : TriggerSkill("#gu_xianlue-record")
	{
		events << Damaged << CardsMoveOneTime << EnterDying;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (event == EnterDying) {
			const DyingStruct dying = data.value<DyingStruct>();
			if (!dying.who || !dying.damage || !dying.damage->from || dying.damage->from == dying.who) return false;
			ServerPlayer *from = dying.damage->from;
			if (!from->hasSkill("gu_xianlue")) return false;
			const int cap = qMax(0, guXianlueCap(room, from) - 3);
			room->setPlayerMark(from, "gu_xianlue_cap", cap);
			if (from->getTag(QStringLiteral("gu_xianlue_nums")).toStringList().size() >= cap)
				guXianlueClear(room, from);
			return false;
		}
		if (room->findPlayersBySkillName("gu_xianlue").isEmpty()) return false;
		if (event == Damaged) {
			const DamageStruct damage = data.value<DamageStruct>();
			if (damage.to) room->setPlayerMark(damage.to, "&gu_xianlue_hurt-Clear", 1);
			return false;
		}
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		ServerPlayer *from = qobject_cast<ServerPlayer *>(move.from);
		if (!from) return false;
		for (int i = 0; i < move.card_ids.size(); ++i) {
			const Player::Place place = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
			if (!guPlayerZone(place)) continue;
			if (move.to == from && guPlayerZone(move.to_place)) continue;
			room->setPlayerMark(from, "&gu_xianlue_lost-Clear", 1);
			break;
		}
		return false;
	}
};

class GuHaoxianCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE GuHaoxianCard()
	{
		target_fixed = true;
		will_throw = false;
		mute = true;
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
		room->doSuperLightbox(source, "gu_haoxian");
		room->removePlayerMark(source, "@gu_haoxian");
		QList<int> pile;
		foreach (int id, room->getDiscardPile()) {
			const Card *card = Sanguosha->getCard(id);
			if (card && card->getNumber() == 3) pile << id;
		}
		if (!pile.isEmpty()) room->shuffleIntoDrawPile(source, pile, "gu_haoxian", false);
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			QList<int> ids;
			foreach (int id, p->handCards()) {
				const Card *card = Sanguosha->getCard(id);
				if (card && card->getNumber() == 3) ids << id;
			}
			if (ids.isEmpty()) continue;
			DummyCard *dummy = new DummyCard(ids);
			room->obtainCard(source, dummy, "gu_haoxian");
			dummy->deleteLater();
		}
	}
};

class GuHaoxian : public ZeroCardViewAsSkill
{
public:
	GuHaoxian() : ZeroCardViewAsSkill("gu_haoxian")
	{
		frequency = Limited;
		limit_mark = "@gu_haoxian";
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && player->getMark("@gu_haoxian") > 0;
	}

	const Card *viewAs() const override
	{
		return new GuHaoxianCard;
	}
};

void guAudit2026June(Package *pkg)
{
	pkg->addMetaObject<GuHuangnuCard>();
	pkg->addMetaObject<GuXianlueCard>();
	pkg->addMetaObject<GuHaoxianCard>();

	pkg->addSkills(new GuLirenRange);
	pkg->insertRelatedSkills("gu_liren", "#gu_liren-range");
	pkg->addSkills(new GuJianchuJink);
	pkg->insertRelatedSkills("gu_jianchu", "#gu_jianchu-jink");
	pkg->addSkills(new GuJige);
	pkg->addSkills(new GuHuangnuMod);
	pkg->insertRelatedSkills("gu_huangnu", "#gu_huangnu-mod");
	pkg->addSkills(new GuHuangnuNull);
	pkg->addSkills(new GuXianlueRecordSkill);

	General *xunxu = new General(pkg, "gu_xunxu", "wei", 3, true);
	xunxu->addSkill(new GuDiyin);
	xunxu->addSkill(new GuBoqia);

	General *shia = new General(pkg, "gu_shia", "qun", 5, true);
	shia->addSkill(new GuLiren);
	shia->addSkill(new GuJianchu);

	General *xingcai = new General(pkg, "gu_weizhangxingcai", "shu", 3, false);
	xingcai->addSkill(new GuHuangnu);
	xingcai->addSkill(new GuXiankuang);

	General *zhangfei = new General(pkg, "gu_wuzhangfei", "shu", 4, true);
	zhangfei->addSkill(new GuZisheng);
	zhangfei->addSkill(new GuXianlue);
	zhangfei->addSkill(new GuHaoxian);
}

#include "gu-audit2026-m06.moc"
