//#include "standard.h"
#include "standard-cards.h"
//#include "maneuvering.h"
//#include "general.h"
#include "engine.h"
//#include "client.h"
#include "room.h"
//#include "ai.h"
#include "settings.h"
#include "clientplayer.h"
#include "clientstruct.h"
//#include "util.h"
#include "wrapped-card.h"
#include "roomthread.h"

Slash::Slash(Suit suit, int number) : BasicCard(suit, number)
{
    setObjectName("slash");
    nature = DamageStruct::Normal;
    damage_card = true;
    single_target = true;
}

bool Slash::IsAvailable(const Player *player, const Card *slash, bool)
{
	if (!slash){
		Card *c = new Slash(Card::NoSuit, 0);
		c->deleteLater();
		slash = c;
	}
	foreach (const Player *p, player->getAliveSiblings()) {
		if(slash->targetFilter(QList<const Player *>(),p,player))
			return true;
	}
	return false;
}

bool Slash::isAvailable(const Player *player) const
{
	return Slash::IsAvailable(player,this);
}

QString Slash::getSubtype() const
{
    return "attack_card";
}

void Slash::onUse(Room *room, CardUseStruct &use) const
{
    if (use.from->hasFlag("slashTargetFix")) {
        room->setPlayerFlag(use.from, "-slashTargetFix");
        room->setPlayerFlag(use.from, "-slashTargetFixToOne");
        foreach(ServerPlayer *target, room->getAlivePlayers())
            room->setPlayerFlag(target, "-SlashAssignee");
    }
    if (use.m_isOwnerUse) {
        QVariant data = QVariant::fromValue(use);

        QStringList flags = getFlags();
        room->getThread()->trigger(PreChangeSlash, room, use.from, data);  //用以在此时机给卡牌添加flag或其他记录，请勿在此时机将use.card进行更改
        use = data.value<CardUseStruct>();
		if(this!=use.card){
			foreach (QString flag, flags)
				room->setCardFlag(use.card, flag);
			flags = use.card->getFlags();
		}

        room->getThread()->trigger(ChangeSlash, room, use.from, data);  //请勿在此时机将【杀】变为其他不属于【杀】的牌
        use = data.value<CardUseStruct>();
		foreach (QString flag, flags)
			room->setCardFlag(use.card, flag);
    }
	int n = use.from->getMark("drank");
	if (n > 0) {
		use.card->setTag("drank", n);
		room->setCardFlag(use.card, "drank");
		room->setPlayerMark(use.from, "drank", 0);
	}
	if(use.from->hasFlag("slashDisableExtraTarget"))
		room->setPlayerFlag(use.from, "-slashDisableExtraTarget");
	else if(this!=use.card){
		n = 1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, use.from, use.card)-use.to.length();
        if (n>0) {
			QList<ServerPlayer *> targets;
            foreach(ServerPlayer *p, room->getAlivePlayers())
                if (!use.to.contains(p) && use.from->canSlash(p, use.card, !use.from->hasFlag("slashNoDistanceLimit")))
                    targets << p;
            use.to << room->askForPlayersChosen(use.from, targets, "slash_extra_targets", 0, n, "@slash_extra_targets", false, false);
        }
	}
    room->setPlayerFlag(use.from, "-slashNoDistanceLimit");

    if (use.from->getPhase() == Player::Play && use.from->getSlashCount() > 1) {
        QString name;
        if (use.from->hasSkill("paoxiao"))
            name = "paoxiao";
        if (use.from->hasSkill("tenyearpaoxiao"))
            name = "tenyearpaoxiao";
        if (use.from->hasSkill("olpaoxiao"))
            name = "olpaoxiao";
        else if (use.from->getMark("huxiao") > 0)
            name = "huxiao";
        if (name.isEmpty()) {
			if (use.from->hasWeapon("Crossbow")){
				room->setEmotion(use.from, "weapon/crossbow");
				room->notifySkillInvoked(use.from, "crossbow");
			}
        }else {
            n = qrand() % 2 + 1;
            if (name == "paoxiao") {
				if (Player::isNostalGeneral(use.from, "zhangfei"))
                    n += 2;
                else if (use.from->hasSkill("baobian"))
                    n += 4;
            }
            room->broadcastSkillInvoke(name, n);
            room->notifySkillInvoked(use.from, name);
		}
    }
	if (use.to.length() > 1){
		if (use.from->hasSkill("shenji")) {
			room->broadcastSkillInvoke("shenji");
			room->notifySkillInvoked(use.from, "shenji");
		} else if (use.card->isKindOf("FireSlash") && use.card->getSkillNames().contains("lihuo") && use.from->hasSkill("lihuo",true)) {
			n = 1;
			if (use.from->isJieGeneral()) n = 3;
			room->broadcastSkillInvoke("lihuo", n);
			room->notifySkillInvoked(use.from, "lihuo");
		} else if (use.from->hasSkill("duanbing")) {
			n = 1;
			if (use.from->getGeneralName().contains("heqi") || (!use.from->getGeneralName().contains("dingfeng") && use.from->getGeneral2Name().contains("heqi")))
				n++;
			room->broadcastSkillInvoke("duanbing", n);
			room->notifySkillInvoked(use.from, "duanbing");
		} else if (use.from->hasSkill("olnewshichou")) {
			room->broadcastSkillInvoke("olnewshichou");
			room->notifySkillInvoked(use.from, "olnewshichou");
		} else if (use.from->hasSkill("newshichou")) {
			room->broadcastSkillInvoke("newshichou");
			room->notifySkillInvoked(use.from, "newshichou");
		}else if (use.from->hasWeapon("halberd") && use.from->isLastHandCard(this)) {
			room->setEmotion(use.from, "weapon/halberd");
			room->notifySkillInvoked(use.from, "halberd");
		}
	}
    foreach (ServerPlayer *p, use.to) {
        if (p->getHandcardNum() > p->getHp() && p->hasSkill("tongji")) {
			n = 0;
			if (use.card->isVirtualCard()) {
				const Card *c = use.from->getWeapon();
				if (c && use.card->getSubcards().contains(c->getId()))
					n += qobject_cast<const Weapon *>(c->getRealCard())->getRange(use.from)-use.from->getAttackRange(false);
				c = use.from->getOffensiveHorse();
				if (c && use.card->getSubcards().contains(c->getId()))
					n -= qobject_cast<const Horse *>(c->getRealCard())->getCorrect(use.from);
			}
			if (use.from->inMyAttackRange(p, n)){
				room->broadcastSkillInvoke("tongji");
				room->notifySkillInvoked(p, "tongji");
			}
        }
    }
	use.from->tag.remove("Jink_" + use.card->toString());
    BasicCard::onUse(room, use);
}

