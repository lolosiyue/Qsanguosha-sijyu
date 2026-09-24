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

#include "h-strategic-advantage.h"
#include "standard.h"
#include "maneuvering.h"
#include "h-standard-tricks.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "aux-skills.h"
#include "roomthread.h"
#include <QScopeGuard>
#include <algorithm>

HBlade::HBlade(Card::Suit suit, int number)
    : Blade(suit, number)
{
    setObjectName("Blade");
}

class HBladeSkill : public WeaponSkillV2 {
public:
    HBladeSkill() : WeaponSkillV2("heg_Blade", "Blade") {
        events << CardUsed << CardFinished;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        TriggerList list;
        CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == CardUsed && player && WeaponSkillV2::triggerable(player)
            && use.card && use.card->isKindOf("Slash"))
            list.insert(player, QStringList(objectName()));
        return list;
    }

    void record(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (triggerEvent != CardFinished || !ctx.original_data)
            return;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash"))
            return;
        foreach (ServerPlayer *p, use.to) {
            QStringList blade_use = p->property("blade_use").toStringList();
            if (!blade_use.contains(use.card->toString()))
                continue;
            blade_use.removeOne(use.card->toString());
            room->setPlayerProperty(p, "blade_use", blade_use);
            if (blade_use.isEmpty())
                room->removePlayerDisableShow(p, "heg_Blade");
        }
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        bool play_animation = false;
        foreach (ServerPlayer *p, use.to) {
            if (!player->hasWeapon("Blade", p))
                continue;
            QStringList blade_use = p->property("blade_use").toStringList();
            if (blade_use.contains(use.card->toString()))
                return false;

            blade_use << use.card->toString();
            room->setPlayerProperty(p, "blade_use", blade_use);

            if (!p->hasShownAllGenerals())
                play_animation = true;

            room->setPlayerDisableShow(p, "hd", "heg_Blade"); // this effect should always make sense.
        }

        if (play_animation)
            room->setEmotion(player, "weapon/blade");

        return false;
    }
};

HHalberd::HHalberd(Card::Suit suit, int number)
    : Halberd(suit, number)
{
    setObjectName("Halberd");
}

HHalberdCard::HHalberdCard() {
    target_fixed = true;
    m_skillName = "heg_Halberd";
}

namespace {
bool askForHalberdSlash(ServerPlayer *player)
{
    Room *room = player->getRoom();
    const int weaponId = player->getWeapon() ? player->getWeapon()->getEffectiveId() : -1;
    room->setPlayerFlag(player, "HalberdUse");
    room->setPlayerFlag(player, "HalberdSlashFilter");
    if (weaponId >= 0)
        room->setCardFlag(weaponId, "using");
    // Selection state must also clear on cancellation and interrupted resolutions.
    auto clearSelection = qScopeGuard([&]() {
        if (weaponId >= 0) room->setCardFlag(weaponId, "-using");
        room->setPlayerFlag(player, "-HalberdUse");
        room->setPlayerFlag(player, "-HalberdSlashFilter");
        room->setPlayerMark(player, "halberd_count", 0);
    });
    const bool used = room->askForUseCard(player, "slash", "@heg_Halberd") != nullptr;
    if (!used) room->setPlayerFlag(player, "Global_HalberdFailed");
    return used;
}
}

const Card *HHalberdCard::validate(CardUseStruct &card_use) const
{
    return askForHalberdSlash(card_use.from) ? this : nullptr;
}

const Card *HHalberdCard::validateInResponse(ServerPlayer *player) const
{
    return askForHalberdSlash(player) ? this : nullptr;
}
void HHalberdCard::onUse(Room *, CardUseStruct &) const{
    // do nothing
}

class HHalberdSkill : public ViewAsSkillV2 {
public:
    HHalberdSkill() : ViewAsSkillV2("heg_Halberd") {}
    bool isEquipSkill() const override { return true; }
    bool canActivate(const ActiveSkillRequest &request) const override {
        const Player *player = request.initiator;
        if (!player || !player->hasWeapon("Halberd") || player->hasFlag("Global_HalberdFailed")
            || player->hasFlag("HalberdUse") || player->hasFlag("slashDisableExtraTarget")) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Slash::IsAvailable(player)
            : request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "slash";
    }
    const Card *createCard(const ActiveSkillRequest &request) const override {
        return cardSelectionFeasible(request) ? new HHalberdCard : nullptr;
    }
};

