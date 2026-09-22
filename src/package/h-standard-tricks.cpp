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

#include "h-standard-tricks.h"
#include "h-standard-package.h"
#include "room.h"
#include "util.h"
#include "engine.h"
#include "original-hegemony-compat.h"
#include "serverplayer.h"
#include "clientplayer.h"
#include "skill.h"
#include "json.h"
#include "roomthread.h"
#include "settings.h"

HegNullification::HegNullification(Suit suit, int number)
    : Nullification(suit, number)
{
    setObjectName("heg_nullification");
}

void HegNullification::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    const CardUseStruct use = room->getTag("UseHistory" + toString()).value<CardUseStruct>();
    if (use.nullified_list.contains("_ALL_TARGETS")) return;

    // Capture the original effect before the counter-nullification chain can replace the tag.
    const CardEffectStruct effect = source->getTag("NullifyingEffect").value<CardEffectStruct>();
    const bool factionChoice = Config.EnableHegemony && effect.to && effect.card
        && effect.card->isNDTrick() && !effect.card->isKindOf("Nullification");
    QString selection;
    if (factionChoice) {
        selection = room->askForChoice(source, objectName(), "single+all", QVariant::fromValue(effect));
        LogMessage log;
        log.type = "#HegNullificationSelection";
        log.from = source;
        log.arg = "hegnul_" + selection;
        room->sendLog(log);
    }

    Nullification::use(room, source, targets);
    const CardUseStruct resolved = room->getTag("UseHistory" + toString()).value<CardUseStruct>();
    if (!factionChoice || selection != QLatin1String("all")
        || !resolved.no_offset_list.contains("_HAS_EFFECT")) return;

    // Only a successful card effect grants pending offsets; the room merely consumes them.
    const QString key = effect.card->toString() + QStringLiteral("PendingNullification");
    QVariantMap pending = room->getTag(key).toMap();
    QStringList covered = pending.value("targets").toStringList();
    for (ServerPlayer *player : room->getAlivePlayers()) {
        if (player->isFriendWith(effect.to) && !covered.contains(player->objectName()))
            covered << player->objectName();
    }
    pending.insert("targets", covered);
    pending.insert("card", QVariant::fromValue(use.card));
    pending.insert("log_type", QStringLiteral("#HegNullificationEffect"));
    room->setTag(key, pending);
}

HAwaitExhausted::HAwaitExhausted(Card::Suit suit, int number) : TrickCard(suit, number){
    setObjectName("await_exhausted");
    target_fixed = true;
}

QString HAwaitExhausted::getSubtype() const{
    return "await_exhausted";
}

bool HAwaitExhausted::isAvailable(const Player *player) const{
    bool canUse = false;
    if (!player->isProhibited(player, this))
        canUse = true;
    if (!canUse) {
        QList<const Player *> players = player->getAliveSiblings();
        foreach (const Player *p, players) {
            if (player->isProhibited(p, this))
                continue;
            if (player->isFriendWith(p)) {
                canUse = true;
                break;
            }
        }
    }

    return canUse && TrickCard::isAvailable(player);
}

void HAwaitExhausted::onUse(Room *room, CardUseStruct &card_use) const{
    CardUseStruct &new_use = card_use;
    if (!card_use.from->isProhibited(card_use.from, this))
        new_use.to << new_use.from;
    foreach (ServerPlayer *p, room->getOtherPlayers(new_use.from)) {
        if (p->isFriendWith(new_use.from)) {
            const ProhibitSkill *skill = room->isProhibited(card_use.from, p, this);
            if (skill) {
                LogMessage log;
                log.type = "#SkillAvoid";
                log.from = p;
                log.arg = skill->objectName();
                log.arg2 = objectName();
                room->sendLog(log);

                room->broadcastSkillInvoke(skill->objectName(), p);
            } else {
                new_use.to << p;
            }
        }
    }

    TrickCard::onUse(room, new_use);
}