void Slash::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    CardUseStruct cardUse = room->getTag("UseHistory"+toString()).value<CardUseStruct>();
	QVariantList jink_list = source->tag["Jink_"+toString()].toList();
    foreach (ServerPlayer *target, targets) {
		CardEffectStruct effect;

		if(jink_list.length()>0)
			effect.offset_num = jink_list.takeFirst().toInt();

        effect.nullified = cardUse.nullified_list.contains("_ALL_TARGETS")
			||cardUse.nullified_list.contains(target->objectName());
		if (effect.nullified){
			room->setEmotion(target,"skill_nullify");
			continue;
		}
		effect.card = this;
		effect.from = source;
        effect.to = target;
		effect.multiple = targets.length() > 1;
        effect.no_respond = cardUse.no_respond_list.contains("_ALL_TARGETS")
			||cardUse.no_respond_list.contains(target->objectName());
        effect.no_offset = cardUse.no_offset_list.contains("_ALL_TARGETS")
			||cardUse.no_offset_list.contains(target->objectName());
        room->cardEffect(effect);
    }
}

void Slash::onEffect(CardEffectStruct &effect) const
{
	effect.to->getRoom()->damage(DamageStruct(effect.card, effect.from, effect.to, 1, nature));
	/*
	SlashEffectStruct effect;
    effect.from = card_effect.from;
    effect.nature = nature;
    effect.slash = this;

    effect.to = card_effect.to;
    effect.drank = tag["drank"].toInt();
    effect.nullified = card_effect.nullified;
    effect.no_offset = card_effect.no_offset;
    effect.no_respond = card_effect.no_respond;
    effect.multiple = card_effect.multiple;

    if (!effect.no_offset && !effect.no_respond) {
        QVariantList jink_list = effect.from->tag["Jink_" + toString()].toList();
        effect.jink_num = jink_list.takeFirst().toInt();
        if (jink_list.isEmpty())
            effect.from->tag.remove("Jink_" + toString());
        else
            effect.from->tag["Jink_" + toString()] = QVariant::fromValue(jink_list);
    }
    effect.from->getRoom()->slashEffect(effect);*/
}

bool Slash::IsSpecificAssignee(const Player *player, const Player *from, const Card *slash)
{
    if (from->hasFlag("slashTargetFix") && player->hasFlag("SlashAssignee"))
        return true;
    else if (!Slash::IsAvailable(from, slash, true)) {
        QStringList chixin_list = from->property("chixin").toString().split("+");
        if (from->hasSkill("chixin") && from->inMyAttackRange(player) && !chixin_list.contains(player->objectName())) return true;
        if (from->hasSkill("limu") && !from->getJudgingArea().isEmpty() && from->inMyAttackRange(player)) return true;
    } else {
        const Slash *s = qobject_cast<const Slash *>(slash);
        if (s && s->hasSpecificAssignee(player))
            return true;
    }
    return false;
}

bool Slash::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Self->hasFlag("slashDisableExtraTarget")){
		if (!to_select->hasFlag("SlashAssignee"))
			return false;
	}else if(Self->hasFlag("slashTargetFix")){
		int n = 0;
		foreach (const Player *p, Self->getAliveSiblings())
			if (p->hasFlag("SlashAssignee")) n++;
		if (n>targets.length()&&!to_select->hasFlag("SlashAssignee"))
			return false;
	}

    if (Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_PLAY
		&& Self->getSlashCount()>Sanguosha->correctCardTarget(TargetModSkill::Residue, Self, this, to_select))
		return false;

	int rangefix = 0;
	const Card *card = Self->getOffensiveHorse();
    if (card&&subcards.contains(card->getId())&&Self->hasOffensiveHorse(card->objectName()))
        rangefix -= qobject_cast<const Horse *>(card->getRealCard())->getCorrect(Self);

	/*foreach (const Player *p, Self->getAliveSiblings()) {
        if (Slash::IsSpecificAssignee(p, Self, this)) {
			if (targets.isEmpty())
				return Slash::IsSpecificAssignee(to_select, Self, this)
				&& Self->canSlash(to_select, this, distance_limit, rangefix);
			else {
				if (Self->hasFlag("slashDisableExtraTarget")) return false;
				bool canSelect = false;
				foreach (const Player *tp, targets) {
					if (Slash::IsSpecificAssignee(tp, Self, this)) {
						canSelect = true;
						break;
					}
				}
				if (!canSelect) return false;
			}
            break;
        }
    }*/
    int slash_targets = 1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);

    if (targets.length() >= slash_targets) {
        if (slash_targets>0&&targets.length()==slash_targets&&Self->hasSkill("duanbing"))
            return Self->distanceTo(to_select, rangefix)==1&&!Self->isProhibited(to_select, this);
		return false;
    }
	card = Self->getWeapon();
	if (card&&subcards.contains(card->getId())&&Self->hasWeapon(card->objectName()))
        rangefix += qobject_cast<const Weapon *>(card->getRealCard())->getRange(Self) - Self->getAttackRange(false);

    return Self->canSlash(to_select, this, !Self->hasFlag("slashNoDistanceLimit"), rangefix, targets);
}

Jink::Jink(Suit suit, int number) : BasicCard(suit, number)
{
    setObjectName("jink");
    target_fixed = true;
}

QString Jink::getSubtype() const
{
    return "defense_card";
}

bool Jink::isAvailable(const Player *) const
{
    return false;
}

bool Jink::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    return false;
}

/*
void Jink::onUse(Room *room, CardUseStruct &card_use) const
{
    room->setEmotion(card_use.from, "jink");
    BasicCard::onUse(room, card_use);
}*/