// All Halberd selection and cancellation rules live with its equipment skills.
class HHalberdTrigger : public WeaponSkillV2 {
protected:
    bool usesEventSource(const SkillContext &ctx) const override {
        // A selected/marked Slash keeps its resolution after the weapon leaves.
        return ctx.current_event == CardOffset || ctx.current_event == CardEffected
            || ctx.current_event == CardFinished
            || (ctx.current_event == ChangeSlash && ctx.owner && ctx.owner->hasFlag("HalberdUse"));
    }

public:
    HHalberdTrigger() : WeaponSkillV2("heg_Halberd", "Halberd") {
        view_as_skill = new HHalberdSkill;
        events << ChangeSlash << CardOffset << CardEffected << CardFinished;
        global = true;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override {
        TriggerList list;
        if (!room || !player)
            return list;
        ServerPlayer *owner = nullptr;
        if (event == CardOffset || event == CardEffected) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card || !effect.card->hasFlag("halberd_slash")
                || (event == CardEffected && !effect.card->hasFlag("halberd_slash_missed")))
                return list;
            owner = effect.from;
        } else {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Slash")) return list;
            if (event == CardFinished && !use.card->hasFlag("halberd_slash")) return list;
            if (event == ChangeSlash && (use.from != player
                || (!player->hasFlag("HalberdUse")
                    && !(use.card->isVirtualCard() && use.card->subcardsLength() == 0
                        && WeaponSkillV2::triggerable(player)
                        && !player->hasFlag("slashDisableExtraTarget")))))
                return list;
            owner = use.from;
        }
        // CardFinished/CardOffset/CardEffected must still clear or resolve an
        // already marked Halberd slash after the weapon has been removed.
        if (owner)
            list.insert(owner, QStringList(objectName()));
        return list;
    }
    int getPriority(TriggerEvent) const override { return 2; }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        QVariant &data = *ctx.original_data;
        if (event == CardOffset || event == CardEffected) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card || !effect.card->hasFlag("halberd_slash")) return false;
            if (event == CardOffset) {
                room->setCardFlag(effect.card, "halberd_slash_missed");
                return false;
            }
            if (!effect.card->hasFlag("halberd_slash_missed")) return false;
            LogMessage log;
            log.type = "#HalberdNullified";
            log.from = effect.from;
            log.to << (ctx.invoker ? ctx.invoker : effect.to);
            log.arg = "heg_Halberd";
            log.arg2 = effect.card->objectName();
            room->sendLog(log);
            return true;
        }
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash")) return false;
        if (event == CardFinished) {
            if (!use.card->hasFlag("halberd_slash")) return false;
            room->setCardFlag(use.card, "-halberd_slash");
            room->setCardFlag(use.card, "-halberd_slash_missed");
            return false;
        }
        if (!use.from || use.from != player) return false;
        const bool selectedWithHalberd = player->hasFlag("HalberdUse");
        if (!selectedWithHalberd && use.card->isVirtualCard() && use.card->subcardsLength() == 0
            && player->hasWeapon("Halberd")
            && !player->hasFlag("slashDisableExtraTarget")) {
            room->setPlayerFlag(player, "HalberdSlashFilter");
            auto clearFilter = qScopeGuard([&]() { room->setPlayerFlag(player, "-HalberdSlashFilter"); });
            for (;;) {
                QList<const Player *> selected;
                for (ServerPlayer *target : use.to) selected << target;
                QList<ServerPlayer *> candidates;
                for (ServerPlayer *target : room->getAlivePlayers()) {
                    if (!use.to.contains(target) && use.card->targetFilter(selected, target, player))
                        candidates << target;
                }
                if (candidates.isEmpty()) break;
                ServerPlayer *extra = room->askForPlayerChosen(player, candidates,
                    "heg_Halberd", "@halberd_extra_targets", true);
                if (!extra) break;
                use.to << extra;
                room->setPlayerFlag(player, "HalberdUse");
                room->sortByActionOrder(use.to);
            }
        }
        if (!player->hasFlag("HalberdUse")) return false;
        {
            room->setCardFlag(use.card, "halberd_slash");
            room->setEmotion(player, "weapon/halberd");
            room->notifySkillInvoked(player, "heg_Halberd");
            LogMessage log;
            log.type = "#HalberdUse";
            log.from = player;
            room->sendLog(log);
        }
        if (selectedWithHalberd && player->getWeapon())
            room->setCardFlag(player->getWeapon(), "-using");
        room->setPlayerFlag(player, "-HalberdUse");
        room->setPlayerFlag(player, "-HalberdSlashFilter");
        room->setPlayerMark(player, "halberd_count", 0);
        data.setValue(use);
        return false;
    }
};

