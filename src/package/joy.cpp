#include "joy.h"
#include "engine.h"
//#include "standard-generals.h"
#include "standard-generals.h"
#include "clientplayer.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"

Shit::Shit(Suit suit, int number)
    :BasicCard(suit, number)
{
    setObjectName("shit");

    target_fixed = true;
    damage_card = true;
    single_target = true;
}

QString Shit::getSubtype() const
{
    return "disgusting_card";
}

void Shit::onUse(Room *room, CardUseStruct &use) const
{
    if (use.to.isEmpty()) use.to << use.from;
    BasicCard::onUse(room, use);
}

bool Shit::isAvailable(const Player *player) const
{
    return player->hasFlag("CurrentPlayer")&&!player->isProhibited(player,this)&&BasicCard::isAvailable(player);
}

void Shit::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
	LogMessage log;
	log.from = effect.to;
	log.type = "#ShitDamage";
	log.card_str = toString();
	switch (getSuit()) {
	case Card::Spade:
		room->sendLog(log);
		room->damage(DamageStruct(this, effect.to, effect.to, 1, DamageStruct::Thunder));
		break;
	case Card::Club:
		room->sendLog(log);
		room->damage(DamageStruct(this, effect.to, effect.to));
		break;
	case Card::Heart:
		log.type = "#ShitLostHp";
		room->sendLog(log);
		room->loseHp(HpLostStruct(effect.to, 1, "shit", effect.to));
		break;
	case Card::Diamond:
		room->sendLog(log);
		room->damage(DamageStruct(this, effect.to, effect.to, 1, DamageStruct::Fire));
		break;
	default:
		break;
	}
}

class ShitEffect : public TriggerSkill
{
public:
    ShitEffect() : TriggerSkill("shit_effect")
	{
        events << CardsMoveOneTime;
        frequency = Compulsory;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if(move.from==player&&move.from_places.contains(Player::PlaceHand)
			&&(move.to_place==Player::PlaceTable||move.to_place==Player::DiscardPile)) {
            for (int i = 0; i < move.card_ids.length(); i++) {
                if(move.from_places.at(i)!=Player::PlaceHand) continue;
				const Card*shit = Sanguosha->getCard(move.card_ids.at(i));
                if (shit->isKindOf("Shit")&&player->isAlive()){
					room->useCard(CardUseStruct(shit,player));/*
					LogMessage log;
					log.from = player;
					log.type = "#ShitDamage";
					log.card_str = shit->toString();
					switch (shit->getSuit()) {
					case Card::Spade:
						log.type = "#ShitLostHp";
						room->sendLog(log);
						room->loseHp(HpLostStruct(player, 1, "shit", player));
						break;
					case Card::Heart:
						room->sendLog(log);
						room->damage(DamageStruct(shit, player, player, 1, DamageStruct::Fire));
						break;
					case Card::Club:
						room->sendLog(log);
						room->damage(DamageStruct(shit, player, player, 1, DamageStruct::Thunder));
						break;
					case Card::Diamond:
						room->sendLog(log);
						room->damage(DamageStruct(shit, player, player));
						break;
					default:
						break;
					}*/
				}
            }
        }
        return false;
    }

    bool triggerable(Player *target) const
	{
        return target&&target->hasFlag("CurrentPlayer");
    }

    int getPriority(TriggerEvent) const
    {
        return 1;
    }
};

// -----------  Deluge -----------------

Deluge::Deluge(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("deluge");

    judge.pattern = ".|.|1,13";
    judge.good = false;
    judge.reason = objectName();
}

void Deluge::takeEffect(ServerPlayer *target) const
{
	QList<const Card *> cards = target->getCards("he");

	int n = qMin(cards.length(), target->aliveCount());
	if (n < 1) return;

	qShuffle(cards);
	QList<int> card_ids;
	Room *room = target->getRoom();
	foreach (const Card *card, cards.mid(0, n))
		card_ids << card->getId();
	room->moveCardsAtomic(CardsMoveStruct(card_ids,nullptr,Player::PlaceTable,CardMoveReason()),true);

	QList<ServerPlayer *> players = room->getOtherPlayers(target);
	players << target;
	room->fillAG(card_ids);
	foreach (ServerPlayer *p, players) {
		if (p->isAlive()) {
			room->getThread()->delay();
			int id = room->askForAG(p, card_ids, false, "deluge");
			room->takeAG(p, id);
			card_ids.removeOne(id);
			if(card_ids.isEmpty()) break;
		}
	}
	room->clearAG();
	room->throwCard(card_ids,"deluge",nullptr);
}

// -----------  Typhoon -----------------

Typhoon::Typhoon(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("typhoon");

    judge.pattern = ".|diamond|2~9";
    judge.good = false;
    judge.reason = objectName();
}

void Typhoon::takeEffect(ServerPlayer *target) const
{
    Room *room = target->getRoom();
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        if (target->distanceTo(p) == 1) {
            int n = qMin(6, p->getHandcardNum());
            if (n > 0) room->askForDiscard(p, objectName(), n, n);
            room->getThread()->delay();
        }
    }
}

