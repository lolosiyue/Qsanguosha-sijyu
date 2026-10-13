// GU-audit2026 2026-08b: 谋祝融（OL） / 曹金玉（OL） / 界步练师（OL）.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-08 (translations in lang/zh_CN/Package/Mobile.lua).
// These are separate OL generals; mobilemou_zhurong / caojinyu / bulianshi are other versions and stay untouched.
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"

static const char *const GU08B_SLASH_X = "&gu_renche_x-Clear";

// X = number of 【殺】 put into the discard pile this turn, at least 1 (刃掣 / 牙镰 share it).
static int gu08bRencheX(const Player *player)
{
	return player ? qMax(1, player->getMark(GU08B_SLASH_X)) : 1;
}

static bool gu08bMovedFromOwnArea(const CardsMoveOneTimeStruct &move, int index)
{
	const Player::Place from = index < move.from_places.size() ? move.from_places.at(index) : Player::PlaceUnknown;
	return from == Player::PlaceHand || from == Player::PlaceEquip;
}

// ---------------- 谋祝融 刃掣 ----------------
// X is read when the skill is used. Targets: zero to X other characters ("至多").
// Each target with a card discards one card of its choice (hand or equip); you draw
// once for every target whose discarded card is not a 【殺】.

class Gu08bRencheCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu08bRencheCard()
	{
		mute = true;
		setSkillName(QStringLiteral("gu_renche"));
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		return Self && to_select && to_select != Self && !targets.contains(to_select)
			&& targets.length() < subcardsLength();
	}

	bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override
	{
		return targets.length() <= subcardsLength();
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		if (!room || !source) return;
		room->addPlayerMark(source, "gu_renche_used-PlayClear");
		int draws = 0;
		foreach (ServerPlayer *p, targets) {
			if (!p || !p->isAlive() || p->getCardCount(true, false) < 1) continue;
			const Card *thrown = room->askForDiscard(p, QStringLiteral("gu_renche"), 1, 1, false, true,
				"@gu_renche-discard:" + source->objectName());
			if (!thrown || thrown->subcardsLength() < 1) continue;
			const Card *real = Sanguosha->getCard(thrown->getSubcards().first());
			if (real && !real->isKindOf("Slash")) ++draws;
		}
		if (draws > 0 && source->isAlive()) source->drawCards(draws, QStringLiteral("gu_renche"));
	}
};

class Gu08bRenche : public ViewAsSkill
{
public:
	Gu08bRenche() : ViewAsSkill("gu_renche") {}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		return Self && to_select && selected.length() < gu08bRencheX(Self) && Self->hasCard(to_select)
			&& !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (!Self || cards.length() != gu08bRencheX(Self)) return nullptr;
		Gu08bRencheCard *card = new Gu08bRencheCard;
		card->addSubcards(cards);
		card->setSkillName(objectName());
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		if (!player) return false;
		const int x = gu08bRencheX(player);
		return player->getMark("gu_renche_used-PlayClear") < x && player->getCardCount(true, false) >= x;
	}

	bool isEnabledAtResponse(const Player *, const QString &) const override
	{
		return false;
	}
};

// Counts 【殺】 (physical cards) entering the discard pile this turn for every holder
// of 刃掣 or 牙镰. Each holder receives its own CardsMoveOneTime dispatch.
class Gu08bRencheCount : public TriggerSkill
{
public:
	Gu08bRencheCount() : TriggerSkill("#gu_renche-count")
	{
		events << CardsMoveOneTime;
		global = true;
	}

	int getPriority(TriggerEvent) const override
	{
		return 9;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		if (!player->hasSkill("gu_renche", true) && !player->hasSkill("gu_yalian", true)) return false;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile) return false;
		int n = 0;
		foreach (int id, move.card_ids) {
			const Card *card = Sanguosha->getCard(id);
			if (card && card->isKindOf("Slash")) ++n;
		}
		if (n > 0) room->addPlayerMark(player, GU08B_SLASH_X, n);
		return false;
	}
};

// ---------------- 谋祝融 牙镰 ----------------
// Any phase (any character's). A 【殺】 leaving your hand/equip area for any reason
// other than use (discard, response, give, ...) arms it. At that phase's end you
// choose any number (0 = skip) of other characters with hand count <= X (X of 刃掣);
// the fire 【殺】 has no distance restriction because the skill names its targets.

