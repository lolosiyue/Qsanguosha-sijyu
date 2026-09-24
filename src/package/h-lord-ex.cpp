// Ported from QSanguosha-For-Hegemony-xxyheaven cf61c15; retain donor package boundaries.
/********************************************************************
    Copyright (c) 2013-2015 - Mogara

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

    Mogara
    *********************************************************************/

#include "h-lord-ex.h"
#include "h-formation.h"
#include "skill.h"
#include "h-strategic-advantage.h"
#include "standard.h"
#include "maneuvering.h"
#include "h-standard-tricks.h"


#include "general.h"
#include "serverplayer.h"
#include "room.h"
#include "util.h"
#include <QScopedPointer>
#include <QScopeGuard>
#include <memory>
#include <limits>
#include "engine.h"
#include "structs.h"
#include "gamerule.h"
#include "settings.h"
#include "roomthread.h"
#include "json.h"

namespace {
int lordExDiscardCount(const ServerPlayer *player, bool discardPhase)
{
    Room *room = player->getRoom();
    const QVariant turn = room->historyScopes().value("turn_id");
    if (turn.toULongLong() == 0) return 0;
    const QVariantMap history = room->queryHistoryMoves({{"turn_id", turn},
        {"limit", std::numeric_limits<int>::max()}});
    // These rules inspect explicit move endpoints/reason, not causing skill ownership.
    if (!history.value("complete").toBool()) return -1;
    int count = 0;
    for (const QVariant &entry : history.value("items").toList()) {
        const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
        const int place = move.value("from_place").toInt();
        if ((place != Player::PlaceHand && place != Player::PlaceEquip)
            || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON)
                != CardMoveReason::S_REASON_DISCARD) continue;
        if (!discardPhase) {
            if (move.value("reason_player").toString() == player->objectName()) ++count;
            continue;
        }
        // Tongdu/Juejue count this seat's Discard phases within the current turn.
        const QVariantMap phase = room->historyEvent(fact.value("phase_id").toULongLong())
            .value("data").toMap();
        if (move.value("from").toString() == player->objectName()
            && phase.value("player").toString() == player->objectName()
            && phase.value("phase").toInt() == Player::Discard) ++count;
    }
    return count;
}

bool lordExInjuredThisPhase(const ServerPlayer *player)
{
    Room *room = player->getRoom();
    const QVariant phase = room->historyScopes().value("phase_id");
    if (phase.toULongLong() == 0) return false;
    return !room->queryActualDamage({{"phase_id", phase}, {"to", player->objectName()},
        {"limit", 1}}).value("items").toList().isEmpty();
}
}



namespace {
QString lordExChoice(Room *room, ServerPlayer *player, const QString &skill,
                     const QString &choices, const QVariant &data = QVariant(),
                     const QString &tip = QString(), const QString &allChoices = QString())
{
    QStringList excluded = allChoices.split('+', Qt::SkipEmptyParts);
    for (const QString &choice : choices.split('+')) excluded.removeAll(choice);
    return room->askForChoice(player, skill, choices, data, excluded.join('+'), tip);
}

QList<int> lordExExchange(Room *room, ServerPlayer *player, const QString &skill,
                          int maximum, int minimum, const QString &prompt,
                          const QString & = QString(), const QString &pattern = ".")
{
    const Card *selection = room->askForExchange(player, skill, maximum, minimum,
                                                true, prompt, minimum == 0, pattern);
    return selection ? selection->getSubcards() : QList<int>();
}

QList<int> lordExCards(const Player *player, const QString &key)
{
    return ListS2I(player->property(("heg_" + key).toUtf8()).toString().split('+', Qt::SkipEmptyParts));
}

void lordExSetCards(Room *room, ServerPlayer *player, const QString &key, const QList<int> &cards)
{
    // These IDs can identify face-down hand cards. Only the owner receives them.
    const QByteArray property = ("heg_" + key).toUtf8();
    player->setProperty(property.constData(), ListI2S(cards).join('+'));
    player->addProperty(property.constData()); // Reconnect replays this owner-only projection.
    room->notifyProperty(player, player, property.constData());
}

void lordExAddCard(Room *room, ServerPlayer *player, const QString &key, int id)
{
    QList<int> cards = lordExCards(player, key);
    if (!cards.contains(id)) cards << id;
    lordExSetCards(room, player, key, cards);
}

QList<int> lordExCardIds(const Card *card)
{
    if (!card) return {};
    return card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
}

QStringList lordExUsedGenerals(Room *room)
{
    QStringList result;
    for (ServerPlayer *player : room->getAllPlayers(true)) {
        result << player->getActualGeneral1Name() << player->getActualGeneral2Name();
        for (const QString &pile : player->getGeneralPileNames()) result << player->getGeneralPile(pile);
    }
    return result;
}

bool lordExBigKingdom(const Player *player)
{
    if (!player || !player->hasShownOneGeneral()) return false;
    const QStringList big = player->getBigKingdoms("heg_imperialedictattach");
    return big.contains(player->getSeemingKingdom()) || big.contains(player->objectName());
}

void lordExFactionUse(Room *room, CardUseStruct &use, const QString &kingdom)
{
    if (!use.card || !use.from) return;
    // Offer the donor's optional reveal, then bind the bonus to this actual use.
    if (!use.from->hasShownOneGeneral() && use.from->getKingdom() == kingdom) {
        ServerPlayer *lord = room->getLord(kingdom, true);
        bool canRemainInFaction = lord && lord->isAlive();
        if (!lord) {
            int shown = 0;
            for (ServerPlayer *player : room->getAllPlayers(true))
                if (player->hasShownOneGeneral() && player->getRole() != "careerist"
                    && player->getSeemingKingdom() == kingdom) ++shown;
            canRemainInFaction = shown < room->getPlayers().length() / 2;
        }
        QStringList choices;
        if (canRemainInFaction && use.from->canShowGeneral("h") && use.from->getActualGeneral1()
            && use.from->getActualGeneral1()->getKingdom() != "careerist") choices << "show_head";
        if (canRemainInFaction && use.from->canShowGeneral("d")) choices << "show_deputy";
        if (!choices.isEmpty()) {
            choices << "cancel";
            const QString choice = lordExChoice(room, use.from, "trick_show", choices.join('+'),
                QVariant::fromValue(use), "@trick-show:::" + use.card->objectName(), "show_head+show_deputy+cancel");
            if (choice == "show_head") use.from->showGeneral(true);
            else if (choice == "show_deputy") use.from->showGeneral(false);
        }
    }
    use.card->setFlags(use.from->getSeemingKingdom() == kingdom ? "CompleteEffect" : "-CompleteEffect");
}

}

class HQiuan : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    HQiuan() : TriggerSkillV2("heg_qiuan")
    {
        events << DamageInflicted;
    }

    int getPriority(TriggerEvent) const override
    {
        return -2;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && player->getPile("letter").isEmpty()) {
            DamageStruct damage = data.value<DamageStruct>();
            const Card *card = damage.card;
            if (card && room->isAllOnPlace(damage.card, Player::PlaceTable))
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        player->addToPile("letter", damage.card);

        return true;
    }
};

class HLiangfan : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLiangfan() : TriggerSkillV2("heg_liangfan")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Start && !player->getPile("letter").isEmpty()) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        QList<int> ids = player->getPile("letter");

        DummyCard *dummy = new DummyCard(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName(), objectName(), QString());
        room->obtainCard(player, dummy, reason);
        dummy->deleteLater();

        room->loseHp(player);

        QList<int> mark = lordExCards(player, "@liangfan-turn");
        foreach (int card_id, ids) {
            if (player->handCards().contains(card_id))
                mark << card_id;
        }
        lordExSetCards(room, player, "@liangfan-turn", mark);
        return false;
    }
};

class HLiangfanEffect : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLiangfanEffect() : TriggerSkillV2("#heg_liangfan-effect")
    {
        events << EventPhaseChanging << Damage << CardsMoveBatch << PreCardUsed;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            lordExSetCards(room, player, "@liangfan-turn", QList<int>());
         if (triggerEvent == CardsMoveBatch) {
             QList<int> mark = lordExCards(player, "@liangfan-turn");
             QList<int> mark_copy = mark;
             foreach (int id, mark) {
                 if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand)
                     mark_copy.removeOne(id);
             }
             lordExSetCards(room, player, "@liangfan-turn", mark_copy);
         }
         if (triggerEvent == PreCardUsed) {
             CardUseStruct use = data.value<CardUseStruct>();
             if (use.card && use.card->getTypeId() != Card::TypeSkill) {
                 QList<int> mark = lordExCards(player, "@liangfan-turn");
                 foreach (int card_id, mark) {
                     if (lordExCardIds(use.card).contains(card_id)) {
                         room->setCardFlag(use.card, "liangfanEffect");
                         break;
                     }
                 }
             }
         }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == Damage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->hasFlag("liangfanEffect") && !damage.chain && !damage.transfer && damage.by_user) {
                ServerPlayer *target = damage.to;
                if (target && target->isAlive() && player->isAlive() && player->canGet(target, "he")) {
                    return TriggerList{{player, {objectName()}}};
                }
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target = damage.to;

        if (target && player->canGet(target, "he") && lordExChoice(room, player, "heg_liangfan", "yes+no", QVariant(), "@liangfan::" + target->objectName()) == "yes") {
            LogMessage log;
            log.type = "#LiangfanEffect";
            log.from = player;
            log.to << target;
            log.arg = "heg_liangfan";
            room->sendLog(log);
            int card_id = room->askForCardChosen(player, target, "he", "heg_liangfan", false, Card::MethodGet);
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
        }

        return false;
    }
};

class HXingzhao : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXingzhao() : TriggerSkillV2("heg_xingzhao")
    {
        events << Damaged << EventPhaseStart << CardsMoveBatch;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (triggerEvent == Damaged && getWoundedKingdoms(room) > 1) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from->isAlive() && damage.from->getHandcardNum() != player->getHandcardNum()) return TriggerList{{player, {objectName()}}};
        }
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Discard && getWoundedKingdoms(room) > 2) {
            return TriggerList{{player, {objectName()}}};
        }
        if (triggerEvent == CardsMoveBatch && getWoundedKingdoms(room) > 3) {
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from == player && move.from_places.contains(Player::PlaceEquip)) {
                    return TriggerList{{player, {objectName()}}};
                }
            }
            return TriggerList();
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else {

            invoke = player->askForSkillInvoke(this, data);
        }

        if (invoke) {
            int n = qsanRandomBounded(2) + 1;
            if (triggerEvent == EventPhaseStart)
                n+=2;
            if (triggerEvent == CardsMoveBatch) {
                n+=4;
            }
            room->broadcastSkillInvoke(objectName(), n, player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *from = damage.from;
            if (from->getHandcardNum() < player->getHandcardNum())
                from->drawCards(1, objectName());
            if (from->getHandcardNum() > player->getHandcardNum())
                player->drawCards(1, objectName());
        }
        if (triggerEvent == CardsMoveBatch)
            player->drawCards(1, objectName());
        else if (triggerEvent == EventPhaseStart)
            room->addPlayerMark(player, "heg_xingzhao_maxcards", 4);
        return false;
    }

private:
    static int getWoundedKingdoms(Room *room)
    {
        QList<ServerPlayer *> to_count, players = room->getAlivePlayers();

        foreach (ServerPlayer *p, players) {
            if (p->isWounded() && p->hasShownOneGeneral()) {
                bool record = true;
                foreach (ServerPlayer *p2, to_count) {
                    if (p->isFriendWith(p2)) {
                        record = false;
                        break;
                    }
                }
                if (record)
                    to_count << p;

            }

        }

        return to_count.length();
    }
};


class HXingzhaoMaxCards : public MaxCardsSkillV2
{
public:
    HXingzhaoMaxCards() : MaxCardsSkillV2("#heg_xingzhao-maxcards") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        return context.primary ? CorrectSkillResult::useAmount(context.primary->getMark("heg_xingzhao_maxcards"))
                               : CorrectSkillResult::noEffect();
    }
};

// A real V2 child source replaces the donor's synthetic ViewHas grant.
class HXunxunTangzi : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXunxunTangzi() : TriggerSkillV2("heg_xunxun_tangzi") {
        events << EventPhaseStart;}
    bool shouldBeVisible(const Player *player) const override
    {
        return player && player->hasShownSkill("heg_xingzhao") && hasWoundedFaction(player);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasShownSkill("heg_xingzhao")
            && hasWoundedFaction(player) && player->getPhase() == Player::Draw
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        return player->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        QList<int> bottom = room->getNCards(4), top;
        const int count = qMin(2, int(bottom.size()));
        room->fillAG(bottom, player);
        for (int i = 0; i < count; ++i) {
            const int id = room->askForAG(player, bottom, false, objectName());
            if (!bottom.contains(id)) break;
            bottom.removeOne(id);
            top << id;
            room->takeAG(player, id, false, QList<ServerPlayer *>{player});
        }
        room->clearAG(player);
        room->askForGuanxing(player, bottom, Room::GuanxingDownOnly);
        room->returnToTopDrawPile(top);
        // The ordinary Draw phase then draws the selected top two cards.
        return false;
    }
private:
    static bool hasWoundedFaction(const Player *player)
    {
        QList<const Player *> all = player->getAliveSiblings();
        all << player;
        for (const Player *p : all)
            if (p->hasShownOneGeneral() && p->isWounded()) return true;
        return false;
    }
};

class HFankuiSimazhao : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFankuiSimazhao() : TriggerSkillV2("heg_fankui_simazhao") {
        events << Damaged;}
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return (player && player->isAlive() && player->hasSkill(objectName())) && damage.from && player->canGet(damage.from, "he")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.from && !damage.from->isNude() && player->askForSkillInvoke(this, data);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || !player->canGet(damage.from, "he")) return false;
        Room *room = player->getRoom();
        const int id = room->askForCardChosen(player, damage.from, "he", objectName(), false, Card::MethodGet);
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
        room->obtainCard(player, Sanguosha->getCard(id), reason, false);
        return false;
    }
};

class HBushi : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HBushi() : TriggerSkillV2("heg_bushi")
    {
        events << EventPhaseStart;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->isAlive() && player->getPhase() == Player::Start) {
            TriggerList skill_list;
            QList<ServerPlayer *> skill_owners = room->findPlayersBySkillName(objectName());
            foreach (ServerPlayer *skill_owner, skill_owners) {
                if (skill_owner != player && !skill_owner->isNude() && skill_owner->getMark("#heg_yishe") > 0)
                    skill_list.insert(skill_owner, QStringList(objectName()));
            }
            return skill_list;
        }

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *skill_owner = ctx.owner;
        QList<int> result = lordExExchange(room, skill_owner, objectName(), 1, 0, "@bushi-give:"+ player->objectName());
        if (!result.isEmpty()) {
            LogMessage l;
            l.type = "#InvokeSkill";
            l.from = player;
            l.arg = objectName();
            room->sendLog(l);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, skill_owner->objectName(), player->objectName());
            room->broadcastSkillInvoke(objectName(), skill_owner);
            room->notifySkillInvoked(skill_owner, objectName());

            room->removePlayerMark(skill_owner, "#heg_yishe");
            DummyCard dummy(result);
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, skill_owner->objectName(), player->objectName(), objectName(), QString());
            room->obtainCard(player, &dummy, reason, false);

            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *skill_owner = ctx.owner;
        ctx.manual_effect = true;
        skill_owner->drawCards(2, objectName());
        return false;
    }
};

class HBushiCompulsory : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HBushiCompulsory() : TriggerSkillV2("#heg_bushi-compulsory")
    {
        events << EventPhaseStart << EventPhaseChanging << EventLoseSkill;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventLoseSkill && player && data.value<SkillChangeStruct>().skillName == "heg_bushi")
            room->setPlayerMark(player, "#heg_yishe", 0);
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player == NULL || player->isDead() || !player->hasSkill("heg_bushi")) return TriggerList();

        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            return TriggerList{{player, {objectName()}}};
        else if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Start) {
            int x = room->alivePlayerCount() - player->getHp() - 2;
            if (player->getMark("#heg_yishe") > 0 || (x > 0 && !player->isNude()))
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill("heg_bushi")) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, "heg_bushi");
        } else {
            if (triggerEvent == EventPhaseChanging)
                invoke = player->askForSkillInvoke("heg_bushi", "mark");
            else if (triggerEvent == EventPhaseStart) {
                int x = room->alivePlayerCount() - player->getHp() - 2;
                invoke = player->askForSkillInvoke("heg_bushi", "discard:::" + QString::number(x));
            }
        }

        if (invoke) {

            room->broadcastSkillInvoke("heg_bushi", player);

            return true;
        }

        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (triggerEvent == EventPhaseChanging)
            room->addPlayerMark(player, "#heg_yishe", player->getHp());
        if (triggerEvent == EventPhaseStart) {
            int x = room->alivePlayerCount() - player->getHp() - 2;
            if (x > 0)
                room->askForDiscard(player, "bushi_discard", x, x, false, true);
            room->setPlayerMark(player, "#heg_yishe", 0);
        }
        return false;
    }
};

class HMidaoViewAsSkill : public ViewAsSkillV2
{
public:
    HMidaoViewAsSkill() : ViewAsSkillV2("heg_midao", 1)
    {
        expand_pile = "rice";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        return request.pattern == "@@heg_midao"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        return Sanguosha->matchExpPattern(".|.|.|rice", request.initiator, to_select);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator
            || request.selectedCardIds.size() != 1) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        const Card *real = originalCard->getRealCard();
        if (!real) return nullptr;
        // Rice belongs to the room; source decoration must only touch a virtual clone.
        std::unique_ptr<Card> selection(Sanguosha->cloneCard(originalCard->objectName(), originalCard->getSuit(),
            originalCard->getNumber(), originalCard->getFlags()));
        if (selection && selection->metaObject() != real->metaObject())
            selection.reset();
        if (!selection)
            selection.reset(Sanguosha->cloneCard(QString::fromLatin1(real->metaObject()->className()),
                originalCard->getSuit(), originalCard->getNumber(), originalCard->getFlags()));
        if (!selection || selection->metaObject() != real->metaObject())
            return nullptr;
        selection->setObjectName(originalCard->objectName());
        selection->setTransferable(originalCard->isTransferable());
        selection->setSkillName(objectName());
        selection->addSubcard(originalCard->getEffectiveId());
        return selection.release();
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return QString();
        return Sanguosha->getCard(request.selectedCardIds.first())->getClassName();
    }
};