// -----------  Earthquake -----------------

Earthquake::Earthquake(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("earthquake");

    judge.pattern = ".|club|2~9";
    judge.good = false;
    judge.reason = objectName();
}

void Earthquake::takeEffect(ServerPlayer *target) const
{
    Room *room = target->getRoom();
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        if (2 - target->distanceTo(p, p->getOffensiveHorse() ? -1 : 0) <= 1) {// ignore plus 1 horse
            p->throwAllEquips(objectName());
            room->getThread()->delay();
        }
    }
}

// -----------  Volcano -----------------

Volcano::Volcano(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("volcano");
    damage_card = true;

    judge.pattern = ".|heart|2~9";
    judge.good = false;
    judge.reason = objectName();
}

void Volcano::takeEffect(ServerPlayer *target) const
{
    Room *room = target->getRoom();

    DamageStruct damage;
    damage.card = this;
    damage.damage = 2;
    damage.to = target;
    damage.nature = DamageStruct::Fire;
    room->damage(damage);

    foreach (ServerPlayer *player, room->getOtherPlayers(target)) {
        bool plus1Horse = (player->getOffensiveHorse() != nullptr);
        if (target->distanceTo(player, plus1Horse ? -1 : 0) == 1) {// ignore plus 1 horse
            DamageStruct damage;
            damage.card = this;
            damage.damage = 1;
            damage.to = player;
            damage.nature = DamageStruct::Fire;
            room->damage(damage);
        }
    }
}

// -----------  MudSlide -----------------
MudSlide::MudSlide(Card::Suit suit, int number)
    :Disaster(suit, number)
{
    setObjectName("mudslide");
    damage_card = true;

    judge.pattern = ".|black|1,13,4,7";
    judge.good = false;
    judge.reason = objectName();
}

void MudSlide::takeEffect(ServerPlayer *target) const
{
    int to_destroy = 4;
    Room *room = target->getRoom();
    foreach (ServerPlayer *player, room->getAllPlayers()) {
        QList<const Card *> equips = player->getEquips();
		room->getThread()->delay();
        if (equips.isEmpty()) {
            DamageStruct damage;
            damage.card = this;
            damage.to = player;
            room->damage(damage);
        } else {
			foreach (const Card *e, equips) {
                CardMoveReason reason(CardMoveReason::S_REASON_DISCARD, player->objectName(), "mudslide", "");
                room->throwCard(e, reason, player);
				to_destroy--;
				if (to_destroy<=0) return;
				room->getThread()->delay();
            }
        }
    }
}

class GrabPeach : public TriggerSkill
{
public:
    GrabPeach() :TriggerSkill("grab_peach")
    {
        events << CardUsed;
        global = true;
    }

    bool trigger(TriggerEvent, Room* room, ServerPlayer *player, QVariant &data) const
    {
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("Peach")) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getOffensiveHorse() && p->getOffensiveHorse()->isKindOf("Monkey")
					&& p->getMark("Equips_Nullified") < 1 && p->askForSkillInvoke("grab_peach", data)) {
					use.to.clear();
					data = QVariant::fromValue(use);
					room->throwCard(p->getOffensiveHorse(), "grab_peach", p);
					p->obtainCard(use.card);
				}
			}
		}
        if (player->getPhase()==Player::Play&&use.card->getTypeId()>0) {
            player->addMark("yongjue-PlayClear");
            if (use.card->isKindOf("Slash")&&player->getMark("yongjue-PlayClear")==1)
                room->setCardFlag(use.card,"yongjueBf"+player->objectName());
        }
        return false;
    }
};

Monkey::Monkey(Card::Suit suit, int number)
    :OffensiveHorse(suit, number)
{
    setObjectName("monkey");
}

class GaleShellSkill : public ArmorSkill
{
public:
    GaleShellSkill() :ArmorSkill("gale_shell")
    {
        events << DamageInflicted;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature == DamageStruct::Fire) {
            LogMessage log;
            log.type = "#GaleShellDamage";
            log.from = player;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

GaleShell::GaleShell(Suit suit, int number) :Armor(suit, number)
{
    setObjectName("gale_shell");

    target_fixed = false;
}

bool GaleShell::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->distanceTo(to_select) <= 1 && to_select->hasEquipArea(1);
}

/*
1.rende
2.jizhi
3.jieyin
4.guose
5.kurou
*/

class FiveLinesVS : public ViewAsSkill
{
public:
    FiveLinesVS() : ViewAsSkill("five_lines")
    {
    }

