#include "maneuvering.h"
//#include "client.h"
#include "engine.h"
//#include "general.h"
#include "room.h"
#include "wrapped-card.h"
#include "roomthread.h"

NatureSlash::NatureSlash(Suit suit, int number, DamageStruct::Nature nature)
    : Slash(suit, number)
{
    this->nature = nature;
    damage_card = true;
    single_target = true;
}

bool NatureSlash::match(const QString &pattern) const
{
    QStringList patterns = pattern.split("+");
    if (patterns.contains("slash")) return true;
    return Slash::match(pattern);
}

ThunderSlash::ThunderSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::Thunder)
{
    setObjectName("thunder_slash");
    damage_card = true;
    single_target = true;
}

FireSlash::FireSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::Fire)
{
    setObjectName("fire_slash");
    nature = DamageStruct::Fire;
    damage_card = true;
    single_target = true;
}

Analeptic::Analeptic(Card::Suit suit, int number)
    : BasicCard(suit, number)
{
    setObjectName("analeptic");
    target_fixed = true;
    single_target = true;
}

QString Analeptic::getSubtype() const
{
    return "buff_card";
}

bool Analeptic::IsAvailable(const Player *player, const Card *analeptic)
{
	if (!analeptic){
		Card *ac = new Analeptic(Card::NoSuit, 0);
		ac->deleteLater();
		analeptic = ac;
	}
    return player->usedTimes("Analeptic")<=Sanguosha->correctCardTarget(TargetModSkill::Residue,player,analeptic,player)
		&&!player->isCardLimited(analeptic,Card::MethodUse)&&!player->isProhibited(player,analeptic);
}

bool Analeptic::isAvailable(const Player *player) const
{
    return IsAvailable(player, this) && BasicCard::isAvailable(player);
}

bool Analeptic::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->isProhibited(to_select, this, targets);
}

void Analeptic::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty()) use.to << use.from;
	foreach (ServerPlayer *p, use.to) {
		if(p->hasFlag("Global_Dying") && Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_PLAY)
			setFlags("AnalepticRecover");
		break;
	}
    BasicCard::onUse(room, use);
}

void Analeptic::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    if (hasFlag("AnalepticRecover"))
        room->recover(effect.to, RecoverStruct(effect.from, this));
    else
        room->addPlayerMark(effect.to, "drank");
}

class FanVSSkill : public OneCardViewAsSkill
{
public:
    bool isEquipSkill() const override { return true; }

    FanVSSkill() : OneCardViewAsSkill("fan")
    {
        filter_pattern = "%slash";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->hasWeapon("fan") && Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return player->hasWeapon("fan")
            && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (pattern.contains("slash") || pattern.contains("Slash"));
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *acard = new FireSlash(originalCard->getSuit(), originalCard->getNumber());
        acard->addSubcard(originalCard->getId());
        acard->setSkillName(objectName());
        return acard;
    }
};

class FanSkill : public WeaponSkillV2
{
public:
    FanSkill() : WeaponSkillV2("fan", "fan")
    {
        events << ChangeSlash;
        view_as_skill = new FanVSSkill;
    }

    void record(TriggerEvent, Room *room, ServerPlayer *player,
                SkillContext &ctx) const override
    {
        if (player && ctx.original_data) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (use.card && use.card->getSkillName() == objectName())
                room->setEmotion(player, "weapon/fan");
        }
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player,
                            QVariant &data) const override
    {
        TriggerList result;
        if (!WeaponSkillV2::triggerable(player))
            return result;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->objectName() != "slash")
            return result;
        // Fan's own FireSlash conversion is handled by record() only; the
        // legacy path never opened a second invoke dialog for it.
        if (use.card->getSkillName() == objectName())
            return result;
        if (Config.EnableHegemony) {
            const ViewAsSkill *conversion = Sanguosha->getViewAsSkill(use.card->getSkillName());
            if (conversion && !conversion->inherits("FilterSkill")
                && use.card->subcardsLength() > 0)
                return result;
        }
        FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
        if (use.card->isVirtualCard())
            fire_slash->addSubcards(use.card->getSubcards());
        else
            fire_slash->addSubcard(use.card);
        fire_slash->setSkillName(objectName());
        for (ServerPlayer *target : use.to) {
            if (!player->canSlash(target, fire_slash, false)) {
                delete fire_slash;
                return result;
            }
        }
        delete fire_slash;
        result.insert(player, QStringList(objectName()));
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player,
              SkillContext &ctx) const override
    {
        if (!ctx.original_data || !player->askForSkillInvoke(this, *ctx.original_data, false))
            return false;
        room->setEmotion(player, "weapon/fan");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card)
            return false;
        QString skill_name = use.card->getSkillName();

        if (use.card->objectName() != "slash")
			return false;

        if (Config.EnableHegemony) {
            const ViewAsSkill *conversion = Sanguosha->getViewAsSkill(skill_name);
            if (conversion && !conversion->inherits("FilterSkill")
                && use.card->subcardsLength() > 0)
                return false;
        }
		FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
		if (use.card->isVirtualCard()) fire_slash->addSubcards(use.card->getSubcards());
		else fire_slash->addSubcard(use.card);
		fire_slash->setSkillName("fan");
		foreach (ServerPlayer *p, use.to) {
			if (!player->canSlash(p, fire_slash, false)) {
				delete fire_slash;
				return false;
			}
		}
		use.changeCard(fire_slash);
		*ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