void HAwaitExhausted::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const{
    QStringList nullified_list = room->getTag("CardUseNullifiedList").toStringList();
    bool all_nullified = nullified_list.contains("_ALL_TARGETS");
    foreach (ServerPlayer *target, targets) {
        CardEffectStruct effect;
        effect.card = this;
        effect.from = source;
        effect.to = target;
        effect.multiple = (targets.length() > 1);
        effect.nullified = (all_nullified || nullified_list.contains(target->objectName()));

        room->cardEffect(effect);
    }

    foreach (ServerPlayer *target, targets) {
        if (target->hasFlag("AwaitExhaustedEffected")) {
            room->setPlayerFlag(target, "-AwaitExhaustedEffected");
            room->askForDiscard(target, objectName(), 2, 2, false, true);
        }
    }

    QList<int> table_cardids = room->getCardIdsOnTable(this);
    if (!table_cardids.isEmpty()) {
        DummyCard dummy(table_cardids);
        CardMoveReason reason(CardMoveReason::S_REASON_USE, source->objectName(), QString(), this->getSkillName(), QString());
        if (targets.size() == 1) reason.m_targetId = targets.first()->objectName();
        room->moveCardTo(&dummy, source, NULL, Player::DiscardPile, reason, true);
    }
}

void HAwaitExhausted::onEffect(CardEffectStruct &effect) const {
    effect.to->drawCards(2);
    effect.to->getRoom()->setPlayerFlag(effect.to, "AwaitExhaustedEffected");
}

HKnownBoth::HKnownBoth(Card::Suit suit, int number)
    :SingleTargetTrick(suit, number)
{
    setObjectName("known_both");
    can_recast = true;
}

bool HKnownBoth::isAvailable(const Player *player) const{
    bool can_use = false;
    foreach (const Player *p, player->getSiblings()) {
        if (player->isProhibited(p, this))
            continue;
        if (p->isKongcheng() && p->hasShownAllGenerals())
            continue;
        can_use = true;
        break;
    }
    bool can_rec = true;
    QList<int> sub;
    if (isVirtualCard())
        sub = subcards;
    else
        sub << getEffectiveId();
    if (sub.isEmpty() || sub.contains(-1))
        can_rec = false;
    return (can_use && !player->isCardLimited(this, Card::MethodUse))
           || (can_rec && can_recast && !player->isCardLimited(this, Card::MethodRecast));
}

bool HKnownBoth::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const{
    if (Self->isCardLimited(this, Card::MethodUse))
        return false;

    int total_num = 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    if (targets.length() >= total_num || to_select == Self)
        return false;

    return !to_select->isKongcheng() || !to_select->hasShownAllGenerals();
}

bool HKnownBoth::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const{
    bool rec = (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY);
    QList<int> sub;
    if (isVirtualCard())
        sub = subcards;
    else
        sub << getEffectiveId();
    foreach (int id, sub) {
        if (Self->getPile("wooden_ox").contains(id)) {
            rec = false;
            break;
        }
    }

    if (rec && Self->isCardLimited(this, Card::MethodUse))
        return targets.length() == 0;
    int total_num = 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    if (targets.length() > total_num)
        return false;
    return rec || targets.length() > 0;
}

void HKnownBoth::onUse(Room *room, CardUseStruct &card_use) const{
    if (card_use.to.isEmpty()){
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, card_use.from->objectName());
        reason.m_skillName = getSkillName();
        room->moveCardTo(this, card_use.from, NULL, Player::PlaceTable, reason);
        card_use.from->broadcastSkillInvoke("@recast");

        LogMessage log;
        log.type = "#Card_Recast";
        log.from = card_use.from;
        log.card_str = card_use.card->toString();
        room->sendLog(log);

        QString skill_name = card_use.card->showSkill();
        if (!skill_name.isNull() && card_use.from->ownSkill(skill_name) && !card_use.from->hasShownSkill(skill_name))
            card_use.from->showGeneral(card_use.from->inHeadSkills(skill_name));

        QList<int> table_cardids = room->getCardIdsOnTable(this);
        if (!table_cardids.isEmpty()) {
            DummyCard dummy(table_cardids);
            room->moveCardTo(&dummy, card_use.from, NULL, Player::DiscardPile, reason, true);
        }

        card_use.from->drawCards(1);
    } else
        SingleTargetTrick::onUse(room, card_use);
}