class HMidao : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HMidao() : TriggerSkillV2("heg_midao")
    {
        events << EventPhaseStart << AskForRetrial;
        view_as_skill = new HMidaoViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();

        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish && player->getPile("rice").isEmpty()) {
            return TriggerList{{player, {objectName()}}};
        } else if (triggerEvent == AskForRetrial && !player->getPile("rice").isEmpty()) {
            return TriggerList{{player, {objectName()}}};
        }

        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        if (triggerEvent == EventPhaseStart) {
            if (player->askForSkillInvoke(this)) {
                room->broadcastSkillInvoke(objectName(), player);
                return true;
            }
        } else if (triggerEvent == AskForRetrial) {

            JudgeStruct *judge = data.value<JudgeStruct *>();

            QStringList prompt_list;
            prompt_list << "@midao-card" << judge->who->objectName()
                << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
            QString prompt = prompt_list.join(":");

            const Card *card = room->askForCard(player, "@@heg_midao", prompt, data, Card::MethodResponse, judge->who, true);

            if (card) {

                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);

                LogMessage log2;
                log2.card_str = card->toString();
                log2.from = player;
                log2.type = QString("#%1_Resp").arg(card->getClassName());
                room->sendLog(log2);

                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName(), player);

                CardMoveReason reason(CardMoveReason::S_REASON_RESPONSE, player->objectName(), objectName(), QString());

                room->moveCardTo(card, NULL, Player::PlaceTable, reason);

                // Native Room::retrial publishes the typed retrial response once;
                // emitting the donor response here would double-count the same card.

                QStringList card_list = player->getTag("midao_cards").toStringList();
                card_list.append(card->toString());
                player->setTag("midao_cards", card_list);

                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (triggerEvent == EventPhaseStart) {
            player->drawCards(2, objectName());
            QList<int> result = lordExExchange(room, player, "_heg_midao", 2, 2, "@midao-push");
            player->addToPile("rice", result);

        } else if (triggerEvent == AskForRetrial) {
            QStringList card_list = player->getTag("midao_cards").toStringList();
            if (card_list.isEmpty()) return false;
            QString card_str = card_list.takeLast();
            player->setTag("midao_cards", card_list);

            const Card *card = Card::Parse(card_str);
            if (card) {
                JudgeStruct *judge = data.value<JudgeStruct *>();
                room->retrial(card, player, judge, objectName(), true);
                judge->updateResult();
            }
        }
        return false;
    }
};

class HFengshiX : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFengshiX() : TriggerSkillV2("heg_fengshix")
    {
        // The donor only accepts a single target, so native once-per-use dispatch is exact.
        events << TargetSpecified << ConfirmDamage;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (triggerEvent == ConfirmDamage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->hasFlag("FengshiXEffect")) {
                damage.damage++;
                data = QVariant::fromValue(damage);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != TargetSpecified || !(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() != Card::TypeSkill && use.to.size() == 1) {
            ServerPlayer *target = use.to.first();
            if (player->getHandcardNum() > target->getHandcardNum() && !target->isNude())
                return TriggerList{{player, {objectName()}}};

        }

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        CardUseStruct use = data.value<CardUseStruct>();
        ServerPlayer *target = use.to.first();
        player->setTag("FengshixUsedata", data);
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(target));
        player->removeTag("FengshixUsedata");
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() != Card::TypeSkill && use.to.size() == 1) {
            ServerPlayer *target = use.to.first();

            QList<ServerPlayer *> players;
            players << player << target;
            room->sortByActionOrder(players);

            foreach (ServerPlayer *p, players) {

                if (player->isAlive() && p->isAlive() && player->canDiscard(p, "he")) {
                    int card_id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                    room->throwCard(card_id, p, player);
                }
            }

            room->setCardFlag(use.card, "FengshiXEffect");
        }

        return false;
    }
};

class HFengshiXOther : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFengshiXOther() : TriggerSkillV2("#heg_fengshix-other")
    {
        events << TargetConfirmed;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player == NULL || player->isDead() || !player->hasShownSkill("heg_fengshix")) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() != Card::TypeSkill && use.to.size() == 1 && use.to.first() == player) {
            if (use.from && use.from->isAlive() && use.from->getHandcardNum() > player->getHandcardNum() && !player->isNude())
                return TriggerList{{player, {objectName()}}};

        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        CardUseStruct use = data.value<CardUseStruct>();
        if (lordExChoice(room, use.from, "heg_fengshix", "yes+no", data, "@fengshix:" + player->objectName()) == "yes") {
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = use.from;
            log.to << player;
            log.arg = "heg_fengshix";
            room->sendLog(log);
            room->broadcastSkillInvoke("heg_fengshix", player);
            room->notifySkillInvoked(player, "heg_fengshix");
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        CardUseStruct use = data.value<CardUseStruct>();
        ServerPlayer *source = use.from;
        QList<ServerPlayer *> players;
        players << source << player;
        room->sortByActionOrder(players);
        foreach (ServerPlayer *p, players) {
            if (player->isAlive() && p->isAlive() && player->canDiscard(p, "he")) {
                int card_id = room->askForCardChosen(player, p, "he", "heg_fengshix", false, Card::MethodDiscard);
                room->throwCard(card_id, p, player);
            }
        }
        room->setCardFlag(use.card, "FengshiXEffect");
        return false;
    }
};

class HWenji : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HWenji() : TriggerSkillV2("heg_wenji")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Play) {
            QList<ServerPlayer *> players = room->getOtherPlayers(player);
            foreach (ServerPlayer *p, players) {
                if (!p->isNude())
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isNude())
                targets << p;
        }
        ServerPlayer *victim;
        if ((victim = room->askForPlayerChosen(player, targets, objectName(), "@wenji", true, true)) != NULL) {
            room->broadcastSkillInvoke(objectName(), player);

            QStringList target_list = player->getTag("wenji_target").toStringList();
            target_list.append(victim->objectName());
            player->setTag("wenji_target", target_list);

            return true;
        }
        return false;

    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        QStringList target_list = player->getTag("wenji_target").toStringList();
        QString target_name = target_list.last();
        target_list.removeLast();
        player->setTag("wenji_target", target_list);

        ServerPlayer *target = room->findPlayerByObjectName(target_name);
        if (target != NULL && !target->isNude()) {
            QList<int> ints = lordExExchange(room, target, "wenji_give", 1, 1, "@wenji-give:" + player->objectName());
            int card_id = -1;
            if (ints.isEmpty()) {
                card_id = target->getCards("he").first()->getEffectiveId();
            } else
                card_id = ints.first();

            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), objectName(), QString());
            reason.m_playerId = player->objectName();
            room->moveCardTo(Sanguosha->getCard(card_id), player, Player::PlaceHand, reason, true);

            if (player->isFriendWith(target) || !target->hasShownOneGeneral()) {
                if (player->handCards().contains(card_id)) {
                    lordExAddCard(room, player, "@wenji-turn", card_id);
                }
            } else {
                int give_back = -1;
                QList<int> to_give = player->handCards();
                to_give.removeOne(card_id);
                if (to_give.isEmpty()) {
                    if (player->hasEquip())
                        give_back = player->getEquips().first()->getEffectiveId();
                } else {
                    give_back = to_give.first();
                }

                if (give_back == -1) return false;

                QString pattern = QString("^%1").arg(card_id);

                target->setFlags("WenjiTarget");
                QList<int> ints = lordExExchange(room, player, "wenji_giveback", 1, 1, "@wenji-give:" + target->objectName(), QString(), pattern);
                target->setFlags("-WenjiTarget");

                int card_id = -1;
                if (ints.isEmpty()) {
                    card_id = give_back;
                } else
                    card_id = ints.first();

                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), objectName(), QString());
                reason.m_playerId = target->objectName();
                room->moveCardTo(Sanguosha->getCard(card_id), target, Player::PlaceHand, reason, true);

            }
        }

        return false;
    }
};

class HWenjiEffect : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HWenjiEffect() : TriggerSkillV2("#heg_wenji-effect")
    {
        events << EventPhaseChanging << CardUsed << CardsMoveBatch << PreCardUsed;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            lordExSetCards(room, player, "@wenji-turn", QList<int>());
         if (triggerEvent == CardsMoveBatch) {
             QList<int> mark = lordExCards(player, "@wenji-turn");
             QList<int> mark_copy = mark;
             foreach (int id, mark) {
                 if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand)
                     mark_copy.removeOne(id);
             }
             lordExSetCards(room, player, "@wenji-turn", mark_copy);
         }
         if (triggerEvent == PreCardUsed) {
             CardUseStruct use = data.value<CardUseStruct>();
             if (use.card && use.card->getTypeId() != Card::TypeSkill) {
                 QList<int> mark = lordExCards(player, "@wenji-turn");
                 foreach (int card_id, mark) {
                     if (lordExCardIds(use.card).contains(card_id)) {
                         room->setCardFlag(use.card, "wenjiEffect");
                         break;
                     }
                 }
             }
         }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player == NULL || player->isDead() || triggerEvent != CardUsed) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && (use.card->isKindOf("Slash") || use.card->isNDTrick()) && use.card->hasFlag("wenjiEffect")) {
            return TriggerList{{player, {objectName()}}};

        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card) {
            LogMessage log;
            log.type = "#WenjiEffect";
            log.from = player;
            log.arg = "heg_wenji";
            log.arg2 = use.card->objectName();
            room->sendLog(log);
            use.no_respond_list << "_ALL_TARGETS";
            data = QVariant::fromValue(use);
        }
        return false;
    }
};

class HWenjiTargetMod : public TargetModSkillV2
{
public:
    HWenjiTargetMod() : TargetModSkillV2("#heg_wenji-target")
    {
        pattern = "^SkillCard";
    }

    virtual int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card)) return 0;
        QList<int> mark = lordExCards(from, "@wenji-turn");
        foreach (int card_id, mark) {
            if (lordExCardIds(card).contains(card_id) || card->hasFlag("Global_AvailabilityChecker")) {
                return 1000;
            }
        }
        return 0;
    }

    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *to) const
    {
        return getResidueNum(from, card, to);
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.primary) return CorrectSkillResult::noEffect();
        int value = 0;
        if (context.modType == TargetModSkill::Residue) value = getResidueNum(context.primary, context.card, context.secondary);
        else if (context.modType == TargetModSkill::DistanceLimit) value = getDistanceLimit(context.primary, context.card, context.secondary);
        return CorrectSkillResult::useAmount(value);
    }
};

class HTunjiang : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HTunjiang() : TriggerSkillV2("heg_tunjiang")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->hasFlag("TunjiangDisabled")) return TriggerList();
        if (player->getPhase() == Player::Finish && player->getMark("heg_tunjiang_play_used") > 0) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        QList<ServerPlayer *> to_count, players = room->getAlivePlayers();
        foreach (ServerPlayer *p, players) {
            if (!p->hasShownOneGeneral()) continue;
            bool no_friend = true;
            foreach (ServerPlayer *p2, to_count) {
                if (p2->isFriendWith(p)) {
                    no_friend = false;
                    break;
                }
            }
            if (no_friend)
                to_count << p;
        }

        int x = to_count.length();

        player->drawCards(x, objectName());

        return false;
    }
};

class HBiluan : public DistanceSkillV2
{
public:
    HBiluan() : DistanceSkillV2("heg_biluan")
    {
        setHolderSelector(CorrectSkill_Secondary);

    }

    virtual int getCorrect(const Player *, const Player *to) const
    {
        if (to->hasShownSkill(objectName()))
            return qMax(int(to->getEquips().length()), 1);
        else
            return 0;
    }    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        return context.secondary ? CorrectSkillResult::useAmount(getCorrect(context.primary, context.secondary)) : CorrectSkillResult::noEffect();
    }
};

class HLixia : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLixia() : TriggerSkillV2("heg_lixia")
    {
        events << EventPhaseStart;

    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return false;
    }
};

class HLixiaOther : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLixiaOther() : TriggerSkillV2("#heg_lixia-other")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        TriggerList skill_list;
        if (player == NULL || player->isDead() || !player->hasShownOneGeneral() || player->getPhase() != Player::Start) return skill_list;
        QList<ServerPlayer *> shixies = room->findPlayersBySkillName("heg_lixia");
        foreach (ServerPlayer *shixie, shixies) {
            if (!player->isFriendWith(shixie) && shixie->hasEquip() && shixie->hasShownSkill("heg_lixia"))
                skill_list.insert(shixie, QStringList(objectName()));
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *owner = ctx.owner;
        if (lordExChoice(room, player, "heg_lixia", "yes+no", QVariant(), "@lixia:" + owner->objectName()) == "yes") {
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = player;
            log.to << owner;
            log.arg = "heg_lixia";
            room->sendLog(log);
            room->broadcastSkillInvoke("heg_lixia", owner);
            room->notifySkillInvoked(owner, "heg_lixia");
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *shixie = ctx.owner;
        ctx.manual_effect = true;
        if (!player->canDiscard(shixie, "e")) return false;
        int card_id = room->askForCardChosen(player, shixie, "e", "heg_lixia", false, Card::MethodDiscard);
        CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), shixie->objectName(), "heg_lixia", QString());
        CardsMoveStruct dis_move(card_id, NULL, Player::DiscardPile, reason);
        const QVariant moved = room->moveCardsSub(dis_move, true);
        bool paid = false;
        for (const QVariant &entry : moved.toList()) {
            const CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.from != shixie || move.reason.m_reason != CardMoveReason::S_REASON_DISMANTLE) continue;
            for (Player::Place place : move.from_places)
                if (place == Player::PlaceHand || place == Player::PlaceEquip) paid = true;
        }
        if (!paid) return false;
        QStringList choices;
        choices << "draw%from:"+ shixie->objectName() << "losehp";
        QStringList all_choices = choices;
        all_choices << "discard";
        if (player->forceToDiscard(2, true, true).length() > 1)
            choices << "discard";
        QString choice = lordExChoice(room, player, "lixia_effect", choices.join("+"), QVariant(), "@lixia-choose:" + shixie->objectName(), all_choices.join("+"));
        if (choice.contains("draw"))
            shixie->drawCards(2, "heg_lixia");
        if (choice == "losehp")
            room->loseHp(player);
        if (choice == "discard")
            room->askForDiscard(player, "lixia_discard", 2, 2, false, true);
        return false;
    }


};

class HQuanji : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HQuanji() : TriggerSkillV2("heg_quanji")
    {
        events << Damage << Damaged;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && !player->hasFlag((triggerEvent == Damage)? "Quanji1Used" : "Quanji2Used"))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this)) {
            player->setFlags((triggerEvent == Damage)? "Quanji1Used" : "Quanji2Used");
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        player->drawCards(1, objectName());

        if (player->isNude()) return false;

        int id = player->getCards("he").first()->getEffectiveId();

        QList<int> result = lordExExchange(room, player, "_heg_quanji", 1, 1, "@quanji-push");

        if (!result.isEmpty()) id = result.first();

        player->addToPile("power_pile", id);

        return false;
    }
};

class HQuanjiMaxCards : public MaxCardsSkillV2
{
public:
    HQuanjiMaxCards() : MaxCardsSkillV2("#heg_quanji-maxcards")
    {
    }

    virtual int getExtra(const Player *target) const
    {
        if (target->hasShownSkill("heg_quanji"))
            return target->getPile("power_pile").length();
        return 0;
    }    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.primary) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(getExtra(context.primary));
    }
};

HPaiyiCard::HPaiyiCard()
{
    will_throw = true;
    handling_method = Card::MethodNone;
}

bool HPaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void HPaiyiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *zhonghui = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = zhonghui->getRoom();

    if (!zhonghui->getPile("power_pile").isEmpty())
        target->drawCards(qMin(int(zhonghui->getPile("power_pile").length()), 7), objectName());

    if (target->getHandcardNum() > zhonghui->getHandcardNum())
        room->damage(DamageStruct("heg_paiyi", zhonghui, target));
}

class HPaiyi : public ViewAsSkillV2
{
public:
    HPaiyi() : ViewAsSkillV2("heg_paiyi", 1)
    {
        expand_pile = "power_pile";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->getPile("power_pile").isEmpty() && !player->hasUsed("HPaiyiCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        return Sanguosha->matchExpPattern(".|.|.|power_pile", request.initiator, to_select);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator
            || request.selectedCardIds.size() != 1) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *c = Sanguosha->getCard(request.selectedCardIds.first());
        HPaiyiCard *py = new HPaiyiCard;
        py->addSubcard(c);
        return py;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HPaiyiCard";
    }
};

HQuanjinCard::HQuanjinCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool HQuanjinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    const ServerPlayer *target = qobject_cast<const ServerPlayer *>(to_select);
    return targets.isEmpty() && to_select != Self
        && (target ? lordExInjuredThisPhase(target) : to_select->getMark("heg_quanjin_injured") > 0);
}

class HQuanjinTargets : public TriggerSkillV2
{
public:
    HQuanjinTargets() : TriggerSkillV2("#heg_quanjin-targets")
    {
        events << HpChanged << Damaged << DamageComplete << EventPhaseChanging << EventPhaseStart;
        global = true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        // Publish only client target legality; history remains the authoritative record.
        for (ServerPlayer *target : room->getAllPlayers(true))
            room->setPlayerMark(target, "heg_quanjin_injured", lordExInjuredThisPhase(target) ? 1 : 0);
        return true;
    }
};

void HQuanjinCard::extraCost(Room *room, const CardUseStruct &card_use) const
{
    ServerPlayer *target = card_use.to.first();
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), target->objectName(), "rende", QString());
    room->obtainCard(target, this, reason, false);
}

void HQuanjinCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    if (target->doCommand("heg_quanjin", source->startCommand("heg_quanjin", target), source))
        source->drawCards(1, "heg_quanjin");
    else {
        int x = 0;
        QList<ServerPlayer *> all_players = room->getAlivePlayers();
        foreach (ServerPlayer *p, all_players) {
            x = qMax(x, p->getHandcardNum());
        }
        if (x > 0 && x > source->getHandcardNum())
            source->drawCards(qMin(x - source->getHandcardNum(), 5), "heg_quanjin");


    }
}

