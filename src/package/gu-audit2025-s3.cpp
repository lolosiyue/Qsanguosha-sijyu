// SOURCE-3 follow-up: 有诸葛亮 / 祢衡[OL] per Tom 2026-10-13 10:45 JST.
// Authoritative text: materials/gu-audit2025-tom-20261013/SOURCE-3.md
// 谋吕布 HP and title are edited in mobilemougong.cpp and Mobile.lua. 无双 is unchanged.
#include "gu-audit2025.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "serverplayer.h"
#include "wrapped-card.h"
#include "game-rng.h"
#include "skill-dialog-info.h"
#include "card-lifetime-manager.h"

// SOURCE-3 five-tiger pool, one object name per listed general.
// Verified against lang translations. getGeneral misses are skipped at runtime.
// 武关羽 has no packaged general here (wuguanyu / ol_wuguanyu / mobile_wuguanyu / tenyear_wuguanyu).
// Not in SOURCE-3, so not pooled: 界赵云, 界张飞, 神马超.
// SP赵云 maps to bgm_zhaoyun (☆SP赵云). jsp_zhaoyun is J.SP赵云 and is not pooled.
static const char *const GuYingyouGenerals[] = {
	"guanyu",            // 界关羽
	"tenyear_guanyu",    // 界关羽（十周年）
	"shenguanyu",        // 神关羽
	"ol_shenguanyu",     // 神关羽[OL]
	"wuguanyu",          // 武关羽 — not in this repo
	"ol_wuguanyu",       // 武关羽 alternate id — not in this repo
	"mobile_wuguanyu",   // 武关羽 alternate id — not in this repo
	"tenyear_wuguanyu",  // 武关羽 alternate id — not in this repo
	"sp_guanyu",         // SP关羽
	"shenzhaoyun",       // 神赵云
	"new_shenzhaoyun",   // 新神赵云
	"bgm_zhaoyun",       // ☆SP赵云
	"shenzhangfei",      // 神张飞
	"machao",            // 界马超
	"sp_machao",         // 马超[SP]
	"new_spmachao",      // 新sp马超
	"ol_new_spmachao",   // 新sp马超[OL]
	"tenyear_new_spmachao", // 新sp马超[十周年]
	"thwei_machao",      // 威马超
	"huangzhong",        // 黄忠
	"shenhuangzhong",    // 神黄忠
	nullptr
};

static QStringList guYingyouPool(const ServerPlayer *player)
{
	QStringList skills;
	for (int i = 0; GuYingyouGenerals[i] != nullptr; i++) {
		const General *general = Sanguosha->getGeneral(QString::fromLatin1(GuYingyouGenerals[i]));
		if (!general) continue;
		foreach (const Skill *skill, general->getVisibleSkillList()) {
			if (!skill || !skill->isVisible() || skill->isLordSkill()) continue;
			const QString name = skill->objectName();
			if (name.isEmpty() || name.startsWith("#") || name.startsWith("_")) continue;
			if (player->hasSkill(name)) continue;
			if (!skills.contains(name)) skills << name;
		}
	}
	return skills;
}

// 1 = the shown card was used, 0 = played / not used (1 HP lost), -1 = process did not start.
static int kuangjuanStep(Room *room, ServerPlayer *player, ServerPlayer *fixedTarget, bool optional)
{
	ServerPlayer *target = fixedTarget;
	if (!target) {
		QList<ServerPlayer *> cands;
		foreach (ServerPlayer *p, room->getOtherPlayers(player))
			if (!p->isKongcheng()) cands << p;
		if (cands.isEmpty()) return -1;
		target = room->askForPlayerChosen(player, cands, "kuangjuan", "@kuangjuan", optional, true);
		if (!target) return -1;
	}
	if (!target->isAlive() || target->isKongcheng() || !player->isAlive()) return -1;
	int id = room->askForCardChosen(player, target, "h", "kuangjuan");
	if (id < 0) return -1;
	room->showCard(target, id);
	const Card *card = Sanguosha->getCard(id);
	bool used = false;
	if (card && player->isAlive() && player->canUse(card)) {
		QList<int> one;
		one << id;
		room->notifyMoveToPile(player, one, "kuangjuan", Player::PlaceHand, true, true);
		const Card *got = room->askForUseCard(player, "@@kuangjuan", "@kuangjuan-use");
		const bool leftHand = room->getCardPlace(id) != Player::PlaceHand;
		room->notifyMoveToPile(player, one, "kuangjuan", Player::PlaceHand, false, true);
		used = got != nullptr && leftHand;
	}
	if (used) return 1;
	if (room->getCardPlace(id) == Player::PlaceHand) {
		CardMoveReason reason(CardMoveReason::S_REASON_RESPONSE, player->objectName(), "kuangjuan", "");
		ServerPlayer *owner = room->getCardOwner(id);
		room->throwCard(card, reason, owner ? owner : target, player);
	}
	if (player->isAlive()) room->loseHp(player, 1, true, player, "kuangjuan");
	return 0;
}