Peach::Peach(Suit suit, int number) : BasicCard(suit, number)
{
    setObjectName("peach");
    target_fixed = true;
    single_target = true;
}

QString Peach::getSubtype() const
{
    return "recover_card";
}

bool Peach::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty()//目标表为空（未选择目标）
	&& to_select->isWounded()//待选角色已受伤
	&& !Self->isProhibited(to_select, this, targets);//使用者不是禁止选择待选角色
}

void Peach::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty())
        use.to << use.from;
    BasicCard::onUse(room, use);
}

void Peach::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    //room->setEmotion(effect.from, "peach");
    room->recover(effect.to, RecoverStruct(effect.from, this));
}

bool Peach::isAvailable(const Player *player) const
{
    return player->isWounded() && !player->isProhibited(player, this) && BasicCard::isAvailable(player);
}

class CrossbowSkill : public TargetModSkill
{
public:
    CrossbowSkill() : TargetModSkill("crossbow")
    {
        frequency = Compulsory;
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasWeapon("crossbow"))
            return 999;
        return 0;
    }
};

Crossbow::Crossbow(Suit suit, int number)
    : Weapon(suit, number, 1)
{
    setObjectName("crossbow");
}

class DoubleSwordSkill : public WeaponSkill
{
public:
    DoubleSwordSkill() : WeaponSkill("double_sword")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        foreach (ServerPlayer *to, use.to) {
            if (use.from->getGender() != to->getGender() && use.card->isKindOf("Slash")) {
                if (use.from->askForSkillInvoke(this)) {
                    room->setEmotion(use.from, "weapon/double_sword");
                    if (to->canDiscard(to,"h")&&room->askForCard(to,".","double-sword-card:"+use.from->objectName(),data))
                        continue;
                    use.from->drawCards(1, objectName());
                }
            }
        }
        return false;
    }
};

DoubleSword::DoubleSword(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("double_sword");
}

class QinggangSwordSkill : public WeaponSkill
{
public:
    QinggangSwordSkill() : WeaponSkill("qinggang_sword")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")) {
            room->setEmotion(use.from, "weapon/qinggang_sword");
            room->sendCompulsoryTriggerLog(use.from, this);
            foreach (ServerPlayer *p, use.to) {
                p->addQinggangTag(use.card);
            }
        }
        return false;
    }
};

QinggangSword::QinggangSword(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("qinggang_sword");
}

class BladeSkill : public WeaponSkill
{
public:
    BladeSkill() : WeaponSkill("blade")
    {
        events << CardOffset << PreCardUsed;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == PreCardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash") && player->hasFlag("BladeUse")){
				player->setFlags("-BladeUse");
				LogMessage log;
				log.type = "#BladeUse";
				log.from = use.from;
				log.to << use.to;
				room->sendLog(log);
				room->setEmotion(player, "weapon/blade");
				room->notifySkillInvoked(player, "blade");
			}
			return false;
		}
		CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!(effect.card->isKindOf("Slash")&&effect.to->isAlive()&&effect.from->canSlash(effect.to, nullptr, false)))
            return false;

        int weapon_id = -1;
        if (player->getWeapon() && player->getWeapon()->objectName() == objectName()) {
            weapon_id = player->getWeapon()->getId();
            room->setCardFlag(weapon_id, "using");
        }
        effect.from->setFlags("BladeUse");
        room->askForUseSlashTo(effect.from, effect.to, QString("blade-slash:%1").arg(effect.to->objectName()), false, true);
        effect.from->setFlags("-BladeUse");

        if (weapon_id > 0)
            room->setCardFlag(weapon_id, "-using");

        return false;
    }
};

Blade::Blade(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("blade");
}

class SpearViewAsSkill : public ViewAsSkill
{
public:
    SpearViewAsSkill() : ViewAsSkill("spear")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && player->hasWeapon("spear");
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (pattern.contains("slash") || pattern.contains("Slash")) && player->hasWeapon("spear");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && !to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2) return nullptr;
        Slash *slash = new Slash(Card::SuitToBeDecided, 0);
        slash->setSkillName(objectName());
        slash->addSubcards(cards);
        return slash;
    }
};

class SpearSkill : public TriggerSkill
{
public:
    SpearSkill() : TriggerSkill("spear")
    {
        events << PreCardUsed << PreCardResponded;
        view_as_skill = new SpearViewAsSkill;
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = nullptr;
        if (triggerEvent == PreCardUsed)
            card = data.value<CardUseStruct>().card;
        else
            card = data.value<CardResponseStruct>().m_card;
        if (card->isKindOf("Slash") && card->getSkillNames().contains("spear"))
            room->setEmotion(player, "weapon/spear");
        return false;
    }
};

Spear::Spear(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("spear");
}

class AxeViewAsSkill : public ViewAsSkill
{
public:
    AxeViewAsSkill() : ViewAsSkill("axe")
    {
        response_pattern = "@axe";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped() && to_select->objectName() == objectName()) return false;
        return selected.length() < 2 && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(cards);
        return card;
    }
};

class AxeSkill : public WeaponSkill
{
public:
    AxeSkill() : WeaponSkill("axe&")
    {
        events << CardOffset;
        view_as_skill = new AxeViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();

        if (!effect.card->isKindOf("Slash")||!effect.to->isAlive())
            return false;

        int n = 3;
        if (!player->getWeapon() || player->getWeapon()->objectName() != objectName())
            n = 2;

        const Card *card = nullptr;
        if (player->getCardCount() >= n) // Need 2 more cards except from the weapon itself
            card = room->askForCard(player, "@axe", "@axe:" + effect.to->objectName(), data, objectName());
        if (card) {
            room->setEmotion(player, "weapon/axe");
            room->notifySkillInvoked(player, "axe");
			//effect.card->onEffect(effect);
            return true;
        }
        return false;
    }
};

Axe::Axe(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("axe");
}

class HalberdSkill : public TargetModSkill
{
public:
    HalberdSkill() : TargetModSkill("halberd")
    {
    }

    int getExtraTargetNum(const Player *from, const Card *card) const
    {
        if (from->hasWeapon("halberd") && from->isLastHandCard(card))
            return 2;
        return 0;
    }
};