class HQuanjin : public ViewAsSkillV2
{
public:
    HQuanjin() : ViewAsSkillV2("heg_quanjin", 1)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HQuanjinCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        return Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, to_select);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator
            || request.selectedCardIds.size() != 1) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *c = Sanguosha->getCard(request.selectedCardIds.first());
        HQuanjinCard *skillcard = new HQuanjinCard;
        skillcard->addSubcard(c);
        skillcard->setShowSkill(objectName());
        return skillcard;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HQuanjinCard";
    }
};

HZaoyunCard::HZaoyunCard()
{
    will_throw = true;
}

bool HZaoyunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->isFriendWith(to_select) && to_select->hasShownOneGeneral()
            && Self->distanceTo(to_select)-1 == subcardsLength();
}

void HZaoyunCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    QStringList target_list = source->getTag("zaoyun_target").toStringList();
    target_list.append(target->objectName());
    source->setTag("zaoyun_target", target_list);

    room->setFixedDistance(source, target, 1);

    room->damage(DamageStruct("heg_zaoyun", source, target));
}

class HZaoyunViewAsSkill : public ViewAsSkillV2
{
public:
    HZaoyunViewAsSkill() : ViewAsSkillV2("heg_zaoyun", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HZaoyunCard") && player->hasShownOneGeneral();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }
        return !request.initiator->isJilei(to_select) && !to_select->isEquipped();
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return !request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QList<const Card *> cards;
        for (int id : request.selectedCardIds) cards << Sanguosha->getCard(id);
        if (cards.isEmpty()) return NULL;

        HZaoyunCard *skillcard = new HZaoyunCard;
        skillcard->addSubcards(cards);
        skillcard->setShowSkill(objectName());
        return skillcard;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HZaoyunCard";
    }
};

class HZaoyun : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZaoyun() : TriggerSkillV2("heg_zaoyun")
    {
        events << EventPhaseStart;
        view_as_skill = new HZaoyunViewAsSkill;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
         if (player->getPhase() != Player::NotActive) return true;
         QStringList target_list = player->getTag("zaoyun_target").toStringList();
         player->removeTag("zaoyun_target");

         foreach (QString name, target_list) {
             ServerPlayer *target = room->findPlayerByObjectName(name, true);
             if (target)
                 room->setFixedDistance(player, target, -1);
         }

        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }
};

HDiaoguiCard::HDiaoguiCard()
{
    will_throw = false;
}

bool HDiaoguiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    HLureTiger *trick = new HLureTiger(getSuit(), getNumber());
    trick->addSubcard(this);
    trick->setSkillName("heg_diaogui");
    return trick->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, trick, targets);
}

void HDiaoguiCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<ServerPlayer *> to_count, players, all_players = room->getAllPlayers(true);

    foreach (ServerPlayer *p, all_players) {
        if (!p->isRemoved() && p->isAlive())
            players << p;
    }

    HLureTiger *trick = new HLureTiger(getSuit(), getNumber());
    trick->addSubcard(this);
    trick->setShowSkill("heg_diaogui");
    trick->setSkillName("heg_diaogui");
    room->useCard(CardUseStruct(trick, card_use.from, card_use.to));

    foreach (ServerPlayer *p, all_players) {
        if ((p->isRemoved() || p->isDead()) && players.contains(p))
            to_count << p;
    }

    int x = 0;

    foreach (ServerPlayer *p, to_count) {
        Player *p1 = p->getNextAlive();
        Player *p2 = p->getLastAlive();

        if (p1 && p2 && p1 != p2 && p1->getFormation().contains(p2)) {
            if (card_use.from->isFriendWith(p1))
                x = qMax(x, int(p1->getFormation().length()));
        }
    }

    if (x > 0)
        card_use.from->drawCards(x, "heg_diaogui");
}

class HDiaogui : public ViewAsSkillV2
{
public:
    HDiaogui() : ViewAsSkillV2("heg_diaogui", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HDiaoguiCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        if (to_select->getTypeId() != Card::TypeEquip) return false;
        HLureTiger trick(to_select->getSuit(), to_select->getNumber());
        trick.addSubcard(to_select);
        trick.setSkillName("heg_diaogui");
        return trick.isAvailable(request.initiator);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator
            || request.selectedCardIds.size() != 1) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *c = Sanguosha->getCard(request.selectedCardIds.first());
        HDiaoguiCard *skillcard = new HDiaoguiCard;
        skillcard->addSubcard(c);
        return skillcard;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HDiaoguiCard";
    }
};

class HFengyang : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFengyang() : TriggerSkillV2("heg_fengyang")
    {
        view_as_skill = new HArraySummon("heg_fengyang", "Formation");
        events << BeforeCardsMoveBatch;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()) && player->aliveCount() >= 4)) {
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if ((move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE)
                     || (move.to && move.to != move.from && move.to_place == Player::PlaceHand
                     && move.reason.m_reason != CardMoveReason::S_REASON_GIVE)) {
                    ServerPlayer *source = room->findPlayerByObjectName(move.reason.m_playerId);
                    if (source != NULL && source->hasShownOneGeneral() && !player->isFriendWith(source)
                            && move.from && player->inFormationRalation((ServerPlayer *)move.from)) {
                        if (move.from_places.contains(Player::PlaceEquip))
                            return TriggerList{{player, {objectName()}}};
                    }
                }
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doBattleArrayAnimate(player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QVariantList move_datas = data.toList();
        QList<int> ids;
        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if ((move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE)
                 || (move.to && move.to != move.from && move.to_place == Player::PlaceHand
                 && move.reason.m_reason != CardMoveReason::S_REASON_GIVE)) {
                ServerPlayer *source = room->findPlayerByObjectName(move.reason.m_playerId);
                if (source != NULL && source->hasShownOneGeneral() && !player->isFriendWith(source)
                        && move.from && player->inFormationRalation((ServerPlayer *)move.from)) {
                    for (int i = 0; i < move.card_ids.length(); ++i) {
                        if (move.from_places.at(i) == Player::PlaceEquip) {
                            ids << move.card_ids.at(i);
                        }
                    }
                }
            }
        }
        QVariantList retained;
        for (const QVariant &entry : data.toList()) {
            CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            move.removeCardIds(ids);
            retained << QVariant::fromValue(move);
        }
        data = retained;
        return false;
    }
};

class HZhidao : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZhidao() : TriggerSkillV2("heg_zhidao")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
         if (player->getPhase() != Player::NotActive) return true;
         QStringList target_list = player->property("zhidao_targets").toString().split("+");

         foreach (QString name, target_list) {
             ServerPlayer *target = room->findPlayerByObjectName(name, true);
             if (target) {
                 room->setPlayerMark(target, "##zhidao", 0);
                 room->setFixedDistance(player, target, -1);
             }
         }

         room->setPlayerProperty(player, "zhidao_targets", QVariant());

        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Play) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        room->setPlayerFlag(player, "ZhidaoInvoked");

        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@zhidao-target");

        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

        QStringList assignee_list = player->property("zhidao_targets").toString().split("+");
        assignee_list << target->objectName();
        room->setPlayerProperty(player, "zhidao_targets", assignee_list.join("+"));

        room->setFixedDistance(player, target, 1);
        room->addPlayerMark(target, "##zhidao");

        return false;
    }
};

class HZhidaoDamage : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZhidaoDamage() : TriggerSkillV2("#heg_zhidao-damage")
    {
        events << Damage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player->getPhase() != Player::Play) return TriggerList();
        DamageStruct damage = data.value<DamageStruct>();
        const QVariant phase = room->historyScopes().value("phase_id");
        if (!damage.to || phase.toULongLong() == 0) return {};
        const QVariantMap history = room->queryActualDamage({{"phase_id", phase},
            {"from", player->objectName()}, {"to", damage.to->objectName()},
            {"limit", std::numeric_limits<int>::max()}});
        const QVariantList damages = history.value("items").toList();
        const qint64 damageEvent = room->historyParent(room->currentHistoryEventId(), "damage", true)
            .value("id").toLongLong();
        // The current committed damage must be the first damage to this target in this phase.
        if (!history.value("complete").toBool() || damages.isEmpty()
            || damages.first().toMap().value("event_id").toLongLong() != damageEvent) return {};
        ServerPlayer *target = damage.to;
        QStringList target_list = player->property("zhidao_targets").toString().split("+");
        if (target && target_list.contains(target->objectName()) && player->canGet(target, "hej"))
            return TriggerList{{player, {objectName()}}};

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target = damage.to;

        if (target && player->canGet(target, "hej")) {
            LogMessage log;
            log.type = "#ZhidaoEffect";
            log.from = player;
            log.to << target;
            log.arg = "heg_zhidao";
            room->sendLog(log);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

            int card_id = room->askForCardChosen(player, target, "hej", "heg_zhidao", false, Card::MethodGet);
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
        }

        return false;
    }
};

class HZhidaoProhibit : public ProhibitSkill
{
public:
    HZhidaoProhibit() : ProhibitSkill("#heg_zhidao-prohibit")
    {
    }

    virtual bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (from && to && from->hasFlag("ZhidaoInvoked") && card->getTypeId() != Card::TypeSkill) {
            QStringList assignee_list = from->property("zhidao_targets").toString().split("+");

            return from != to && !assignee_list.contains(to->objectName());
        }
        return false;
    }
};

class HJiliX : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJiliX() : TriggerSkillV2("heg_jilix")
    {
        events << CardFinished;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *ask_who = player;
        if (triggerEvent == CardFinished && player != NULL && player->isAlive()) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isRed() && (use.card->isNDTrick() || use.card->getTypeId() == Card::TypeBasic)
                    && !use.card->isKindOf("AllianceFeast") && use.to.size() == 1) {
                ServerPlayer *target = use.to.first();
                if ((target && target->isAlive() && target->hasSkill(objectName()))) {
                    ask_who = target;
                    return TriggerList{{ask_who, {objectName()}}};
                }
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        QString prompt = "damage";
        if (triggerEvent == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            prompt = "target:"+use.from->objectName()+"::"+use.card->objectName();
        }
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, prompt);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        if (triggerEvent == DamageInflicted) {
            if (player->ownSkill(objectName()))
                player->removeGeneral(player->inHeadSkills(objectName()));
            return true;
        } else if (triggerEvent == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            Card *use_card = Sanguosha->cloneCard(use.card->objectName(), Card::NoSuit, 0);
            use_card->setSkillName("_heg_jilix");
            QList<ServerPlayer *> targets;
            targets << player;
            room->useCard(CardUseStruct(use_card, use.from, targets), false);
        }
        return false;
    }
};

class HJiliXDecrease : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    HJiliXDecrease() : TriggerSkillV2("#heg_jilix-decrease")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    int getPriority(TriggerEvent) const override
    {
        return -2;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->hasSkill("heg_jilix")) {
            Room *room = player->getRoom();
            const QVariant phase = room->historyScopes().value("phase_id");
            if (phase.toULongLong() == 0) return {};
            // DamageInflicted precedes this damage's commit, so only prior injuries are counted.
            const QVariantMap history = room->queryActualDamage({{"phase_id", phase},
                {"to", player->objectName()}, {"limit", 2}});
            if (history.value("complete").toBool()
                && history.value("items").toList().size() == 1)
                return TriggerList{{player, {"heg_jilix"}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return false;
    }
};

HImperialEdict::HImperialEdict(Card::Suit suit, int number)
    : Treasure(suit, number)
{
    setObjectName("ImperialEdict");
}

void HImperialEdict::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
    if (room->getCardPlace(getEffectiveId()) != Player::PlaceTable || targets.isEmpty()) return;

    ServerPlayer *target = targets.first();

    if (target->isDead()) return;

    target->addToPile("ImperialEdict", getEffectiveId());
}

HImperialEdictTrickCard::HImperialEdictTrickCard()
{
    target_fixed = true;
}

void HImperialEdictTrickCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;

    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, player, data);

    LogMessage log;
    log.type = "#InvokeSkill";
    log.from = player;
    log.arg = "ImperialEdict";
    room->sendLog(log);

    thread->trigger(CardUsed, room, player, data);
    thread->trigger(CardFinished, room, player, data);
}

void HImperialEdictTrickCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> pile = source->getPile("ImperialEdict"), to_throw;

    foreach (int id, pile) {
        if (!Sanguosha->getCard(id)->isKindOf("HImperialEdict"))
            to_throw << id;
    }

    DummyCard dummy(to_throw);
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName());
    room->throwCard(&dummy, reason, NULL);

    if (source->isDead()) return;

    QVariantList reservoir = room->getTag("ImperialEdictTrick").toList();
    if (reservoir.isEmpty()) return;
    const int offset = qsanRandomBounded(int(reservoir.length()));
    const int id = reservoir.takeAt(offset).toInt();
    room->setTag("ImperialEdictTrick", reservoir);
    LogMessage log;
    log.type = "$TakeAG";
    log.from = source;
    log.card_str = QString::number(id);
    room->sendLog(log);
    room->setCardMapping(id, nullptr, Player::PlaceWuGu);
    source->obtainCard(Sanguosha->getCard(id));

}

class HImperialEdictTrick : public ViewAsSkillV2
{
public:
    HImperialEdictTrick() : ViewAsSkillV2("heg_imperialedicttrick", 0)
    {
        attached_lord_skill = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        if (player->hasUsed("HImperialEdictTrickCard")) return false;
        QList<int> cards = player->getPile("ImperialEdict");

        QStringList suits;
        foreach (int id, cards) {
            const Card *card = Sanguosha->getCard(id);
            if (card->isKindOf("HImperialEdict")) continue;
            QString suit = card->getSuitString();
            if (!suits.contains(suit))
             suits << suit;
        }

        return suits.length() == 4;
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override
    {
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        return new HImperialEdictTrickCard;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HImperialEdictTrickCard";
    }
};

HImperialEdictAttachCard::HImperialEdictAttachCard()
{
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void HImperialEdictAttachCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;

    ServerPlayer *lord = NULL;
    QList<ServerPlayer *> all_players = room->getAlivePlayers();
    foreach (ServerPlayer *p, all_players) {
        if (!p->getPile("ImperialEdict").isEmpty()) {
            lord = p;
            break;
        }
    }
    if (lord == NULL) return;

    CardUseStruct new_use = card_use;
    new_use.to << lord;

    QVariant data = QVariant::fromValue(new_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, player, data);

    LogMessage log;
    log.type = "#InvokeOthersSkill";
    log.from = player;
    log.to << lord;
    log.arg = "ImperialEdict";
    room->sendLog(log);
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), lord->objectName());

    thread->trigger(CardUsed, room, player, data);
    thread->trigger(CardFinished, room, player, data);
}

void HImperialEdictAttachCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->addToPile("ImperialEdict", getSubcards(), true);
}

class HImperialEdictAttach : public ViewAsSkillV2
{
public:
    HImperialEdictAttach() : ViewAsSkillV2("heg_imperialedictattach", 0)
    {
        attached_lord_skill = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
       if (player->hasUsed("HImperialEdictAttachCard")) return false;
       if (!player->getPile("ImperialEdict").isEmpty()) return true;
       foreach (const Player *lord, player->getAliveSiblings()) {
           if (!lord->getPile("ImperialEdict").isEmpty() && player->isFriendWith(lord))
               return true;
       }
       return false;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }
        int x = 1;
        if (!lordExBigKingdom(request.initiator)) {
            foreach (const Player *p, request.initiator->getAliveSiblings()) {
                if (lordExBigKingdom(p)) {
                    x++;
                    break;
                }
            }
        }
        return !to_select->isEquipped() && selected.length() < x;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return !request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QList<const Card *> cards;
        for (int id : request.selectedCardIds) cards << Sanguosha->getCard(id);
        if (cards.isEmpty()) return NULL;

        HImperialEdictAttachCard *rende_card = new HImperialEdictAttachCard;
        rende_card->addSubcards(cards);
        return rende_card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HImperialEdictAttachCard";
    }
};

class HImperialEdictSkill : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HImperialEdictSkill() : TriggerSkillV2("heg_ImperialEdict")
    {
        events << CardsMoveBatch << GeneralShown << GeneralHidden << Death << DFDebut;
        global = true;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *, QVariant &) const override
    {
        doImperialEdictAttach(room);
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }

private:
    static void doImperialEdictAttach(Room *room)
    {
        QMap<ServerPlayer *, bool> xuanhuo_map;
        QList<ServerPlayer *> players = room->getAlivePlayers(), fazhengs;
        foreach(ServerPlayer *p, players) {
            if (!p->getPile("ImperialEdict").isEmpty())
                fazhengs << p;
        }
        foreach(ServerPlayer *p, players) {
            bool will_attach = false;
            foreach(ServerPlayer *fazheng, fazhengs) {
                if (fazheng->isFriendWith(p)) {
                    will_attach = true;
                    break;
                }
            }
            xuanhuo_map.insert(p, will_attach);
        }
        foreach (ServerPlayer *p, xuanhuo_map.keys()) {
            bool will_attach = xuanhuo_map.value(p, false);
            if (will_attach == p->getAcquiredSkills().contains("heg_imperialedictattach")) continue;

            if (will_attach)
                room->attachSkillToPlayer(p, "heg_imperialedictattach");
            else
                room->detachSkillFromPlayer(p, "heg_imperialedictattach");

        }

        foreach(ServerPlayer *p, players) {
            if (p->getPile("ImperialEdict").isEmpty() && p->getAcquiredSkills().contains("heg_imperialedicttrick"))
                room->detachSkillFromPlayer(p, "heg_imperialedicttrick");
            if (!p->getPile("ImperialEdict").isEmpty() && !p->getAcquiredSkills().contains("heg_imperialedicttrick"))
                room->attachSkillToPlayer(p, "heg_imperialedicttrick");
        }
    }
};

HRuleTheWorld::HRuleTheWorld(Card::Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("rule_the_world");
    target_fixed = false;
}