    bool isResponseOrUse() const
    {
        return Self->getHp() == 4;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped()&&to_select->objectName()==objectName())
			return false;
        int hp = Self->getHp();
		if(hp<=1){
            const ViewAsSkill *rende = Sanguosha->getViewAsSkill("nosrende");
            return rende && rende->viewFilter(selected, to_select);
		}else if(hp==2){ // Trigger Skill
		}else if(hp==3){
            const ViewAsSkill *jieyin = Sanguosha->getViewAsSkill("jieyin");
            return jieyin && jieyin->viewFilter(selected, to_select);
		}else if(hp==4){
            return selected.isEmpty() && to_select->getSuit() == Card::Diamond;
		}
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int hp = Self->getHp();
        if (hp <= 1){
            if (cards.length() > 0) {
                NosRendeCard *rd = new NosRendeCard;
                rd->addSubcards(cards);
                return rd;
            }
		}else if(hp==2){
		}else if(hp==3){
            if (cards.length() == 2) {
                JieyinCard *jy = new JieyinCard;
                jy->addSubcards(cards);
                return jy;
            }
		}else if(hp==4){
            if (cards.length() == 1) {
                Indulgence *indulgence = new Indulgence(cards.first()->getSuit(), cards.first()->getNumber());
                indulgence->setSkillName("nosguose");
                indulgence->addSubcards(cards);
                return indulgence;
            }
		}else
			return new NosKurouCard;
        return nullptr;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        int hp = player->getHp();
        if (hp <= 1){
            const ViewAsSkill *rende = Sanguosha->getViewAsSkill("nosrende");
            return rende && rende->isEnabledAtPlay(player);
		}else if(hp==2){
		}else if(hp==3){
            const ViewAsSkill *jieyin = Sanguosha->getViewAsSkill("jieyin");
            return jieyin && jieyin->isEnabledAtPlay(player);
		}else if(hp==4){
            return true;
		}else{
            const ViewAsSkill *kurou = Sanguosha->getViewAsSkill("noskurou");
            return kurou && kurou->isEnabledAtPlay(player);
		}
        return false;
    }
};

class FiveLinesSkill : public ArmorSkill
{
public:
    FiveLinesSkill() : ArmorSkill("five_lines")
    {
        events << CardUsed;
        view_as_skill = new FiveLinesVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return ArmorSkill::triggerable(target) && target->getHp() == 2;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const TriggerSkill *jz = Sanguosha->getTriggerSkill("jizhi");
        if (jz) return jz->trigger(triggerEvent, room, player, data);

        return false;
    }
};

FiveLines::FiveLines(Card::Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("five_lines");
}

void FiveLines::onInstall(ServerPlayer *player) const
{
    const TriggerSkill *rd = Sanguosha->getTriggerSkill("nosrende");
    if (rd) player->getRoom()->getThread()->addTriggerSkill(rd);

    Armor::onInstall(player);
}

DisasterPackage::DisasterPackage()
    :Package("Disaster")
{
    QList<Card *> cards;

    cards << new Deluge(Card::Spade, 1)
        << new Typhoon(Card::Spade, 4)
        << new Earthquake(Card::Club, 10)
        << new Volcano(Card::Heart, 13)
        << new MudSlide(Card::Heart, 7);

    foreach(Card *card, cards)
        card->setParent(this);

    type = CardPack;
}

JoyPackage::JoyPackage()
    :Package("joy")
{
    QList<Card *> cards;

    cards << new Shit(Card::Club, 1)
    << new Shit(Card::Heart, 8)
    << new Shit(Card::Diamond, 13)
    << new Shit(Card::Spade, 10);

    foreach(Card *card, cards)
    card->setParent(this);

    type = CardPack;
    skills << new ShitEffect;
}

class YxSwordSkill : public WeaponSkill
{
public:
    YxSwordSkill() :WeaponSkill("yx_sword")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")) {
            QList<ServerPlayer *> players = room->getOtherPlayers(player);
            QMutableListIterator<ServerPlayer *> itor(players);

            while (itor.hasNext()) {
                itor.next();
                if (!player->inMyAttackRange(itor.value()))
                    itor.remove();
            }

            if (players.isEmpty())
                return false;

            QVariant _data = QVariant::fromValue(damage);
            room->setTag("YxSwordData", _data);
            ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@yxsword-select", true, true);
            room->removeTag("YxSwordData");
            if (target != nullptr) {
                damage.from = target;
                data = QVariant::fromValue(damage);
                room->moveCardTo(player->getWeapon(), player, target, Player::PlaceHand,
                    CardMoveReason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), objectName(), ""));
            }
        }
        return damage.to->isDead();
    }
};

YxSword::YxSword(Suit suit, int number)
    :Weapon(suit, number, 3)
{
    setObjectName("yx_sword");
}

JoyEquipPackage::JoyEquipPackage()
    : Package("JoyEquip")
{
    (new Monkey(Card::Diamond, 5))->setParent(this);
    (new GaleShell(Card::Heart, 1))->setParent(this);
    (new YxSword(Card::Club, 9))->setParent(this);
    (new FiveLines(Card::Heart, 5))->setParent(this);

    type = CardPack;
    skills << new GaleShellSkill << new YxSwordSkill << new GrabPeach << new FiveLinesSkill;
}

ADD_PACKAGE(Joy)
ADD_PACKAGE(Disaster)
ADD_PACKAGE(JoyEquip)