class HHalberdTargetMod : public TargetModSkillV2 {
public:
    HHalberdTargetMod() : TargetModSkillV2("heg_halberd-target") {
        setHolderSelector(CorrectSkill_System);
        setBaseAmount(1000);
    }
    bool isEquipSkill() const override { return true; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override {
        if (context.modType == ExtraTarget && context.primary
            && context.primary->hasFlag("HalberdSlashFilter") && context.primary->hasWeapon("Halberd"))
            return CorrectSkillResult::useAmount(context.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class HHalberdProhibit : public ProhibitSkill {
public:
    HHalberdProhibit() : ProhibitSkill("heg_halberd-prohibit") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card,
                      const QList<const Player *> &others) const override {
        if (!from || !to || !from->hasFlag("HalberdSlashFilter") || !card || !card->isKindOf("Slash"))
            return false;
        if (!from->hasWeapon("Halberd", to)) return true;
        if (!to->hasShownOneGeneral() || to->getRole() == "careerist") return false;
        for (const Player *target : others) {
            if (target->hasShownOneGeneral() && target->getRole() != "careerist"
                && target->getSeemingKingdom() == to->getSeemingKingdom())
                return true;
        }
        return false;
    }
};
HBreastplate::HBreastplate(Card::Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("Breastplate");
    setTransferable(true);
}

class HBreastplateViewAsSkill : public ViewAsSkillV2 {
public:
    HBreastplateViewAsSkill() : ViewAsSkillV2("heg_Breastplate") {}
    bool isEquipSkill() const override { return true; }
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getArmor() && request.initiator->hasArmorEffect("Breastplate");
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *candidate) const override {
        TransferCard card;
        return card.targetFilter(targets, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        ServerPlayer *owner = ctx.owner;
        const Card *equip = owner ? owner->getArmor() : nullptr;
        if (!equip || equip->objectName() != "Breastplate" || owner->isEquipsNullified(equip)) return ContinueEffects;
        // The equipped card is chosen by the equipment source, not a forged subcard.
        TransferCard transfer;
        transfer.addSubcard(equip);
        CardEffectStruct effect;
        effect.card = &transfer;
        effect.from = owner;
        effect.to = target;
        transfer.onEffect(effect);
        return ContinueEffects;
    }
};

class HBreastplateSkill : public ArmorSkillV2 {
public:
    HBreastplateSkill() : ArmorSkillV2("heg_Breastplate", "Breastplate") {
        events << DamageInflicted;
        frequency = Compulsory;
        view_as_skill = new HBreastplateViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        TriggerList list;
        DamageStruct damage = data.value<DamageStruct>();
        if (player && ArmorSkillV2::triggerable(player) && player->hasArmorEffect("Breastplate", damage.from)
            && damage.damage >= player->getHp() && player->getArmor())
            list.insert(player, QStringList(objectName()));
        return list;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override {
        return player->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
        room->moveCardTo(player->getArmor(), NULL, Player::DiscardPile, reason, true);
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#Breastplate";
        log.from = player;
        if (damage.from)
            log.to << damage.from;
        log.arg = QString::number(damage.damage);
        if (damage.nature == DamageStruct::Normal)
            log.arg2 = "normal_nature";
        else if (damage.nature == DamageStruct::Fire)
            log.arg2 = "fire_nature";
        else if (damage.nature == DamageStruct::Thunder)
            log.arg2 = "thunder_nature";
        room->sendLog(log);
        return true;
    }
};

HIronArmor::HIronArmor(Card::Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("IronArmor");
}

class HIronArmorSkill : public ArmorSkillV2 {
public:
    HIronArmorSkill() : ArmorSkillV2("heg_IronArmor", "IronArmor") {
        events << TargetConfirming << ChainStateChange;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override {
        TriggerList list;
        if (!player || !ArmorSkillV2::triggerable(player)) return list;
        if (event == ChainStateChange) {
            if (!player->isChained() && !player->canBeChainedBy(data.value<ServerPlayer *>()))
                list.insert(player, QStringList(objectName()));
            return list;
        }
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card) return list;
        if (!use.to.contains(player) || !player->hasArmorEffect("IronArmor", use.from)) return list;
        if (use.card->isKindOf("FireAttack") || use.card->isKindOf("FireSlash") || use.card->isKindOf("BurningCamps"))
            list.insert(player, QStringList(objectName()));
        return list;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (event == ChainStateChange) return true;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        LogMessage log2;
        log2.type = "#IronArmor";
        log2.from = player;
        log2.arg = objectName();
        room->sendLog(log2);

        room->cancelTarget(use, player); // Room::cancelTarget(use, player);

        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class HJadeSealViewAsSkill : public ViewAsSkillV2 {
public:
    HJadeSealViewAsSkill() : ViewAsSkillV2("heg_JadeSeal") {}
    bool isEquipSkill() const override { return true; }
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.initiator->hasTreasure("JadeSeal")
            && request.initiator->hasShownOneGeneral()
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@heg_JadeSeal!";
    }
    const Card *createCard(const ActiveSkillRequest &request) const override {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        HKnownBoth *card = new HKnownBoth(Card::NoSuit, 0);
        card->setSkillName(objectName());
        return card;
    }
};

HJadeSeal::HJadeSeal(Card::Suit suit, int number) : Treasure(suit, number)
{
    setObjectName("JadeSeal");
}

class HJadeSealSkill: public TreasureSkillV2 {
public:
    HJadeSealSkill(): TreasureSkillV2("heg_JadeSeal", "JadeSeal") {
        frequency = Compulsory;
        events << DrawNCards << EventPhaseStart;
        view_as_skill = new HJadeSealViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        TriggerList list;
        if (!player || !TreasureSkillV2::triggerable(player) || !player->hasShownOneGeneral())
            return list;
        if (triggerEvent == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
            if (draw.reason == "draw_phase")
                list.insert(player, QStringList(objectName()));
        } else if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
            HKnownBoth *kb = new HKnownBoth(Card::NoSuit, 0);
            kb->setSkillName(objectName());
            kb->deleteLater();
            if (kb->isAvailable(player))
                list.insert(player, QStringList(objectName()));
        }
        return list;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (triggerEvent == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            ++draw.num;
            *ctx.original_data = QVariant::fromValue(draw);
            return false;
        }
        // The mandatory card use is an effect, after V2 admission and interception.
        if (!room->askForUseCard(player, "@@heg_JadeSeal!", "@heg_JadeSeal")) {
            HKnownBoth *kb = new HKnownBoth(Card::NoSuit, 0);
            kb->setSkillName(objectName());
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!player->isProhibited(p, kb) && (!p->isKongcheng() || !p->hasShownAllGenerals()))
                    targets << p;
            }
            if (targets.isEmpty()) {
                delete kb;
            } else {
                ServerPlayer *target = targets.at(qsanRandomBounded(targets.length()));
                room->useCard(CardUseStruct(kb, player, target), false);
            }
        }
        return false;
    }

};

HDrowning::HDrowning(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("drowning");
}

bool HDrowning::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const {
    int total_num = 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    if (targets.length() >= total_num)
        return false;

    return to_select->hasEquip() && to_select != Self;
}

void HDrowning::onEffect(CardEffectStruct &effect) const{
    Room *room = effect.to->getRoom();
    if (!effect.to->getEquips().isEmpty()
        && room->askForChoice(effect.to, objectName(), "throw+damage", QVariant::fromValue(effect)) == "throw")
        effect.to->throwAllEquips();
    else
        room->damage(DamageStruct(this, effect.from && effect.from->isAlive() ? effect.from : nullptr, effect.to, 1, DamageStruct::Thunder));
}

bool HDrowning::isAvailable(const Player *player) const{
    bool canUse = false;
    QList<const Player *> players = player->getAliveSiblings();
    foreach (const Player *p, players) {
        if (player->isProhibited(p, this))
            continue;
        if (!p->hasEquip())
            continue;
        canUse = true;
        break;
    }

    return canUse && TrickCard::isAvailable(player);
}

HBurningCamps::HBurningCamps(Card::Suit suit, int number, bool is_transferable)
    : AOE(suit, number)
{
    setObjectName("burning_camps");
    setTransferable(is_transferable);
}

bool HBurningCamps::isAvailable(const Player *player) const{
    bool canUse = false;
    QList<const Player *> players = player->getNextAlive()->getFormation();
    foreach (const Player *p, players) {
        if (player->isProhibited(p, this))
            continue;

        canUse = true;
        break;
    }

    return canUse && TrickCard::isAvailable(player);
}

void HBurningCamps::onUse(Room *room, CardUseStruct &card_use) const{
    CardUseStruct &new_use = card_use;
    QList<const Player *> targets = card_use.from->getNextAlive()->getFormation();
    foreach (const Player *player, targets) {
        const Skill *skill = room->isProhibited(card_use.from, player, this);
        // Formation members carry stable player IDs, not general names.
        ServerPlayer *splayer = room->findPlayerByObjectName(player->objectName());
        if (skill) {
            if (!skill->isVisible())
                skill = Sanguosha->getMainSkill(skill->objectName());
            if (skill->isVisible()) {
                LogMessage log;
                log.type = "#SkillAvoid";
                log.from = splayer;
                log.arg = skill->objectName();
                log.arg2 = objectName();
                room->sendLog(log);

                room->broadcastSkillInvoke(skill->objectName());
            }
        } else
            new_use.to << splayer;
    }

    TrickCard::onUse(room, new_use);
}

void HBurningCamps::onEffect(CardEffectStruct &effect) const {
    effect.to->getRoom()->damage(DamageStruct(this, effect.from, effect.to, 1, DamageStruct::Fire));
}

HLureTiger::HLureTiger(Card::Suit suit, int number, bool is_transferable)
    : TrickCard(suit, number)
{
    setObjectName("lure_tiger");
    setTransferable(is_transferable);
}

QString HLureTiger::getSubtype() const{
    return "lure_tiger";
}

bool HLureTiger::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    int total_num = 2 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    if (targets.length() >= total_num)
        return false;
    if (Self->isCardLimited(this, Card::MethodUse))
        return false;

    return to_select != Self;
}

void HLureTiger::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const {
    // Shared card use owns per-target nullification, extra effects and cleanup.
    TrickCard::use(room, source, targets);
    if (source && source->isAlive()) source->drawCards(1, objectName());
}

void HLureTiger::onEffect(CardEffectStruct &effect) const{
    Room *room = effect.to->getRoom();

    room->setPlayerCardLimitation(effect.to, "use", ".", false, "heg_lure_tiger");
    room->setPlayerProperty(effect.to, "removed", true);
    room->setPlayerFlag(effect.to, "LureTigerTarget");
    if (effect.from) effect.from->setFlags("LureTigerUser");
}

class HLureTigerProhibit : public ProhibitSkill {
public:
    HLureTigerProhibit() : ProhibitSkill("#heg_lure_tiger-prohibit") {
        setProperty("supportsSourceLessProhibition", true);
    }