class Gu08bYalian : public TriggerSkill
{
public:
	Gu08bYalian() : TriggerSkill("gu_yalian")
	{
		events << CardsMoveOneTime << EventPhaseStart << EventPhaseEnd;
		global = true;
	}

	int getPriority(TriggerEvent event) const override
	{
		// Reset before any phase-start skill can make the holder lose a 【殺】.
		return event == EventPhaseStart ? 10 : TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardsMoveOneTime) {
			if (!player || !player->isAlive() || !player->hasSkill(this)) return false;
			const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from != player) return false;
			if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE)
				return false;
			for (int i = 0; i < move.card_ids.size(); ++i) {
				if (!gu08bMovedFromOwnArea(move, i)) continue;
				const Card *card = Sanguosha->getCard(move.card_ids.at(i));
				if (card && card->isKindOf("Slash")) {
					room->setPlayerMark(player, "gu_yalian_lost", 1);
					break;
				}
			}
			return false;
		}
		if (event == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getAllPlayers())
				if (p->getMark("gu_yalian_lost") > 0) room->setPlayerMark(p, "gu_yalian_lost", 0);
			return false;
		}
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (owner->getMark("gu_yalian_lost") <= 0) continue;
			room->setPlayerMark(owner, "gu_yalian_lost", 0);
			if (!owner->isAlive() || !owner->hasSkill(this)) continue;
			Card *slash = Sanguosha->cloneCard("fire_slash", Card::NoSuit, 0);
			if (!slash) continue;
			slash->setSkillName(objectName());
			slash->deleteLater();
			if (owner->isCardLimited(slash, Card::MethodUse)) continue;
			const int x = gu08bRencheX(owner);
			QList<ServerPlayer *> cands;
			foreach (ServerPlayer *p, room->getOtherPlayers(owner))
				if (p->getHandcardNum() <= x && !room->isProhibited(owner, p, slash)) cands << p;
			if (cands.isEmpty()) continue;
			QList<ServerPlayer *> chosen = room->askForPlayersChosen(owner, cands, objectName(), 0, cands.length(),
				"@gu_yalian:::" + QString::number(x), true);
			if (chosen.isEmpty()) continue;
			room->useCard(CardUseStruct(slash, owner, chosen), false);
		}
		return false;
	}
};

// ---------------- 曹金玉 shared state ----------------
// 秋暮 replaces 「紅色」 with 「黑色」 in all three descriptions (one-way; a second
// replacement changes nothing). State lives on the owner as a visible mark.

static bool gu08bQiumuChanged(const Player *player)
{
	return player && player->getMark("&gu_qiumu_black") > 0;
}

// Whether the card has the colour the descriptions currently call 「紅色」.
static bool gu08bIsRedSlot(const Player *owner, const Card *card)
{
	if (!card) return false;
	return gu08bQiumuChanged(owner) ? card->isBlack() : card->isRed();
}

static void gu08bCountHand(const Player *player, const Card *exclude, int &red, int &black)
{
	red = 0;
	black = 0;
	if (!player) return;
	QList<int> skip;
	if (exclude) skip = exclude->getSubcards();
	foreach (const Card *c, player->getHandcards()) {
		if (!c || skip.contains(c->getEffectiveId())) continue;
		if (c->isRed()) ++red;
		else if (c->isBlack()) ++black;
	}
}

// ---------------- 曹金玉 春晖 ----------------
// 鎖定技. 準備階段 or after each damage instance you take: an other character with a
// hand card (mandatory choice) gives you a hand card of its choice. A black one links
// you and it: the next time either of you becomes the target of a 「紅色」 card, both
// of you discard a hand card, and that link ends.

static const char *const GU08B_CHUNHUI_LINKS = "gu_chunhui_links";

class Gu08bChunhui : public TriggerSkill
{
public:
	Gu08bChunhui() : TriggerSkill("gu_chunhui")
	{
		events << EventPhaseStart << Damaged << TargetConfirming;
		frequency = Compulsory;
		global = true;
	}