Halberd::Halberd(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("halberd");
}

class KylinBowSkill : public WeaponSkill
{
public:
    KylinBowSkill() : WeaponSkill("kylin_bow")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();

        if (damage.card && damage.card->isKindOf("Slash") && damage.by_user && !damage.chain && !damage.transfer) {
			QList<int> horses;
            const Card*h = damage.to->getDefensiveHorse();
			if (h && damage.from->canDiscard(damage.to, h->getId()))
                horses << h->getId();
			h = damage.to->getOffensiveHorse();
            if (h && damage.from->canDiscard(damage.to, h->getId()))
                horses << h->getId();

            if (horses.isEmpty()||!player->askForSkillInvoke(this, data)) return false;

            room->setEmotion(player, "weapon/kylin_bow");

            room->fillAG(horses, player);
            int horse = room->askForAG(player, horses, false, objectName(), "@kylin_bow_horse");
            room->clearAG(player);

            room->throwCard(horse, damage.to, damage.from);
        }

        return false;
    }
};

KylinBow::KylinBow(Suit suit, int number)
    : Weapon(suit, number, 5)
{
    setObjectName("kylin_bow");
}

class EightDiagramSkill : public ArmorSkill
{
public:
    EightDiagramSkill() : ArmorSkill("eight_diagram")
    {
        events << CardAsked;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        QStringList asked = data.toStringList();
		if(!asked.first().contains("jink")&&!asked.first().contains("Jink")) return false;
		Jink *jink = new Jink(Card::NoSuit, 0);
		jink->setSkillName("_"+objectName());
		jink->deleteLater();
        Card::HandlingMethod method = asked.at(2)=="use"?Card::MethodUse:Card::MethodResponse;
        if(player->isCardLimited(jink,method)||!player->askForSkillInvoke(this,data)) return false;
		room->setEmotion(player, "armor/eight_diagram");
		int armor_id = -1;
		if (player->getArmor() && player->getArmor()->objectName() == objectName()) {
			armor_id = player->getArmor()->getId();
			room->setCardFlag(armor_id, "using");
		}
		JudgeStruct judge;
		judge.pattern = ".|red";
		judge.good = true;
		judge.reason = objectName();
		judge.who = player;

		room->judge(judge);
		if (armor_id > -1)
			room->setCardFlag(armor_id, "-using");

		if (judge.isGood()) {
			room->provide(jink);
			return true;
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return -2;
    }
};

EightDiagram::EightDiagram(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("eight_diagram");
}

AmazingGrace::AmazingGrace(Suit suit, int number)
    : GlobalEffect(suit, number)
{
    setObjectName("amazing_grace");
    has_preact = true;
}

void AmazingGrace::clearRestCards(Room *room) const
{
    room->getThread()->delay();
	room->clearAG();
    QVariantList ag_list = room->getTag("AmazingGrace"+toString()).toList();
    if (ag_list.isEmpty()) return;
	room->removeTag("AmazingGrace"+toString());
    room->throwCard(ListV2I(ag_list), CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, "", "amazing_grace", ""), nullptr);
}

/*void AmazingGrace::doPreAction(Room *room, const CardUseStruct &) const
{
    //QList<int> card_ids = room->getNCards(room->getAllPlayers().length());
    room->fillAG(card_ids);
    room->setTag("AmazingGrace", ListI2V(card_ids));
}*/

void AmazingGrace::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (targets.isEmpty()) return;
    QList<int> card_ids = ListV2I(room->getTag("AmazingGrace"+toString()).toList());
	if(card_ids.isEmpty()) card_ids = room->getNCards(targets.length());
    room->fillAG(card_ids);
    room->setTag("AmazingGrace"+toString(), ListI2V(card_ids));
    try {
        GlobalEffect::use(room, source, targets);
        clearRestCards(room);
    }catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            clearRestCards(room);
        throw triggerEvent;
    }
}

void AmazingGrace::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    QVariantList ag_list = room->getTag("AmazingGrace"+toString()).toList();
    if (ag_list.isEmpty()) return;

    int card_id = room->askForAG(effect.to, ListV2I(ag_list), false, objectName(), "amazing_grace_take_ag");

    room->takeAG(effect.to, card_id);
    ag_list.removeOne(card_id);

    room->setTag("AmazingGrace"+toString(), ag_list);
}

GodSalvation::GodSalvation(Suit suit, int number)
    : GlobalEffect(suit, number)
{
    setObjectName("god_salvation");
}

bool GodSalvation::isCancelable(const CardEffectStruct &effect) const
{
    return effect.to->isWounded() && TrickCard::isCancelable(effect);
}

void GodSalvation::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    if (effect.to->isWounded())
		room->recover(effect.to, RecoverStruct(effect.from, this));
	else
        room->setEmotion(effect.to, "skill_nullify");
}

SavageAssault::SavageAssault(Suit suit, int number)
    : AOE(suit, number)
{
    setObjectName("savage_assault");
    damage_card = true;
}

void SavageAssault::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();/*
    if (effect.no_respond) {  // || effect.no_offset) {
        room->damage(DamageStruct(this, effect.from->isAlive() ? effect.from : nullptr, effect.to));
        room->getThread()->delay();
    } else {*/
        const Card *slash = room->askForCard(effect.to, "slash", "savage-assault-slash:" + effect.from->objectName(),
            QVariant::fromValue(effect), Card::MethodResponse, effect.from, false, "", false, this);
        if (!slash) {
            room->damage(DamageStruct(this, effect.from, effect.to));
            room->getThread()->delay();
        }
    //}
}

ArcheryAttack::ArcheryAttack(Card::Suit suit, int number)
    : AOE(suit, number)
{
    setObjectName("archery_attack");
    damage_card = true;
}

void ArcheryAttack::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();/*
    if (effect.no_respond) {  // || effect.no_offset) {
        room->damage(DamageStruct(this, effect.from->isAlive() ? effect.from : nullptr, effect.to));
        room->getThread()->delay();
    } else {*/
        const Card *jink = room->askForCard(effect.to, "jink", "archery-attack-jink:" + effect.from->objectName(),
            QVariant::fromValue(effect), Card::MethodResponse, effect.from, false, "", false, this);
        if (!jink) {
            room->damage(DamageStruct(this, effect.from, effect.to));
            room->getThread()->delay();
        }
    //}
}

