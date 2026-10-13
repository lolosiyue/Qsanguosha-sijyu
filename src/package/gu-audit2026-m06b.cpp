// GU-audit2026 month 2026-06: 谋关羽 / 孙綝 / 势程普.
// 族陆郁生 is already zu_luyusheng in the extension repo and is not registered here.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-06 (verbatim in Mobile.lua).
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "skill-dialog-info.h"

#include <QSet>

static bool guRealSuit(Card::Suit suit)
{
	return suit == Card::Spade || suit == Card::Club || suit == Card::Heart || suit == Card::Diamond;
}

static int guDiscardableHand(const ServerPlayer *player)
{
	int n = 0;
	if (!player) return 0;
	foreach (const Card *card, player->getHandcards())
		if (card && !player->isJilei(card, true)) ++n;
	return n;
}

static bool guSlashOrNdTrick(const Card *card)
{
	return card && (card->isKindOf("Slash") || card->isNDTrick());
}

// ---------------- 谋关羽 冠武 ----------------
// 场上 = equip areas of living characters, then the discard pile, then the draw pile.
// The slash counter advances on CardFinished, so the slash that reaches a multiple of two
// does not itself receive the new +1.

class GuGuanwu : public TriggerSkill
{
public:
	GuGuanwu() : TriggerSkill("gu_guanwu")
	{
		events << EventPhaseStart << CardFinished << ConfirmDamage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			int id = -1;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				foreach (const Card *card, p->getEquips()) {
					if (card && card->isKindOf("Blade")) {
						id = card->getEffectiveId();
						break;
					}
				}
				if (id >= 0) break;
			}
			if (id < 0) {
				foreach (int pileId, room->getDiscardPile()) {
					const Card *card = Sanguosha->getCard(pileId);
					if (card && card->isKindOf("Blade")) {
						id = pileId;
						break;
					}
				}
			}
			if (id < 0) {
				foreach (int pileId, room->getDrawPile()) {
					const Card *card = Sanguosha->getCard(pileId);
					if (card && card->isKindOf("Blade")) {
						id = pileId;
						break;
					}
				}
			}
			if (id < 0) return false;
			room->sendCompulsoryTriggerLog(player, this);
			room->obtainCard(player, id, objectName());
			return false;
		}
		if (event == CardFinished) {
			const CardUseStruct use = data.value<CardUseStruct>();
			if (use.from == player && use.card && use.card->isKindOf("Slash"))
				room->addPlayerMark(player, "gu_guanwu_slash_lun");
			return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player || !damage.card || !damage.card->isKindOf("Slash")) return false;
		const int bonus = player->getMark("gu_guanwu_slash_lun") / 2;
		if (bonus <= 0) return false;
		room->sendCompulsoryTriggerLog(player, this);
		damage.damage += bonus;
		data = QVariant::fromValue(damage);
		return false;
	}
};

// ---------------- 谋关羽 威势 ----------------
// Each option once per turn. "他" is the other character who used the damage card.
// Half of an empty hand cannot be paid, so the nullify option is not offered.

class GuWeishi : public TriggerSkill
{
public:
	GuWeishi() : TriggerSkill("gu_weishi")
	{
		events << TargetConfirmed << CardFinished;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.card->getTypeId() == Card::TypeSkill) return false;
		if (event == TargetConfirmed) {
			if (!player || !player->isAlive() || !player->hasSkill(this)) return false;
			if (!use.to.contains(player) || use.card->isDamageCard()) return false;
			if (player->getMark("gu_weishi_null-Clear") > 0) return false;
			const int half = (player->getHandcardNum() + 1) / 2;
			if (half <= 0 || guDiscardableHand(player) < half) return false;
			if (!player->askForSkillInvoke(this, data)) return false;
			if (!room->askForDiscard(player, objectName(), half, half, false, false, "@gu_weishi-discard"))
				return false;
			room->setPlayerMark(player, "gu_weishi_null-Clear", 1);
			if (!use.nullified_list.contains(player->objectName()))
				use.nullified_list << player->objectName();
			data = QVariant::fromValue(use);
			return false;
		}
		if (!use.from || !use.card->isDamageCard() || use.to.isEmpty()) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (!owner->hasSkill(this) || owner == use.from || !use.to.contains(owner)) continue;
			if (owner->getMark("gu_weishi_draw-Clear") > 0) continue;
			if (!owner->askForSkillInvoke(this, data)) continue;
			room->setPlayerMark(owner, "gu_weishi_draw-Clear", 1);
			owner->drawCards(2, objectName());
			if (owner->isAlive() && use.from->isAlive())
				room->askForUseSlashTo(owner, use.from, "@gu_weishi-slash:" + use.from->objectName(),
					true, false, true);
		}
		return false;
	}
};