Fan::Fan(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("fan");
}

class GudingBladeSkill : public WeaponSkill
{
public:
    GudingBladeSkill() : WeaponSkill("guding_blade")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")
            && damage.to->isKongcheng() && damage.by_user && !damage.chain && !damage.transfer) {
            room->setEmotion(player, "weapon/guding_blade");

            LogMessage log;
            log.type = "#GudingBladeEffect";
            log.from = player;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());

            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

GudingBlade::GudingBlade(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("guding_blade");
}

class VineSkill : public ArmorSkillV2
{
public:
    VineSkill() : ArmorSkillV2("vine", "vine")
    {
        events << DamageInflicted << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player,
                            QVariant &data) const override
    {
        TriggerList result;
        if (!ArmorSkillV2::triggerable(player))
            return result;
        if (triggerEvent == CardEffected) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!player->hasArmorEffect(objectName(), effect.from)) return result;
            if ((effect.card->isKindOf("Slash") && effect.card->objectName() == "slash")
				|| effect.card->isKindOf("SavageAssault") || effect.card->isKindOf("ArcheryAttack")
				|| (!Config.EnableHegemony && effect.card->isKindOf("Chuqibuyi")))
                result.insert(player, QStringList(objectName()));
        } else if (triggerEvent == DamageInflicted) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature == DamageStruct::Fire && player->hasArmorEffect(objectName(), damage.from))
                result.insert(player, QStringList(objectName()));
        }
        return result;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        if (triggerEvent == CardEffected) {
            CardEffectStruct cardEffect = data.value<CardEffectStruct>();
            room->setEmotion(player, "armor/vine");
            LogMessage log;
            log.from = player;
            log.type = "#ArmorNullify";
            log.arg = objectName();
            log.arg2 = cardEffect.card->objectName();
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            cardEffect.to->setFlags("Global_NonSkillNullify");
            return true;
        }

        DamageStruct damage = data.value<DamageStruct>();
        room->setEmotion(player, "armor/vineburn");
        LogMessage log;
        log.type = "#VineDamage";
        log.from = player;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(++damage.damage);
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        data = QVariant::fromValue(damage);
        return false;
    }
};

Vine::Vine(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("vine");
}

class SilverLionSkill : public ArmorSkillV2
{
public:
    SilverLionSkill() : ArmorSkillV2("silver_lion", "silver_lion")
    {
        events << DamageInflicted << CardsMoveOneTime;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player,
                            QVariant &data) const override
    {
        TriggerList result;
        if (triggerEvent == DamageInflicted) {
            DamageStruct damage = data.value<DamageStruct>();
            if (player && player->isAlive() && ArmorSkillV2::triggerable(player)
                && player->hasArmorEffect(objectName(), damage.from) && damage.damage > 1)
                result.insert(player, QStringList(objectName()));
        } else if (Config.EnableHegemony && player
                   && player->hasFlag("SilverLionRecover")) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || !move.from_places.contains(Player::PlaceEquip))
                return result;
            for (int i = 0; i < move.card_ids.size(); ++i) {
                if (move.from_places[i] != Player::PlaceEquip) continue;
                const Card *card = Sanguosha->getEngineCard(move.card_ids[i]);
                if (card->objectName() == objectName()) {
                    if (!player->isWounded())
                        player->setFlags("-SilverLionRecover");
                    else
                        result.insert(player, QStringList(objectName()));
                    break;
                }
            }
        }
        return result;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player,
                SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        if (triggerEvent == DamageInflicted) {
            DamageStruct damage = data.value<DamageStruct>();
            room->setEmotion(player, "armor/silver_lion");
            LogMessage log;
            log.type = "#SilverLion";
            log.from = player;
            log.arg = QString::number(damage.damage);
            log.arg2 = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            damage.damage = 1;
            data = QVariant::fromValue(damage);
            return false;
        }

        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        for (int i = 0; i < move.card_ids.size(); ++i) {
            if (move.from_places[i] != Player::PlaceEquip) continue;
            const Card *card = Sanguosha->getEngineCard(move.card_ids[i]);
            if (card->objectName() != objectName()) continue;
            player->setFlags("-SilverLionRecover");
            room->setEmotion(player, "armor/silver_lion");
            RecoverStruct recover;
            recover.card = card;
            room->recover(player, recover);
            break;
        }
        return false;
    }
};