bool HRuleTheWorld::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || Self->isProhibited(to_select, this, targets)) return false;
    int x = Self->getHp();
    QList<const Player *> players = Self->getAliveSiblings();
    foreach (const Player *p, players) {
        x = qMin(x, p->getHp());
    }
    return to_select->getHp() > x;
}

void HRuleTheWorld::onUse(Room *room, CardUseStruct &use) const
{
    lordExFactionUse(room, use, "wei");
    SingleTargetTrick::onUse(room, use);
}

void HRuleTheWorld::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    QList<ServerPlayer *> players = room->getOtherPlayers(effect.to);

    foreach (ServerPlayer *p, players) {
        if (effect.to->isDead()) break;
        if (p->isDead()) continue;

        bool completeEffect = hasFlag("CompleteEffect") && p->getSeemingKingdom() == "wei";

        QStringList choices, allchoices;

        QString choice1 = QString("slash%to:%1").arg(effect.to->objectName());
        QString choice2 = QString("discard%to:%1").arg(effect.to->objectName());
        if (completeEffect) {
            choice1 = choice1 + "%log: ";
            choice2 = choice2 + "%log:rule_the_world_getcard";
            if (p->canGet(effect.to, "he"))
                choices << choice2;
        } else {
            choice1 = choice1 + "%log:rule_the_world_slash";
            choice2 = choice2 + "%log:rule_the_world_discard";
            if (p->canDiscard(effect.to, "he"))
                choices << choice2;
        }

        if (p->canSlash(effect.to, false))
            choices << choice1;

        if (choices.isEmpty()) continue;

        choices << "cancel";
        allchoices << choice1 << choice2 << "cancel";

        QString choice = lordExChoice(room, p, objectName(), choices.join("+"), QVariant::fromValue(effect.to), QString(), allchoices.join("+"));

        if (choice.startsWith("slash")) {
            if (completeEffect ||room->askForDiscard(p, objectName(), 1, 1, true, false, "@rule_the_world-slash::"+effect.to->objectName())) {
                Slash *slash = new Slash(Card::NoSuit, 0);
                slash->setSkillName("_heg_rule_the_world");
                room->useCard(CardUseStruct(slash, p, effect.to), false);
            }
        }
        if (choice.startsWith("discard")) {
            if (completeEffect && p->canGet(effect.to, "he")) {
                int card_id = room->askForCardChosen(p, effect.to, "he", objectName(), false, Card::MethodGet);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
                room->obtainCard(p, Sanguosha->getCard(card_id), reason, false);
            } else if (!completeEffect && p->canDiscard(effect.to, "he")) {
                int card_id = room->askForCardChosen(p, effect.to, "he", objectName(), false, Card::MethodDiscard);
                room->throwCard(card_id, effect.to, p);
            }
        }
    }
}

HConquering::HConquering(Suit suit, int number)
    : GlobalEffect(suit, number)
{
    setObjectName("conquering");
    target_fixed = false;
}


bool HConquering::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
    return to_select && Self && !Self->isProhibited(to_select, this);
}

void HConquering::onUse(Room *room, CardUseStruct &card_use) const
{
    lordExFactionUse(room, card_use, "shu");
    CardUseStruct use = card_use;


    Card::onUse(room, use);
}

void HConquering::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    if (effect.to->isDead()) return;

    bool completeEffect = hasFlag("CompleteEffect") && effect.to->getSeemingKingdom() == "shu";

    QList<ServerPlayer *> targets, allplayers = room->getAlivePlayers();
    foreach (ServerPlayer *p, allplayers) {
        if (effect.to->canSlash(p))
            targets << p;
    }
    if (!targets.isEmpty()) {

        ServerPlayer *target = room->askForPlayerChosen(effect.to, targets, "conquering-slash", "@conquering-slash", true);
        if (target) {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_heg_conquering");
            CardUseStruct slashUse(slash, effect.to, target);
            if (completeEffect) slash->setFlags("heg_conquering_damage");
            room->useCard(slashUse, false);

            return;
        }
    }

    effect.to->drawCards(completeEffect ? 2 : 1, objectName());
}

HConsolidateCountryGiveCard::HConsolidateCountryGiveCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool HConsolidateCountryGiveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->isFriendWith(to_select) && to_select != Self
        && to_select->getMark("heg_consolidate_arranged") + subcardsLength() <= 2;
}

void HConsolidateCountryGiveCard::onUse(Room *room, CardUseStruct &card_use) const
{
    if (card_use.to.isEmpty()) return;
    ServerPlayer *target = card_use.to.first();
    QStringList arranged = target->getTag("consolidate_country_arrange").toStringList();
    arranged << ListI2S(getSubcards());
    target->setTag("consolidate_country_arrange", arranged);
    // Other viewers need only the recipient capacity, never the selected hand IDs.
    room->setPlayerMark(target, "heg_consolidate_arranged", int(arranged.size()));
}

class HConsolidateCountryGive : public ViewAsSkillV2
{
public:
    HConsolidateCountryGive() : ViewAsSkillV2("heg_consolidatecountrygive", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        return request.pattern == "@@heg_consolidatecountrygive"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }
    QStringList card_list = ListI2S(lordExCards(request.initiator, "consolidate_country_cards"));
        return selected.length() < 2 && card_list.contains(QString::number(to_select->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return !request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QList<const Card *> cards;
        for (int id : request.selectedCardIds) cards << Sanguosha->getCard(id);
        if (cards.isEmpty()) return NULL;
        HConsolidateCountryGiveCard *Lirang_card = new HConsolidateCountryGiveCard;
        Lirang_card->addSubcards(cards);
        return Lirang_card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HConsolidateCountryGiveCard";
    }
};

HConsolidateCountry::HConsolidateCountry(Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("consolidate_country");
    target_fixed = true;
}


void HConsolidateCountry::onUse(Room *room, CardUseStruct &card_use) const
{
    lordExFactionUse(room, card_use, "wu");
    CardUseStruct use = card_use;
    if (use.to.isEmpty())
        use.to << use.from;
    SingleTargetTrick::onUse(room, use);
}

bool HConsolidateCountry::isAvailable(const Player *player) const
{
    return !player->isProhibited(player, this) && TrickCard::isAvailable(player);
}

void HConsolidateCountry::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    effect.to->drawCards(8, objectName());
    if (effect.to->isDead() || effect.to->isKongcheng()) return;

    QList<int> all_cards = effect.to->forceToDiscard(998, false);
    QList<int> to_thrown = effect.to->forceToDiscard(6, false);

    if (to_thrown.isEmpty()) return;


    if (all_cards.length() > to_thrown.length()) {

        QList<int> result = lordExExchange(room, effect.to, objectName(), 998, to_thrown.length(),
        "@consolidate_country-discard", QString(), ListI2S(all_cards).join(","));

        if (!result.isEmpty())
            to_thrown = result;
    }

    QList<CardsMoveStruct> moves;

    if (hasFlag("CompleteEffect") && effect.to->getSeemingKingdom() == "wu") {
        while (!to_thrown.isEmpty()) {

            QList<ServerPlayer *> all_players = room->getOtherPlayers(effect.to);

            bool cant_give = true;
            foreach (ServerPlayer *p, all_players) {
                if (effect.to->isFriendWith(p)) {
                    if (p->getMark("heg_consolidate_arranged") < 2) {
                        cant_give = false;
                        break;
                    }

                }
            }
            if (cant_give) break;

            lordExSetCards(room, effect.to, "consolidate_country_cards", to_thrown);
            const Card *to_give = room->askForUseCard(effect.to, "@@heg_consolidatecountrygive", "@consolidate_country-give");
            lordExSetCards(room, effect.to, "consolidate_country_cards", QList<int>());
            if (to_give == NULL) break;
            foreach (int id, to_give->getSubcards()) {
                to_thrown.removeOne(id);
            }
        }

        QList<ServerPlayer *> alls = room->getAlivePlayers();
        foreach (ServerPlayer *p, alls) {

        QString str = p->getTag("consolidate_country_arrange").toStringList().join("+");
        p->removeTag("consolidate_country_arrange");
            room->setPlayerMark(p, "heg_consolidate_arranged", 0);
            if (str.isEmpty()) continue;
            QStringList arrange_list = str.split("+");

            QList<int> to_arrange;
            foreach (QString id_str, arrange_list) {
                int id = id_str.toInt();
                to_arrange << id;
            }
            if (!to_arrange.isEmpty()) {
                CardsMoveStruct move(to_arrange, p, Player::PlaceHand,
                    CardMoveReason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), p->objectName(), objectName(), QString()));
                moves << move;
            }
        }
    }

    if (!to_thrown.isEmpty()) {
        CardsMoveStruct move(to_thrown, NULL, Player::DiscardPile,
            CardMoveReason(CardMoveReason::S_REASON_THROW, effect.to->objectName(), objectName(), QString()));
        moves << move;
    }

    room->moveCardsAtomic(moves, false);
}

HChaos::HChaos(Suit suit, int number)
    : GlobalEffect(suit, number)
{
    setObjectName("chaos");
}

void HChaos::onUse(Room *room, CardUseStruct &use) const
{
    lordExFactionUse(room, use, "qun");
    GlobalEffect::onUse(room, use);
}

void HChaos::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    if (effect.to->isDead() || effect.to->isKongcheng()) return;

    room->showAllCards(effect.to);

    if (effect.from->isDead()) return;

    QStringList choices, allchoices;

    QString choice1 = QString("letdiscard%to:%1").arg(effect.to->objectName());
    QString choice2 = QString("discard%to:%1").arg(effect.to->objectName());

    QList<const Card *> handcard = effect.to->getHandcards();

    choices << choice1;

    if (effect.from->canDiscard(effect.to, "h"))
        choices << choice2;

    allchoices << choice1 << choice2;

    room->fillAG(effect.to->handCards(), effect.from);
    QString choice = lordExChoice(room, effect.from, objectName(), choices.join("+"), QVariant::fromValue(effect.to), QString(), allchoices.join("+"));
    room->clearAG(effect.from);

    if (choice == choice1) {
        QList<int> to_discard;

        foreach (const Card *card, handcard) {
            if (effect.to->isJilei(card)) continue;
            bool append = true;
            foreach (int id, to_discard) {
                if (Sanguosha->getCard(id)->getTypeId() == card->getTypeId()) {
                    append = false;
                    break;
                }
            }
            if (append)
                to_discard << card->getEffectiveId();
            if (to_discard.length() > 1) break;
        }
        if (!to_discard.isEmpty()) {

            if (to_discard.length() < effect.to->getHandcardNum()) {
                const Card *card = room->askForCard(effect.to, "@@heg_chaosselect!", "@chaos-select", QVariant(), Card::MethodNone);
                if (card != NULL)
                    to_discard = card->getSubcards();
            }

            DummyCard *dummy = new DummyCard(to_discard);
            CardMoveReason mreason(CardMoveReason::S_REASON_THROW, effect.to->objectName(), QString(), objectName(), QString());
            room->throwCard(dummy, mreason, effect.to);
            dummy->deleteLater();
        }
    }

    if (choice == choice2 && effect.from->canDiscard(effect.to, "h")) {
        int card_id = room->askForCardChosen(effect.from, effect.to, "h", objectName(), true, Card::MethodDiscard);
        room->throwCard(card_id, effect.to, effect.from);
    }

    if (hasFlag("CompleteEffect") && effect.to->getSeemingKingdom() == "qun" && effect.to->isKongcheng())
        effect.to->drawCards(qMax(0, effect.to->getHp() - effect.to->getHandcardNum()), objectName());
}

class HChaosSelect : public ViewAsSkillV2
{
public:
    HChaosSelect() : ViewAsSkillV2("heg_chaosselect", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        return request.pattern == "@@heg_chaosselect!"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }
        if (selected.length() > 1 || request.initiator->isJilei(to_select) || to_select->isEquipped()) return false;

        foreach (const Card *card, selected) {
            if (card->getTypeId() == to_select->getTypeId()) return false;
        }

        return true;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        if (request.selectedCardIds.isEmpty()) return false;
        if (request.selectedCardIds.size() == 1) {
            const Card *selected = Sanguosha->getCard(request.selectedCardIds.first());
            for (const Card *card : request.initiator->getHandcards()) {
                if (!request.initiator->isJilei(card) && card->getTypeId() != selected->getTypeId()) return false;
            }
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QList<const Card *> cards;
        for (int id : request.selectedCardIds) cards << Sanguosha->getCard(id);
        if (cards.isEmpty())
            return NULL;
        if (cards.length() == 1) {
            const Card *to_select = cards.first();
            QList<const Card *> cards = request.initiator->getHandcards();
            foreach (const Card *card, cards) {
                if (!request.initiator->isJilei(card) && card->getTypeId() != to_select->getTypeId())
                    return NULL;
            }
        }

        DummyCard *dummy = new DummyCard;
        dummy->addSubcards(cards);
        return dummy;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }
};

class HSuzhi : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HSuzhi() : TriggerSkillV2("heg_suzhi")
    {
        events << DamageCaused << CardUsed << CardsMoveBatch << EventPhaseChanging << EventPhaseStart;
        frequency = Skill::Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
         if (triggerEvent == EventPhaseStart) {
             if (player->getPhase() == Player::RoundStart) {
                 room->detachSkillFromPlayer(player, "heg_fankui_simazhao");
             }
             if (player->getPhase() == Player::NotActive) {
                 QList<ServerPlayer *> allplayers = room->getAlivePlayers();
                 foreach (ServerPlayer *p, allplayers) {
                     room->setPlayerMark(p, "#suzhi", 0);
                 }
             }
         }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->getMark("#suzhi") > 2 || player->getPhase() == Player::NotActive)
            return TriggerList();
        if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))
                    && damage.by_user && !damage.chain && !damage.transfer) {
                return TriggerList{{player, {objectName()}}};
            }
        } else if (triggerEvent == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() == Card::TypeTrick) {
                if (!use.card->isVirtualCard() || use.card->getSubcards().isEmpty())
                    return TriggerList{{player, {objectName()}}};
            }
        } else if (triggerEvent == CardsMoveBatch) {
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from && move.from != player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) && move.to_place == Player::DiscardPile) {
                    if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                        if (player->canGet(move.from, "he"))
                            return TriggerList{{player, {objectName()}}};
                    }
                }
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            if (triggerEvent != EventPhaseChanging) {
                int n = qsanRandomBounded(2) + 1;
                if (triggerEvent == CardUsed)
                    n+=2;
                if (triggerEvent == CardsMoveBatch)
                    n+=4;
                room->broadcastSkillInvoke(objectName(), n, player);
                room->addPlayerMark(player, "#suzhi");
            }
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            damage.damage++;
            data = QVariant::fromValue(damage);
        } else if (triggerEvent == CardUsed)
            player->drawCards(1, objectName());
        else if (triggerEvent == CardsMoveBatch) {
            QList<ServerPlayer *> targets;
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from && move.from != player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) && move.to_place == Player::DiscardPile) {
                    if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                        targets << (ServerPlayer *)move.from;
                    }
                }
            }
            foreach (ServerPlayer *p, targets) {
                if (player->canGet(p, "he")) {
                    int card_id = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodGet);
                    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                    room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
                }
            }
        } else if (triggerEvent == EventPhaseChanging) {
            room->acquireSkill(player, "heg_fankui_simazhao", true, true);
        }
        return false;
    }
};

class HSuzhiTarget : public TargetModSkillV2
{
public:
    HSuzhiTarget() : TargetModSkillV2("#heg_suzhi-target")
    {
        pattern = "TrickCard";
    }

    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        if (from->hasShownSkill("heg_suzhi") && from->getMark("#suzhi") < 3 && from->getPhase() != Player::NotActive) {
            if (!card->isVirtualCard() || card->getSubcards().isEmpty())
                return 1000;
        }
        return 0;
    }    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.primary) return CorrectSkillResult::noEffect();
        int value = 0;
        if (context.modType == TargetModSkill::Residue) value = getResidueNum(context.primary, context.card, context.secondary);
        else if (context.modType == TargetModSkill::DistanceLimit) value = getDistanceLimit(context.primary, context.card, context.secondary);
        return CorrectSkillResult::useAmount(value);
    }
};

class HZhaoxin : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZhaoxin() : TriggerSkillV2("heg_zhaoxin")
    {
        events << Damaged;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && !player->isKongcheng())
            return TriggerList{{player, {objectName()}}};

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            room->showAllCards(player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *first = ctx.invoker;
        Room *room = first->getRoom();

        if (first->isDead() || first->isKongcheng()) return false;

        QList<ServerPlayer *> targets, all_players = room->getAlivePlayers();

        foreach (ServerPlayer *p, all_players) {
            if (p->getHandcardNum() <= first->getHandcardNum() && p != first)
                targets << p;
        }

        if (targets.isEmpty()) return false;

        ServerPlayer *second = room->askForPlayerChosen(first, targets, "zhaoxin-exchange", "@zhaoxin-exchange");

        foreach (ServerPlayer *p, all_players) {
            if (p != first && p != second)
                room->doNotify(p, QSanProtocol::S_COMMAND_EXCHANGE_KNOWN_CARDS,
                JsonArray() << first->objectName() << second->objectName());
        }

        QList<int> handcards1 = first->handCards(), handcards2 = second->handCards();

        CardMoveReason reason1(CardMoveReason::S_REASON_SWAP, first->objectName(), second->objectName(), objectName(), QString());
        CardMoveReason reason2(CardMoveReason::S_REASON_SWAP, first->objectName(), first->objectName(), objectName(), QString());
        CardMoveReason reason3(CardMoveReason::S_REASON_NATURAL_ENTER, QString());

        QList<CardsMoveStruct> move_to_table;
        CardsMoveStruct move1(handcards1, NULL, Player::PlaceTable, reason1);
        CardsMoveStruct move2(handcards2, NULL, Player::PlaceTable, reason2);
        move_to_table.push_back(move2);
        move_to_table.push_back(move1);
        if (!move_to_table.isEmpty()) {
            room->moveCardsAtomic(move_to_table, false);

            QList<CardsMoveStruct> back_move;

            handcards1 = room->getCardIdsOnTable(handcards1);
            handcards2 = room->getCardIdsOnTable(handcards2);

            QList<ServerPlayer *> others = room->getAllPlayers(true), players;
            others.removeOne(first);
            others.removeOne(second);
            players << first;
            players << second;

            if (!handcards2.isEmpty()) {
                if (first->isAlive()) {

                    LogMessage log;
                    log.type = "$MoveCard";
                    log.from = first;
                    log.to << second;
        log.card_str = ListI2S(handcards2).join("+");
                    room->doBroadcastNotify(players, QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());

                    LogMessage log2;
                    log2.type = "#MoveNCards";
                    log2.from = first;
                    log2.to << second;
                    log2.arg = QString::number(handcards2.length());
                    room->doBroadcastNotify(others, QSanProtocol::S_COMMAND_LOG_SKILL, log2.toVariant());

                    CardsMoveStruct move3(handcards2, first, Player::PlaceHand, reason2);
                    back_move.push_back(move3);
                } else {
                    CardsMoveStruct move3(handcards2, NULL, Player::DiscardPile, reason3);
                    back_move.push_back(move3);
                }
            }
            if (!handcards1.isEmpty()) {
                if (second->isAlive()) {

                    LogMessage log;
                    log.type = "$MoveCard";
                    log.from = second;
                    log.to << first;
        log.card_str = ListI2S(handcards1).join("+");
                    room->doBroadcastNotify(players, QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());

                    LogMessage log2;
                    log2.type = "#MoveNCards";
                    log2.from = second;
                    log2.to << first;
                    log2.arg = QString::number(handcards1.length());
                    room->doBroadcastNotify(others, QSanProtocol::S_COMMAND_LOG_SKILL, log2.toVariant());

                    CardsMoveStruct move3(handcards1, second, Player::PlaceHand, reason1);
                    back_move.push_back(move3);
                } else {
                    CardsMoveStruct move3(handcards1, NULL, Player::DiscardPile, reason3);
                    back_move.push_back(move3);
                }
            }

            if (!back_move.isEmpty())
                room->moveCardsAtomic(back_move, false);
        }


        return false;
    }
};