// ---------------- 谋关羽 倨傲 ----------------
// Targets are any number of other characters in attack range. The produced card
// does not spend a use. Their next slash or ordinary trick keeps the mark until
// that card is used, including across turns.

static QStringList guJuaoPool()
{
	static QStringList cached;
	if (!cached.isEmpty()) return cached;
	QStringList names;
	QSet<QString> seen;
	names << QStringLiteral("slash");
	seen.insert(QStringLiteral("slash"));
	foreach (const QString &name, Sanguosha->getCardNames(QStringLiteral("TrickCard"))) {
		if (name.startsWith(QLatin1Char('_')) || seen.contains(name)) continue;
		Card *card = Sanguosha->cloneCard(name);
		if (!card) continue;
		const bool ok = card->isNDTrick();
		delete card;
		if (!ok) continue;
		seen.insert(name);
		names << name;
	}
	if (names.size() > 1) cached = names;
	return names;
}

static SkillDialogInfo guJuaoDialog()
{
	return SkillDialogInfo::juguan(QStringLiteral("gu_juao"), guJuaoPool().join(QStringLiteral(",")));
}

class GuJuaoCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE GuJuaoCard()
	{
		mute = true;
		setSkillName(QStringLiteral("gu_juao"));
	}

	bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const override
	{
		return Self && to_select && to_select != Self && !targets.contains(to_select)
			&& Self->inMyAttackRange(to_select);
	}

	bool targetsFeasible(const QList<const Player *> &targets, const Player *) const override
	{
		return !targets.isEmpty();
	}

	void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const override
	{
		if (!room || !source || !source->isAlive()) return;
		Card *card = Sanguosha->cloneCard(getUserString(), Card::NoSuit, -1);
		if (!card) return;
		card->setSkillName(QStringLiteral("gu_juao"));
		card->deleteLater();
		QList<ServerPlayer *> legal;
		foreach (ServerPlayer *p, targets) {
			if (!p || !p->isAlive() || p == source) continue;
			if (room->isProhibited(source, p, card)) continue;
			legal << p;
		}
		if (legal.isEmpty()) return;
		if (!room->useCard(CardUseStruct(card, source, legal), false)) return;
		foreach (ServerPlayer *p, legal) {
			if (!p->isAlive()) continue;
			room->setPlayerMark(p, "&gu_juao_next", 1);
			p->setTag(QStringLiteral("gu_juao_from"), source->objectName());
		}
	}
};

class GuJuao : public ViewAsSkill
{
public:
	GuJuao() : ViewAsSkill("gu_juao") {}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		return Self && selected.isEmpty() && to_select && Self->hasCard(to_select) && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (!Self || cards.length() != 1 || Self->hasUsed(QStringLiteral("GuJuaoCard"))) return nullptr;
		const Card *picked = Self->getTag(QStringLiteral("gu_juao")).value<const Card *>();
		if (!picked) return nullptr;
		QString name = picked->objectName();
		if (name.isEmpty()) name = picked->getClassName();
		GuJuaoCard *card = new GuJuaoCard;
		card->addSubcard(cards.first());
		card->setUserString(name);
		card->setSkillName(objectName());
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		if (!player || player->hasUsed(QStringLiteral("GuJuaoCard")) || player->getCardCount(true, false) < 1)
			return false;
		foreach (const Player *p, player->getAliveSiblings())
			if (player->inMyAttackRange(p)) return true;
		return false;
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
		return guJuaoDialog();
	}
};