    virtual bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const{
        return to && card && to->isRemoved() && card->getTypeId() != Card::TypeSkill;
    }
};

class HLureTigerDistance : public DistanceSkillV2 {
public:
    HLureTigerDistance() : DistanceSkillV2("#heg_lure_tiger-distance") {
        setHolderSelector(CorrectSkill_System);
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        const Player *from = ctx.primary, *to = ctx.secondary;
        if (!from || !to || from == to || from->isRemoved() || to->isRemoved())
            return CorrectSkillResult::noEffect();
        QList<const Player *> active;
        bool removed = false;
        for (const Player *p : from->getAliveSiblings(true)) {
            if (p->isRemoved()) removed = true;
            else active << p;
        }
        if (!removed) return CorrectSkillResult::noEffect();
        std::sort(active.begin(), active.end(), [](const Player *a, const Player *b) {
            return a->getSeat() < b->getSeat();
        });
        const int first = active.indexOf(from), second = active.indexOf(to);
        if (first < 0 || second < 0) return CorrectSkillResult::noEffect();
        const int steps = qAbs(first - second);
        const int distance = qMin(steps, int(active.size()) - steps);
        const int raw = qAbs(from->getSeat() - to->getSeat());
        // Replace only the seat contribution; preserve other distance modifiers.
        const int base = qMin(raw, from->aliveCount() - raw);
        return CorrectSkillResult::useAmount(distance - base);
    }
};

HFightTogether::HFightTogether(Card::Suit suit, int number)
    : GlobalEffect(suit, number)
{
    setObjectName("fight_together");
    can_recast = true;
}

bool HFightTogether::canRecastFor(const Player *player) const {
    bool rec = (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY);
    QList<int> sub;
    if (isVirtualCard())
        sub = subcards;
    else
        sub << getEffectiveId();
    foreach (int id, sub) {
        if (player->getPile("wooden_ox").contains(id)) {
            rec = false;
            break;
        }
    }

    return rec && !player->isCardLimited(this, Card::MethodRecast);
}


bool HFightTogether::isAvailable(const Player *player) const {
    if (player->hasFlag("Global_FightTogetherFailed")) return false;
    if (canRecastFor(player)) return true;
    QStringList big_kingdoms = player->getBigKingdoms(objectName());
    return (!big_kingdoms.isEmpty() || (player->hasLordSkill("heg_hongfa") && !player->getPile("heavenly_army").isEmpty())) // HongfaTianbing
        && GlobalEffect::isAvailable(player);
}

void HFightTogether::onUse(Room *room, CardUseStruct &card_use) const{
    ServerPlayer *source = card_use.from;
    QStringList big_kingdoms = source->getBigKingdoms(objectName(), MaxCardsType::Normal);
    if (big_kingdoms.isEmpty()) {
        if (canRecastFor(source)) {
            CardMoveReason reason(CardMoveReason::S_REASON_RECAST, card_use.from->objectName());
            reason.m_skillName = getSkillName();
            room->moveCardTo(this, card_use.from, NULL, Player::PlaceTable, reason, true);
            card_use.from->broadcastSkillInvoke("@recast");

            LogMessage log;
            log.type = "#Card_Recast";
            log.from = card_use.from;
            log.card_str = card_use.card->toString();
            room->sendLog(log);

            // Recast uses the already selected V2 source, never a same-name general.
            const bool revealed = !card_use.activationRef.isValid()
                || room->showGeneralForSkill(card_use.activationRef);

            QList<int> table_cardids = room->getCardIdsOnTable(this);
            if (!table_cardids.isEmpty()) {
                DummyCard dummy(table_cardids);
                room->moveCardTo(&dummy, card_use.from, NULL, Player::DiscardPile, reason, true);
            }

            if (revealed) card_use.from->drawCards(1);
            return;
        } else
            room->setPlayerFlag(source, "Global_FightTogetherFailed");
        return;
    }
    QList<ServerPlayer *> bigs, smalls;
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        const Skill *skill = room->isProhibited(source, p, this);
        if (skill) {
            if (!skill->isVisible())
                skill = Sanguosha->getMainSkill(skill->objectName());
            if (skill->isVisible()) {
                LogMessage log;
                log.type = "#SkillAvoid";
                log.from = p;
                log.arg = skill->objectName();
                log.arg2 = objectName();
                room->sendLog(log);

                room->broadcastSkillInvoke(skill->objectName());
            }
            continue;
        }
        const QString kingdom = p->getSeemingKingdom();
        if (p->hasShownOneGeneral() && big_kingdoms.contains(kingdom)) bigs << p;
        else smalls << p;
    }
    QStringList choices;
    if (!bigs.isEmpty())
        choices << "big";
    if (!smalls.isEmpty())
        choices << "small";
    if (canRecastFor(source))
        choices << "recast";