class HShicai : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HShicai() : TriggerSkillV2("heg_shicai")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.damage > 1 && player->forceToDiscard(1, false).isEmpty())
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.damage > 1)
            room->askForDiscard(player, "shicai_discard", 2, 2, false, true);
        else
            player->drawCards(1, objectName());

        return false;
    }
};

class HChenglve : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HChenglve() : TriggerSkillV2("heg_chenglve")
    {
        events << CardFinished;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const
    {
        TriggerList skill_list;
        CardUseStruct use = data.value<CardUseStruct>();
        if (player == NULL || player->isDead()) return skill_list;
        if (use.card->getTypeId() != Card::TypeSkill && use.to.length() > 1) {
            QList<ServerPlayer *> xuyous = room->findPlayersBySkillName(objectName());
            foreach (ServerPlayer *xuyou, xuyous) {
                if (player->isFriendWith(xuyou))
                    skill_list.insert(xuyou, QStringList(objectName()));
            }
        }

        return skill_list;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *xuyou = ctx.owner;
        if (xuyou->askForSkillInvoke(this, QVariant::fromValue(player))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, xuyou->objectName(), player->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *xuyou = ctx.owner;
        ctx.manual_effect = true;
        player->drawCards(1, objectName());
        QStringList damage_record;
        for (const QVariant &entry : room->queryCardUseDamage().value("items").toList())
            damage_record << entry.toMap().value("data").toMap().value("to").toString();

        if (damage_record.contains(xuyou->objectName())) {
            QList<ServerPlayer *> players = room->getAlivePlayers(), targets;
            foreach (ServerPlayer *p, players) {
                if (p->isFriendWith(xuyou) && p->getMark("@companion") + p->getMark("@halfmaxhp") + p->getMark("@firstshow") + p->getMark("@careerist") == 0 && p->hasShownAllGenerals())
                    targets << p;
            }
            if (!targets.isEmpty()) {
                ServerPlayer *target = room->askForPlayerChosen(xuyou, targets, "chenglve_mark", "@chenglve-mark", true);
                if (target) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, xuyou->objectName(), target->objectName());
                    room->addPlayerMark(target, "@halfmaxhp");
                }
            }
        }
        return false;
    }
};

class HBaolie : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HBaolie() : TriggerSkillV2("heg_baolie")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Play && player->hasShownOneGeneral()) {
            QList<ServerPlayer *> players = room->getAlivePlayers();
            foreach (ServerPlayer *p, players) {
                if (p->hasShownOneGeneral() && !p->isFriendWith(player) && p->inMyAttackRange(player))
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        QList<ServerPlayer *> players = room->getAlivePlayers(), targets;
        room->sortByActionOrder(players);
        foreach (ServerPlayer *p, players) {
            if (p->hasShownOneGeneral() && !p->isFriendWith(player) && p->inMyAttackRange(player)) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
                targets << p;
            }
        }
        foreach (ServerPlayer *p, targets) {
            if (player->isDead()) break;
            if (p->isDead()) continue;
            if (!p->canSlash(player) || !room->askForUseSlashTo(p, player, "@baolie-slash:" + player->objectName())) {
                if (player->canDiscard(p, "he")) {
                    room->throwCard(room->askForCardChosen(player, p, "he", "heg_baolie", false, Card::MethodDiscard), p, player);
                }
            }
        }
        return false;
    }
};

class HBaolieTargetMod : public TargetModSkillV2
{
public:
    HBaolieTargetMod() : TargetModSkillV2("#heg_baolie-target")
    {
        pattern = "Slash";
    }

    virtual int getResidueNum(const Player *from, const Card *card, const Player *to) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card) || !from->hasShownSkill("heg_baolie"))
            return 0;

        if (to && to->getHp() >= from->getHp())
            return 10000;

        return 0;
    }

    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *to) const
    {
        return getResidueNum(from, card, to);
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.primary) return CorrectSkillResult::noEffect();
        int value = 0;
        if (context.modType == TargetModSkill::Residue) value = getResidueNum(context.primary, context.card, context.secondary);
        else if (context.modType == TargetModSkill::DistanceLimit) value = getDistanceLimit(context.primary, context.card, context.secondary);
        return CorrectSkillResult::useAmount(value);
    }
};


class HAocaiViewAsSkill : public ViewAsSkillV2
{
public:
    HAocaiViewAsSkill() : ViewAsSkillV2("heg_aocai", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && !(request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN && request.pattern.startsWith("@@"))) return false;
        const QString &pattern = request.pattern;
        if (player->getPhase() != Player::NotActive || player->hasFlag("Global_AocaiFailed")) return false;
        return pattern == "slash" || pattern == "jink" || pattern == "peach" || pattern.contains("analeptic");
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override
    {
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HAocaiCard *aocai_card = new HAocaiCard;
        aocai_card->setUserString(request.pattern);
        aocai_card->setShowSkill(objectName());
        return aocai_card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HAocaiCard";
    }
};

class HAocai : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HAocai() : TriggerSkillV2("heg_aocai")
    {
        view_as_skill = new HAocaiViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }


    static bool cheak(ServerPlayer *player, int id)
    {
        const Card *card = Sanguosha->getCard(id);

        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            return !player->isCardLimited(card, Card::MethodUse);
        else if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
            return !player->isCardLimited(card, Card::MethodResponse);

        return false;
    }

    static int view(Room *room, ServerPlayer *player, QList<int> &ids, QList<int> &enabled)
    {
        int result = -1;

        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = player;
        log.card_str = ListI2S(ids).join("+");
        room->sendLog(log, player);

        player->broadcastSkillInvoke("heg_aocai");
        room->notifySkillInvoked(player, "heg_aocai");

        QList<int> disabled = ids;
        for (int id : enabled) disabled.removeOne(id);
        room->fillAG(ids, player, disabled);
        if (!enabled.isEmpty()) result = room->askForAG(player, enabled, true, "heg_aocai");
        room->clearAG(player);
        if (result < 0) room->setPlayerFlag(player, "Global_AocaiFailed");
        room->returnToTopDrawPile(ids);
        return result;
    }
};

HAocaiCard::HAocaiCard()
{
}

bool HAocaiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    QScopedPointer<Card> card;
    if (!user_string.isEmpty())
        card.reset(Sanguosha->cloneCard(user_string.split("+").first()));
    return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card.data(), targets);
}

bool HAocaiCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
        return true;

    QScopedPointer<Card> card;
    if (!user_string.isEmpty())
        card.reset(Sanguosha->cloneCard(user_string.split("+").first()));
    return card && card->targetFixed();
}

bool HAocaiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    QScopedPointer<Card> card;
    if (!user_string.isEmpty())
        card.reset(Sanguosha->cloneCard(user_string.split("+").first()));
    return card && card->targetsFeasible(targets, Self);
}

const Card *HAocaiCard::validateInResponse(ServerPlayer *user) const
{
    Room *room = user->getRoom();

    LogMessage log;
    log.type = "#InvokeSkill";
    log.from = user;
    log.arg = "heg_aocai";
    room->sendLog(log);

    QString card_names = toString().split(":").last();
    QStringList names = card_names.split("+");
    if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

    QList<int> ids = room->getNCards(2, false), enabled;
    foreach (int id, ids) {
        if (HAocai::cheak(user, id) && names.contains(Sanguosha->getCard(id)->objectName()))
            enabled << id;
    }

    int id = HAocai::view(room, user, ids, enabled);
    return id >= 0 ? Sanguosha->getCard(id) : nullptr;
}

const Card *HAocaiCard::validate(CardUseStruct &cardUse) const
{
    ServerPlayer *user = cardUse.from;
    Room *room = user->getRoom();

    LogMessage log;
    log.from = user;
    log.to = cardUse.to;
    log.type = "#UseCard";
    log.card_str = toString();
    room->sendLog(log);

    QList<int> ids = room->getNCards(2, false);

    QString card_names = toString().split(":").last();
    QStringList names = card_names.split("+");
    if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

    QList<int> enabled;
    foreach (int id, ids)
        if (HAocai::cheak(user, id) && names.contains(Sanguosha->getCard(id)->objectName()))
            enabled << id;

    int id = HAocai::view(room, user, ids, enabled);

    return id >= 0 ? Sanguosha->getCard(id) : nullptr;
}

HDuwuCard::HDuwuCard()
{
    mute = true;
    target_fixed = true;
}

void HDuwuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *source = card_use.from;
    room->setPlayerMark(source, "@duwu", 0);
    room->broadcastSkillInvoke("heg_duwu", source);
    room->doSuperLightbox("heg_zhugeke", "heg_duwu");

    CardUseStruct use = card_use;
    QList<ServerPlayer *> all_players = room->getAlivePlayers();

    foreach (ServerPlayer *p, all_players) {
        if (source->inMyAttackRange(p) && !source->willBeFriendWith(p))
            use.to << p;
    }

    SkillCard::onUse(room, use);
}

void HDuwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->setPlayerFlag(source, "DuwuUsing");
    int index = source->startCommand("heg_duwu");

    foreach (ServerPlayer *p, targets) {
        if (source->isDead()) break;
        if (p->isAlive() && !p->doCommand("heg_duwu", index, source)) {
            room->damage(DamageStruct("heg_duwu", source, p));
            source->drawCards(1, objectName());
        }
    }
    QStringList list = source->property("duwu_targets").toString().split("+");
    foreach (QString player_name, list) {
        ServerPlayer *p = room->findPlayerByObjectName(player_name);
        if (p && p->isAlive()) {
            room->loseHp(source);
            break;
        }
    }
    room->setPlayerProperty(source, "duwu_targets", QVariant());

    room->setPlayerFlag(source, "-DuwuUsing");

}

class HDuwuViewAsSkill : public ViewAsSkillV2
{
public:
    HDuwuViewAsSkill() : ViewAsSkillV2("heg_duwu", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->getMark("@duwu") > 0;
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override
    {
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HDuwuCard *card = new HDuwuCard;
        card->setShowSkill(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HDuwuCard";
    }
};

class HDuwu : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HDuwu() : TriggerSkillV2("heg_duwu")
    {
        frequency = Limited;
        limit_mark = "@duwu";
        view_as_skill = new HDuwuViewAsSkill;
        events << Dying;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player->hasFlag("DuwuUsing")) {
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who) {
                QStringList list = player->property("duwu_targets").toString().split("+");
                list << dying.who->objectName();
                room->setPlayerProperty(player, "duwu_targets", list.join("+"));
            }
        }

        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }
};

class HShiluViewAsSkill : public ViewAsSkillV2
{
public:
    HShiluViewAsSkill() : ViewAsSkillV2("heg_shilu", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        return request.pattern == "@@heg_shilu"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }
        return !request.initiator->isJilei(to_select) && selected.length() < request.initiator->getGeneralPile("massacre").length();
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return !request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QList<const Card *> cards;
        for (int id : request.selectedCardIds) cards << Sanguosha->getCard(id);
        if (cards.isEmpty()) return NULL;
        DummyCard *dummy = new DummyCard;
        dummy->addSubcards(cards);
        return dummy;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }
};

class HShilu : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    HShilu() : TriggerSkillV2("heg_shilu")
    {
        events << BuryVictim << EventPhaseStart;
        // Finish burial/rewards (GameRule priority -1) before collecting the deceased generals.
        view_as_skill = new HShiluViewAsSkill;
    }

    int getPriority(TriggerEvent event) const override
    { return event == BuryVictim ? -2 : 3; }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != BuryVictim)
            return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
                && player->getPhase() == Player::Start && !player->isNude()
                && !player->getGeneralPile("massacre").isEmpty()
                ? TriggerList{{player, {objectName()}}} : TriggerList();

        TriggerList result;
        const DeathStruct death = data.value<DeathStruct>();
        if (!death.who || death.who->isAlive()) return result;
        const General *head = death.who->getGeneral();
        const General *deputy = death.who->getGeneral2();
        if ((!head || head->objectName().contains("sujiang"))
            && (!deputy || deputy->objectName().contains("sujiang"))) return result;

        // Burial is dispatched to the victim; owner-indexed candidates retain each
        // surviving holder's exact source, and the effect uses ctx.owner.
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if ((owner && owner->isAlive() && owner->hasSkill(objectName())))
                result.insert(owner, QStringList(objectName()));
        }
        return result;
    }



    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        if (triggerEvent == BuryVictim) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who && player->askForSkillInvoke(this, QVariant::fromValue(death.who))) {
                int n = qsanRandomBounded(2) + 1;
                room->broadcastSkillInvoke(objectName(), n, player);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), death.who->objectName());
                return true;
            }
        } else if (triggerEvent == EventPhaseStart) {
            int x = player->getGeneralPile("massacre").length();
            const Card *card = room->askForCard(player, "@@heg_shilu", "@shilu:::"+QString::number(x), data, Card::MethodNone);
            if (card != NULL) {
                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName(), 3, player);
                room->throwCard(card, objectName(), player);
                QStringList n_list = player->getTag("shilu_count").toStringList();
                n_list.append(QString::number(card->subcardsLength()));
                player->setTag("shilu_count", n_list);
                return true;
            }

        }

        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        if (triggerEvent == BuryVictim) {
            DeathStruct death = data.value<DeathStruct>();
            ServerPlayer *target = death.who;

            int x = (death.damage && death.damage->from == player) ? 2:0;

            QStringList generals;
            if (!target->getGeneral()->objectName().contains("sujiang")) {
                QString name = target->getGeneral()->objectName();
                generals << name;
            }

            if (target->getGeneral2() && !target->getGeneral2()->objectName().contains("sujiang")) {
                QString name = target->getGeneral2()->objectName();
                generals << name;
            }

            if (x > 0) {
                QStringList available, all = Sanguosha->getLimitedGeneralNames();

                foreach (QString name, all) {
                    if (!name.startsWith("heg_lord_") && !lordExUsedGenerals(room).contains(name)) {
                        const General *general = Sanguosha->getGeneral(name);
                        if (general && !general->isDoubleKingdoms() && general->getKingdom() != "careerist")
                        available << name;
                    }
                }

                if (!available.isEmpty()) {

    qsanShuffle(available);

                    int n = qMin(x, int(available.length()));
                    QStringList acquired = available.mid(0, n);

                    foreach (QString name, acquired) {
                        generals << name;
                    }

                }
            }

            player->addGeneralToPile("massacre", generals);

        } else if (triggerEvent == EventPhaseStart) {
            QStringList effect_list = player->getTag("shilu_count").toStringList();
            QString effect_name = effect_list.takeLast();
            player->setTag("shilu_count", effect_list);
            int x = effect_name.toInt();
            if (x > 0)
                player->drawCards(x, objectName());
        }

        return false;
    }
};