class GuJuaoMod : public TargetModSkill
{
public:
	GuJuaoMod() : TargetModSkill("#gu_juao-mod")
	{
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const override
	{
		if (!from || !guSlashOrNdTrick(card)) return 0;
		if (card->getSkillName() == QLatin1String("gu_juao")) return -1;
		if (from->getMark("&gu_juao_next") > 0) return -1;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const override
	{
		if (from && from->getMark("&gu_juao_next") > 0 && guSlashOrNdTrick(card)) return 1000;
		return 0;
	}
};

class GuJuaoForce : public TriggerSkill
{
public:
	GuJuaoForce() : TriggerSkill("#gu_juao-force")
	{
		events << TargetSpecifying << CardFinished;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.from || !guSlashOrNdTrick(use.card) || use.from->getMark("&gu_juao_next") <= 0)
			return false;
		if (event == TargetSpecifying) {
			const QString sourceName = use.from->getTag(QStringLiteral("gu_juao_from")).toString();
			ServerPlayer *source = room->findPlayerByObjectName(sourceName);
			if (source && source->isAlive() && source != use.from && !use.to.contains(source)
				&& !room->isProhibited(use.from, source, use.card)) {
				use.to << source;
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
		}
		room->setPlayerMark(use.from, "&gu_juao_next", 0);
		use.from->removeTag(QStringLiteral("gu_juao_from"));
		return false;
	}
};

// ---------------- 孙綝 逆固 ----------------
// "同時" is asked in action order. A character in range who gives nothing, including
// one with no card, counts toward X. The next X damage instances this turn are +1.

class GuNiguCard : public SkillCard
{
	Q_OBJECT

public:
	Q_INVOKABLE GuNiguCard()
	{
		target_fixed = true;
		mute = true;
		setSkillName(QStringLiteral("gu_nigu"));
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
		if (!room || !source || !source->isAlive()) return;
		QList<ServerPlayer *> zone;
		foreach (ServerPlayer *p, room->getOtherPlayers(source))
			if (source->inMyAttackRange(p)) zone << p;
		room->sortByActionOrder(zone);
		int refused = 0;
		foreach (ServerPlayer *p, zone) {
			if (!source->isAlive()) {
				++refused;
				continue;
			}
			if (!p->isAlive() || p->getCardCount(true, false) < 1) {
				++refused;
				continue;
			}
			const Card *card = room->askForCard(p, ".", "@gu_nigu-give:" + source->objectName(),
				QVariant::fromValue(source), Card::MethodNone);
			if (!card || !source->isAlive()) {
				++refused;
				continue;
			}
			room->obtainCard(source, card,
				CardMoveReason(CardMoveReason::S_REASON_GIVE, p->objectName(), source->objectName(),
					QStringLiteral("gu_nigu"), QString()),
				true);
		}
		if (source->isAlive() && refused > 0)
			room->setPlayerMark(source, "gu_nigu_charge-Clear", refused);
	}
};

class GuNiguVS : public ViewAsSkill
{
public:
	GuNiguVS() : ViewAsSkill("gu_nigu") {}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const override
	{
		if (!Self || !to_select || selected.length() >= 4 || !Self->hasCard(to_select) || Self->isJilei(to_select))
			return false;
		if (!guRealSuit(to_select->getSuit())) return false;
		foreach (const Card *card, selected)
			if (card && card->getSuit() == to_select->getSuit()) return false;
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const override
	{
		if (!Self || cards.isEmpty() || Self->hasUsed(QStringLiteral("GuNiguCard"))) return nullptr;
		GuNiguCard *card = new GuNiguCard;
		card->addSubcards(cards);
		card->setSkillName(objectName());
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const override
	{
		return player && !player->hasUsed(QStringLiteral("GuNiguCard")) && player->getCardCount(true, false) > 0;
	}

	bool isEnabledAtResponse(const Player *, const QString &) const override
	{
		return false;
	}
};

class GuNigu : public TriggerSkill
{
public:
	GuNigu() : TriggerSkill("gu_nigu")
	{
		events << ConfirmDamage;
		view_as_skill = new GuNiguVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const int n = player->getMark("gu_nigu_charge-Clear");
		if (n <= 0) return false;
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.from != player || damage.damage < 1) return false;
		room->notifySkillInvoked(player, objectName());
		damage.damage += 1;
		room->setPlayerMark(player, "gu_nigu_charge-Clear", n - 1);
		data = QVariant::fromValue(damage);
		return false;
	}
};

// ---------------- 孙綝 戮连 ----------------
// Omitted subjects are read off the targets: any living target at or below your
// Hp chains every living target; any living target with as few or fewer equip
// cards draws 1. 乘势 happens only when both of those are met. 横置 sets chained.

class GuLulian : public TriggerSkill
{
public:
	GuLulian() : TriggerSkill("gu_lulian")
	{
		events << CardFinished;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive()) return false;
		const CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || use.from != player || !use.m_isHandcard || use.to.isEmpty()) return false;
		const Card::CardType type = use.card->getTypeId();
		if (type != Card::TypeBasic && type != Card::TypeTrick && type != Card::TypeEquip) return false;
		foreach (const Card *card, player->getHandcards())
			if (card && card->getTypeId() == type) return false;
		bool byHp = false;
		bool byEquip = false;
		QList<ServerPlayer *> victims;
		foreach (ServerPlayer *target, use.to) {
			if (!target || !target->isAlive()) continue;
			victims << target;
			if (target->getHp() <= player->getHp()) byHp = true;
			if (target->getEquips().length() <= player->getEquips().length()) byEquip = true;
		}
		if (victims.isEmpty() || (!byHp && !byEquip)) return false;
		room->sendCompulsoryTriggerLog(player, this);
		if (byHp) {
			foreach (ServerPlayer *target, victims)
				if (target->isAlive() && !target->isChained())
					room->setPlayerChained(target, true, player);
		}
		if (byEquip && player->isAlive()) player->drawCards(1, objectName());
		if (!byHp || !byEquip || !player->isAlive()) return false;
		int minHp = 0;
		bool haveMin = false;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (!haveMin || p->getHp() < minHp) {
				minHp = p->getHp();
				haveMin = true;
			}
		}
		QList<ServerPlayer *> picks;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			if (p->getHp() > minHp) picks << p;
		if (picks.isEmpty()) return false;
		ServerPlayer *to = room->askForPlayerChosen(player, picks, objectName(), "@gu_lulian-burn", false, true);
		if (to && to->isAlive())
			room->damage(DamageStruct(objectName(), player, to, 1, DamageStruct::Fire));
		return false;
	}
};