    if (choices.isEmpty()) return;

    QString choice = room->askForChoice(source, objectName(), choices.join("+"));
    if (choice == "recast") {
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, card_use.from->objectName());
        reason.m_skillName = getSkillName();
        room->moveCardTo(this, card_use.from, NULL, Player::PlaceTable, reason, true);
        card_use.from->broadcastSkillInvoke("@recast");

        LogMessage log;
        log.type = "#Card_Recast";
        log.from = card_use.from;
        log.card_str = card_use.card->toString();
        room->sendLog(log);

        // Recast uses the already selected V2 source, never a same-name general.
        const bool revealed = !card_use.activationRef.isValid()
            || room->showGeneralForSkill(card_use.activationRef);

        QList<int> table_cardids = room->getCardIdsOnTable(this);
        if (!table_cardids.isEmpty()) {
            DummyCard dummy(table_cardids);
            room->moveCardTo(&dummy, card_use.from, NULL, Player::DiscardPile, reason, true);
        }

        if (revealed) card_use.from->drawCards(1);
        return;
    }

    CardUseStruct &use = card_use;
    if (choice == "big")
        use.to = bigs;
    else if (choice == "small")
        use.to = smalls;
    if (use.to.isEmpty()) return;
    TrickCard::onUse(room, use);
}

void HFightTogether::onEffect(CardEffectStruct &effect) const {
    Room *room = effect.to->getRoom();
    if (!effect.to->isChained()) {
        room->setPlayerChained(effect.to, true, effect.from);
    } else {
        effect.to->drawCards(1, objectName());
    }
}