class HXiongnve : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXiongnve() : TriggerSkillV2("heg_xiongnve")
    {
        events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::RoundStart)
            room->setPlayerMark(player, "##xiongnve_avoid", 0);
        else if (triggerEvent == EventPhaseChanging) {
            room->setPlayerProperty(player, "xiongnve_adddamage", QVariant());
            room->setPlayerProperty(player, "xiongnve_extraction", QVariant());
            room->setPlayerProperty(player, "xiongnve_nolimit", QVariant());
            room->setPlayerMark(player, "##xiongnve", 0);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && !player->getGeneralPile("massacre").isEmpty() && player->getPhase() == Player::Play) {
            if (triggerEvent == EventPhaseEnd && player->getGeneralPile("massacre").length() < 2) return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        if (triggerEvent == EventPhaseStart) {
            if (player->askForSkillInvoke(this, "attack")) {
                room->broadcastSkillInvoke(objectName(), 1, player);
                return true;
            }
        } else if (triggerEvent == EventPhaseEnd) {
            if (player->askForSkillInvoke(this, "defence")) {
                room->broadcastSkillInvoke(objectName(), 6, player);
                return true;
            }
        }

        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QStringList huashens = player->getGeneralPile("massacre");

        if (triggerEvent == EventPhaseStart) {

            QString name = room->askForGeneral(player, huashens, QString(), true, "xiongnve_attack");

            LogMessage log;
            log.type = "#dropMassacreDetail";
            log.from = player;
            log.arg = name;
            room->sendLog(log);

            player->removeGeneralFromPile("massacre", name);

            const General *general = Sanguosha->getGeneral(name);
            if (general == NULL) return false;
            QStringList g_kingdoms = general->getKingdoms().split("+", Qt::SkipEmptyParts);

            QString choice = lordExChoice(room, player, objectName(), "adddamage+extraction+nolimit", QVariant(), "@xiongnve-choice");
            if (choice == "adddamage") {
                foreach (QString kingdom, g_kingdoms) {
                    LogMessage log;
                    log.type = "#xiongnveAdddamage";
                    log.from = player;
                    log.arg = kingdom;
                    room->sendLog(log);
                }
                QStringList kingdoms = player->property("xiongnve_adddamage").toString().split("+");
                kingdoms << g_kingdoms;
                room->setPlayerProperty(player, "xiongnve_adddamage", kingdoms.join("+"));
            } else if (choice == "extraction") {
                foreach (QString kingdom, g_kingdoms) {
                    LogMessage log;
                    log.type = "#xiongnveExtraction";
                    log.from = player;
                    log.arg = kingdom;
                    room->sendLog(log);
                }
                QStringList kingdoms = player->property("xiongnve_extraction").toString().split("+");
                kingdoms << g_kingdoms;
                room->setPlayerProperty(player, "xiongnve_extraction", kingdoms.join("+"));
            } else if (choice == "nolimit") {
                foreach (QString kingdom, g_kingdoms) {
                    LogMessage log;
                    log.type = "#xiongnveNolimit";
                    log.from = player;
                    log.arg = kingdom;
                    room->sendLog(log);
                }
                QStringList kingdoms = player->property("xiongnve_nolimit").toString().split("+");
                kingdoms << g_kingdoms;
                room->setPlayerProperty(player, "xiongnve_nolimit", kingdoms.join("+"));
            }

            room->addPlayerMark(player, "##xiongnve");

        } else if (triggerEvent == EventPhaseEnd && huashens.length() > 1) {

            QString name = room->askForGeneral(player, huashens, QString(), true, "xiongnve_defence");
            LogMessage log;
            log.type = "#dropMassacreDetail";
            log.from = player;
            log.arg = name;
            room->sendLog(log);
            huashens.removeOne(name);

            player->removeGeneralFromPile("massacre", name);

            name = room->askForGeneral(player, huashens, QString(), true, "xiongnve_defence");
            log.arg = name;
            room->sendLog(log);
            huashens.removeOne(name);

            player->removeGeneralFromPile("massacre", name);

            room->addPlayerMark(player, "##xiongnve_avoid");
        }

        return false;
    }
};

class HXiongnveEffect : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXiongnveEffect() : TriggerSkillV2("#heg_xiongnve-effect")
    {
        events << DamageCaused << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (triggerEvent == DamageCaused) {
            if (damage.to && damage.to->isAlive() && damage.to->hasShownOneGeneral()) {
                QStringList kingdoms1 = player->property("xiongnve_adddamage").toString().split("+"),
                        kingdoms2 = player->property("xiongnve_extraction").toString().split("+");
                if ((kingdoms1.contains(damage.to->getSeemingKingdom())) ||
                        (kingdoms2.contains(damage.to->getSeemingKingdom()) && player->canGet(damage.to, "he")))
                    return TriggerList{{player, {objectName()}}};
            }
        } else if (triggerEvent == DamageInflicted) {
            if (damage.from && damage.from != player && player->getMark("##xiongnve_avoid") > 0)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        if (triggerEvent == DamageCaused) {
            QStringList kingdoms1 = player->property("xiongnve_adddamage").toString().split("+"),
                    kingdoms2 = player->property("xiongnve_extraction").toString().split("+");
            if (kingdoms1.contains(damage.to->getSeemingKingdom())) {
                room->broadcastSkillInvoke("heg_xiongnve", qsanRandomBounded(2) + 2, player);
                damage.damage++;
                data = QVariant::fromValue(damage);
            }

            if (kingdoms2.contains(damage.to->getSeemingKingdom()) && player->canGet(damage.to, "he")) {
                room->broadcastSkillInvoke("heg_xiongnve", qsanRandomBounded(2) + 4, player);
                int card_id = room->askForCardChosen(player, damage.to, "he", "heg_xiongnve", false, Card::MethodGet);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
            }
        } if (triggerEvent == DamageInflicted) {
            room->broadcastSkillInvoke("heg_xiongnve", 7, player);
            damage.damage--;
            data = QVariant::fromValue(damage);

            if (damage.damage <= 0)
                return true;
        }
        return false;
    }
};

class HXiongnveTarget : public TargetModSkillV2
{
public:
    HXiongnveTarget() : TargetModSkillV2("#heg_xiongnve-target")
    {
        pattern = "^SkillCard";
    }

    virtual int getResidueNum(const Player *from, const Card *card, const Player *to) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        QStringList kingdoms = from->property("xiongnve_nolimit").toString().split("+");

        if (to && to->hasShownOneGeneral() && kingdoms.contains(to->getSeemingKingdom()))
            return 1000;

        return 0;
    }    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.primary) return CorrectSkillResult::noEffect();
        int value = 0;
        if (context.modType == TargetModSkill::Residue) value = getResidueNum(context.primary, context.card, context.secondary);
        else if (context.modType == TargetModSkill::DistanceLimit) value = getDistanceLimit(context.primary, context.card, context.secondary);
        return CorrectSkillResult::useAmount(value);
    }
};



class HCongcha : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HCongcha() : TriggerSkillV2("heg_congcha")
    {
        events << DrawNCards << EventPhaseStart;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::RoundStart) {
            player->removeTag("congcha_targets");
            QList<ServerPlayer *> alls = room->getAlivePlayers();
            foreach (ServerPlayer *p1, alls) {
                bool remove_mark = true;
                foreach (ServerPlayer *p2, alls) {
                    QStringList list = p2->getTag("congcha_targets").toStringList();
                    if (list.contains(p1->objectName())) {
                        remove_mark = false;
                        break;
                    }
                }
                if (remove_mark)
                    room->setPlayerMark(p1, "##congcha", 0);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == DrawNCards && data.value<DrawStruct>().reason != "draw_phase") return {};
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (triggerEvent == DrawNCards) {
            QList<ServerPlayer *> allplayers = room->getAlivePlayers();
            foreach (ServerPlayer *p, allplayers) {
                if (!p->hasShownOneGeneral())
                    return TriggerList();
            }
            return TriggerList{{player, {objectName()}}};
        } else if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Start) {
            QList<ServerPlayer *> allplayers = room->getOtherPlayers(player);
            foreach (ServerPlayer *p, allplayers) {
                if (!p->hasShownOneGeneral())
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        if (triggerEvent == DrawNCards) {
            if (player->askForSkillInvoke(this)) {
                room->broadcastSkillInvoke(objectName(), player);
                return true;
            }
        } else if (triggerEvent == EventPhaseStart) {
            QList<ServerPlayer *> to_choose, allplayers = room->getOtherPlayers(player);
            foreach (ServerPlayer *p, allplayers) {
                if (!p->hasShownOneGeneral())
                    to_choose << p;
            }
            if (to_choose.isEmpty()) return false;

            ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "@congcha-target", true, true);
            if (to != NULL) {
                room->broadcastSkillInvoke(objectName(), player);

                QStringList target_list = player->getTag("congcha_vic").toStringList();
                target_list.append(to->objectName());
                player->setTag("congcha_vic", target_list);
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (triggerEvent == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
            draw.num += 2;
            data = QVariant::fromValue(draw);
        } else if (triggerEvent == EventPhaseStart) {
            QStringList target_list = player->getTag("congcha_vic").toStringList();
            QString target_name = target_list.takeLast();
            player->setTag("congcha_vic", target_list);
            ServerPlayer *to = room->findPlayerByObjectName(target_name);
            if (to && !to->hasShownOneGeneral()) {
                room->addPlayerMark(to, "##congcha");
                QStringList list = player->getTag("congcha_targets").toStringList();
                list << to->objectName();
                player->setTag("congcha_targets", list);
            }
        }
        return false;
    }
};

class HCongchaEffect : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HCongchaEffect() : TriggerSkillV2("#heg_congcha-effect")
    {
        events << GeneralShowed;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const
    {
        TriggerList skill_list;
        if (player->isDead()) return skill_list;
        QList<ServerPlayer *> owners = room->getAlivePlayers();
        foreach (ServerPlayer *owner, owners) {
            QStringList target_list = owner->getTag("congcha_targets").toStringList();
            if (target_list.contains(player->objectName()))
                skill_list.insert(owner, QStringList(objectName()));
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *panjun = ctx.owner;
        ctx.manual_effect = true;
        QStringList target_list = panjun->getTag("congcha_targets").toStringList();
        target_list.removeAll(player->objectName());
        panjun->setTag("congcha_targets", target_list);

        bool remove_mark = true;
        QList<ServerPlayer *> alls = room->getAlivePlayers();
        foreach (ServerPlayer *p, alls) {
            QStringList list = p->getTag("congcha_targets").toStringList();
            if (list.contains(player->objectName())) {
                remove_mark = false;
                break;
            }
        }
        if (remove_mark)
            room->setPlayerMark(player, "##congcha", 0);

        if (panjun->isFriendWith(player)) {
            QList<ServerPlayer *> players;
            players << player << panjun;
            room->sortByActionOrder(players);
            foreach (ServerPlayer *p, players) {
                if (p->isAlive())
                    p->drawCards(2, "heg_congcha");
            }

        } else
            room->loseHp(player);

        return false;
    }
};

class HGongqing : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HGongqing() : TriggerSkillV2("heg_gongqing")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from->isAlive() && damage.from->getAttackRange() > 3)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->isAlive()) {
            int x = damage.from->getAttackRange();
            if (x > 3)
                damage.damage++;
            else if (x < 3)
                damage.damage = 1;
            data = QVariant::fromValue(damage);
        }

        return false;
    }
};

class HGongqingDecrease : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    HGongqingDecrease() : TriggerSkillV2("#heg_gongqing-decrease")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    int getPriority(TriggerEvent) const override
    {
        return -2;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player && player->isAlive() && player->hasSkill("heg_gongqing")) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from->isAlive() && damage.from->getAttackRange() < 3 && damage.damage > 1)
                return TriggerList{{player, {"heg_gongqing"}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return false;
    }
};

HJinfaCard::HJinfaCard()
{

}

bool HJinfaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isNude();
}

void HJinfaCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    QList<int> result = lordExExchange(room, target, "_heg_jinfa", 1, 0, "@jinfa-give:"+ source->objectName(), "", "EquipCard");
    if (result.isEmpty()) {
        if (source->canGet(target, "he")) {
            int card_id = room->askForCardChosen(source, target, "he", "heg_jinfa", false, Card::MethodGet);
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
            room->obtainCard(source, Sanguosha->getCard(card_id), reason, false);
        }
    } else {
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), source->objectName(), "heg_jinfa", QString());
        reason.m_playerId = source->objectName();

        CardsMoveStruct give_move(result, source, Player::PlaceHand, reason);
        const QVariant moved = room->moveCardsSub(give_move, true);
        bool is_spade = false;
        for (const QVariant &entry : moved.toList()) {
            const CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.to != source) continue;
            for (int id : move.card_ids)
                if (Sanguosha->getCard(id)->getSuit() == Card::Spade && room->getCardOwner(id) == source
                    && room->getCardPlace(id) == Player::PlaceHand) is_spade = true;
        }

        if (is_spade && target->canSlash(source, false)) {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_heg_jinfa");
            room->useCard(CardUseStruct(slash, target, source), false);
        }
    }
}

class HJinfa : public ViewAsSkillV2
{
public:
    HJinfa() : ViewAsSkillV2("heg_jinfa", 1)
    {
    }

    virtual int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return (card->getTypeId() == Card::TypeSkill) ? -1 : 0;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HJinfaCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        if (request.initiator->isJilei(to_select)) return false;
        return Sanguosha->matchExpPattern(".", request.initiator, to_select);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator
            || request.selectedCardIds.size() != 1) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HJinfaCard *skill_card = new HJinfaCard;
        skill_card->addSubcard(originalCard);
        skill_card->setShowSkill(objectName());
        return skill_card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HJinfaCard";
    }
};

class HXishe : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXishe() : TriggerSkillV2("heg_xishe")
    {
        events << EventPhaseStart << Death;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.damage && death.damage->card && death.damage->card->getSkillName() == objectName() && death.damage->from == player) {
                room->setPlayerFlag(player, "xisheKilledPlayer");
            }
        }
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Start && player->isAlive()) {
            QList<ServerPlayer *> owners = room->findPlayersBySkillName(objectName());
            TriggerList skill_list;
            foreach (ServerPlayer *owner, owners)
                if (owner != player && owner->hasEquip())
                    skill_list.insert(owner, QStringList(objectName()));
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *huangzu = ctx.owner;
        if (room->askForCard(huangzu, "heg_.|.|.|equipped", "@xishe-slash:"+player->objectName(), data, Card::MethodDiscard, NULL, false, "heg_xishe"))
            return true;
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *huangzu = ctx.owner;
        ctx.manual_effect = true;
        do {
            if (huangzu->canSlash(player, false)) {
                Slash *slash = new Slash(Card::NoSuit, 0);
                slash->setSkillName("_heg_xishe");
                if (player->getHp() < huangzu->getHp())
                    slash->setFlags("GlobalCardUseDisresponsive");
                room->useCard(CardUseStruct(slash, huangzu, player), false);
            }
        } while(huangzu->isAlive() && player->isAlive() && huangzu->hasEquip() &&
                room->askForCard(huangzu, "heg_.|.|.|equipped", "@xishe-slash:"+player->objectName()));

        return false;
    }
};

class HXisheTransform : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXisheTransform() : TriggerSkillV2("#heg_xishe-transform")
    {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *, QVariant &data) const
    {
        if (data.value<PhaseChangeStruct>().to == Player::NotActive) {
            QList<ServerPlayer *> owners = room->getAlivePlayers();
            TriggerList skill_list;
            foreach (ServerPlayer *owner, owners)
                if (owner->hasFlag("xisheKilledPlayer") && owner->getMark("xishetransformUsed") == 0 && owner->canTransform())
                    skill_list.insert(owner, QStringList(objectName()));
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *huangzu = ctx.owner;
        ctx.manual_effect = true;
        if (lordExChoice(room, huangzu, "transform_xishe", "yes+no", QVariant(), "@transform-ask:::xishe") == "yes") {
            room->broadcastSkillInvoke("transform", huangzu->isMale(), -1);
            room->addPlayerMark(huangzu, "xishetransformUsed");
            room->transformDeputyGeneral(huangzu, QString(), false);
        }
        return false;
    }
};

HHuaiyiCard::HHuaiyiCard()
{
    target_fixed = true;
}

void HHuaiyiCard::extraCost(Room *room, const CardUseStruct &card_use) const
{
    room->showAllCards(card_use.from);
}

void HHuaiyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> blacks;
    QList<int> reds;
    foreach (const Card *c, source->getHandcards()) {
        if (source->isJilei(c)) continue;
        if (c->isRed())
            reds << c->getId();
        else
            blacks << c->getId();
    }

    if (reds.isEmpty() || blacks.isEmpty()) return;

    QString to_discard = lordExChoice(room, source, "heg_huaiyi", "black+red", QVariant(), "@huaiyi-choose");

    QList<int> *pile = NULL;
    if (to_discard == "black")
        pile = &blacks;
    else
        pile = &reds;

    int n = pile->length();

    DummyCard dm(*pile);
    room->throwCard(&dm, source);

    QList<ServerPlayer *> to_choose;
    foreach(ServerPlayer *p, room->getOtherPlayers(source)) {
        if (!p->isNude())
            to_choose << p;
    }

    if (to_choose.isEmpty()) return;

    QList<ServerPlayer *> choosees = room->askForPlayersChosen(source, to_choose, "huaiyi_snatch", 0, n, "@huaiyi-snatch:::"+QString::number(n));

    if (choosees.isEmpty()) return;

    room->sortByActionOrder(choosees);

    foreach (ServerPlayer *to, choosees) {
        if (source->isAlive() && to->isAlive() && !to->isNude()) {
            int card_id = room->askForCardChosen(source, to, "he", "heg_huaiyi", false, Card::MethodNone);
            if (Sanguosha->getCard(card_id)->getTypeId() == Card::TypeEquip)
                source->addToPile("disloyalty", card_id);
            else {
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
                room->obtainCard(source, Sanguosha->getCard(card_id), reason, false);
            }
        }
    }
}

class HHuaiyi : public ViewAsSkillV2
{
public:
    HHuaiyi() : ViewAsSkillV2("heg_huaiyi", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HHuaiyiCard");
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override
    {
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HHuaiyiCard *skill_card = new HHuaiyiCard;
        skill_card->setShowSkill(objectName());
        return skill_card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HHuaiyiCard";
    }
};

class HZisui : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZisui() : TriggerSkillV2("heg_zisui")
    {
        events << DrawNCards << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == DrawNCards && data.value<DrawStruct>().reason != "draw_phase") return {};
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if ((triggerEvent == DrawNCards && !player->getPile("disloyalty").isEmpty()) ||
                (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish
                 && player->getPile("disloyalty").length() > player->getMaxHp())) {
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            if (triggerEvent == DrawNCards)
                room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (triggerEvent == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
            draw.num += player->getPile("disloyalty").length();
            data = QVariant::fromValue(draw);
        } else if (triggerEvent == EventPhaseStart)
            room->killPlayer(player);
        return false;
    }
};

class HLianpian : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLianpian() : TriggerSkillV2("heg_lianpian")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Finish && lordExDiscardCount(player, false) > player->getHp())
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        QList<ServerPlayer *> to_choose, all_players = room->getAlivePlayers();
        foreach (ServerPlayer *p, all_players) {
            if (player->isFriendWith(p))
                to_choose << p;
        }
        if (to_choose.isEmpty()) return false;

        ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "@lianpian-target", true, true);
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);

            QStringList target_list = player->getTag("lianpian_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("lianpian_target", target_list);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        QStringList target_list = player->getTag("lianpian_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("lianpian_target", target_list);
        ServerPlayer *to = room->findPlayerByObjectName(target_name);

        if (to)
            to->drawCards(qMax(0, to->getMaxHp() - to->getHandcardNum()), objectName());

        return false;
    }
};