Collateral::Collateral(Card::Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("collateral");
}

bool Collateral::isAvailable(const Player *player) const
{
	int maxVotes = 0;
    foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player, maxVotes)||maxVotes>0)
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool Collateral::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &maxVotes) const
{
    int collateral_targets = 1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
	if (targets.length()>=collateral_targets*2) return false;
	if (targets.length()%2==1) {/*感觉不用考虑空城，指定杀目标最后手牌是无懈，目标出无懈后就无法杀他了
        if (to_select == Self && Self->hasSkill("kongcheng") && Self->isLastHandCard(this, true))
            return false;*/
        const Player *slashFrom = targets[targets.length()-1];
        if (slashFrom->inMyAttackRange(to_select)){
			Slash *newslash = new Slash(Card::NoSuit, 0);
			if(!slashFrom->isProhibited(to_select, newslash))
				maxVotes = collateral_targets;
			delete newslash;
		}
    } else {
		if (to_select!=Self&&to_select->getWeapon()&&!Self->isProhibited(to_select, this, targets))
			maxVotes = collateral_targets;
    }
    return false;
}

bool Collateral::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return (targets.length()>1&&targets.length()%2==0);
}

void Collateral::onUse(Room *room, CardUseStruct &card_use) const
{
	int n = 1;
	QList<ServerPlayer *> tos;
    foreach (ServerPlayer *p, card_use.to) {
		if (n%2==1){
			tos << p;
			if(card_use.to.length()>n)
				p->tag["attachTarget"] = QVariant::fromValue(card_use.to[n]);
		}
		n++;
	}
	card_use.to = tos;
    SingleTargetTrick::onUse(room, card_use);
}

void Collateral::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *p, targets) {
		ServerPlayer *victim = p->tag["attachTarget"].value<ServerPlayer *>();
		if (victim==nullptr){
			QList<ServerPlayer *> tops;
			foreach (ServerPlayer *tp, room->getOtherPlayers(p)) {
				int x = 0;
				if(targetFilter(QList<const Player*>() << p,tp,source,x)||x>0)
					tops << tp;
			}
			victim = room->askForPlayerChosen(source,tops,objectName());
			if(victim) p->tag["attachTarget"] = QVariant::fromValue(victim);
			else continue;
		}
		room->doAnimate(1, p->objectName(), victim->objectName());
		LogMessage log;
		log.type = "#CollateralSlash";
		log.from = source;
		log.to << victim;
		room->sendLog(log);
    }
    SingleTargetTrick::use(room, source, targets);
}

bool Collateral::doCollateral(Room *room, ServerPlayer *killer, ServerPlayer *victim, const QString &prompt, ServerPlayer *user) const
{
    bool useSlash = false;
    if (killer->canSlash(victim, nullptr, false))
        useSlash = room->askForUseSlashTo(killer, victim, prompt, true, false, false, user, this);
    return useSlash;
}

void Collateral::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    ServerPlayer *victim = effect.to->tag["attachTarget"].value<ServerPlayer *>();
    effect.to->tag.remove("attachTarget");
    if (victim && victim->isAlive() && effect.to->isAlive()) {
        QString prompt = QString("collateral-slash:%1:%2").arg(victim->objectName()).arg(effect.from->objectName());
        if (doCollateral(room, effect.to, victim, prompt, effect.from)) return;
    }
	if (effect.from->isAlive() && effect.to->getWeapon())
		effect.from->obtainCard(effect.to->getWeapon());
}

Nullification::Nullification(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    target_fixed = true;
    setObjectName("nullification");
}

bool Nullification::isAvailable(const Player *) const
{
    return false;
}

bool Nullification::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    return false;
}

void Nullification::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &tos) const
{
    CardUseStruct card_use = room->getTag("UseHistory"+toString()).value<CardUseStruct>();
	if(card_use.nullified_list.contains("_ALL_TARGETS")) return;
	CardEffectStruct effect = source->tag["NullifyingEffect"].value<CardEffectStruct>();
	QString tn = source->objectName();
	if (effect.to) tn = effect.to->objectName();
    QVariant data = "Nullification:"+card_use.whocard->getClassName()+":"+tn+":"+(effect.nullified?"true":"false");
	if (effect.from&&effect.card->isKindOf("Nullification")) tn = effect.from->objectName();
	room->doAnimate(QSanProtocol::S_ANIMATE_NULLIFICATION, source->objectName(), tn);
	LogMessage log;
	log.type = "#NullificationDetails";
	log.from = effect.from;
	log.to << effect.to;
	log.card_str = effect.card->toString();
	room->sendLog(log);
    room->getThread()->delay(Config.AIDelay/2);
    room->getThread()->trigger(ChoiceMade, room, source, data);
	if(room->_askForNullification(this,source,effect.to,!effect.nullified)) return;
    SingleTargetTrick::use(room,source,tos);
	card_use.no_offset_list << "_HAS_EFFECT";
	room->setTag("UseHistory"+toString(),QVariant::fromValue(card_use));
}

ExNihilo::ExNihilo(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("ex_nihilo");
    target_fixed = true;
}

bool ExNihilo::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->isProhibited(to_select, this, targets);
}

void ExNihilo::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty()) use.to << use.from;
    SingleTargetTrick::onUse(room, use);
}

bool ExNihilo::isAvailable(const Player *player) const
{
    return !player->isProhibited(player, this) && TrickCard::isAvailable(player);
}

void ExNihilo::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    int extra = 0;
    if (room->getMode() == "06_3v3" && Config.value("3v3/OfficialRule", "2013").toString() == "2013") {
        int friend_num = 0, enemy_num = 0;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (AI::GetRelation3v3(effect.to, p) == AI::Friend)
                friend_num++;
            else
                enemy_num++;
        }
        if (friend_num < enemy_num) extra = 1;
    }
    effect.to->drawCards(2 + extra, "ex_nihilo");
}

Duel::Duel(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("duel");
    damage_card = true;
}