static QList<ServerPlayer *> feibianNeighbors(ServerPlayer *player)
{
	QList<ServerPlayer *> out;
	if (!player || !player->isAlive()) return out;
	ServerPlayer *next = player->getNextAlive(1);
	ServerPlayer *prev = player->getPreviousAlive(1);
	if (next && next != player && next->isAlive()) out << next;
	if (prev && prev != player && prev->isAlive() && prev != next) out << prev;
	return out;
}

// Out of the deck: one leading underscore (two underscores are hidden from getDerivativeCard).
// Not a Crossbow subclass: hasWeapon("Crossbow") is isKindOf("Crossbow").
class GuZhenCrossbow : public Weapon
{
	Q_OBJECT
	Q_CLASSINFO("CardName", "_gu_zhen_crossbow")
public:
	Q_INVOKABLE GuZhenCrossbow(Card::Suit suit = Card::Club, int number = 1)
		: Weapon(suit, number, 99)
	{
		setObjectName("_gu_zhen_crossbow");
	}
};

class GuZhenCrossbowSkill : public TargetModSkill
{
public:
	GuZhenCrossbowSkill() : TargetModSkill("_gu_zhen_crossbow")
	{
		pattern = "Slash";
	}
	int getResidueNum(const Player *from, const Card *, const Player *) const override
	{
		return from && from->hasWeapon("_gu_zhen_crossbow") ? -1 : 0;
	}
};

// WeaponSkill::triggerable requires a living holder, so death-discard would not destroy the card.
class GuZhenCrossbowRule : public TriggerSkill
{
public:
	GuZhenCrossbowRule() : TriggerSkill("#_gu_zhen_crossbow")
	{
		events << TargetSpecified << BeforeCardsMove;
		frequency = Compulsory;
		global = true;
	}
	int getPriority(TriggerEvent event) const override
	{
		return event == BeforeCardsMove ? 9 : 2;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || !use.card->isKindOf("Slash") || use.from != player || !player->hasWeapon("_gu_zhen_crossbow"))
				return false;
			if (use.card->getNumber() >= 7) return false;
			if (!use.no_respond_list.contains("_ALL_TARGETS"))
				use.no_respond_list << "_ALL_TARGETS";
			const QString key = "Jink_" + use.card->toString();
			QVariantList jinks = player->getTag(key).toList();
			while (jinks.length() < use.to.length()) jinks << 1;
			for (int i = 0; i < jinks.length(); i++) jinks[i] = 0;
			player->setTag(key, jinks);
			data = QVariant::fromValue(use);
			return false;
		}
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.reason.m_skillName == "BreakCard" || move.reason.m_eventName == "break_equip") return false;
		QList<int> ids;
		for (int i = 0; i < move.card_ids.size(); i++) {
			if (move.from_places.value(i) != Player::PlaceEquip) continue;
			const int id = move.card_ids.at(i);
			if (room->getCardPlace(id) != Player::PlaceEquip) continue;
			const Card *card = Sanguosha->getEngineCard(id);
			if (card && card->objectName() == "_gu_zhen_crossbow") ids << id;
		}
		if (ids.isEmpty()) return false;
		move.removeCardIds(ids);
		data = QVariant::fromValue(move);
		ServerPlayer *owner = move.from ? room->findPlayerByObjectName(move.from->objectName(), true) : nullptr;
		room->breakCard(ids, owner);
		return false;
	}
};

// ---------------- 有诸葛亮：应有 ----------------
class GuYingyou : public TriggerSkill
{
public:
	GuYingyou() : TriggerSkill("gu_yingyou")
	{
		// 回合开始 = RoundStart. 结束阶段 = Finish. 受到伤害后 = Damaged.
		events << EventPhaseStart << Damaged << CardUsed;
		frequency = Frequent;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.from != player || !use.card || !use.m_isHandcard) return false;
			if (use.card->getTypeId() == Card::TypeSkill || use.card->hasFlag("gu_mantou")) return false;
			const int n = use.card->getNumber();
			if (n <= 0 || player->getMark("@gu_mantou") < n) return false;
			if (!player->askForSkillInvoke("gu_mantou", data)) return false;
			if (player->getMark("@gu_mantou") < n) return false;
			room->setCardFlag(use.card, "gu_mantou");
			player->loseMark("@gu_mantou", n);
			use.extra_use += 1;
			data = QVariant::fromValue(use);
			return false;
		}
		if (event == EventPhaseStart
			&& player->getPhase() != Player::RoundStart
			&& player->getPhase() != Player::Finish) return false;
		QStringList choices;
		if (!guYingyouPool(player).isEmpty()) choices << "wuhu";
		if (player->hasEquipArea(0)) choices << "crossbow";
		choices << "mantou";
		if (!player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		const QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "wuhu") {
			const QStringList pool = guYingyouPool(player);
			if (!pool.isEmpty())
				room->acquireSkill(player, pool.at(qsanRandomBounded(pool.length())));
		} else if (choice == "crossbow") {
			player->getDerivativeCard("_gu_zhen_crossbow");
		} else if (choice == "mantou") {
			player->gainMark("@gu_mantou", 10);
		}
		if (player->isAlive()) player->drawCards(1, objectName());
		return false;
	}
};