SilverLion::SilverLion(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("silver_lion");
}

void SilverLion::onUninstall(ServerPlayer *player) const
{
    if (Config.EnableHegemony) {
        if (player->isAlive() && player->hasArmorEffect(objectName()))
            player->setFlags("SilverLionRecover");
        EquipCard::onUninstall(player);
        return;
    }

    EquipCard::onUninstall(player);
	if (player->isAlive() && player->hasArmorEffect(objectName(), nullptr, false)
        && player->isWounded()) {
		Room *room = player->getRoom();
		room->setEmotion(player, "armor/silver_lion");
		room->notifySkillInvoked(player, objectName());
		room->recover(player, RecoverStruct(nullptr, this));
	}
}

FireAttack::FireAttack(Card::Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("fire_attack");
    damage_card = true;
}

bool FireAttack::isAvailable(const Player *player) const
{
    QList<const Player *> targets = player->getAliveSiblings();
	targets << player;
	foreach (const Player *p, targets) {
		if(targetFilter(QList<const Player *>(), p, player))
			return SingleTargetTrick::isAvailable(player);
	}
	return false;
}

bool FireAttack::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return !to_select->isKongcheng() && (to_select != Self || !Self->isLastHandCard(this, true))
	&& targets.length() <= Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)
	&& !Self->isProhibited(to_select, this, targets);
}

void FireAttack::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.to->isKongcheng())
        return;

    const Card *card = room->askForCardShow(effect.to, effect.from, objectName());
    room->showCard(effect.to, card->getEffectiveId());

    QString suit_str = card->getSuitString();
    QString suit_str_png = suit_str;
    if (card->hasSuit()) suit_str_png = "<img src='image/system/cardsuit/" + suit_str + ".png' height=17/>";
    QString prompt = QString("@fire-attack:%1::%2").arg(effect.to->objectName()).arg(suit_str_png);
    if (effect.from->isAlive()) {
        if (room->askForCard(effect.from, QString(".%1").arg(suit_str.at(0).toUpper()), prompt, QVariant::fromValue(effect)))
            room->damage(DamageStruct(this, effect.from, effect.to, 1, DamageStruct::Fire));
        else
            effect.from->setFlags("FireAttackFailed_" + effect.to->objectName()); // For AI
    }
}

IronChain::IronChain(Card::Suit suit, int number)
    : TrickCard(suit, number)
{
    setObjectName("iron_chain");
    can_recast = true;
}

QString IronChain::getSubtype() const
{
    return "damage_spread";
}

bool IronChain::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings(true)) {
		if(targetFilter(QList<const Player *>(), p, player))
			return TrickCard::isAvailable(player);
	}
	return !player->isCardLimited(this, Card::MethodRecast);
}

bool IronChain::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if(Self->isCardLimited(this, Card::MethodUse) || Self->isProhibited(to_select, this, targets)) return false;
	return targets.length() < 2+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
}

bool IronChain::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    bool rec = (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
			&& can_recast && getSkillName() != "sangu"; //暂时先这样解决吧
    foreach (int id, Self->getHandPile()) {
        if (subcards.contains(id)) {
            rec = false;
            break;
        }
    }
	return rec||targets.length()>0;/*
    if (rec){
		if(Self->isCardLimited(this, Card::MethodUse)) return targets.isEmpty();
		return targets.length() <= 2+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
	}
	return targets.length() > 0 && targets.length() <= 2+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);*/
}

void IronChain::onUse(Room *room, CardUseStruct &card_use) const
{
    if (card_use.to.isEmpty()) {
        card_use.from->broadcastSkillInvoke("@recast");

        LogMessage log;
        log.type = "#UseCard_Recast";
        log.from = card_use.from;
        log.card_str = toString();
        room->sendLog(log);

        room->moveCardTo(this, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, card_use.from->objectName(),getSkillName(),""), true);
        card_use.from->drawCards(1, "recast");
    } else
        TrickCard::onUse(room, card_use);
}