bool Duel::isAvailable(const Player *player) const
{
    foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool Duel::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select!=Self&&targets.length()<=Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget,Self,this)
		&&!Self->isProhibited(to_select, this, targets);
}

void Duel::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *first = effect.to, *second = effect.from;
    Room *room = first->getRoom();

    room->setEmotion(first, "duel-pk");
    room->setEmotion(second, "duel-pk");
    first->tag.remove("DuelSlash" + toString());
    second->tag.remove("DuelSlash" + toString());

	while(first->isAlive()){
		const Card *slash = room->askForCard(first,"slash","duel-slash:"+second->objectName(),
			QVariant::fromValue(effect),Card::MethodResponse,second,false,"",false,this);
		if (!slash) break;
		QVariantList slash_list_first = first->tag["DuelSlash" + toString()].toList();
		foreach (int id, slash->getSubcards()) {
			if (slash_list_first.contains(QVariant(id))) continue;
			slash_list_first << id;
		}
		first->tag["DuelSlash" + toString()] = slash_list_first;
		qSwap(first, second);
	}
    DamageStruct damage(this, second, first);
    if (second != effect.from) damage.by_user = false;
	room->damage(damage);
}

Snatch::Snatch(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("snatch");
}

bool Snatch::isAvailable(const Player *player) const
{
    foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool Snatch::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (to_select==Self||to_select->getCardCount(true,!(ServerInfo.GameMode=="02_1v1"&&ServerInfo.GameRuleMode!="Classical"))<1
		||targets.length()>=1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget,Self,this))
        return false;
    int rangefix = 0;
    if (Self->getOffensiveHorse() && subcards.contains(Self->getOffensiveHorse()->getId()))
        rangefix += 1;
    if (getSkillName() == "jixi")
        rangefix += 1;

    if (Self->distanceTo(to_select,rangefix)>1+Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit,Self,this,to_select))
        return false;

    return !Self->isProhibited(to_select, this, targets);
}

void Snatch::onEffect(CardEffectStruct &effect) const
{
    if (effect.from->isDead() || effect.to->isAllNude())
        return;
    Room *room = effect.to->getRoom();
    QString flag = (room->getMode() == "02_1v1" && Config.value("1v1/Rule", "2013").toString() != "Classical") ? "he" : "hej";
    int card_id = room->askForCardChosen(effect.from, effect.to, flag, objectName());
    room->obtainCard(effect.from, card_id, room->getCardPlace(card_id) != Player::PlaceHand);
}

Dismantlement::Dismantlement(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("dismantlement");
}

bool Dismantlement::isAvailable(const Player *player) const
{
    foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool Dismantlement::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select!=Self&&targets.length()<=Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget,Self,this)
	&&to_select->getCardCount(true,!(ServerInfo.GameMode=="02_1v1"&&ServerInfo.GameRuleMode!="Classical"))>0
	&&!Self->isProhibited(to_select, this, targets);
}

void Dismantlement::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    bool using_2013 = (room->getMode() == "02_1v1" && Config.value("1v1/Rule", "2013").toString() != "Classical");
    QString flag = using_2013 ? "he" : "hej";
    if (effect.from->isDead()||!effect.from->canDiscard(effect.to, flag))
        return;

    int card_id = -1;
    if (!using_2013 || effect.from->getAI())
        card_id = room->askForCardChosen(effect.from, effect.to, flag, objectName(), false, Card::MethodDiscard);
    else {
        if (effect.to->hasEquip())
            card_id = room->askForCardChosen(effect.from, effect.to, flag, objectName(), false, Card::MethodDiscard);
        if (card_id == -1 || effect.to->handCards().contains(card_id)) {
            LogMessage log;
            log.type = "$ViewAllCards";
            log.from = effect.from;
            log.to << effect.to;
            log.card_str = ListI2S(effect.to->handCards()).join("+");
            room->sendLog(log, effect.from);

            card_id = room->askForCardChosen(effect.from, effect.to, "h", objectName(), true, Card::MethodDiscard);
        }
    }
    room->throwCard(card_id, objectName(), room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
}

Indulgence::Indulgence(Suit suit, int number)
    : DelayedTrick(suit, number)
{
    setObjectName("indulgence");

    judge.pattern = ".|heart";
    judge.good = true;
    judge.reason = objectName();
}

bool Indulgence::isAvailable(const Player *player) const
{
    foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return DelayedTrick::isAvailable(player);
	}
	return false;
}

bool Indulgence::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty()&&to_select!=Self&&to_select->hasJudgeArea()
	&&!to_select->containsTrick(objectName())&&!Self->isProhibited(to_select, this, targets);
}

void Indulgence::takeEffect(ServerPlayer *target) const
{
    target->clearHistory();
    target->skip(Player::Play);
}

Disaster::Disaster(Card::Suit suit, int number)
    : DelayedTrick(suit, number, true)
{
    target_fixed = true;
}

void Disaster::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty()) use.to << use.from;
    DelayedTrick::onUse(room, use);
}

bool Disaster::isAvailable(const Player *player) const
{
    return player->hasJudgeArea()&&DelayedTrick::isAvailable(player)
	&&!player->containsTrick(objectName())&&!player->isProhibited(player,this);
}

Lightning::Lightning(Suit suit, int number) :Disaster(suit, number)
{
    setObjectName("lightning");
    damage_card = true;

    judge.pattern = ".|spade|2~9";
    judge.good = false;
    judge.reason = objectName();
}

void Lightning::takeEffect(ServerPlayer *target) const
{
    target->getRoom()->damage(DamageStruct(this, nullptr, target, 3, DamageStruct::Thunder));
}

// EX cards

class IceSwordSkill : public WeaponSkill
{
public:
    IceSwordSkill() : WeaponSkill("ice_sword")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();