	static void discardOneHand(Room *room, ServerPlayer *p)
	{
		if (!p || !p->isAlive() || p->isKongcheng()) return;
		room->askForDiscard(p, QStringLiteral("gu_chunhui"), 1, 1, false, false, QStringLiteral("@gu_chunhui-discard"));
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return false;
		if (event == TargetConfirming) {
			if (!player->isAlive()) return false;
			const CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.to.contains(player)) return false;
			foreach (ServerPlayer *owner, room->getAllPlayers(true)) {
				const QStringList links = owner->getTag(GU08B_CHUNHUI_LINKS).toStringList();
				if (links.isEmpty()) continue;
				if (!owner->isAlive()) {
					owner->removeTag(GU08B_CHUNHUI_LINKS);
					continue;
				}
				if (!gu08bIsRedSlot(owner, use.card)) continue;
				QStringList fired;
				QStringList kept;
				foreach (const QString &name, links) {
					if (player == owner || player->objectName() == name) fired << name;
					else kept << name;
				}
				if (fired.isEmpty()) continue;
				if (kept.isEmpty()) owner->removeTag(GU08B_CHUNHUI_LINKS);
				else owner->setTag(GU08B_CHUNHUI_LINKS, kept);
				foreach (const QString &name, fired) {
					ServerPlayer *partner = room->findPlayerByObjectName(name, true);
					room->removePlayerMark(owner, "&gu_chunhui");
					if (partner) room->removePlayerMark(partner, "&gu_chunhui");
					room->sendCompulsoryTriggerLog(owner, objectName());
					discardOneHand(room, owner);
					discardOneHand(room, partner);
				}
			}
			return false;
		}
		if (!player->isAlive() || !player->hasSkill(this)) return false;
		if (event == EventPhaseStart && player->getPhase() != Player::Start) return false;
		if (event == Damaged && data.value<DamageStruct>().to != player) return false;
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (!p->isKongcheng()) cands << p;
		if (cands.isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, this);
		ServerPlayer *target = room->askForPlayerChosen(player, cands, objectName(), "@gu_chunhui-choose", false, true);
		if (!target || !target->isAlive() || target->isKongcheng()) return false;
		const Card *given = room->askForExchange(target, objectName(), 1, 1, false,
			"@gu_chunhui-give:" + player->objectName());
		int id = -1;
		if (given && given->subcardsLength() > 0) id = given->getSubcards().first();
		else if (!target->isKongcheng()) id = target->getRandomHandCardId();
		if (id < 0 || !player->isAlive()) return false;
		const Card *card = Sanguosha->getCard(id);
		const bool black = card && card->isBlack();
		room->obtainCard(player, card,
			CardMoveReason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), objectName(), QString()),
			false);
		if (!black) return false;
		QStringList links = player->getTag(GU08B_CHUNHUI_LINKS).toStringList();
		links << target->objectName();
		player->setTag(GU08B_CHUNHUI_LINKS, links);
		room->addPlayerMark(player, "&gu_chunhui");
		room->addPlayerMark(target, "&gu_chunhui");
		return false;
	}
};

// ---------------- 曹金玉 夏晟 ----------------
// 「較多」 compares red and black cards in your hand, not counting the card being used.
// Original: red > black and you use a black card -> draw 1;
//           black > red and you use a red card -> may add one target.
// After 秋暮 (紅色 -> 黑色): black > red and you use a black card -> draw 1,
//           and black > red and you use a black card -> may add one target.

class Gu08bXiasheng : public TriggerSkill
{
public:
	Gu08bXiasheng() : TriggerSkill("gu_xiasheng")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill || !use.card->isBlack())
			return false;
		int red = 0, black = 0;
		gu08bCountHand(player, use.card, red, black);
		// 「紅色牌較多」: the slot word is 紅色 before 秋暮, 黑色 after it.
		const bool slotMore = gu08bQiumuChanged(player) ? black > red : red > black;
		if (!slotMore) return false;
		room->sendCompulsoryTriggerLog(player, this);
		player->drawCards(1, objectName());
		return false;
	}
};

class Gu08bXiashengTarget : public TargetModSkill
{
public:
	Gu08bXiashengTarget() : TargetModSkill("#gu_xiasheng-target")
	{
		pattern = ".";
	}