// ---------------- 势程普 督佐 / 蔽扞 ----------------
// The extra pile is a finite stock of derivative slashes parked on the table.
// The document does not give a pile size.

static int guExtraSlashId(Room *room)
{
	if (!room || !Sanguosha) return -1;
	const int n = Sanguosha->getCardCount();
	for (int id = 0; id < n; ++id) {
		const Card *card = Sanguosha->getEngineCard(id);
		if (!card || card->objectName() != QLatin1String("_gu_extra_slash")) continue;
		if (room->getCardOwner(id) == nullptr && room->getCardPlace(id) == Player::PlaceTable)
			return id;
	}
	return -1;
}

static bool guDuzuoGain(const CardsMoveOneTimeStruct &move, ServerPlayer *player)
{
	if (!player || move.to != player || move.to_place != Player::PlaceHand) return false;
	if (move.reason.m_skillName == QLatin1String("gu_duzuo")) return false;
	for (int i = 0; i < move.card_ids.size(); ++i) {
		const Player::Place from = i < move.from_places.size() ? move.from_places.at(i) : Player::PlaceUnknown;
		if (move.from == player && (from == Player::PlaceHand || from == Player::PlaceEquip
			|| from == Player::PlaceJudge || from == Player::PlaceDelayedTrick || from == Player::PlaceSpecial))
			continue;
		return true;
	}
	return false;
}