        if (damage.card && damage.card->isKindOf("Slash")
            && !damage.to->isNude() && damage.by_user && !damage.chain
			 && !damage.transfer && player->askForSkillInvoke("ice_sword", data)) {
            room->setEmotion(player, "weapon/ice_sword");
			room->notifySkillInvoked(player, "ice_sword");
            if (damage.from->canDiscard(damage.to, "he")) {
                int card_id = room->askForCardChosen(player, damage.to, "he", "ice_sword", false, Card::MethodDiscard);
                room->throwCard(Sanguosha->getCard(card_id), damage.to, damage.from);

                if (damage.from->isAlive() && damage.to->isAlive() && damage.from->canDiscard(damage.to, "he")) {
                    card_id = room->askForCardChosen(player, damage.to, "he", "ice_sword", false, Card::MethodDiscard);
                    room->throwCard(Sanguosha->getCard(card_id), damage.to, damage.from);
                }
            }
            return true;
        }
        return false;
    }
};

IceSword::IceSword(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("ice_sword");
}

class RenwangShieldSkill : public ArmorSkill
{
public:
    RenwangShieldSkill() : ArmorSkill("renwang_shield")
    {
        events << CardEffected;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card->isKindOf("Slash")&&effect.card->isBlack()) {
            LogMessage log;
            log.type = "#ArmorNullify";
            log.from = player;
            log.arg = objectName();
            log.arg2 = effect.card->objectName();
            player->getRoom()->sendLog(log);

            room->setEmotion(player, "armor/renwang_shield");
            room->notifySkillInvoked(player, "renwang_shield");

            effect.to->setFlags("Global_NonSkillNullify");
            return true;
        }
		return false;
    }
};

RenwangShield::RenwangShield(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("renwang_shield");
}

class HorseSkill : public DistanceSkill
{
public:
    HorseSkill() : DistanceSkill("horse")
    {
    }

    int getCorrect(const Player *from, const Player *to) const
    {
        int oh_correct = 0, dh_correct = 0;
		static QList<const OffensiveHorse *> from_ohs = Sanguosha->findChildren<const OffensiveHorse *>();
		foreach (const OffensiveHorse *oh, from_ohs) {
			if (from->hasOffensiveHorse(oh->objectName()))
				oh_correct = qMin(oh->getCorrect(from), oh_correct);
		}

		static QList<const DefensiveHorse *> to_dhs = Sanguosha->findChildren<const DefensiveHorse *>();
		foreach (const DefensiveHorse *dh, to_dhs){
			if(dh->objectName()=="god_horse") continue;
			if(to->hasDefensiveHorse(dh->objectName()))
				dh_correct = qMax(dh_correct, dh->getCorrect(to));
		}
        return oh_correct + dh_correct;
    }
};

WoodenOxCard::WoodenOxCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "wooden_ox";
}

void WoodenOxCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("wooden_ox", subcards, false);
	const Card *treasure = source->getTreasure();
    if (!treasure||treasure->objectName()!=m_skillName) return;

    QList<ServerPlayer *> targets;
    foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
        if (!p->getTreasure() && p->hasTreasureArea())
            targets << p;
    }
    ServerPlayer *target = room->askForPlayerChosen(source, targets, "wooden_ox", "@wooden_ox-move", true);
    if (target) {
		room->moveCardTo(treasure, source, target, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_TRANSFER, source->objectName(), target->objectName(),  "wooden_ox", ""));
    }
}

class WoodenOxViewAsSkill : public OneCardViewAsSkill
{
public:
    WoodenOxViewAsSkill() : OneCardViewAsSkill("wooden_ox")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("WoodenOxCard") && player->hasTreasure("wooden_ox");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        WoodenOxCard *card = new WoodenOxCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class WoodenOxSkill : public TreasureSkill
{
public:
    WoodenOxSkill() : TreasureSkill("wooden_ox")
    {
        events << CardsMoveOneTime << BeforeCardsMove;
        view_as_skill = new WoodenOxViewAsSkill;
    }
    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == BeforeCardsMove) return 0;
        return TriggerSkill::getPriority(triggerEvent);
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (event==CardsMoveOneTime) {
			if (move.to_place==Player::PlaceEquip&&move.to==player&&move.from&&!move.from->getPile("wooden_ox").isEmpty()){
				foreach (int id, move.card_ids) {
					if (Sanguosha->getCard(id)->objectName()=="wooden_ox"){
						if (player->isAlive()){
							QList<ServerPlayer *> p_list;
							p_list << player;
							player->addToPile("wooden_ox", move.from->getPile("wooden_ox"), false, p_list);
						}else
							room->throwCard(move.from->getPile("wooden_ox"),"wooden_ox",nullptr);
					}
				}
			}
			if (move.from==player&&player->hasTreasure("wooden_ox")&&move.from_pile_names.contains("wooden_ox")
				&&(move.reason.m_reason==CardMoveReason::S_REASON_RESPONSE||(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE)){
				int count = 0;
				for (int i = 0; i < move.card_ids.length(); i++)
					if (move.from_pile_names[i] == "wooden_ox") count++;
				if (count > 0) {
					LogMessage log;
					log.type = "#WoodenOx";
					log.from = player;
					log.arg = QString::number(count);
					log.arg2 = "wooden_ox";
					log.arg3 = move.reason.m_reason==CardMoveReason::S_REASON_RESPONSE ? "response" : "use";
					room->sendLog(log);
				}
			}
		}else if (move.from==player&&move.to_place!=Player::PlaceEquip&&move.from_places.contains(Player::PlaceEquip)){
			int i = 0;
			foreach (int id, move.card_ids) {
				if (move.from_places.at(i) == Player::PlaceEquip && Sanguosha->getCard(id)->objectName()=="wooden_ox")
					room->throwCard(player->getPile("wooden_ox"),"wooden_ox",nullptr);
				i++;
			}
		}
        return false;
    }
};

WoodenOx::WoodenOx(Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("wooden_ox");
}

void WoodenOx::onUninstall(ServerPlayer *player) const
{
    player->getRoom()->addPlayerHistory(player, "WoodenOxCard", 0);
    Treasure::onUninstall(player);
}