	int getExtraTargetNum(const Player *from, const Card *card) const override
	{
		if (!from || !card || !from->hasSkill("gu_xiasheng")) return 0;
		if (!gu08bIsRedSlot(from, card)) return 0;
		int red = 0, black = 0;
		gu08bCountHand(from, card, red, black);
		return black > red ? 1 : 0;
	}
};

// ---------------- 曹金玉 秋暮 ----------------
// Tracks characters that became targets of your red card this turn. When one of them
// is dying you may apply the 紅色 -> 黑色 rewrite; it only changes something the
// first time, and only then do you obtain the dying character's black hand cards.

class Gu08bQiumu : public TriggerSkill
{
public:
	Gu08bQiumu() : TriggerSkill("gu_qiumu")
	{
		events << TargetSpecified << Dying;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == TargetSpecified) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill) return false;
			if (!gu08bIsRedSlot(player, use.card)) return false;
			foreach (ServerPlayer *t, use.to)
				if (t) room->setPlayerMark(t, "gu_qiumu_" + player->objectName() + "-Clear", 1);
			return false;
		}
		const DyingStruct dying = data.value<DyingStruct>();
		ServerPlayer *who = dying.who;
		if (!who || who->getMark("gu_qiumu_" + player->objectName() + "-Clear") <= 0) return false;
		if (gu08bQiumuChanged(player)) return false; // 「黑色」改為「黑色」: no change possible.
		if (!player->askForSkillInvoke(this, QVariant::fromValue(who))) return false;
		room->setPlayerMark(player, "&gu_qiumu_black", 1);
		LogMessage log;
		log.type = "#GuQiumuChange";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		if (who == player || !who->isAlive()) return false;
		QList<int> blacks;
		foreach (const Card *c, who->getHandcards())
			if (c && c->isBlack()) blacks << c->getEffectiveId();
		if (blacks.isEmpty()) return false;
		DummyCard dummy(blacks);
		room->obtainCard(player, &dummy,
			CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, player->objectName(), who->objectName(), objectName(), QString()),
			false);
		return false;
	}
};

// ---------------- 界步练师 安恤 ----------------
// Two other characters with a card each. You take one card (hand or equip) from each,
// show one of those still in your hand and give it to whichever of the two has fewer
// hand cards (equal counts: nobody is 「較少者」, the card stays). The draw depends on
// the shown card only.

class Gu08bAnxuCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE Gu08bAnxuCard()
	{
		mute = true;
		setSkillName(QStringLiteral("gu_anxu"));
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		return Self && to_select && to_select != Self && !targets.contains(to_select) && targets.length() < 2
			&& !to_select->isNude();
	}

	bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override
	{
		return targets.length() == 2;
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		if (!room || !source || targets.length() != 2) return;
		const QString skill = QStringLiteral("gu_anxu");
		QList<int> got;
		foreach (ServerPlayer *t, targets) {
			if (!source->isAlive()) return;
			if (!t || !t->isAlive() || t->isNude()) continue;
			const int id = room->askForCardChosen(source, t, "he", skill);
			if (id < 0) continue;
			room->obtainCard(source, Sanguosha->getCard(id),
				CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, source->objectName(), t->objectName(), skill, QString()),
				false);
			got << id;
		}
		if (!source->isAlive()) return;
		QList<int> held;
		foreach (int id, got)
			if (room->getCardOwner(id) == source && room->getCardPlace(id) == Player::PlaceHand) held << id;
		if (held.isEmpty()) return;
		int shown = held.first();
		if (held.length() > 1) {
			room->fillAG(held, source);
			shown = room->askForAG(source, held, false, skill, "@gu_anxu-show");
			room->clearAG(source);
			if (!held.contains(shown)) shown = held.first();
		}
		room->showCard(source, shown);
		const bool nonSpade = Sanguosha->getCard(shown)->getSuit() != Card::Spade;
		ServerPlayer *a = targets.first();
		ServerPlayer *b = targets.last();
		ServerPlayer *fewer = nullptr;
		if (a->isAlive() && b->isAlive()) {
			if (a->getHandcardNum() < b->getHandcardNum()) fewer = a;
			else if (b->getHandcardNum() < a->getHandcardNum()) fewer = b;
		} else if (a->isAlive()) {
			fewer = a;
		} else if (b->isAlive()) {
			fewer = b;
		}
		if (fewer && room->getCardOwner(shown) == source && room->getCardPlace(shown) == Player::PlaceHand)
			room->obtainCard(fewer, Sanguosha->getCard(shown),
				CardMoveReason(CardMoveReason::S_REASON_GIVE, source->objectName(), fewer->objectName(), skill, QString()),
				true);
		if (nonSpade && source->isAlive()) source->drawCards(1, skill);
	}
};