HAllianceFeast::HAllianceFeast(Card::Suit suit, int number)
    : AOE(suit, number)
{
    setObjectName("alliance_feast");
    target_fixed = false;
}

bool HAllianceFeast::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    if (!targets.isEmpty())
        return false;
    return to_select->hasShownOneGeneral() && !Self->isFriendWith(to_select);
}

void HAllianceFeast::onUse(Room *room, CardUseStruct &card_use) const{
    ServerPlayer *source = card_use.from;
    const QVariant previousFaction = getTag("AllianceFeastFaction");
    auto restoreFaction = qScopeGuard([&]() { setTag("AllianceFeastFaction", previousFaction); });
    if (card_use.to.size() == 1) setTag("AllianceFeastFaction", card_use.to.first()->objectName());
    QList<ServerPlayer *> targets;
    if (!source->isProhibited(source, this))
        targets << source;
    if (card_use.to.length() == 1) {
        ServerPlayer *target = card_use.to.first();
        QList<ServerPlayer *> other_players = room->getOtherPlayers(source);
        foreach (ServerPlayer *player, other_players) {
            if (!target->isFriendWith(player))
                continue;
            const Skill *skill = room->isProhibited(source, player, this);
            if (skill) {
                if (!skill->isVisible())
                    skill = Sanguosha->getMainSkill(skill->objectName());
                if (skill->isVisible()) {
                    LogMessage log;
                    log.type = "#SkillAvoid";
                    log.from = player;
                    log.arg = skill->objectName();
                    log.arg2 = objectName();
                    room->sendLog(log);

                    room->broadcastSkillInvoke(skill->objectName());
                }
            } else
                targets << player;
        }
    } else
        targets = card_use.to;

    CardUseStruct &use = card_use;
    use.to = targets;
    TrickCard::onUse(room, use);
}

void HAllianceFeast::onEffect(CardEffectStruct &effect) const{
    Room *room = effect.to->getRoom();
    if (effect.to == effect.from) {
        ServerPlayer *faction = room->findPlayerByObjectName(getTag("AllianceFeastFaction").toString(), true);
        int count = 0;
        for (ServerPlayer *p : room->getOtherPlayers(effect.from))
            if (faction && faction->isFriendWith(p)) ++count;
        if (count > 0) effect.to->drawCards(count, objectName());
    } else {
        QStringList choices;
        if (effect.to->isWounded())
            choices << "recover";
        choices << "draw";
        QString choice = room->askForChoice(effect.to, objectName(), choices.join("+"));
        if (choice == "recover") {
            RecoverStruct recover;
            recover.who = effect.from;
            room->recover(effect.to, recover);
        } else {
            effect.to->drawCards(1, objectName());
            room->setPlayerChained(effect.to, false);
        }
    }
}

bool HAllianceFeast::isAvailable(const Player *player) const{
    if (!player->hasShownOneGeneral() || player->isProhibited(player, this) || !TrickCard::isAvailable(player))
        return false;
    for (const Player *other : player->getAliveSiblings())
        if (targetFilter({}, other, player) && !player->isProhibited(other, this)) return true;
    return false;
}

HThreatenEmperor::HThreatenEmperor(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("threaten_emperor");
    target_fixed = true;
    setTransferable(true);
}

void HThreatenEmperor::onUse(Room *room, CardUseStruct &card_use) const{
    CardUseStruct &use = card_use;
    if (use.to.isEmpty())
        use.to << use.from;
    SingleTargetTrick::onUse(room, use);
}

bool HThreatenEmperor::isAvailable(const Player *player) const{
    if (!player->hasShownOneGeneral())
        return false;
    QStringList big_kingdoms = player->getBigKingdoms(objectName(), MaxCardsType::Max);
    // Jade Seal identifies a careerist faction by its owner's stable object ID.
    const QString kingdom = player->getRole() == QLatin1String("careerist")
        ? player->objectName() : player->getSeemingKingdom();
    const bool invoke = big_kingdoms.contains(kingdom);
    return invoke && !player->isProhibited(player, this) && TrickCard::isAvailable(player);
}

void HThreatenEmperor::onEffect(CardEffectStruct &effect) const{
    if (effect.to->getPhase() != Player::Play) return;
    effect.to->setFlags("Global_PlayPhaseTerminated");
    effect.to->setMark("ThreatenEmperorExtraTurn", 1);
}