// ---------------- 祢衡：狂狷 ----------------
GuKuangjuanCard::GuKuangjuanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}
bool GuKuangjuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}
bool GuKuangjuanCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 1;
}
void GuKuangjuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if (targets.isEmpty() || !source->isAlive()) return;
	if (kuangjuanStep(room, source, targets.first(), false) == 1)
		room->setPlayerMark(source, "kuangjuan_chain", 1);
}

class GuKuangjuanVS : public ViewAsSkill
{
public:
	GuKuangjuanVS() : ViewAsSkill("kuangjuan")
	{
		expand_pile = "#kuangjuan";
	}
	bool isEnabledAtPlay(const Player *player) const override
	{
		if (player->hasUsed("GuKuangjuanCard")) return false;
		foreach (const Player *p, player->getAliveSiblings())
			if (!p->isKongcheng()) return true;
		return false;
	}
	bool isEnabledAtResponse(const Player *, const QString &pattern) const override
	{
		return pattern == "@@kuangjuan";
	}
	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		if (Sanguosha->getCurrentCardUsePattern() != "@@kuangjuan") return false;
		return selected.isEmpty() && Self->getPile("#kuangjuan").contains(to_select->getEffectiveId());
	}
	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (Sanguosha->getCurrentCardUsePattern() == "@@kuangjuan") {
			if (cards.length() != 1) return nullptr;
			return cards.first();
		}
		if (!cards.isEmpty()) return nullptr;
		return new GuKuangjuanCard;
	}
};

class GuKuangjuan : public TriggerSkill
{
public:
	GuKuangjuan() : TriggerSkill("kuangjuan")
	{
		events << EventPhaseEnd;
		view_as_skill = new GuKuangjuanVS;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (player->getPhase() != Player::Play || player->getMark("kuangjuan_chain") <= 0) return false;
		room->setPlayerMark(player, "kuangjuan_chain", 0);
		for (int i = 0; i < 20 && player->isAlive(); i++) {
			if (kuangjuanStep(room, player, nullptr, false) != 1) break;
		}
		return false;
	}
};

// ---------------- 祢衡：飞辩 ----------------
class GuFeibian : public TriggerSkill
{
public:
	GuFeibian() : TriggerSkill("feibian")
	{
		events << TargetSpecified << TargetConfirmed;
	}
	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill) return false;
		if (event == TargetSpecified) {
			if (use.from != player || !player->hasSkill(this)) return false;
			if (player->getMark("feibian_first-Clear") > 0) return false;
			room->addPlayerMark(player, "feibian_first-Clear");
			QList<ServerPlayer *> cands;
			foreach (ServerPlayer *p, feibianNeighbors(player))
				if (player->canDiscard(p, "he")) cands << p;
			if (cands.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, cands, objectName(), "@feibian-discard", true, true);
			if (!target) return false;
			int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
			if (id < 0 || !player->canDiscard(target, id)) return false;
			room->broadcastSkillInvoke(objectName());
			room->throwCard(id, objectName(), target, player);
			room->setPlayerMark(target, "feibian_" + player->objectName() + "-Clear", 1);
			target->setTag("feibian_same_" + player->objectName(), use.card->toString());
			return false;
		}
		if (!use.to.contains(player)) return false;
		foreach (ServerPlayer *owner, room->getAllPlayers()) {
			if (!owner->hasSkill(this) || owner->isDead()) continue;
			if (player->getMark("feibian_" + owner->objectName() + "-Clear") <= 0) continue;
			const QString tagKey = "feibian_same_" + owner->objectName();
			const QString same = player->getTag(tagKey).toString();
			if (!same.isEmpty()) {
				if (same == use.card->toString()) continue;
				player->removeTag(tagKey);
			}
			if (!owner->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			room->broadcastSkillInvoke(objectName());
			player->drawCards(1, objectName());
		}
		return false;
	}
};

void guAudit2025Source3Tenyear(Package *pkg)
{
	General *zhugeliang = new General(pkg, "gu_you_zhugeliang", "shu", 3);
	zhugeliang->addSkill(new GuYingyou);
	pkg->addSkills(new GuZhenCrossbowSkill);
	pkg->addSkills(new GuZhenCrossbowRule);
	pkg->addMetaObject<GuZhenCrossbow>();
	// Simultaneous equips need one unowned copy each. breakCard clears the owner, so copies recycle.
	for (int i = 0; i < 8; i++)
		(new GuZhenCrossbow(Card::Club, 1))->setParent(pkg);
}

void guAudit2025Source3OL(Package *pkg)
{
	General *miheng = new General(pkg, "gu_ol_miheng", "qun", 3);
	miheng->addSkill(new GuKuangjuan);
	miheng->addSkill(new GuFeibian);
	pkg->addMetaObject<GuKuangjuanCard>();
}

#include "gu-audit2025-s3.moc"