class Gu08bAnxu : public ZeroCardViewAsSkill
{
public:
	Gu08bAnxu() : ZeroCardViewAsSkill("gu_anxu") {}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && !player->hasUsed(QStringLiteral("Gu08bAnxuCard"));
	}

	const Card *viewAs() const override
	{
		return new Gu08bAnxuCard;
	}
};

// ---------------- 界步练师 追忆 ----------------
// 限定技 at your 結束階段. The chosen character is remembered; on your death you may
// resolve the same effect on that character once more (only if it was used before).

class Gu08bZhuiyi : public TriggerSkill
{
public:
	Gu08bZhuiyi() : TriggerSkill("gu_zhuiyi")
	{
		events << EventPhaseStart << Death;
		frequency = Limited;
		limit_mark = "@gu_zhuiyi";
	}

	bool triggerable(ServerPlayer *target, Room *room, TriggerEvent event, ServerPlayer *owner, QVariant data) const override
	{
		if (event == Death) {
			if (!target || !data.canConvert<DeathStruct>()) return false;
			return data.value<DeathStruct>().who == target && target->hasSkill(objectName());
		}
		return TriggerSkill::triggerable(target, room, event, owner, data);
	}

	static void resolve(Room *room, ServerPlayer *source, ServerPlayer *target)
	{
		if (!target || !target->isAlive()) return;
		target->drawCards(3, QStringLiteral("gu_zhuiyi"));
		if (target->isAlive() && target->isWounded())
			room->recover(target, RecoverStruct(QStringLiteral("gu_zhuiyi"), source, 1));
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == Death) {
			const DeathStruct death = data.value<DeathStruct>();
			if (death.who != player) return false;
			const QString name = player->getTag("gu_zhuiyi_target").toString();
			if (name.isEmpty()) return false;
			ServerPlayer *target = room->findPlayerByObjectName(name);
			if (!target || !target->isAlive()) return false;
			if (!player->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
			player->removeTag("gu_zhuiyi_target");
			resolve(room, player, target);
			return false;
		}
		if (player->getPhase() != Player::Finish || player->getMark("@gu_zhuiyi") <= 0) return false;
		QList<ServerPlayer *> others = room->getOtherPlayers(player);
		if (others.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, others, objectName(), "@gu_zhuiyi-choose", true, true);
		if (!target) return false;
		room->removePlayerMark(player, "@gu_zhuiyi");
		room->doSuperLightbox(player, objectName());
		player->setTag("gu_zhuiyi_target", target->objectName());
		resolve(room, player, target);
		return false;
	}
};

void guAudit2026M08b(Package *pkg)
{
	pkg->addMetaObject<Gu08bRencheCard>();
	pkg->addMetaObject<Gu08bAnxuCard>();

	pkg->addSkills(new Gu08bRencheCount);
	pkg->insertRelatedSkills("gu_renche", "#gu_renche-count");
	pkg->addSkills(new Gu08bXiashengTarget);
	pkg->insertRelatedSkills("gu_xiasheng", "#gu_xiasheng-target");

	General *zhurong = new General(pkg, "gu_olmouzhurong", "shu", 4, false);
	zhurong->addSkill(new Gu08bRenche);
	zhurong->addSkill(new Gu08bYalian);

	General *caojinyu = new General(pkg, "gu_olcaojinyu", "wei", 3, false);
	caojinyu->addSkill(new Gu08bChunhui);
	caojinyu->addSkill(new Gu08bXiasheng);
	caojinyu->addSkill(new Gu08bQiumu);

	General *bulianshi = new General(pkg, "gu_oljiebulianshi", "wu", 3, false);
	bulianshi->addSkill(new Gu08bAnxu);
	bulianshi->addSkill(new Gu08bZhuiyi);
}

#include "gu-audit2026-m08b.moc"