void IronChain::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->setPlayerChained(effect.to);
}

SupplyShortage::SupplyShortage(Card::Suit suit, int number)
    : DelayedTrick(suit, number)
{
    setObjectName("supply_shortage");

    judge.pattern = ".|club";
    judge.good = true;
    judge.reason = objectName();
}

bool SupplyShortage::isAvailable(const Player *player) const
{
	foreach (const Player *p, player->getAliveSiblings()) {
		if(targetFilter(QList<const Player *>(), p, player))
			return DelayedTrick::isAvailable(player);
	}
	return false;
}

bool SupplyShortage::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self || !to_select->hasJudgeArea() || to_select->containsTrick(objectName()))
        return false;

    int rangefix = 0;
    if (Self->getOffensiveHorse() && subcards.contains(Self->getOffensiveHorse()->getId()))
        rangefix += 1;

    if (Self->distanceTo(to_select, rangefix) > 1 + Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, Self, this, to_select))
        return false;

    return !Self->isProhibited(to_select, this, targets);
}

void SupplyShortage::takeEffect(ServerPlayer *target) const
{
    target->skip(Player::Draw);
}

ManeuveringPackage::ManeuveringPackage()
    : Package("maneuvering", Package::CardPack)
{
    QList<Card *> cards;
    // spade
    cards << new GudingBlade(Card::Spade, 1)
        << new Vine(Card::Spade, 2)
        << new Analeptic(Card::Spade, 3)
        << new ThunderSlash(Card::Spade, 4)
        << new ThunderSlash(Card::Spade, 5)
        << new ThunderSlash(Card::Spade, 6)
        << new ThunderSlash(Card::Spade, 7)
        << new ThunderSlash(Card::Spade, 8)
        << new Analeptic(Card::Spade, 9)
        << new SupplyShortage(Card::Spade, 10)
        << new IronChain(Card::Spade, 11)
        << new IronChain(Card::Spade, 12)
        << new Nullification(Card::Spade, 13);
    // club
    cards << new SilverLion(Card::Club, 1)
        << new Vine(Card::Club, 2)
        << new Analeptic(Card::Club, 3)
        << new SupplyShortage(Card::Club, 4)
        << new ThunderSlash(Card::Club, 5)
        << new ThunderSlash(Card::Club, 6)
        << new ThunderSlash(Card::Club, 7)
        << new ThunderSlash(Card::Club, 8)
        << new Analeptic(Card::Club, 9)
        << new IronChain(Card::Club, 10)
        << new IronChain(Card::Club, 11)
        << new IronChain(Card::Club, 12)
        << new IronChain(Card::Club, 13);
    // heart
    cards << new Nullification(Card::Heart, 1)
        << new FireAttack(Card::Heart, 2)
        << new FireAttack(Card::Heart, 3)
        << new FireSlash(Card::Heart, 4)
        << new Peach(Card::Heart, 5)
        << new Peach(Card::Heart, 6)
        << new FireSlash(Card::Heart, 7)
        << new Jink(Card::Heart, 8)
        << new Jink(Card::Heart, 9)
        << new FireSlash(Card::Heart, 10)
        << new Jink(Card::Heart, 11)
        << new Jink(Card::Heart, 12)
        << new Nullification(Card::Heart, 13);
    // diamond
    cards << new Fan(Card::Diamond, 1)
        << new Peach(Card::Diamond, 2)
        << new Peach(Card::Diamond, 3)
        << new FireSlash(Card::Diamond, 4)
        << new FireSlash(Card::Diamond, 5)
        << new Jink(Card::Diamond, 6)
        << new Jink(Card::Diamond, 7)
        << new Jink(Card::Diamond, 8)
        << new Analeptic(Card::Diamond, 9)
        << new Jink(Card::Diamond, 10)
        << new Jink(Card::Diamond, 11)
        << new FireAttack(Card::Diamond, 12);
    DefensiveHorse *hualiu = new DefensiveHorse(Card::Diamond, 13);
    hualiu->setObjectName("hualiu");
    cards << hualiu;
    foreach(Card *card, cards)
        card->setParent(this);

    skills << new GudingBladeSkill << new FanSkill
        << new VineSkill << new SilverLionSkill;
}

ADD_PACKAGE(Maneuvering)
