/********************************************************************
    Copyright (c) 2013-2014 - QSanguosha-Rara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    QSanguosha-Rara
    *********************************************************************/

#include "h-standard-equips.h"
#include "h-standard-package.h"
#include "skill.h"
#include "standard.h"
#include "maneuvering.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "clientplayer.h"

HSixSwords::HSixSwords(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("SixSwords");
}

class HSixSwordsSkill : public AttackRangeSkillV2{
public:
    HSixSwordsSkill() : AttackRangeSkillV2("heg_SixSwords"){
        // Equipment aura: apply once even when multiple allies carry SixSwords.
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override{
        const Player *target = context.primary;
        if (!target)
            return CorrectSkillResult::noEffect();
        foreach (const Player *p, target->getAliveSiblings()){
            if (p->hasWeapon("SixSwords", target) && p->isFriendWith(target))
                return CorrectSkillResult::useAmount(context.currentAmount);
        }

        return CorrectSkillResult::noEffect();
    }
};

HTriblade::HTriblade(Card::Suit suit, int number) : Weapon(suit, number, 3){
    setObjectName("Triblade");
}

class HTribladeSkillVS : public ViewAsSkillV2{
public:
    HTribladeSkillVS() : ViewAsSkillV2("heg_Triblade", 1){
    }

    bool canActivate(const ActiveSkillRequest &request) const override{
        return request.initiator && request.initiator->isAlive()
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@heg_Triblade";
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override{
        // Preserve hand-only discard selection; the V2 proxy pays it exactly once.
        return request.initiator && ViewAsSkillV2::canSelectCard(request, candidate)
            && !candidate->hasFlag("using") && !request.initiator->isJilei(candidate)
            && Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, candidate);
    }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *candidate) const override{
        return selected.isEmpty() && candidate && candidate->isAlive()
            && candidate->hasFlag("TribladeCanBeSelected");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override{
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override{
        return "HTribladeSkillCard";
    }

    EffectFlow effectOnTarget(SkillContext &context, ServerPlayer *target) const override{
        const int amount = getEffectiveAmount(context);
        if (amount > 0)
            target->getRoom()->damage(DamageStruct(objectName(), context.invoker, target, amount));
        return ContinueEffects;
    }
};

class HTribladeSkill : public WeaponSkillV2{
public:
    HTribladeSkill() : WeaponSkillV2("heg_Triblade", "Triblade"){
        events << Damage;
        view_as_skill = new HTribladeSkillVS;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override{
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to && damage.to->isAlive() && damage.card && damage.card->isKindOf("Slash")
            && damage.by_user && !damage.chain && !damage.transfer){
            QList<ServerPlayer *> players;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)){
                if (damage.to->distanceTo(p) == 1 && player->hasWeapon("Triblade", p)){
                    players << p;
                    room->setPlayerFlag(p, "TribladeCanBeSelected");
                }
            }
            if (players.isEmpty())
                return false;
            room->askForUseCard(player, "@@heg_Triblade", "@heg_Triblade");
        }

        foreach (ServerPlayer *p, room->getAllPlayers())
            if (p->hasFlag("TribladeCanBeSelected"))
                room->setPlayerFlag(p, "-TribladeCanBeSelected");

        return false;
    }
};

QList<Card *> HStandardCardPackage::equipCards(){

    QList<Card *> cards;

    cards
        << new Crossbow(Card::Diamond, 1)
        << new DoubleSword
        << new QinggangSword
        << new IceSword(Card::Spade, 2)
        << new Spear
        << new Fan(Card::Diamond, 1)
        << new Axe
        << new KylinBow
        << new HSixSwords
        << new HTriblade

        << new EightDiagram(Card::Spade, 2)
        << new RenwangShield(Card::Club, 2)
        << new Vine(Card::Club, 2)
        << new SilverLion(Card::Club, 1);


    QList<Card *> horses;

    horses
        << new DefensiveHorse(Card::Spade, 5)
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

    return cards;
}

void HStandardCardPackage::addEquipSkills(){
    // Shared equipment skills are registered once by their native packages.
    skills << new HTribladeSkill << new HSixSwordsSkill;
}