class HLianpianOther : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLianpianOther() : TriggerSkillV2("#heg_lianpian-other")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Finish && player->isAlive()) {
            QList<ServerPlayer *> owners = room->findPlayersBySkillName("heg_lianpian");
            TriggerList skill_list;
            foreach (ServerPlayer *owner, owners)
                if (owner->hasShownSkill("heg_lianpian") && player != owner && lordExDiscardCount(player, false) > owner->getHp()
                        && (owner->isWounded() || player->canDiscard(owner, "he")))
                    skill_list.insert(owner, QStringList(objectName()));
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        QStringList choices;
        if (ask_who->isWounded()) choices << "recover";
        if (player->canDiscard(ask_who, "he")) choices << "discard";
        if (choices.isEmpty()) return false;
        choices << "cancel";
        QString all_choices = "recover+discard+cancel";


        QString choice = lordExChoice(room, player, "heg_lianpian", choices.join("+"), data, "@lianpian:" + ask_who->objectName(), all_choices);
        if (choice == "cancel") return false;

        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = player;
        log.to << ask_who;
        log.arg = "heg_lianpian";
        room->sendLog(log);
        room->broadcastSkillInvoke("heg_lianpian", ask_who);
        room->notifySkillInvoked(ask_who, "heg_lianpian");
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), player->objectName());

        QStringList choice_list = ask_who->getTag("lianpian_choice").toStringList();
        choice_list.append(choice);
        ask_who->setTag("lianpian_choice", choice_list);

        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        ctx.manual_effect = true;
        QStringList choice_list = ask_who->getTag("lianpian_choice").toStringList();
        QString choice = choice_list.takeLast();
        ask_who->setTag("lianpian_choice", choice_list);
        if (choice == "recover") {
            RecoverStruct recover;
            recover.who = player;
            room->recover(ask_who, recover);
        } else if (choice == "discard" && player->canDiscard(ask_who, "he")) {
            int card_id = room->askForCardChosen(player, ask_who, "he", "heg_lianpian", false, Card::MethodDiscard);
            room->throwCard(card_id, ask_who, player);
        }
        return false;
    }
};

class HTongdu : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HTongdu() : TriggerSkillV2("heg_tongdu")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Finish && lordExDiscardCount(player, true) > 0)
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        player->drawCards(qMin(lordExDiscardCount(player, true), 3), objectName());
        return false;
    }
};

class HTongduOther : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HTongduOther() : TriggerSkillV2("#heg_tongdu-other")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Finish && lordExDiscardCount(player, true) > 0) {
            QList<ServerPlayer *> owners = room->findPlayersBySkillName("heg_tongdu");
            TriggerList skill_list;
            foreach (ServerPlayer *owner, owners)
                if (owner->hasShownSkill("heg_tongdu") && player->isFriendWith(owner) && player != owner)
                    skill_list.insert(owner, QStringList(objectName()));
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        if (lordExChoice(room, player, "heg_tongdu", "yes+no", data, "@tongdu:" + ask_who->objectName()) == "yes") {
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = player;
            log.to << ask_who;
            log.arg = "heg_tongdu";
            room->sendLog(log);
            room->broadcastSkillInvoke("heg_tongdu", ask_who);
            room->notifySkillInvoked(ask_who, "heg_tongdu");
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), player->objectName());
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        player->drawCards(qMin(lordExDiscardCount(player, true), 3), objectName());
        return false;
    }
};

HQingyinCard::HQingyinCard()
{
    mute = true;
    target_fixed = true;
}

void HQingyinCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *source = card_use.from;
    room->setPlayerMark(source, "@qingyin", 0);
    room->broadcastSkillInvoke("heg_qingyin", source);
    room->doSuperLightbox("heg_liuba", "heg_qingyin");

    CardUseStruct use = card_use;
    QList<ServerPlayer *> all_players = room->getAlivePlayers();

    foreach (ServerPlayer *p, all_players) {
        if (source->willBeFriendWith(p))
            use.to << p;
    }

    SkillCard::onUse(room, use);
}

void HQingyinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *p, targets) {
        if (p->isAlive() && p->isWounded()) {
            RecoverStruct recover;
            recover.recover = p->getMaxHp()- p->getHp();
            recover.who = source;
            room->recover(p, recover);
        }
    }

    if (source->inHeadSkills("heg_qingyin"))
        source->removeGeneral(true);
    else if (source->inDeputySkills("heg_qingyin"))
        source->removeGeneral(false);
}

class HQingyin : public ViewAsSkillV2
{
public:
    HQingyin() : ViewAsSkillV2("heg_qingyin", 0)
    {

        frequency = Limited;
        limit_mark = "@qingyin";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->getMark(limit_mark) > 0;
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override
    {
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HQingyinCard *card = new HQingyinCard;
        card->setShowSkill(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HQingyinCard";
    }
};

class HJuejue : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJuejue() : TriggerSkillV2("heg_juejue")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Discard && player->getHp() > 0) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        room->loseHp(player);
        room->setPlayerFlag(player, "juejueInvoked");

        return false;
    }
};

class HJuejueDiscard : public ViewAsSkillV2
{
public:
    HJuejueDiscard() : ViewAsSkillV2("heg_juejue_discard", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        return request.pattern == "@@heg_juejue_discard"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }
        return !to_select->isEquipped() && selected.length() < request.initiator->getMark("juejue_discard_count");
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return request.selectedCardIds.size() == request.initiator->getMark("juejue_discard_count");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        QList<const Card *> cards;
        for (int id : request.selectedCardIds) cards << Sanguosha->getCard(id);
        if (cards.length() != request.initiator->getMark("juejue_discard_count")) return NULL;

        DummyCard *discard = new DummyCard;
        discard->addSubcards(cards);
        return discard;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "DummyCard";
    }
};

class HJuejueEffect : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJuejueEffect() : TriggerSkillV2("#heg_juejue-effect")
    {
        events << EventPhaseEnd;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player->getPhase() == Player::Discard && player->hasFlag("juejueInvoked") && lordExDiscardCount(player, true) > 0)
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        int x = lordExDiscardCount(player, true);
        QList<ServerPlayer *> all_players = room->getOtherPlayers(player);
        foreach (ServerPlayer *p, all_players) {
            room->setPlayerMark(p, "juejue_discard_count", x);
            QString prompt = "@juejue-discard:"+player->objectName()+"::"+QString::number(x);
            const Card *card = room->askForCard(p, "@@heg_juejue_discard", prompt, QVariant(), Card::MethodNone);
            room->setPlayerMark(p, "juejue_discard_count", 0);

            if (card) {
                CardMoveReason reason(CardMoveReason::S_REASON_PUT, p->objectName(), "heg_juejue", QString());
                room->throwCard(card, reason, NULL);
            } else
                room->damage(DamageStruct("heg_juejue", player, p));
        }

        return false;
    }
};

class HFangyuan : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFangyuan() : TriggerSkillV2("heg_fangyuan")
    {
        view_as_skill = new HArraySummon("heg_fangyuan", "Siege");
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()) && player->aliveCount() >= 4) && player->getPhase() == Player::Finish) {
            QList<ServerPlayer *> all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (p->inSiegeRelation(p, player) && player->canSlash(p, false))
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QList<ServerPlayer *> targets, all_players = room->getAlivePlayers();
        foreach (ServerPlayer *p, all_players) {
            if (p->inSiegeRelation(p, player) && player->canSlash(p, false))
                targets << p;
        }
        if (!targets.isEmpty()) {
            ServerPlayer *target = room->askForPlayerChosen(player, targets, "_heg_fangyuan", "@fangyuan-slash");
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_heg_fangyuan");
            room->useCard(CardUseStruct(slash, player, target), false);
        }
        return false;
    }
};

class HFangyuanMaxCards : public MaxCardsSkillV2
{
public:
    HFangyuanMaxCards() : MaxCardsSkillV2("#heg_fangyuan-maxcards")
    {
        setHolderSelector(CorrectSkill_AllHolders);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        const Player *target = context.primary;
        const Player *holder = context.holder;
        if (!target || !holder) return CorrectSkillResult::noEffect();
        int x = 0;

        QList<const Player *> to_count, siblings = target->getAliveSiblings();

        if (!target->hasShownOneGeneral() || target->isRemoved() || siblings.length() < 3 || target->aliveCount(false) < 3) return CorrectSkillResult::noEffect();

        Player *p1 = target->getNextAlive();
        Player *p2 = target->getLastAlive();
        Player *p3 = target->getNextAlive(2);
        Player *p4 = target->getLastAlive(2);

        if (target->aliveCount(false) > 3) {

            if (p1 && p2 && p1->isFriendWith(p2) && !p1->isFriendWith(target)) {
                if (p1 == holder) x--;
                if (p2 == holder) x--;
            }

            if (p1 && p3 && target->isFriendWith(p3) && !target->isFriendWith(p1) && p1->hasShownOneGeneral()) {
                if (target == holder && !to_count.contains(target))
                    to_count << target;
                if (p3 == holder && !to_count.contains(p3))
                    to_count << p3;
            }
            if (p2 && p4 && target->isFriendWith(p4) && !target->isFriendWith(p2) && p2->hasShownOneGeneral()) {
                if (target == holder && !to_count.contains(target))
                    to_count << target;
                if (p4 == holder && !to_count.contains(p4))
                    to_count << p4;
            }
        }
        return CorrectSkillResult::useAmount(int(to_count.length()) + x);
    }
};

HTonglingCard::HTonglingCard()
{

}

bool HTonglingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    const Card *mutable_card = Sanguosha->getCard(getEffectiveId());
    if (targets.isEmpty() && to_select->objectName() != Self->property("tongling_usetarget").toString())
        return false;
    return mutable_card && mutable_card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, mutable_card, targets);
}

bool HTonglingCard::targetFixed() const
{
    const Card *mutable_card = Sanguosha->getCard(getEffectiveId());
    return mutable_card && mutable_card->targetFixed();
}

bool HTonglingCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    const Card *mutable_card = Sanguosha->getCard(getEffectiveId());
    return mutable_card && mutable_card->targetsFeasible(targets, Self);
}

void HTonglingCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *source = card_use.from;

    const Card *tongling_card = Sanguosha->getCard(getEffectiveId());

    const Card *use_card = Card::Parse(tongling_card->toString());

    if (use_card->isAvailable(source)) {
        const QVariantMap beforeUse = room->queryHistoryEvents({{"kind", "use_card"}, {"limit", 1}});
        room->useCard(CardUseStruct(use_card, source, card_use.to), false);

        DamageStruct damage = source->getTag("tongling-damage").value<DamageStruct>();
        // The nested use has finished: anchor its own event rather than the enclosing Tongling card.
        const QVariantMap uses = room->queryHistoryEvents({{"kind", "use_card"},
            {"from", source->objectName()}, {"after", beforeUse.value("watermark")}, {"limit", 1}});
        const QVariantList events = uses.value("items").toList();
        if (!beforeUse.value("complete").toBool() || !uses.value("complete").toBool()
            || events.isEmpty()) return;
        const QVariantMap damageHistory = room->queryCardUseDamage(events.first().toMap().value("id").toLongLong());
        if (!damageHistory.value("complete").toBool() || !damageHistory.value("attribution_complete").toBool()) return;
        if (damageHistory.value("items").toList().isEmpty()) {

            if (damage.to && damage.card) {
                QList<int> table_cardids = room->getCardIdsOnTable(damage.card);
                if (!table_cardids.isEmpty() && table_cardids.length() == lordExCardIds(damage.card).length())
                    damage.to->obtainCard(damage.card);
            }
        } else {
            if (damage.from && damage.from != source)
                damage.from->drawCards(2, "heg_tongling");

            source->drawCards(2, "heg_tongling");
        }
    }
}

class HTonglingUseCard : public ViewAsSkillV2
{
public:
    HTonglingUseCard() : ViewAsSkillV2("heg_tongling_usecard", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        return request.pattern == "@@heg_tongling_usecard"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        if (to_select->isAvailable(request.initiator) && !to_select->isEquipped()) {
            QString target_name = request.initiator->property("tongling_usetarget").toString();

            const Player *target = NULL;

            foreach (const Player *p, request.initiator->getAliveSiblings()) {
                if (p->objectName() == target_name) {
                    target = p;
                    break;
                }
            }

            if (target == NULL || (!to_select->targetFixed() && !to_select->targetFilter(QList<const Player *>(), target, request.initiator)) || request.initiator->isProhibited(target, to_select)) return false;
            if (to_select->targetFixed() && !to_select->isKindOf("AOE") && !to_select->isKindOf("GlobalEffect")) return false;
            return true;
        }
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator
            || request.selectedCardIds.size() != 1) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HTonglingCard *tongling_card = new HTonglingCard;
        tongling_card->addSubcard(originalCard->getId());
        return tongling_card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HTonglingCard";
    }
};

class HTongling : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HTongling() : TriggerSkillV2("heg_tongling")
    {
        events << Damage << EventPhaseChanging;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play) {
            room->setPlayerFlag(player, "-tonglingUsed");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != Damage) return TriggerList();
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || !player->hasShownOneGeneral()) return TriggerList();
        if (player->getPhase() != Player::Play || player->hasFlag("tonglingUsed")) return TriggerList();
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isAlive() && !player->isFriendWith(damage.to) && damage.to->hasShownOneGeneral()) {
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        DamageStruct damage = data.value<DamageStruct>();
        QList<ServerPlayer *> to_choose, all_players = room->getAlivePlayers();
        foreach (ServerPlayer *p, all_players) {
            if (player->isFriendWith(p))
                to_choose << p;
        }
        if (to_choose.isEmpty()) return false;

        player->setTag("tongling-damage", data);
        ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(),
                "@tongling-invoke::" + damage.to->objectName(), true, true);
        player->removeTag("tongling-damage");
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);
            player->setFlags("tonglingUsed");

            QStringList target_list = player->getTag("tongling_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("tongling_target", target_list);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ServerPlayer *source = ctx.invoker;
        ctx.manual_effect = true;
        QStringList target_list = source->getTag("tongling_target").toStringList();
        QString target_name = target_list.takeLast();
        source->setTag("tongling_target", target_list);

        ServerPlayer *to = room->findPlayerByObjectName(target_name);
        if (to) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to && damage.to->isAlive()) {
                room->setPlayerProperty(to, "tongling_usetarget", damage.to->objectName());
                to->setTag("tongling-damage", data);
                room->askForUseCard(to, "@@heg_tongling_usecard", "@tongling-usecard::" + damage.to->objectName(), -1, Card::MethodUse, false);
                to->removeTag("tongling-damage");
            }
        }
        return false;
    }
};

class HJinxian : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJinxian() : TriggerSkillV2("heg_jinxian")
    {

    }

    virtual bool canPreshow() const
    {
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *eventPlayer, QVariant &) const override
    {
        return TriggerList();
    }
};

class HJinxianCompulsory : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJinxianCompulsory() : TriggerSkillV2("#heg_jinxian-compulsory")
    {
        events << GeneralShowed;
        frequency = Compulsory;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player->hasShownSkill("heg_jinxian")
            && ((data.toStringList().contains("head") && player->inHeadSkills("heg_jinxian"))
                || (data.toStringList().contains("deputy") && player->inDeputySkills("heg_jinxian"))))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        room->sendCompulsoryTriggerLog(player, "heg_jinxian");
        room->broadcastSkillInvoke("heg_jinxian", player);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QList<ServerPlayer *> targets, allplayers = room->getAlivePlayers();
        foreach (ServerPlayer *p, allplayers) {
            if (player->distanceTo(p) == 0 || player->distanceTo(p) == 1) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
                targets << p;
            }
        }
        room->sortByActionOrder(targets);
        foreach (ServerPlayer *p, targets) {
            if (p->isAlive() && p->getGeneral2()) {
                if (p->hasShownAllGenerals()) {
                    QStringList generals, allchoices;
                    allchoices << "head" << "deputy";
                    if (!p->getActualGeneral1Name().contains("sujiang") && !p->isLord())
                        generals << "head";

                    if (p->getGeneral2() != NULL && !p->getGeneral2Name().contains("sujiang"))
                        generals << "deputy";

                    if (generals.isEmpty()) continue;

                    QString choice = lordExChoice(room, p, "jinxian_hide", generals.join("+"), QVariant(),
                                                        "@jinxian-hide", allchoices.join("+"));
                    bool head = (choice == "head");

                    p->hideGeneral(head);
                } else
                    room->askForDiscard(p, "jinxian_discard", 2, 2, false, true);
            }
        }

        return false;
    }
};



class HWuyan : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    HWuyan() : TriggerSkillV2("heg_wuyan")
    {
        events << DamageInflicted << DamageCaused;
        frequency = Compulsory;

    }

    int getPriority(TriggerEvent) const override
    {
        return -2;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && player->getKingdom() == "wei") {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card != NULL && damage.card->getTypeId() == Card::TypeTrick)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return true;
    }
};

HJianyanCard::HJianyanCard()
{
    target_fixed = true;
}

void HJianyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList choice_list, pattern_list;
    choice_list << "basic" << "trick" << "equip" << "red" << "black";
    pattern_list << "BasicCard" << "TrickCard" << "EquipCard" << ".|red" << ".|black";

    QString choice = lordExChoice(room, source, "heg_jianyan", choice_list.join("+"), QVariant(), "@jianyan-choice");
    QString pattern = pattern_list.at(choice_list.indexOf(choice));

    LogMessage log;
    log.type = "#JianyanChoice";
    log.from = source;
    log.arg = choice;
    room->sendLog(log);

    int card_id = -1;
    for (int i = room->getDrawPile().length()-1; i >= 0; i--) {
        int id = room->getDrawPile().at(i);
        if (Sanguosha->matchExpPattern(pattern, NULL, Sanguosha->getCard(id))) {
            card_id = id;
            break;
        }
    }
    if (card_id < 0) {
        bool swappile = false;
        foreach (int card_id, room->getDiscardPile()) {
            if (Sanguosha->matchExpPattern(pattern, NULL, Sanguosha->getCard(card_id))) {
                swappile = true;
                break;
            }
        }
        if (swappile) {
            room->swapPile();
            for (int i = room->getDrawPile().length()-1; i >= 0; i--) {
                int id = room->getDrawPile().at(i);
                if (Sanguosha->matchExpPattern(pattern, NULL, Sanguosha->getCard(id))) {
                    card_id = id;
                    break;
                }
            }
        }
    }
    if (card_id < 0) {
        LogMessage log;
        log.type = "$SearchFailed";
        log.from = source;
        log.arg = pattern;
        room->sendLog(log);
    } else {
        const Card *card = Sanguosha->getCard(card_id);
        CardMoveReason reason(CardMoveReason::S_REASON_TURNOVER, source->objectName(), "heg_jianyan", QString());
        room->moveCardTo(card, NULL, Player::PlaceTable, reason, true);

        QList<ServerPlayer *> males;
        foreach (ServerPlayer *player, room->getAlivePlayers()) {
            if (player->isMale())
                males << player;
        }
        if (!males.isEmpty()) {
            source->setMark("heg_jianyan", card_id); // For AI
            ServerPlayer *target = room->askForPlayerChosen(source, males, "heg_jianyan",
                QString("@jianyan-give:::%1:%2\\%3").arg(card->objectName())
                .arg(card->getSuitString() + "_char")
                .arg(card->getNumberString()));
            room->obtainCard(target, card);
        } else {
            CardMoveReason reason2(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), "heg_jianyan", QString());
            room->throwCard(card, reason2, NULL);
        }
    }
}

class HJianyan : public ViewAsSkillV2
{
public:
    HJianyan() : ViewAsSkillV2("heg_jianyan", 0)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->getKingdom() == "shu" && !player->hasUsed("HJianyanCard");
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override
    {
        return false;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HJianyanCard *card = new HJianyanCard;
        card->setShowSkill(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HJianyanCard";
    }
};

HJujianCard::HJujianCard()
{
}

bool HJujianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->isFriendWith(to_select) && to_select->getMark("jujiantransformUsed") == 0;
}

void HJujianCard::onEffect(CardEffectStruct &effect) const
{
    QVariantList effect_list = effect.from->getTag("jujianTag").toList();
    effect_list << QVariant::fromValue(effect);
    effect.from->setTag("jujianTag", effect_list);
}

class HJujianViewAsSkill : public ViewAsSkillV2
{
public:
    HJujianViewAsSkill() : ViewAsSkillV2("heg_jujian", 1)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        return request.pattern == "@@heg_jujian"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        if (request.initiator->isJilei(to_select)) return false;
        return Sanguosha->matchExpPattern("^BasicCard", request.initiator, to_select);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator
            || request.selectedCardIds.size() != 1) return false;
        // Validate in selection order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HJujianCard *jujianCard = new HJujianCard;
        jujianCard->addSubcard(originalCard);
        return jujianCard;
    }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HJujianCard";
    }
};

class HJujian : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJujian() : TriggerSkillV2("heg_jujian")
    {
        events << EventPhaseStart;
        view_as_skill = new HJujianViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Finish && !player->isNude()) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        return room->askForUseCard(player, "@@heg_jujian", "@jujian-card", -1, Card::MethodDiscard);
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ServerPlayer *player = ctx.invoker;
        Room *room = player->getRoom();
        QVariantList data_list = player->getTag("jujianTag").toList();
        if (data_list.isEmpty()) return false;
        QVariant jujian_data = data_list.takeLast();
        player->setTag("jujianTag", data_list);
        CardEffectStruct effect = jujian_data.value<CardEffectStruct>();
        ServerPlayer *target = effect.to;
        room->broadcastSkillInvoke("transform", target->isMale(), -1);
        room->addPlayerMark(target, "jujiantransformUsed");
        room->transformDeputyGeneral(target);
        const General *general = target->getGeneral2();
        if (general == NULL) return false;
        QList<const Skill *> skills = general->getVisibleSkillList();
        foreach (const Skill *skill, skills) {
            if (skill->getFrequency() == Skill::Compulsory) {
                if (player->isAlive())
                    player->drawCards(2, objectName());
                if (target->isAlive() && target != player)
                    target->drawCards(2, objectName());
                return false;
            }
        }
        return false;
    }
};

// Package bookkeeping uses native event payloads and runs once per event.
class HLordEXRecord : public TriggerSkillV2
{
public:
    HLordEXRecord() : TriggerSkillV2("#heg_lord_ex-record")
    {
        events << CardUsed << ConfirmDamage << EventPhaseChanging;
        global = true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == ConfirmDamage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.chain && !damage.transfer && damage.card && damage.card->hasFlag("heg_conquering_damage")) {
                ++damage.damage;
                data = QVariant::fromValue(damage);
            }
        } else if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.card && use.card->getTypeId() != Card::TypeSkill
                && use.from->getPhase() == Player::Play) {
                room->addPlayerMark(use.from, "heg_tunjiang_play_used");
                for (ServerPlayer *target : use.to)
                    if (!use.from->isFriendWith(target)) use.from->setFlags("TunjiangDisabled");
            }
            if (use.card && use.card->hasFlag("GlobalCardUseDisresponsive")) {
                use.no_respond_list << "_ALL_TARGETS";
                data = QVariant::fromValue(use);
            }
        } else if (player && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            room->setPlayerMark(player, "heg_xingzhao_maxcards", 0);
            room->setPlayerMark(player, "heg_tunjiang_play_used", 0);
            player->setFlags("-TunjiangDisabled");
        }
        return true;
    }
};

class HLordExQiuanClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HLordExQiuanClear() : TriggerSkillV2("#heg_qiuan-clear")
    {
        events << EventLoseSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Cleanup runs after the final exact source has been detached.
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        if (player && change.skillName == "heg_qiuan" && change.instanceID > 0
            && !player->hasSkillInstance(change.skillName, change.instanceID)
            && !player->ownsSkill(change.skillName))
            player->clearOnePrivatePile("letter");
        return true;
    }
};

class HLordExMidaoClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HLordExMidaoClear() : TriggerSkillV2("#heg_midao-clear")
    {
        events << EventLoseSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Cleanup runs after the final exact source has been detached.
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        if (player && change.skillName == "heg_midao" && change.instanceID > 0
            && !player->hasSkillInstance(change.skillName, change.instanceID)
            && !player->ownsSkill(change.skillName))
            player->clearOnePrivatePile("rice");
        return true;
    }
};

class HLordExQuanjiClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HLordExQuanjiClear() : TriggerSkillV2("#heg_quanji-clear")
    {
        events << EventLoseSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Cleanup runs after the final exact source has been detached.
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        if (player && change.skillName == "heg_quanji" && change.instanceID > 0
            && !player->hasSkillInstance(change.skillName, change.instanceID)
            && !player->ownsSkill(change.skillName))
            player->clearOnePrivatePile("power_pile");
        return true;
    }
};

class HLordExShiluClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HLordExShiluClear() : TriggerSkillV2("#heg_shilu-clear")
    {
        events << EventLoseSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Cleanup runs after the final exact source has been detached.
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        if (player && change.skillName == "heg_shilu" && change.instanceID > 0
            && !player->hasSkillInstance(change.skillName, change.instanceID)
            && !player->ownsSkill(change.skillName))
            player->clearOnePrivatePile("massacre");
        return true;
    }
};

class HLordExHuaiyiClear : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HLordExHuaiyiClear() : TriggerSkillV2("#heg_huaiyi-clear")
    {
        events << EventLoseSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // Cleanup runs after the final exact source has been detached.
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        if (player && change.skillName == "heg_huaiyi" && change.instanceID > 0
            && !player->hasSkillInstance(change.skillName, change.instanceID)
            && !player->ownsSkill(change.skillName))
            player->clearOnePrivatePile("disloyalty");
        return true;
    }
};

HLordEXPackage::HLordEXPackage()
    : Package("heg_lord_ex")
{
    General *mengda = new General(this, "heg_mengda", "wei");
    mengda->addSkill(new HQiuan);
    mengda->addSkill(new HLordExQiuanClear);
    insertRelatedSkills("heg_qiuan", "#heg_qiuan-clear");
    mengda->addSkill(new HLiangfan);
    mengda->addSkill(new HLiangfanEffect);
    insertRelatedSkills("heg_liangfan", "#heg_liangfan-effect");
    mengda->setSubordinateKingdom("shu");

    General *mifangfushiren = new General(this, "heg_mifangfushiren", "shu");
    mifangfushiren->addSkill(new HFengshiX);
    mifangfushiren->addSkill(new HFengshiXOther);
    insertRelatedSkills("heg_fengshix", "#heg_fengshix-other");
    mifangfushiren->setSubordinateKingdom("wu");

    General *liuqi = new General(this, "heg_liuqi", "qun", 3);
    liuqi->addSkill(new HWenji);
    liuqi->addSkill(new HWenjiEffect);
    liuqi->addSkill(new HWenjiTargetMod);
    insertRelatedSkills("heg_wenji", "#heg_wenji-effect");
    insertRelatedSkills("heg_wenji", "#heg_wenji-target");
    liuqi->addSkill(new HTunjiang);
    liuqi->setSubordinateKingdom("shu");

    General *zhanglu = new General(this, "heg_zhanglu", "qun", 3);
    zhanglu->addSkill(new HBushi);
    zhanglu->addSkill(new HBushiCompulsory);
    insertRelatedSkills("heg_bushi", "#heg_bushi-compulsory");
    zhanglu->addSkill(new HMidao);
    zhanglu->addSkill(new HLordExMidaoClear);
    insertRelatedSkills("heg_midao", "#heg_midao-clear");
    zhanglu->setSubordinateKingdom("wei");

    General *shixie = new General(this, "heg_shixie", "wu", 3);
    shixie->addSkill(new HBiluan);
    shixie->addSkill(new HLixia);
    shixie->addSkill(new HLixiaOther);
    insertRelatedSkills("heg_lixia", "#heg_lixia-other");
    shixie->setSubordinateKingdom("qun");

    General *tangzi = new General(this, "heg_tangzi", "wei");
    tangzi->addSkill(new HXingzhao);
    tangzi->addSkill(new HXingzhaoMaxCards);
    insertRelatedSkills("heg_xingzhao", "#heg_xingzhao-maxcards");
    tangzi->addSkill(new HXunxunTangzi);
    insertRelatedSkills("heg_xingzhao", "heg_xunxun_tangzi");
    tangzi->addRelateSkill("heg_xunxun_tangzi");
    tangzi->setSubordinateKingdom("wu");

    General *dongzhao = new General(this, "heg_dongzhao", "wei", 3);
    dongzhao->addSkill(new HQuanjin);
    insertRelatedSkills("heg_quanjin", "#heg_quanjin-targets");
    dongzhao->addSkill(new HZaoyun);

    General *xushu = new General(this, "heg_xushu", "shu", 3);
    xushu->setSubordinateKingdom("wei");
    xushu->addSkill(new HWuyan);
    xushu->addSkill(new HJianyan);
    xushu->addSkill(new HJujian);
    xushu->addCompanion("heg_wolong");
    xushu->addCompanion("heg_zhaoyun");

    General *wujing = new General(this, "heg_wujing", "wu");
    wujing->addSkill(new HDiaogui);
    wujing->addSkill(new HFengyang);

    General *yanbaihu = new General(this, "heg_yanbaihu", "qun");
    yanbaihu->addSkill(new HZhidao);
    yanbaihu->addSkill(new HZhidaoDamage);
    yanbaihu->addSkill(new HZhidaoProhibit);
    insertRelatedSkills("heg_zhidao", "#heg_zhidao-damage");
    insertRelatedSkills("heg_zhidao", "#heg_zhidao-prohibit");
    yanbaihu->addSkill(new HJiliX);
    yanbaihu->addSkill(new HJiliXDecrease);
    insertRelatedSkills("heg_jilix", "#heg_jilix-decrease");

    General *xiahouba = new General(this, "heg_xiahouba", "shu");
    xiahouba->addSkill(new HBaolie);
    xiahouba->addSkill(new HBaolieTargetMod);
    insertRelatedSkills("heg_baolie", "#heg_baolie-target");
    xiahouba->setSubordinateKingdom("wei");
    xiahouba->addCompanion("heg_jiangwei");

    General *panjun = new General(this, "heg_panjun", "shu", 3);
    panjun->addSkill(new HCongcha);
    panjun->addSkill(new HCongchaEffect);
    insertRelatedSkills("heg_congcha", "#heg_congcha-effect");
    panjun->addSkill(new HGongqing);
    panjun->addSkill(new HGongqingDecrease);
    insertRelatedSkills("heg_gongqing", "#heg_gongqing-decrease");
    panjun->setSubordinateKingdom("wu");

    General *pengyang = new General(this, "heg_pengyang", "shu", 3);
    pengyang->setSubordinateKingdom("qun");
    pengyang->addSkill(new HTongling);
    pengyang->addSkill(new HJinxian);
    pengyang->addSkill(new HJinxianCompulsory);
    insertRelatedSkills("heg_jinxian", "#heg_jinxian-compulsory");

    General *xuyou = new General(this, "heg_xuyou", "qun", 3);
    xuyou->addSkill(new HChenglve);
    xuyou->addSkill(new HShicai);
    xuyou->setSubordinateKingdom("wei");

    General *sufei = new General(this, "heg_sufei", "wu");
    sufei->setSubordinateKingdom("qun");
    sufei->addSkill(new HLianpian);
    sufei->addSkill(new HLianpianOther);
    insertRelatedSkills("heg_lianpian", "#heg_lianpian-other");
    sufei->addCompanion("heg_ganning");

    General *wenqin = new General(this, "heg_wenqin", "wei");
    wenqin->addSkill(new HJinfa);
    wenqin->setSubordinateKingdom("wu");

    General *zhuling = new General(this, "heg_zhuling", "wei");
    zhuling->addSkill(new HJuejue);
    zhuling->addSkill(new HJuejueEffect);
    insertRelatedSkills("heg_juejue", "#heg_juejue-effect");
    zhuling->addSkill(new HFangyuan);
    zhuling->addSkill(new HFangyuanMaxCards);
    insertRelatedSkills("heg_fangyuan", "#heg_fangyuan-maxcards");

    General *liuba = new General(this, "heg_liuba", "shu", 3);
    liuba->addSkill(new HTongdu);
    liuba->addSkill(new HTongduOther);
    insertRelatedSkills("heg_tongdu", "#heg_tongdu-other");
    liuba->addSkill(new HQingyin);

    General *zhugeke = new General(this, "heg_zhugeke", "wu", 3);
    zhugeke->addSkill(new HAocai);
    zhugeke->addSkill(new HDuwu);
    zhugeke->addCompanion("heg_dingfeng");

    General *huangzu = new General(this, "heg_huangzu", "qun");
    huangzu->addSkill(new HXishe);
    huangzu->addSkill(new HXisheTransform);
    insertRelatedSkills("heg_xishe", "#heg_xishe-transform");

    General *simazhao = new General(this, "heg_simazhao", "careerist", 3);
    simazhao->addSkill(new HSuzhi);
    simazhao->addSkill(new HSuzhiTarget);
    insertRelatedSkills("heg_suzhi", "#heg_suzhi-target");
    simazhao->addSkill(new HZhaoxin);
    simazhao->addCompanion("heg_simayi");
    simazhao->addRelateSkill("heg_fankui_simazhao");

    General *zhonghui = new General(this, "heg_zhonghui", "careerist");
    zhonghui->addSkill(new HQuanji);
    zhonghui->addSkill(new HQuanjiMaxCards);
    zhonghui->addSkill(new HLordExQuanjiClear);
    zhonghui->addSkill(new HPaiyi);
    insertRelatedSkills("heg_quanji", "#heg_quanji-maxcards");
    insertRelatedSkills("heg_quanji", "#heg_quanji-clear");
    zhonghui->addCompanion("heg_jiangwei");

    General *sunchen = new General(this, "heg_sunchen", "careerist");
    sunchen->addSkill(new HShilu);
    sunchen->addSkill(new HLordExShiluClear);
    sunchen->addSkill(new HXiongnve);
    sunchen->addSkill(new HXiongnveEffect);
    sunchen->addSkill(new HXiongnveTarget);
    insertRelatedSkills("heg_shilu", "#heg_shilu-clear");
    insertRelatedSkills("heg_xiongnve", "#heg_xiongnve-effect");
    insertRelatedSkills("heg_xiongnve", "#heg_xiongnve-target");

    General *gongsunyuan = new General(this, "heg_gongsunyuan", "careerist");
    gongsunyuan->addSkill(new HHuaiyi);
    gongsunyuan->addSkill(new HLordExHuaiyiClear);
    gongsunyuan->addSkill(new HZisui);
    insertRelatedSkills("heg_huaiyi", "#heg_huaiyi-clear");

    addMetaObject<HPaiyiCard>();
    addMetaObject<HQuanjinCard>();
    addMetaObject<HZaoyunCard>();
    addMetaObject<HDiaoguiCard>();
    addMetaObject<HAocaiCard>();
    addMetaObject<HDuwuCard>();
    addMetaObject<HJinfaCard>();
    addMetaObject<HHuaiyiCard>();

    addMetaObject<HQingyinCard>();
    addMetaObject<HTonglingCard>();
    addMetaObject<HJianyanCard>();
    addMetaObject<HJujianCard>();

    skills << new HLordEXRecord << new HQuanjinTargets << new HFankuiSimazhao << new HJuejueDiscard << new HTonglingUseCard;
}

ADD_PACKAGE(HLordEX)

HLordEXCardPackage::HLordEXCardPackage() : Package("heg_lord_ex_card", CardPack)
{
    QList<Card *> cards;

    cards
        << new HImperialEdict(Card::Club, 3)
        << new HRuleTheWorld()
        << new HConquering()
        << new HConsolidateCountry()
        << new HChaos();

    foreach(Card *card, cards)
        card->setParent(this);

    addMetaObject<HConsolidateCountryGiveCard>();
    addMetaObject<HImperialEdictAttachCard>();
    addMetaObject<HImperialEdictTrickCard>();

    skills << new HImperialEdictSkill << new HImperialEdictAttach << new HImperialEdictTrick << new HConsolidateCountryGive << new HChaosSelect;
}

ADD_PACKAGE(HLordEXCard)