class GuDuzuo : public TriggerSkill
{
public:
	GuDuzuo() : TriggerSkill("gu_duzuo")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || player->hasFlag("gu_duzuo_busy")) return false;
		const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!guDuzuoGain(move, player)) return false;
		if (guExtraSlashId(room) < 0) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		QList<ServerPlayer *> alive = room->getAlivePlayers();
		if (alive.isEmpty()) return false;
		room->setPlayerFlag(player, "gu_duzuo_busy");
		ServerPlayer *target = room->askForPlayerChosen(player, alive, objectName(), "@gu_duzuo", false, true);
		const int id = target && target->isAlive() ? guExtraSlashId(room) : -1;
		if (id >= 0) room->obtainCard(target, id, objectName());
		if (player->isAlive()) room->setPlayerFlag(player, "-gu_duzuo_busy");
		return false;
	}
};

class GuBihan : public TriggerSkill
{
public:
	GuBihan() : TriggerSkill("gu_bihan")
	{
		events << DamageInflicted;
		global = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.damage < 1 || !damage.to || !damage.to->isAlive()) return false;
		if (!damage.card || !damage.card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *owner, room->getAlivePlayers()) {
			if (damage.damage < 1 || !damage.to->isAlive()) break;
			if (!owner->isAlive() || !owner->hasSkill(this)) continue;
			if (!owner->askForSkillInvoke(this, QVariant::fromValue(damage.to))) continue;
			ServerPlayer *who = owner;
			if (owner != damage.to && damage.to->isAlive()) {
				const QString choice = room->askForChoice(owner, objectName(), "self+victim",
					QVariant::fromValue(damage.to));
				if (choice == QLatin1String("victim")) who = damage.to;
			}
			if (!who || !who->isAlive()) continue;
			const int floorHp = qMax(0, who->getHp());
			const int excess = who->getHandcardNum() - floorHp;
			if (excess > 0) {
				if (guDiscardableHand(who) < excess) continue;
				if (!room->askForDiscard(who, objectName(), excess, excess, false, false, "@gu_bihan-discard"))
					continue;
			}
			damage.damage -= 1;
			data = QVariant::fromValue(damage);
		}
		return damage.damage < 1;
	}
};

void guAudit2026JuneB(Package *pkg)
{
	pkg->addMetaObject<GuJuaoCard>();
	pkg->addMetaObject<GuNiguCard>();
	pkg->addSkills(new GuJuaoMod);
	pkg->insertRelatedSkills("gu_juao", "#gu_juao-mod");
	pkg->addSkills(new GuJuaoForce);
	pkg->insertRelatedSkills("gu_juao", "#gu_juao-force");

	// Eight unowned copies. A single leading underscore keeps them out of the normal deck.
	for (int i = 0; i < 8; ++i) {
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setObjectName("_gu_extra_slash");
		slash->setParent(pkg);
	}

	General *guanyu = new General(pkg, "gu_mouguanyu", "shu", 4, true);
	guanyu->addSkill(new GuGuanwu);
	guanyu->addSkill(new GuWeishi);
	guanyu->addSkill(new GuJuao);

	General *sunchen = new General(pkg, "gu_sunchen", "wu", 4, true);
	sunchen->addSkill(new GuNigu);
	sunchen->addSkill(new GuLulian);

	General *chengpu = new General(pkg, "gu_shichengpu", "wu", 4, true);
	chengpu->addSkill(new GuDuzuo);
	chengpu->addSkill(new GuBihan);
}

#include "gu-audit2026-m06b.moc"