StandardCardPackage::StandardCardPackage()
    : Package("standard_cards", Package::CardPack)
{
    QList<Card *> cards;

    cards << new Slash(Card::Spade, 7)
        << new Slash(Card::Spade, 8)
        << new Slash(Card::Spade, 8)
        << new Slash(Card::Spade, 9)
        << new Slash(Card::Spade, 9)
        << new Slash(Card::Spade, 10)
        << new Slash(Card::Spade, 10)

        << new Slash(Card::Club, 2)
        << new Slash(Card::Club, 3)
        << new Slash(Card::Club, 4)
        << new Slash(Card::Club, 5)
        << new Slash(Card::Club, 6)
        << new Slash(Card::Club, 7)
        << new Slash(Card::Club, 8)
        << new Slash(Card::Club, 8)
        << new Slash(Card::Club, 9)
        << new Slash(Card::Club, 9)
        << new Slash(Card::Club, 10)
        << new Slash(Card::Club, 10)
        << new Slash(Card::Club, 11)
        << new Slash(Card::Club, 11)

        << new Slash(Card::Heart, 10)
        << new Slash(Card::Heart, 10)
        << new Slash(Card::Heart, 11)

        << new Slash(Card::Diamond, 6)
        << new Slash(Card::Diamond, 7)
        << new Slash(Card::Diamond, 8)
        << new Slash(Card::Diamond, 9)
        << new Slash(Card::Diamond, 10)
        << new Slash(Card::Diamond, 13)

        << new Jink(Card::Heart, 2)
        << new Jink(Card::Heart, 2)
        << new Jink(Card::Heart, 13)

        << new Jink(Card::Diamond, 2)
        << new Jink(Card::Diamond, 2)
        << new Jink(Card::Diamond, 3)
        << new Jink(Card::Diamond, 4)
        << new Jink(Card::Diamond, 5)
        << new Jink(Card::Diamond, 6)
        << new Jink(Card::Diamond, 7)
        << new Jink(Card::Diamond, 8)
        << new Jink(Card::Diamond, 9)
        << new Jink(Card::Diamond, 10)
        << new Jink(Card::Diamond, 11)
        << new Jink(Card::Diamond, 11)

        << new Peach(Card::Heart, 3)
        << new Peach(Card::Heart, 4)
        << new Peach(Card::Heart, 6)
        << new Peach(Card::Heart, 7)
        << new Peach(Card::Heart, 8)
        << new Peach(Card::Heart, 9)
        << new Peach(Card::Heart, 12)

        << new Peach(Card::Diamond, 12)

        << new Crossbow(Card::Club)
        << new Crossbow(Card::Diamond)
        << new DoubleSword
        << new QinggangSword
        << new Blade
        << new Spear
        << new Axe
        << new Halberd
        << new KylinBow

        << new EightDiagram(Card::Spade)
        << new EightDiagram(Card::Club);

    skills << new DoubleSwordSkill << new QinggangSwordSkill
        << new BladeSkill << new SpearSkill << new AxeSkill
        << new KylinBowSkill << new EightDiagramSkill
        << new HalberdSkill << new CrossbowSkill;

    QList<Card *> horses;
    horses << new DefensiveHorse(Card::Spade, 5)
        << new DefensiveHorse(Card::Club, 5)
        << new DefensiveHorse(Card::Heart, 13)
        << new OffensiveHorse(Card::Heart, 5)
        << new OffensiveHorse(Card::Spade, 13)
        << new OffensiveHorse(Card::Diamond, 13);

    horses.at(0)->setObjectName("jueying");
    horses.at(1)->setObjectName("dilu");
    horses.at(2)->setObjectName("zhuahuangfeidian");
    horses.at(3)->setObjectName("chitu");
    horses.at(4)->setObjectName("dayuan");
    horses.at(5)->setObjectName("zixing");

    cards << horses;
    skills << new HorseSkill;

    cards << new AmazingGrace(Card::Heart, 3)
        << new AmazingGrace(Card::Heart, 4)
        << new GodSalvation
        << new SavageAssault(Card::Spade, 7)
        << new SavageAssault(Card::Spade, 13)
        << new SavageAssault(Card::Club, 7)
        << new ArcheryAttack
        << new Duel(Card::Spade, 1)
        << new Duel(Card::Club, 1)
        << new Duel(Card::Diamond, 1)
        << new ExNihilo(Card::Heart, 7)
        << new ExNihilo(Card::Heart, 8)
        << new ExNihilo(Card::Heart, 9)
        << new ExNihilo(Card::Heart, 11)
        << new Snatch(Card::Spade, 3)
        << new Snatch(Card::Spade, 4)
        << new Snatch(Card::Spade, 11)
        << new Snatch(Card::Diamond, 3)
        << new Snatch(Card::Diamond, 4)
        << new Dismantlement(Card::Spade, 3)
        << new Dismantlement(Card::Spade, 4)
        << new Dismantlement(Card::Spade, 12)
        << new Dismantlement(Card::Club, 3)
        << new Dismantlement(Card::Club, 4)
        << new Dismantlement(Card::Heart, 12)
        << new Collateral(Card::Club, 12)
        << new Collateral(Card::Club, 13)
        << new Nullification(Card::Spade, 11)
        << new Nullification(Card::Club, 12)
        << new Nullification(Card::Club, 13)
        << new Indulgence(Card::Spade, 6)
        << new Indulgence(Card::Club, 6)
        << new Indulgence(Card::Heart, 6)
        << new Lightning(Card::Spade, 1);

    foreach(Card *card, cards)
        card->setParent(this);
}

StandardExCardPackage::StandardExCardPackage()
    : Package("standard_ex_cards", Package::CardPack)
{
    QList<Card *> cards;
    cards << new IceSword(Card::Spade, 2)
        << new RenwangShield(Card::Club, 2)
        << new Lightning(Card::Heart, 12)
        << new Nullification(Card::Diamond, 12);

    skills << new RenwangShieldSkill << new IceSwordSkill;

    foreach(Card *card, cards)
        card->setParent(this);
}

LimitationBrokenPackage::LimitationBrokenPackage()
    : Package("limitation_broken", Package::CardPack)
{
    QList<Card *> cards;
    cards << new WoodenOx(Card::Diamond, 5);

    skills << new WoodenOxSkill;

    foreach(Card *card, cards)
        card->setParent(this);

    addMetaObject<WoodenOxCard>();
}

ADD_PACKAGE(StandardCard)
ADD_PACKAGE(StandardExCard)
ADD_PACKAGE(LimitationBroken)