void HKnownBoth::onEffect(CardEffectStruct &effect) const {
    QStringList choices;
    if (!effect.to->isKongcheng())
        choices << "handcards";
    if (!effect.to->hasShownGeneral1())
        choices << "head_general";
    if (effect.to->getGeneral2() && !effect.to->hasShownGeneral2())
        choices << "deputy_general";

    Room *room = effect.from->getRoom();

    effect.to->setFlags("KnownBothTarget");// For AI
    QString choice = room->askForChoice(effect.from, objectName(),
        choices.join("+"), QVariant::fromValue(effect.to));
    effect.to->setFlags("-KnownBothTarget");
    LogMessage log;
    log.type = "#KnownBothView";
    log.from = effect.from;
    log.to << effect.to;
    log.arg = choice;
    room->sendLog(log, room->getOtherPlayers(effect.from, true));

    if (choice == "handcards")
        room->showAllCards(effect.to, effect.from);
    else {
        QStringList list = room->getTag(effect.to->objectName()).toStringList();
        list.removeAt(choice == "head_general" ? 1 : 0);
        foreach (const QString &name, list) {
            LogMessage log;
            log.type = "$KnownBothViewGeneral";
            log.from = effect.from;
            log.to << effect.to;
            log.arg = name;
            log.arg2 = choice;
            room->sendLog(log, effect.from);
        }
        JsonArray arg;
        arg << objectName();
        arg << JsonUtils::toJsonArray(list);
        room->doNotify(effect.from, QSanProtocol::S_COMMAND_VIEW_GENERALS, arg);
    }
}

HBefriendAttacking::HBefriendAttacking(Card::Suit suit, int number) : SingleTargetTrick(suit, number) {
    setObjectName("befriend_attacking");
}

bool HBefriendAttacking::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const {
    int total_num = 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    if (targets.length() >= total_num)
        return false;

    return to_select->hasShownOneGeneral() && !Self->isFriendWith(to_select);
}

void HBefriendAttacking::onEffect(CardEffectStruct &effect) const {
    effect.to->drawCards(1);
    effect.from->drawCards(3);
}

bool HBefriendAttacking::isAvailable(const Player *player) const {
    return player->hasShownOneGeneral() && TrickCard::isAvailable(player);
}

QList<Card *> HStandardCardPackage::trickCards(){
    QList<Card *> cards;

    // Common effects use native cards; this list only owns the donor deck composition.
    cards
        << new AmazingGrace(Card::Heart, 3)
        << new GodSalvation
        << new SavageAssault(Card::Spade, 13)
        << new SavageAssault(Card::Club, 7)
        << new ArcheryAttack
        << new Duel(Card::Spade, 1)
        << new Duel(Card::Club, 1)
        << new ExNihilo(Card::Heart, 7)
        << new ExNihilo(Card::Heart, 8)
        << new Snatch(Card::Spade, 3)
        << new Snatch(Card::Spade, 4)
        << new Snatch(Card::Diamond, 3)
        << new Dismantlement(Card::Spade, 3)
        << new Dismantlement(Card::Spade, 4)
        << new Dismantlement(Card::Heart, 12)
        << new IronChain(Card::Spade, 12)
        << new IronChain(Card::Club, 12)
        << new IronChain(Card::Club, 13)
        << new FireAttack(Card::Heart, 2)
        << new FireAttack(Card::Heart, 3)
        << new Collateral(Card::Club, 12)
        << new Nullification(Card::Spade, 11)
        << new HegNullification(Card::Club, 13)
        << new HegNullification(Card::Diamond, 12)
        << new HAwaitExhausted(Card::Heart, 11)
        << new HAwaitExhausted(Card::Diamond, 4)
        << new HKnownBoth(Card::Club, 3)
        << new HKnownBoth(Card::Club, 4)
        << new HBefriendAttacking
        << new Indulgence(Card::Club, 6)
        << new Indulgence(Card::Heart, 6)
        << new SupplyShortage(Card::Spade, 10)
        << new SupplyShortage(Card::Club, 10)
        << new Lightning(Card::Spade, 1);

    return cards;
}