void HStrategicAdvantagePackage::recordCardRules(TriggerEvent event, Room *room,
                                                ServerPlayer *player, QVariant &data)
{
    if (event == GameReady && !player && Sanguosha->getSkill("heg_transfer")) {
        bool transferableDeck = false;
        for (int id : room->getDrawPile())
            if (room->getCard(id)->isTransferable()) { transferableDeck = true; break; }
        if (transferableDeck)
            for (ServerPlayer *p : room->getAlivePlayers())
                if (p->getSkillInstanceIds("heg_transfer").isEmpty()) room->attachSkillToPlayer(p, "heg_transfer");
        return;
    }
    const bool turnEnd = event == EventPhaseChanging
        && data.value<PhaseChangeStruct>().to == Player::NotActive;
    const bool userDied = event == Death && player && player->hasFlag("LureTigerUser")
        && data.value<DeathStruct>().who == player;
    if (!turnEnd && !userDied) return;
    // These are pending card effects, with no player SkillInstance. GameRule
    // owns their lifetime, even if the original source has lost skills or died.
    for (ServerPlayer *p : room->getAllPlayers(true)) {
        if (p->hasFlag("LureTigerTarget")) {
            room->setPlayerProperty(p, "removed", false);
            room->setPlayerFlag(p, "-LureTigerTarget");
        }
        room->removePlayerCardLimitationByReason(p, "heg_lure_tiger");
        room->setPlayerFlag(p, "-LureTigerUser");
    }
    if (!turnEnd) return;
    for (ServerPlayer *p : room->getAllPlayers(true)) {
        if (p->getMark("ThreatenEmperorExtraTurn") <= 0) continue;
        room->setPlayerMark(p, "ThreatenEmperorExtraTurn", 0);
        if (!p->isAlive() || !room->askForCard(p, "..", "@threaten_emperor",
                data, Card::MethodDiscard, nullptr, false, "heg_threaten_emperor")) continue;
        if (!p->isAlive()) continue;
        LogMessage log;
        log.type = "#Fangquan";
        log.to << p;
        room->sendLog(log);
        // Queue after the current turn's cleanup and deferred Imperial Order.
        room->scheduleExtraTurn(p, "heg_threaten_emperor");
    }
}

HImperialOrder::HImperialOrder(Suit suit, int number)
    : GlobalEffect(suit, number)
{
    setObjectName("imperial_order");
}

bool HImperialOrder::isAvailable(const Player *player) const{
    bool invoke = !player->hasShownOneGeneral();
    if (!invoke) {
        foreach (const Player *p, player->getAliveSiblings()) {
            if (!p->hasShownOneGeneral() && !player->isProhibited(p, this)) {
                invoke = true;
                break;
            }
        }
    }
    return invoke && TrickCard::isAvailable(player);
}

void HImperialOrder::onUse(Room *room, CardUseStruct &card_use) const{
    // Preserve an explicitly supplied use (for example a deferred order).
    if (!card_use.to.isEmpty()) {
        TrickCard::onUse(room, card_use);
        return;
    }
    ServerPlayer *source = card_use.from;
    QList<ServerPlayer *> targets;
    foreach (ServerPlayer *p, room->getAllPlayers()) {
        if (p->hasShownOneGeneral())
            continue;
        const Skill *skill = room->isProhibited(source, p, this);
        if (skill) {
            if (!skill->isVisible())
                skill = Sanguosha->getMainSkill(skill->objectName());
            if (skill && skill->isVisible()) {
                LogMessage log;
                log.type = "#SkillAvoid";
                log.from = p;
                log.arg = skill->objectName();
                log.arg2 = objectName();
                room->sendLog(log);

                room->broadcastSkillInvoke(skill->objectName());
            }
            continue;
        }
        targets << p;
    }

    CardUseStruct &use = card_use;
    use.to = targets;
    if (use.to.isEmpty()) return;
    TrickCard::onUse(room, use);
}

void HImperialOrder::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const{
    room->setCardFlag(this, "imperial_order_normal_use");
    GlobalEffect::use(room, source, targets);
}

void HImperialOrder::onEffect(CardEffectStruct &effect) const{
    Room *room = effect.to->getRoom();
    if (room->askForCard(effect.to, "EquipCard", "@imperial_order-equip",
            QVariant::fromValue(effect), Card::MethodDiscard, nullptr, false, objectName()))
        return;
    QStringList positions;
    if (effect.to->canShowGeneral("h")) positions << "showhead";
    if (effect.to->canShowGeneral("d")) positions << "showdeputy";
    QStringList choices;
    if (!positions.isEmpty()) choices << "show";
    choices << "losehp";
    const QString choice = room->askForChoice(effect.to, objectName(), choices.join("+"));
    if (choice == "show") {
        const QString position = room->askForChoice(effect.to, "GameRule_AskForGeneralShow", positions.join("+"));
        const bool head = position == "showhead";
        if (positions.contains(position) && effect.to->canShowGeneral(head ? "h" : "d")) {
            effect.to->showGeneral(head);
            if (effect.to->isAlive() && (head ? effect.to->hasShownGeneral() : effect.to->hasShownGeneral2()))
                effect.to->drawCards(1, objectName());
        }
    } else {
        room->loseHp(effect.to);
    }
}

class HJingFanViewAsSkill : public ViewAsSkillV2 {
public:
    HJingFanViewAsSkill() : ViewAsSkillV2("jingfan") {}
    bool isEquipSkill() const override { return true; }
    bool canActivate(const ActiveSkillRequest &request) const override {
        const Player *p = request.initiator;
        const Card *horse = p ? p->getOffensiveHorse() : nullptr;
        return horse && horse->objectName() == "jingfan" && !p->isEquipsNullified(horse)
            && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *candidate) const override {
        TransferCard card;
        return card.targetFilter(targets, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        ServerPlayer *owner = ctx.owner;
        const Card *equip = owner ? owner->getOffensiveHorse() : nullptr;
        if (!equip || equip->objectName() != "jingfan" || owner->isEquipsNullified(equip)) return ContinueEffects;
        // The equipped card is chosen by the equipment source, not a forged subcard.
        TransferCard transfer;
        transfer.addSubcard(equip);
        CardEffectStruct effect;
        effect.card = &transfer;
        effect.from = owner;
        effect.to = target;
        transfer.onEffect(effect);
        return ContinueEffects;
    }
};

class HJingFanSkill : public EquipSkillV2 {
public:
    HJingFanSkill() : EquipSkillV2("jingfan", "jingfan") { view_as_skill = new HJingFanViewAsSkill; }
    bool triggerable(const ServerPlayer *player) const override {
        const Card *horse = player ? player->getOffensiveHorse() : nullptr;
        return horse && horse->objectName() == "jingfan" && !player->isEquipsNullified(horse);
    }
};

class HJingFanDistance : public DistanceSkillV2 {
public:
    HJingFanDistance() : DistanceSkillV2("#jingfan-distance") {
        setHolderSelector(CorrectSkill_System);
        setBaseAmount(-1);
    }
    bool isEquipSkill() const override { return true; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        if (ctx.primary) {
            for (const EquipCard *horse : ctx.primary->getOffensiveHorses())
                if (horse->objectName() == "jingfan" && !ctx.primary->isEquipsNullified(horse, ctx.secondary))
                    return CorrectSkillResult::useAmount(ctx.currentAmount);
        }
        return CorrectSkillResult::noEffect();
    }
};

HStrategicAdvantagePackage::HStrategicAdvantagePackage()
    : Package("heg_strategic_advantage", Package::CardPack){
    QList<Card *> cards;

    cards
        // basics
        // -- spade
        << new Slash(Card::Spade, 4)
        << new Analeptic(Card::Spade, 6);
    // Transferability belongs to this physical card, independent of its class.
    cards.last()->setTransferable(true);
    cards
        << new Slash(Card::Spade, 7)
        << new Slash(Card::Spade, 8)
        << new ThunderSlash(Card::Spade, 9)
        << new ThunderSlash(Card::Spade, 10)
        << new ThunderSlash(Card::Spade, 11);
    cards.last()->setTransferable(true);
    cards
        // -- heart
        << new Jink(Card::Heart, 4)
        << new Jink(Card::Heart, 5)
        << new Jink(Card::Heart, 6);
    cards.last()->setTransferable(true);
    cards
        << new Jink(Card::Heart, 7)
        << new Peach(Card::Heart, 8)
        << new Peach(Card::Heart, 9)
        << new Slash(Card::Heart, 10)
        << new Slash(Card::Heart, 11)
        // -- club
        << new Slash(Card::Club, 4)
        << new ThunderSlash(Card::Club, 5);
    cards.last()->setTransferable(true);
    cards
        << new Slash(Card::Club, 6)
        << new Slash(Card::Club, 7)
        << new Slash(Card::Club, 8)
        << new Analeptic(Card::Club, 9)
        // -- diamond
        << new Peach(Card::Diamond, 2)
        << new Peach(Card::Diamond, 3);
    cards.last()->setTransferable(true);
    cards
        << new Jink(Card::Diamond, 6)
        << new Jink(Card::Diamond, 7)
        << new FireSlash(Card::Diamond, 8)
        << new FireSlash(Card::Diamond, 9)
        << new Jink(Card::Diamond, 13)

        // tricks
        // -- spade
        << new HThreatenEmperor(Card::Spade, 1) // transfer
        << new HBurningCamps(Card::Spade, 3, true) // transfer
        << new HFightTogether(Card::Spade, 12)
        << new Nullification(Card::Spade, 13)
        // -- heart
        << new HAllianceFeast()
        << new HLureTiger(Card::Heart, 2)
        << new HBurningCamps(Card::Heart, 12, true) //transfer
        << new HDrowning(Card::Heart, 13)
        // -- club
        << new HImperialOrder(Card::Club, 3)
        << new HFightTogether(Card::Club, 10)
        << new HBurningCamps(Card::Club, 11, true) //transfer
        << new HDrowning(Card::Club, 12)
        << new HegNullification(Card::Club, 13)
        // -- diamond
        << new HThreatenEmperor(Card::Diamond, 1) // transfer
        << new HThreatenEmperor(Card::Diamond, 4) // transfer
        << new HLureTiger(Card::Diamond, 10, true) // transfer
        << new HegNullification(Card::Diamond, 11)

        // equips
        << new HIronArmor()
        << new HBlade(Card::Spade, 5);
    Horse *horse = new OffensiveHorse(Card::Heart, 3, -1);
    horse->setTransferable(true);
    horse->setObjectName("jingfan");
    cards
        << horse
        << new HJadeSeal(Card::Club, 1)
        << new HBreastplate() // transfer
        << new WoodenOx(Card::Diamond, 5)
        << new HHalberd(Card::Diamond, 12);

    skills << new TransferSkill << new HIronArmorSkill
           << new HBladeSkill
           << new HJadeSealSkill
           << new HBreastplateSkill
           << new HJingFanSkill << new HJingFanDistance
           << new HHalberdTrigger << new HHalberdTargetMod << new HHalberdProhibit
           << new HLureTigerProhibit << new HLureTigerDistance;

    foreach (Card *card, cards)
        card->setParent(this);

    addMetaObject<HHalberdCard>();
    addMetaObject<TransferCard>();
}

ADD_PACKAGE(HStrategicAdvantage)
