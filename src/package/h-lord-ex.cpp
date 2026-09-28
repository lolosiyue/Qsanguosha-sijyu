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
#include "skill-instance-utils.h"
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
#include <algorithm>
#include "engine.h"
#include "structs.h"
#include "gamerule.h"
#include "settings.h"
#include "roomthread.h"
#include "room-state.h"
#include "card-lifetime-manager.h"
#include "json.h"

namespace {
QStringList lordExUsableEntries(const TriggerSkillV2 *skill, Room *room, ServerPlayer *player)
{
    QStringList entries;
    if (!room || !player) return entries;
    for (int id : player->getValidSkillInstanceIds(skill->objectName())) {
        SkillContext ctx;
        ctx.skill_name = skill->objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.instanceID = id;
        ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(skill->objectName(), id));
        ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
        ctx.amount = room->getSkillInstanceAmount(ctx.activationRef);
        if (skill->isUsable(ctx)) entries << SkillInstanceKey(skill->objectName(), id).toString();
    }
    return entries;
}

int lordExDiscardCount(const ServerPlayer *player, bool discardPhase)
{
    Room *room = player->getRoom();
    const QVariant turn = room->historyScopes().value("turn_id");
    if (turn.toLongLong() <= 0) return -1;
    QVariantMap filter{{"turn_id", turn}};
    int count = 0;
    for (;;) {
        const QVariantMap history = room->queryHistoryMoves(filter);
        if (!history.value("complete").toBool()) return -1;
        for (const QVariant &entry : history.value("items").toList()) {
            const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
            const int place = move.value("from_place").toInt();
            if ((place != Player::PlaceHand && place != Player::PlaceEquip)
                || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON)
                    != CardMoveReason::S_REASON_DISCARD) continue;
            if (!discardPhase) {
                if (!move.contains("reason_player")) return -1;
                if (move.value("reason_player").toString() == player->objectName()) ++count;
                continue;
            }
            if (move.value("from").toString() != player->objectName()) continue;
            const QVariantMap phase = room->historyEvent(fact.value("phase_id").toLongLong()).value("data").toMap();
            if (phase.isEmpty()) return -1;
            if (phase.value("player").toString() == player->objectName()
                && phase.value("phase").toInt() == Player::Discard) ++count;
        }
        if (!history.value("has_more").toBool()) return count;
        filter.insert("after", history.value("next_after"));
        filter.insert("watermark", history.value("watermark"));
    }
}

bool lordExInjuredThisPhase(const ServerPlayer *player)
{
    Room *room = player->getRoom();
    const QVariant phase = room->historyScopes().value("phase_id");
    if (phase.toLongLong() <= 0) return false;
    const QVariantMap page = room->queryActualDamage({{"phase_id", phase}, {"to", player->objectName()}, {"limit", 1}});
    return page.value("complete").toBool() && !page.value("items").toList().isEmpty();
}

int lordExReceiptId(ServerPlayer *recipient)
{
    const int id = recipient->getTag("HLordExReceiptSequence").toInt() + 1;
    recipient->setTag("HLordExReceiptSequence", id);
    return id;
}

auto lordExPromptScope(Room *room, ServerPlayer *player, const SkillInstanceRef &entry)
{
    const QString selector = ViewAsSkillV2::borrowedActivationMarkName(entry.key.skillName);
    const int previous = player->getMark(selector);
    RoomState *state = Sanguosha->currentRoomState();
    const auto reason = state->getCurrentCardUseReason();
    const QString pattern = state->getCurrentCardUsePattern();
    room->setPlayerMark(player, selector, entry.key.instanceID);
    return qScopeGuard([=] {
        room->setPlayerMark(player, selector, previous);
        state->setCurrentCardUseReason(reason);
        state->setCurrentCardUsePattern(pattern);
    });
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!ctx.owner || !ctx.owner->getPile("letter").isEmpty() || !damage.card
            || !room->isAllOnPlace(damage.card, Player::PlaceTable)) return false;
        ctx.owner->addToPile("letter", damage.card);
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Whole-damage prevention is boolean; the paid material remains in the letter pile.
        return ctx.original_data->value<DamageStruct>().to == target;
    }
};
class HLiangfan : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLiangfan() : TriggerSkillV2("heg_liangfan") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start && !player->getPile("letter").isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && (ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)
            || ctx.owner->askForSkillInvoke(this, *ctx.original_data));
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx,
        ServerPlayer *target) const override
    {
        if (ctx.choice == "lose-hp") {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            return false;
        }
        const QList<int> ids = ctx.owner->getPile("letter");
        if (ids.isEmpty()) return false;
        DummyCard letters(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, ctx.owner->objectName(), objectName(), QString());
        room->obtainCard(target, &letters, reason);
        SkillContext loss = ctx;
        loss.choice = "lose-hp";
        skillEffect(event, room, player, loss, target);
        QVariantList material;
        QList<int> projection = lordExCards(target, "@liangfan-turn");
        for (int id : ids) {
            if (!target->handCards().contains(id)) continue;
            material << id;
            if (!projection.contains(id)) projection << id;
        }
        if (!material.isEmpty()) {
            QVariantList receipts = target->getTag("HLiangfanLetters").toList();
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"execution", ctx.executionID},
                {"amount", getEffectiveAmount(ctx)}, {"cards", material}, {"dispatch", lordExReceiptId(target)}};
            target->setTag("HLiangfanLetters", receipts);
            lordExSetCards(room, target, "@liangfan-turn", projection);
        }
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
        global = true;
    }
    static qint64 currentUse(Room *room)
    { return room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong(); }
    static QVariantList useReceipts(Room *room, const Card *card)
    {
        QVariantList result;
        const qint64 use = currentUse(room);
        if (!card || use <= 0) return result;
        for (const QVariant &value : card->getTag("HLiangfanUses").toList())
            if (value.toMap().value("use").toLongLong() == use) result << value;
        return result;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            player->removeTag("HLiangfanLetters");
            lordExSetCards(room, player, "@liangfan-turn", {});
        } else if (event == CardsMoveBatch) {
            QVariantList receipts;
            QList<int> projection;
            for (const QVariant &value : player->getTag("HLiangfanLetters").toList()) {
                QVariantMap receipt = value.toMap();
                QVariantList remaining;
                for (const QVariant &card : receipt.value("cards").toList()) {
                    const int id = card.toInt();
                    if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand) continue;
                    remaining << id;
                    if (!projection.contains(id)) projection << id;
                }
                if (remaining.isEmpty()) continue;
                receipt.insert("cards", remaining);
                receipts << receipt;
            }
            player->setTag("HLiangfanLetters", receipts);
            lordExSetCards(room, player, "@liangfan-turn", projection);
        } else if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            const qint64 id = currentUse(room);
            if (!use.card || use.card->getTypeId() == Card::TypeSkill || id <= 0) return true;
            QSet<qint64> ancestors;
            qint64 parent = room->historyParent(id, "use_card", false).value("id").toLongLong();
            while (parent > 0 && !ancestors.contains(parent)) {
                ancestors.insert(parent);
                parent = room->historyParent(parent, "use_card", false).value("id").toLongLong();
            }
            QVariantList receipts;
            for (const QVariant &value : use.card->getTag("HLiangfanUses").toList())
                if (ancestors.contains(value.toMap().value("use").toLongLong())) receipts << value;
            for (const QVariant &value : player->getTag("HLiangfanLetters").toList()) {
                QVariantMap receipt = value.toMap();
                bool matches = false;
                for (const QVariant &card : receipt.value("cards").toList())
                    if (lordExCardIds(use.card).contains(card.toInt())) matches = true;
                if (!matches) continue;
                receipt.insert("use", id);
                receipts << receipt;
            }
            // A reused physical card receives only this use and live ancestor receipts.
            use.card->setTag("HLiangfanUses", receipts);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (event != Damage || !player || !player->isAlive()) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || damage.chain || damage.transfer || !damage.by_user
            || !damage.to || !damage.to->isAlive() || !player->canGet(damage.to, "he")) return true;
        for (const QVariant &value : useReceipts(room, damage.card)) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(),
                SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.setModifiedAmount(receipt.value("amount", 1).toInt());
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = value;
            ctx.targets = {damage.to};
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data ? ctx.original_data->value<DamageStruct>() : DamageStruct();
        return ctx.invoker && useReceipts(room, damage.card).contains(ctx.extra_data);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.to && ctx.invoker->canGet(damage.to, "he")
            && lordExChoice(room, ctx.invoker, "heg_liangfan", "yes+no", QVariant(),
                "@liangfan::" + damage.to->objectName()) == "yes";
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        if (!actor || !victim) return false;
        if (ctx.choice == "receive") {
            const int id = ctx.extra_data.toMap().value("card", -1).toInt();
            if (room->getCardOwner(id) == victim && target->canGet(victim, id)
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) {
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), "heg_liangfan", QString());
                room->obtainCard(target, Sanguosha->getCard(id), reason, false);
            }
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && actor->isAlive() && victim->isAlive() && actor->canGet(victim, "he"); ++i) {
            const int id = room->askForCardChosen(actor, victim, "he", "heg_liangfan", false, Card::MethodGet);
            if (id < 0 || room->getCardOwner(id) != victim || !actor->canGet(victim, id)) continue;
            SkillContext receive = ctx;
            receive.choice = "receive";
            QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("card", id); receive.extra_data = receipt;
            skillEffect(event, room, player, receive, actor);
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

    bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        bool invoke = false;
        if (player->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)) {
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

    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Damaged) {
            ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
            if (from && from->isAlive() && from->getHandcardNum() != ctx.owner->getHandcardNum())
                ctx.targets = {from->getHandcardNum() < ctx.owner->getHandcardNum() ? from : ctx.owner};
        } else ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == EventPhaseStart)
            room->addPlayerMark(target, "heg_xingzhao_maxcards", 4 * getEffectiveAmount(ctx));
        else target->drawCards(getEffectiveAmount(ctx), objectName());
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
    HXingzhaoMaxCards() : MaxCardsSkillV2("#heg_xingzhao-maxcards")
    { setHolderSelector(CorrectSkill_System); }
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
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return TriggerSkillV2::isSourceAvailable(room, ctx)
            && ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID);
    }
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
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.owner;
        return player->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        Room *room = player->getRoom();
        QList<int> bottom = room->getNCards(4 * getEffectiveAmount(ctx)), top;
        const QList<int> original = bottom;
        const auto returnPending = qScopeGuard([&] {
            QList<int> pending;
            for (int id : original)
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) pending << id;
            if (!pending.isEmpty()) room->returnToTopDrawPile(pending);
        });
        const int count = qMin(2 * getEffectiveAmount(ctx), int(bottom.size()));
        room->fillAG(bottom, player);
        bool galleryOpen = true;
        const auto clearGallery = qScopeGuard([&] { if (galleryOpen) room->clearAG(player); });
        for (int i = 0; i < count; ++i) {
            const int id = room->askForAG(player, bottom, false, objectName());
            if (!bottom.contains(id)) break;
            bottom.removeOne(id);
            top << id;
            room->takeAG(player, id, false, QList<ServerPlayer *>{player});
        }
        room->clearAG(player);
        galleryOpen = false;
        for (int id : QList<int>(bottom))
            if (room->getCardOwner(id) || room->getCardPlace(id) != Player::DrawPile || room->getDrawPile().contains(id)) bottom.removeOne(id);
        if (!bottom.isEmpty()) room->askForGuanxing(player, bottom, Room::GuanxingDownOnly);
        for (int id : QList<int>(top))
            if (room->getCardOwner(id) || room->getCardPlace(id) != Player::DrawPile || room->getDrawPile().contains(id)) top.removeOne(id);
        if (!top.isEmpty()) room->returnToTopDrawPile(top);
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
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.from && !damage.from->isNude() && player->askForSkillInvoke(this, data);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.original_data->value<DamageStruct>().from}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().from;
        if (!actor || !victim) return false;
        if (ctx.choice == "receive") {
            const int id = ctx.extra_data.toMap().value("card", -1).toInt();
            if (room->getCardOwner(id) == victim && target->canGet(victim, id)
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) {
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), "heg_fankui_simazhao", QString());
                room->obtainCard(target, Sanguosha->getCard(id), reason, false);
            }
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && actor->isAlive() && victim->isAlive() && actor->canGet(victim, "he"); ++i) {
            const int id = room->askForCardChosen(actor, victim, "he", "heg_fankui_simazhao", false, Card::MethodGet);
            if (id < 0 || room->getCardOwner(id) != victim || !actor->canGet(victim, id)) continue;
            SkillContext receive = ctx;
            receive.choice = "receive";
            QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("card", id); receive.extra_data = receipt;
            skillEffect(event, room, player, receive, actor);
        }
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || ctx.owner->getMark("#heg_yishe") <= 0) return false;
        const QList<int> result = lordExExchange(room, ctx.owner, objectName(), 1, 0,
            "@bushi-give:" + ctx.invoker->objectName());
        if (result.size() != 1) return false;
        ctx.extra_data = result.first();
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->isAlive() || ctx.owner->getMark("#heg_yishe") <= 0
            || room->getCardOwner(id) != ctx.owner
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->removePlayerMark(ctx.owner, "#heg_yishe");
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), ctx.invoker->objectName(),
            objectName(), QString());
        room->obtainCard(ctx.invoker, Sanguosha->getCard(id), reason, false);
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
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
        if (triggerEvent == EventLoseSkill && player && data.value<SkillChangeStruct>().skillName == "heg_bushi"
            && !player->ownsSkill("heg_bushi"))
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

    bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.owner;
        bool invoke = false;
        if (player->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)) {
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

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        if (triggerEvent == EventPhaseChanging)
            room->addPlayerMark(player, "#heg_yishe", qMax(0, player->getHp()) * getEffectiveAmount(ctx));
        if (triggerEvent == EventPhaseStart) {
            int x = room->alivePlayerCount() - player->getHp() - 2;
            if (x > 0)
                room->askForDiscard(player, "bushi_discard", x * getEffectiveAmount(ctx), x * getEffectiveAmount(ctx), false, true);
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
    bool canPreshow() const override { return true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        return ((event == EventPhaseStart && player->getPhase() == Player::Finish && player->getPile("rice").isEmpty())
            || (event == AskForRetrial && !player->getPile("rice").isEmpty()))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) return ctx.owner->askForSkillInvoke(this);
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->card) return false;
        const auto promptScope = lordExPromptScope(room, ctx.owner, ctx.activationRef);
        const QString prompt = QStringList{"@midao-card", judge->who->objectName(), objectName(),
            judge->reason, QString::number(judge->card->getEffectiveId())}.join(":");
        // Retrial selection is read-only; Room::retrial performs the exchange after interception.
        const Card *card = room->askForCard(ctx.owner, "@@heg_midao", prompt, *ctx.original_data,
            Card::MethodResponse, judge->who, true);
        if (!card || !ctx.owner->getPile("rice").contains(card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != AskForRetrial) return true;
        const int id = ctx.extra_data.toInt();
        return ctx.extra_data.isValid() && ctx.owner->getPile("rice").contains(id)
            && !Sanguosha->getCard(id)->hasFlag("using");
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const JudgeStruct *judge = event == AskForRetrial ? ctx.original_data->value<JudgeStruct *>() : nullptr;
        return skillEffect(event, room, player, ctx, judge ? judge->who : ctx.owner);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        if (event == AskForRetrial) {
            const int id = ctx.extra_data.toInt();
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (ctx.extra_data.isValid() && judge && ctx.owner->getPile("rice").contains(id))
                room->retrial(Sanguosha->getCard(id), ctx.owner, judge, objectName(), true);
        } else {
            const int count = 2 * getEffectiveAmount(ctx);
            target->drawCards(count, objectName());
            if (!target->isAlive()) return false;
            QList<int> selected = lordExExchange(room, target, "_heg_midao", count, count, "@midao-push");
            QList<int> valid;
            for (int id : selected)
                if (room->getCardOwner(id) == target
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) valid << id;
            if (!valid.isEmpty()) target->addToPile("rice", valid);
        }
        return false;
    }
};

class HFengshiX : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFengshiX(const QString &name = "heg_fengshix") : TriggerSkillV2(name)
    {
        if (name == "heg_fengshix") {
            events << TargetSpecified << ConfirmDamage << CardFinished;
            global = true;
        } else events << TargetConfirmed;
    }
    static QString receiptKey(Room *room)
    {
        const qint64 use = HLiangfanEffect::currentUse(room);
        return use > 0 ? QString("HFengshiX:%1").arg(use) : QString();
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardFinished && data.value<CardUseStruct>().from == player) {
            const QString key = receiptKey(room);
            if (!key.isEmpty()) room->removeTag(key);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to || damage.from != player) return true;
        const QString key = receiptKey(room);
        if (key.isEmpty()) return true;
        for (const QVariant &value : room->getTag(key).toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(),
                SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = value;
            ctx.targets = {damage.to};
            ctx.is_forced = true;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const QString key = receiptKey(room);
        return !key.isEmpty() && room->getTag(key).toList().contains(ctx.extra_data);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.card->getTypeId() != Card::TypeSkill && use.to.size() == 1
            && player->getHandcardNum() > use.to.first()->getHandcardNum() && !use.to.first()->isNude())
            return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage) return true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.to.size() != 1) return false;
        const QVariant previous = ctx.owner->getTag("FengshixUsedata");
        ctx.owner->setTag("FengshixUsedata", *ctx.original_data);
        const auto restore = qScopeGuard([&] { ctx.owner->setTag("FengshixUsedata", previous); });
        return ctx.owner->askForSkillInvoke(this, QVariant::fromValue(use.to.first()));
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage) {
            ctx.targets = {ctx.original_data->value<DamageStruct>().to};
            return false;
        }
        ctx.manual_effect = true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.to.size() != 1 || !use.from) return false;
        QList<ServerPlayer *> recipients{use.from, use.to.first()};
        room->sortByActionOrder(recipients);
        for (ServerPlayer *recipient : recipients) {
            SkillContext discard = ctx;
            skillEffect(event, room, player, discard, recipient);
        }
        // Keep the applied modifier with this resolution, independently of later source removal.
        const QString key = receiptKey(room);
        if (!key.isEmpty()) {
            QVariantList receipts = room->getTag(key).toList();
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"dispatch", lordExReceiptId(ctx.owner)},
                {"amount", getEffectiveAmount(ctx)}};
            room->setTag(key, receipts);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to == target) {
                damage.damage += getEffectiveAmount(ctx);
                *ctx.original_data = QVariant::fromValue(damage);
            }
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive()
            && ctx.owner->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", "heg_fengshix", false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && ctx.owner->canDiscard(target, id)) room->throwCard(id, target, ctx.owner);
        }
        return false;
    }
};

class HFengshiXOther : public HFengshiX
{
public:
    HFengshiXOther() : HFengshiX("#heg_fengshix-other") { frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetConfirmed || !player || !player->isAlive() || !player->hasShownSkill("heg_fengshix")) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.card->getTypeId() != Card::TypeSkill && use.to.size() == 1 && use.to.first() == player
            && use.from && use.from->isAlive() && use.from->getHandcardNum() > player->getHandcardNum() && !player->isNude())
            return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return use.from && lordExChoice(room, use.from, "heg_fengshix", "yes+no", *ctx.original_data,
            "@fengshix:" + ctx.owner->objectName()) == "yes";
    }
};

class HWenji : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HWenji() : TriggerSkillV2("heg_wenji") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
        for (ServerPlayer *other : room->getOtherPlayers(player))
            if (!other->isNude()) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) if (!other->isNude()) candidates << other;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@wenji", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "receive") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(receipt.value("giver").toString());
            const int id = receipt.value("card", -1).toInt();
            if (!giver || !target->isAlive() || room->getCardOwner(id) != giver
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, giver->objectName(), target->objectName(), objectName(), QString());
            room->moveCardTo(Sanguosha->getCard(id), target, Player::PlaceHand, reason, true);
            QVariantMap result = receipt;
            result.insert("received", target->handCards().contains(id));
            ctx.extra_data = result;
            if (receipt.value("grant").toBool() && target->handCards().contains(id)) {
                QVariantList letters = target->getTag("HWenjiLetters").toList();
                letters << QVariantMap{{"card", id}, {"owner", ctx.sourceRef.ownerObjectName},
                    {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                    {"amount", getEffectiveAmount(ctx)}, {"dispatch", lordExReceiptId(target)}};
                target->setTag("HWenjiLetters", letters);
                lordExAddCard(room, target, "@wenji-turn", id);
            }
            return false;
        }
        for (int n = 0; n < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->isAlive() && !target->isNude(); ++n) {
            QList<int> ids = lordExExchange(room, target, "wenji_give", 1, 1, "@wenji-give:" + ctx.owner->objectName());
            // Replies can run nested skills; rebuild any compulsory fallback from current material.
            int id = ids.isEmpty() ? -1 : ids.first();
            if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) {
                const QList<const Card *> cards = target->getCards("he");
                if (cards.isEmpty()) break;
                id = cards.first()->getEffectiveId();
            }
            const bool grant = ctx.owner->isFriendWith(target) || !target->hasShownOneGeneral();
            SkillContext receive = ctx;
            receive.choice = "receive";
            receive.extra_data = QVariantMap{{"giver", target->objectName()}, {"card", id}, {"grant", grant}};
            skillEffect(event, room, player, receive, ctx.owner);
            if (!receive.extra_data.toMap().value("received").toBool() || grant || !ctx.owner->isAlive() || !target->isAlive()) continue;
            QList<int> candidates;
            for (const Card *card : ctx.owner->getCards("he")) if (card->getEffectiveId() != id) candidates << card->getEffectiveId();
            if (candidates.isEmpty()) continue;
            const bool wasFlagged = target->hasFlag("WenjiTarget");
            target->setFlags("WenjiTarget");
            const auto restore = qScopeGuard([&] { if (!wasFlagged) target->setFlags("-WenjiTarget"); });
            ids = lordExExchange(room, ctx.owner, "wenji_giveback", 1, 1, "@wenji-give:" + target->objectName(), QString(), QString("^%1").arg(id));
            candidates.clear();
            for (const Card *card : ctx.owner->getCards("he")) if (card->getEffectiveId() != id) candidates << card->getEffectiveId();
            if (candidates.isEmpty()) continue;
            const int back = !ids.isEmpty() && candidates.contains(ids.first()) ? ids.first() : candidates.first();
            receive.extra_data = QVariantMap{{"giver", ctx.owner->objectName()}, {"card", back}, {"grant", false}};
            skillEffect(event, room, player, receive, target);
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
        events << EventPhaseChanging << CardUsed << CardsMoveBatch << PreCardUsed << CardFinished;
        global = true;
    }
    static QString receiptKey(Room *room)
    {
        const qint64 use = HLiangfanEffect::currentUse(room);
        return use > 0 ? QString("HWenji:%1").arg(use) : QString();
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            player->removeTag("HWenjiLetters");
            lordExSetCards(room, player, "@wenji-turn", {});
        } else if (event == CardsMoveBatch) {
            QVariantList letters;
            QList<int> projection;
            for (const QVariant &value : player->getTag("HWenjiLetters").toList()) {
                const int id = value.toMap().value("card", -1).toInt();
                if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand) continue;
                letters << value;
                if (!projection.contains(id)) projection << id;
            }
            player->setTag("HWenjiLetters", letters);
            lordExSetCards(room, player, "@wenji-turn", projection);
        } else if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            const QString key = receiptKey(room);
            if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill || key.isEmpty()) return true;
            QVariantList receipts;
            for (const QVariant &value : player->getTag("HWenjiLetters").toList())
                if (lordExCardIds(use.card).contains(value.toMap().value("card").toInt())) receipts << value;
            room->setTag(key, receipts);
        } else if (event == CardFinished && data.value<CardUseStruct>().from == player) {
            const QString key = receiptKey(room);
            if (!key.isEmpty()) room->removeTag(key);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed || !player) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || !(use.card->isKindOf("Slash") || use.card->isNDTrick())) return true;
        const QString key = receiptKey(room);
        if (key.isEmpty()) return true;
        for (const QVariant &value : room->getTag(key).toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = value;
            ctx.is_forced = true;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { const QString key = receiptKey(room); return !key.isEmpty() && room->getTag(key).toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Every player whose response is suppressed is an actual recipient, including non-target rescuers.
        ctx.targets = room->getAlivePlayers();
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class HWenjiTargetMod : public TargetModSkillV2
{
public:
    HWenjiTargetMod() : TargetModSkillV2("#heg_wenji-target")
    { pattern = "^SkillCard"; setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.card || !Sanguosha->matchExpPattern(pattern, ctx.primary, ctx.card)
            || (ctx.modType != TargetModSkill::Residue && ctx.modType != TargetModSkill::DistanceLimit)) return CorrectSkillResult::noEffect();
        for (int id : lordExCards(ctx.primary, "@wenji-turn"))
            if (lordExCardIds(ctx.card).contains(id) || ctx.card->hasFlag("Global_AvailabilityChecker"))
                return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

class HTunjiang : public TriggerSkillV2
{
public:
    HTunjiang() : TriggerSkillV2("heg_tunjiang") { events << EventPhaseStart; frequency = Frequent; }
    static bool eligible(Room *room, ServerPlayer *player)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return false;
        QVariant watermark;
        QMap<qint64, QVariantMap> uses;
        // Accepted use and final target snapshots share one frozen journal boundary.
        for (const QString &kind : {QString("use_card"), QString("use_card_targets")}) {
            QVariantMap filter{{"kind", kind}, {"turn_id", turn}, {"from", player->objectName()}};
            if (watermark.isValid()) filter.insert("watermark", watermark);
            for (;;) {
                const QVariantMap page = room->queryHistoryFacts(filter);
                if (!page.value("complete").toBool()) return false;
                watermark = page.value("watermark");
                for (const QVariant &value : page.value("items").toList()) {
                    const QVariantMap fact = value.toMap(), data = fact.value("data").toMap();
                    const qint64 id = fact.value("event_id").toLongLong();
                    if (kind == "use_card") {
                        const QVariantMap phase = room->historyEvent(fact.value("phase_id").toLongLong()).value("data").toMap();
                        if (phase.isEmpty()) return false;
                        if (phase.value("player").toString() != player->objectName() || phase.value("phase").toInt() != Player::Play) continue;
                        const QVariantMap card = data.value("card").toMap();
                        if (!card.contains("type")) return false;
                        if (card.value("type").toInt() != Card::TypeSkill) uses.insert(id, data);
                    } else if (uses.contains(id)) uses[id].insert("targets", data.value("targets"));
                }
                if (!page.value("has_more").toBool()) break;
                filter.insert("after", page.value("next_after"));
                filter.insert("watermark", watermark);
            }
        }
        if (uses.isEmpty()) return false;
        for (const QVariantMap &use : uses)
            for (const QVariant &target : use.value("targets").toList())
                if (target.toString() != player->objectName()) return false;
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && eligible(room, player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke(this); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<ServerPlayer *> factions;
        for (ServerPlayer *other : room->getAlivePlayers()) {
            if (!other->hasShownOneGeneral()) continue;
            bool represented = false;
            for (ServerPlayer *representative : factions) if (representative->isFriendWith(other)) represented = true;
            if (!represented) factions << other;
        }
        target->drawCards(factions.size() * getEffectiveAmount(ctx), objectName());
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
        return qMax(int(to->getEquips().length()), 1);
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *owner = ctx.owner;
        if (!owner->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) return false;
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

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(2 * getEffectiveAmount(ctx), "heg_lixia"); return false; }
        if (ctx.choice == "losehp") { room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), "heg_lixia", ctx.owner)); return false; }
        if (ctx.choice == "discard") {
            room->askForDiscard(target, "lixia_discard", 2 * getEffectiveAmount(ctx), 2 * getEffectiveAmount(ctx), false, true);
            return false;
        }
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive() || !actor->canDiscard(target, "e")) return false;
        const int id = room->askForCardChosen(actor, target, "e", "heg_lixia", false, Card::MethodDiscard);
        if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip || !actor->canDiscard(target, id)) return false;
        CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, actor->objectName(), target->objectName(), "heg_lixia", QString());
        const QVariant moved = room->moveCardsSub(CardsMoveStruct(id, nullptr, Player::DiscardPile, reason), true);
        bool discarded = false;
        for (const QVariant &value : moved.toList()) {
            const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
            if (move.from == target && move.card_ids.contains(id) && move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE)
                discarded = true;
        }
        if (!discarded || !actor->isAlive()) return false;
        QStringList choices{"draw%from:" + target->objectName(), "losehp"};
        const QString all = choices.join('+') + "+discard";
        if (actor->forceToDiscard(2, true, true).size() >= 2) choices << "discard";
        const QString choice = lordExChoice(room, actor, "lixia_effect", choices.join('+'), QVariant(),
            "@lixia-choose:" + target->objectName(), all);
        SkillContext consequence = ctx;
        consequence.choice = choice.startsWith("draw") ? "draw" : choice;
        skillEffect(event, room, player, consequence, consequence.choice == "draw" ? target : actor);
        return false;
    }
};

class HQuanji : public TriggerSkillV2
{
public:
    HQuanji() : TriggerSkillV2("heg_quanji") { events << Damage << Damaged << EventSkillInvoking; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    void commit(SkillContext &ctx) const
    {
        QVariantMap receipt = ctx.interceptor_data.value(objectName());
        if (!ctx.owner || receipt.value("committed").toBool()) return;
        receipt.insert("committed", true);
        ctx.interceptor_data.insert(objectName(), receipt);
        addUsage(ctx);
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking && data.canConvert<SkillContext>()) {
            SkillContext ctx = data.value<SkillContext>();
            if (ctx.activationRef.key.skillName == objectName()) { commit(ctx); data = QVariant::fromValue(ctx); }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return (event == Damage || event == Damaged) && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, lordExUsableEntries(this, room, player)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return isUsable(ctx) && ctx.owner->askForSkillInvoke(this); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; commit(ctx); return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { commit(ctx); ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = getEffectiveAmount(ctx);
        target->drawCards(count, objectName());
        if (!target->isAlive() || target->isNude() || count <= 0) return false;
        const QList<int> selected = lordExExchange(room, target, "_heg_quanji", count, count, "@quanji-push");
        QList<int> available, ids;
        for (const Card *card : target->getCards("he")) available << card->getEffectiveId();
        for (int id : selected) if (available.removeOne(id) && ids.size() < count) ids << id;
        while (ids.size() < count && !available.isEmpty()) ids << available.takeFirst();
        if (!ids.isEmpty()) target->addToPile("power_pile", ids);
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
        return target->getPile("power_pile").length();
    }    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.primary) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(getExtra(context.primary));
    }
};

HPaiyiCard::HPaiyiCard()
{
    setSkillName("heg_paiyi");
    will_throw = true;
    handling_method = Card::MethodNone;
}

bool HPaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

class HPaiyi : public ViewAsSkillV2
{
public:
    HPaiyi() : ViewAsSkillV2("heg_paiyi", 1)
    {
        expand_pile = "power_pile";
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->getPile("power_pile").isEmpty();
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
        return ViewAsSkillV2::createCard(request);
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return selected.isEmpty() && target && target->isAlive(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive()) return ContinueEffects;
        const int amount = getEffectiveAmount(ctx);
        target->drawCards(qMin(int(source->getPile("power_pile").size()), 7) * amount, objectName());
        if (source->isAlive() && target->isAlive() && target->getHandcardNum() > source->getHandcardNum())
            target->getRoom()->damage(DamageStruct(objectName(), source, target, amount));
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HPaiyiCard";
    }
};

HQuanjinCard::HQuanjinCard()
{
    setSkillName("heg_quanjin");
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

class HQuanjin : public ViewAsSkillV2
{
public:
    HQuanjin() : ViewAsSkillV2("heg_quanjin", 1)
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->isKongcheng();
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
        return ViewAsSkillV2::createCard(request);
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(target);
        return selected.isEmpty() && target && target->isAlive() && target != request.initiator
            && (server ? lordExInjuredThisPhase(server) : target->getMark("heg_quanjin_injured") > 0);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || ctx.targets.size() != 1 || !ctx.targets.first()->isAlive()) return false;
        const int id = request.selectedCardIds.first();
        if (room->getCardOwner(id) != request.initiator || room->getCardPlace(id) != Player::PlaceHand) return false;
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, request.initiator->objectName(), ctx.targets.first()->objectName(), objectName(), QString());
        room->moveCardTo(Sanguosha->getCard(id), ctx.targets.first(), Player::PlaceHand, reason, false);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "draw") {
            target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
            return ContinueEffects;
        }
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive()) return ContinueEffects;
        int count = 1;
        if (!target->doCommand(objectName(), source->startCommand(objectName(), target), source)) {
            int maximum = source->getHandcardNum();
            for (ServerPlayer *other : room->getAlivePlayers()) maximum = qMax(maximum, other->getHandcardNum());
            count = qMin(5, maximum - source->getHandcardNum());
        }
        SkillContext draw = ctx;
        draw.choice = "draw";
        draw.extra_data = count;
        if (source->isAlive() && count > 0) skillEffect(draw, source);
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "HQuanjinCard";
    }
};

HZaoyunCard::HZaoyunCard()
{
    setSkillName("heg_zaoyun");
    will_throw = true;
}

bool HZaoyunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->isFriendWith(to_select) && to_select->hasShownOneGeneral()
            && Self->distanceTo(to_select)-1 == subcardsLength();
}

class HZaoyunViewAsSkill : public ViewAsSkillV2
{
public:
    HZaoyunViewAsSkill() : ViewAsSkillV2("heg_zaoyun", 0)
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->hasShownOneGeneral();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }
        return request.initiator->handCards().contains(to_select->getEffectiveId()) && !request.initiator->isJilei(to_select);
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
        return ViewAsSkillV2::createCard(request);
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return request.initiator && selected.isEmpty() && target && target->isAlive() && target != request.initiator
            && target->hasShownOneGeneral() && !request.initiator->isFriendWith(target)
            && request.initiator->distanceTo(target) > 1
            && request.initiator->distanceTo(target) - 1 == request.selectedCardIds.size();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive()) return ContinueEffects;
        QStringList recipients = source->getTag("zaoyun_target").toStringList();
        if (!recipients.contains(target->objectName())) recipients << target->objectName();
        source->setTag("zaoyun_target", recipients);
        target->getRoom()->setFixedDistance(source, target, 1);
        target->getRoom()->damage(DamageStruct(objectName(), source, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
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
        events << EventPhaseChanging;
        global = true;
        view_as_skill = new HZaoyunViewAsSkill;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const override
    {
         if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
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
    setSkillName("heg_diaogui");
    will_throw = false;
}

bool HDiaoguiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    HLureTiger trick(getSuit(), getNumber());
    trick.addSubcard(this);
    trick.setSkillName("heg_diaogui");
    return trick.targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, &trick, targets);
}

class HDiaoguiViewAsSkill : public ViewAsSkillV2
{
public:
    HDiaoguiViewAsSkill() : ViewAsSkillV2("heg_diaogui", 1)
    {
        response_or_use = true;
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->isNude();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || to_select->hasFlag("using") || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty()) return false;
        if (to_select->getTypeId() != Card::TypeEquip
            || (!request.initiator->handCards().contains(to_select->getEffectiveId())
                && !request.initiator->getEquipsId().contains(to_select->getEffectiveId()))) return false;
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
        HLureTiger *trick = new HLureTiger(c->getSuit(), c->getNumber());
        trick->addSubcard(c);
        trick->setSkillName(objectName());
        trick->setShowSkill(objectName());
        QStringList present;
        // Preserve actual seat order, including the initiator, in the authoritative preview.
        QList<const Player *> players = request.initiator->getSiblings(true);
        std::sort(players.begin(), players.end(), [](const Player *a, const Player *b) { return a->getSeat() < b->getSeat(); });
        for (const Player *player : players) if (player->isAlive() && !player->isRemoved()) present << player->objectName();
        trick->setTag("HDiaoguiSeats", present);
        return trick;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HLureTiger"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.extra_data = QVariantMap{{"seats", ctx.use_card ? ctx.use_card->getTag("HDiaoguiSeats") : QVariant()},
            {"dispatch", lordExReceiptId(ctx.invoker)}};
        return ContinueEffects;
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request); }

};


class HDiaogui : public TriggerSkillV2
{
public:
    HDiaogui() : TriggerSkillV2("heg_diaogui")
    { events << CardFinished; global = true; view_as_skill = new HDiaoguiViewAsSkill; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || player != use.from || !use.from->isAlive() || !use.card
            || !use.card->getSkillNames().contains(objectName()) || !use.sourceRef.isValid() || use.skillExecutionID <= 0) return true;
        const SkillContext accepted = room->getSkillExecutionContext(use.skillExecutionID);
        const QVariantMap receipt = accepted.extra_data.toMap();
        if (receipt.value("seats").toStringList().isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = use.from;
        ctx.sourceRef = use.sourceRef;
        ctx.instanceID = receipt.value("dispatch").toInt();
        ctx.setModifiedAmount(getEffectiveAmount(accepted));
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.extra_data = receipt;
        ctx.targets = {use.from};
        ctx.is_forced = true;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && ctx.original_data
        && ctx.original_data->value<CardUseStruct>().sourceRef == ctx.sourceRef; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QStringList before = ctx.extra_data.toMap().value("seats").toStringList();
        QList<ServerPlayer *> after;
        for (ServerPlayer *player : room->getAllPlayers(true))
            if (player->isAlive() && !player->isRemoved()) after << player;
        int count = 0;
        for (int i = 0; i < after.size(); ++i) {
            ServerPlayer *first = after.at(i), *second = after.at((i + 1) % after.size());
            const int oldFirst = before.indexOf(first->objectName()), oldSecond = before.indexOf(second->objectName());
            if (first == second || oldFirst < 0 || oldSecond < 0 || !target->isFriendWith(first) || !target->isFriendWith(second)) continue;
            if ((oldFirst + 1) % before.size() == oldSecond || (oldSecond + 1) % before.size() == oldFirst) continue;
            count = qMax(count, int(first->getFormation().size()));
        }
        if (count > 0) target->drawCards(count * getEffectiveAmount(ctx), objectName());
        return false;
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        bool invoke = false;
        if (player->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) {
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
        for (const QVariant &value : ctx.original_data->toList()) {
            const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
            if (eligible(room, ctx.owner, move) && move.from && !ctx.targets.contains(move.from)) ctx.targets << move.from;
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QVariantList retained;
        for (const QVariant &value : ctx.original_data->toList()) {
            CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
            if (move.from == target && eligible(room, ctx.owner, move)) {
                QList<int> protectedIds;
                for (int i = 0; i < move.card_ids.size() && i < move.from_places.size(); ++i)
                    if (move.from_places.at(i) == Player::PlaceEquip) protectedIds << move.card_ids.at(i);
                move.removeCardIds(protectedIds);
            }
            retained << QVariant::fromValue(move);
        }
        *ctx.original_data = retained;
        return false;
    }
private:
    static bool eligible(Room *room, ServerPlayer *owner, const CardsMoveOneTimeStruct &move)
    {
        if (!owner || !move.from || !move.from_places.contains(Player::PlaceEquip)
            || !owner->inFormationRalation(move.from)) return false;
        if (move.reason.m_reason != CardMoveReason::S_REASON_DISMANTLE
            && !(move.to && move.to != move.from && move.to_place == Player::PlaceHand && move.reason.m_reason != CardMoveReason::S_REASON_GIVE)) return false;
        ServerPlayer *actor = room->findPlayerByObjectName(move.reason.m_playerId);
        return actor && actor->hasShownOneGeneral() && !owner->isFriendWith(actor);
    }
};

class HZhidao : public TriggerSkillV2
{
public:
    HZhidao() : TriggerSkillV2("heg_zhidao")
    { events << EventPhaseStart << EventPhaseChanging; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (const QVariant &value : player->property("heg_zhidao_receipts").toList()) {
            ServerPlayer *target = room->findPlayerByObjectName(value.toMap().value("target").toString(), true);
            if (!target) continue;
            room->removePlayerMark(target, "##zhidao");
            room->setFixedDistance(player, target, -1);
        }
        room->setPlayerProperty(player, "heg_zhidao_receipts", QVariantList());
        room->setPlayerProperty(player, "zhidao_targets", QString());
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID) && !ctx.owner->askForSkillInvoke(this)) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@zhidao-target");
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QVariantList receipts = ctx.owner->property("heg_zhidao_receipts").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
            {"dispatch", lordExReceiptId(ctx.owner)}, {"target", target->objectName()}};
        room->setPlayerProperty(ctx.owner, "heg_zhidao_receipts", receipts);
        QStringList targets = ctx.owner->property("zhidao_targets").toString().split('+', Qt::SkipEmptyParts);
        if (!targets.contains(target->objectName())) targets << target->objectName();
        room->setPlayerProperty(ctx.owner, "zhidao_targets", targets.join('+'));
        room->setFixedDistance(ctx.owner, target, 1);
        room->addPlayerMark(target, "##zhidao");
        return false;
    }
};

class HZhidaoDamage : public TriggerSkillV2
{
public:
    HZhidaoDamage() : TriggerSkillV2("#heg_zhidao-damage") { events << Damage; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || player->getPhase() != Player::Play || !damage.to || !damage.to->isAlive()) return true;
        const QVariant phase = room->historyScopes().value("phase_id");
        const qint64 current = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (phase.toLongLong() <= 0 || current <= 0) return true;
        const QVariantMap history = room->queryActualDamage({{"phase_id", phase}, {"from", player->objectName()},
            {"to", damage.to->objectName()}, {"limit", 1}});
        if (!history.value("complete").toBool()
            || history.value("items").toList().isEmpty()
            || history.value("items").toList().first().toMap().value("event_id").toLongLong() != current) return true;
        for (const QVariant &value : player->property("heg_zhidao_receipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() != damage.to->objectName()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.extra_data = value;
            ctx.targets = {damage.to};
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->property("heg_zhidao_receipts").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "obtain") {
            const int id = ctx.extra_data.toMap().value("card", -1).toInt();
            ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
            if (victim && room->getCardOwner(id) == victim && target->canGet(victim, id))
                room->obtainCard(target, id, false);
            return false;
        }
        for (int n = 0; n < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && target->isAlive() && ctx.invoker->canGet(target, "hej"); ++n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "hej", "heg_zhidao", false, Card::MethodGet);
            if (room->getCardOwner(id) != target || !ctx.invoker->canGet(target, id)) continue;
            SkillContext gain = ctx;
            gain.choice = "obtain";
            QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("card", id); gain.extra_data = receipt;
            skillEffect(event, room, player, gain, ctx.invoker);
        }
        return false;
    }
};

class HZhidaoProhibit : public ProhibitSkill
{
public:
    HZhidaoProhibit() : ProhibitSkill("#heg_zhidao-prohibit") {}
    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        if (!from || !to || from == to || !card || card->getTypeId() == Card::TypeSkill) return false;
        // Each applied restriction keeps its own permitted recipient after its source disappears.
        for (const QVariant &value : from->property("heg_zhidao_receipts").toList())
            if (value.toMap().value("target").toString() != to->objectName()) return true;
        return false;
    }
};

class HJiliX : public TriggerSkillV2
{
public:
    HJiliX() : TriggerSkillV2("heg_jilix") { events << CardFinished; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !use.card || !use.card->isRed()
            || !(use.card->isNDTrick() || use.card->getTypeId() == Card::TypeBasic)
            || use.card->isKindOf("AllianceFeast") || use.to.size() != 1) return {};
        ServerPlayer *target = use.to.first();
        return target->isAlive() && target->hasSkill(objectName()) ? TriggerList{{target, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)
            || ctx.owner->askForSkillInvoke(this, "target:" + use.from->objectName() + "::" + use.card->objectName());
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const CardUseStruct original = ctx.original_data->value<CardUseStruct>();
        // Snapshot the name before nested uses can retire the original transient card.
        const QString name = original.card ? original.card->objectName() : QString();
        for (int n = 0; n < getEffectiveAmount(ctx) && target->isAlive() && original.from && original.from->isAlive(); ++n) {
            Card *card = Sanguosha->cloneCard(name, Card::NoSuit, 0);
            if (!card) break;
            card->setSkillName("_heg_jilix");
            card->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
            card->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
            card->deleteLater();
            CardUseStruct use(card, original.from, QList<ServerPlayer *>{target});
            use.sourceRef = ctx.sourceRef;
            use.activationRef = ctx.activationRef;
            use.skillExecutionID = ctx.executionID;
            room->useCardFromSkillEffect(use, ctx, false);
        }
        return false;
    }
};

class HJiliXDecrease : public TriggerSkillV2
{
public:
    HJiliXDecrease() : TriggerSkillV2("#heg_jilix-decrease") { events << DamageInflicted; frequency = Compulsory; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return -2; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill("heg_jilix")) return {};
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return {};
        const QVariantMap history = room->queryActualDamage({{"phase_id", phase}, {"to", player->objectName()}, {"limit", 2}});
        return history.value("complete").toBool()
            && !history.value("has_more").toBool() && history.value("items").toList().size() == 1
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)
            || ctx.owner->askForSkillInvoke("heg_jilix", "damage");
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.original_data->value<DamageStruct>().to}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || target != ctx.original_data->value<DamageStruct>().to) return false;
        SkillInstanceRef source = ctx.sourceRef;
        QSet<QString> visited;
        while (source.isValid() && !visited.contains(source.ownerObjectName + source.key.toString())) {
            visited.insert(source.ownerObjectName + source.key.toString());
            ServerPlayer *owner = room->findPlayerByObjectName(source.ownerObjectName, true);
            if (!owner) break;
            bool found = false;
            for (const SkillInstance &instance : owner->getSkillInstances()) {
                if (instance.key() != source.key) continue;
                found = true;
                if (instance.bindHead > 0 && owner == target) {
                    target->removeGeneral(instance.bindHead == 1);
                    return true;
                }
                source = instance.parentRef;
                break;
            }
            if (!found) break;
        }
        return true;
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
    setSkillName("heg_imperialedicttrick");
    target_fixed = true;
}

class HImperialEdictTrick : public ViewAsSkillV2
{
public:
    HImperialEdictTrick() : ViewAsSkillV2("heg_imperialedicttrick", 0) { attached_lord_skill = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    static const Player *provider(const ActiveSkillRequest &request)
    {
        if (!request.initiator || !request.sourceRef.isValid()) return nullptr;
        QList<const Player *> players = request.initiator->getAliveSiblings(); players << request.initiator;
        for (const Player *player : players) {
            if (player->objectName() != request.sourceRef.ownerObjectName) continue;
            const int physical = request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "imperial_edict_card", -1).toInt();
            if (physical >= 0 && player->getPile("ImperialEdict").contains(physical)
                && Sanguosha->getCard(physical)->isKindOf("HImperialEdict")) return player;
        }
        return nullptr;
    }
    static QList<int> edicts(const Player *player)
    {
        QList<int> ids;
        if (player) for (int id : player->getPile("ImperialEdict"))
            if (!Sanguosha->getCard(id)->isKindOf("HImperialEdict")) ids << id;
        return ids;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY || provider(request) != request.initiator) return false;
        QSet<int> suits;
        for (int id : edicts(request.initiator)) {
            const Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (suit >= Card::Spade && suit <= Card::Diamond) suits.insert(int(suit));
        }
        return suits.size() == 4;
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HImperialEdictTrickCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request)) return false;
        const QList<int> ids = edicts(ctx.invoker);
        if (ids.isEmpty()) return false;
        DummyCard cards(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.invoker->objectName(), objectName(), QString());
        room->throwCard(&cards, reason, nullptr);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        for (int n = 0; n < getEffectiveAmount(ctx) && target->isAlive(); ++n) {
            QVariantList reservoir = room->getTag("ImperialEdictTrick").toList();
            if (reservoir.isEmpty()) break;
            const int id = reservoir.takeAt(qsanRandomBounded(int(reservoir.size()))).toInt();
            room->setTag("ImperialEdictTrick", reservoir);
            room->setCardMapping(id, nullptr, Player::PlaceWuGu);
            room->obtainCard(target, id, objectName());
        }
        return FinishSkill;
    }
};

HImperialEdictAttachCard::HImperialEdictAttachCard()
{
    setSkillName("heg_imperialedictattach");
    target_fixed = true;
    handling_method = Card::MethodNone;
}

class HImperialEdictAttach : public ViewAsSkillV2
{
public:
    HImperialEdictAttach() : ViewAsSkillV2("heg_imperialedictattach", 0) { attached_lord_skill = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *holder = HImperialEdictTrick::provider(request);
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && holder && request.initiator->isFriendWith(holder);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || card->getEffectiveId() < 0
            || !request.initiator->handCards().contains(card->getEffectiveId()) || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        int maximum = 1;
        if (!lordExBigKingdom(request.initiator))
            for (const Player *other : request.initiator->getAliveSiblings()) if (lordExBigKingdom(other)) { maximum = 2; break; }
        return request.selectedCardIds.size() < maximum;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HImperialEdictAttachCard"; }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    { return canActivate(request) && cardSelectionFeasible(request); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *holder = ctx.invoker->getRoom()->findPlayerByObjectName(ctx.sourceRef.ownerObjectName);
        if (holder) skillEffect(ctx, holder);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        // The gift is an effect; a paid activation survives removal of its grant.
        if (!ctx.invoker || !ctx.use_card) return FinishSkill;
        QList<int> ids;
        for (int id : ctx.use_card->getSubcards()) if (ctx.invoker->handCards().contains(id)) ids << id;
        if (!ids.isEmpty()) target->addToPile("ImperialEdict", ids, true);
        return FinishSkill;
    }
};

class HImperialEdictSkill : public TriggerSkillV2
{
public:
    HImperialEdictSkill() : TriggerSkillV2("heg_ImperialEdict")
    { events << CardsMoveBatch << GeneralShown << GeneralHidden << Death << DFDebut; global = true; }
    bool canPreshow() const override { return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (room->getTag("HImperialEdictSyncing").toBool()) return true;
        room->setTag("HImperialEdictSyncing", true);
        const auto restore = qScopeGuard([&] { room->removeTag("HImperialEdictSyncing"); });
        QVariantList roots;
        for (const QVariant &value : room->getTag("HImperialEdictSources").toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            const int id = receipt.value("instance").toInt();
            if (holder && holder->isAlive() && holder->getPile("ImperialEdict").contains(receipt.value("physical").toInt())
                && holder->hasSkillInstance("heg_imperialedicttrick", id)) roots << value;
            else if (holder) room->detachSkillFromPlayer(holder, SkillInstanceKey("heg_imperialedicttrick", id).toString(), false, false, false);
        }
        for (ServerPlayer *holder : room->getAlivePlayers()) {
            for (int physical : holder->getPile("ImperialEdict")) {
                if (!Sanguosha->getCard(physical)->isKindOf("HImperialEdict")) continue;
                bool found = false;
                for (const QVariant &value : roots)
                    if (value.toMap().value("physical").toInt() == physical && value.toMap().value("owner").toString() == holder->objectName()) found = true;
                if (found) continue;
                // Each physical card owns a real, precisely removable root.
                const int id = room->acquireSkillUnbound(holder, "heg_imperialedicttrick", true, false, false);
                if (id <= 0) continue;
                holder->setSkillInstanceStateValue("heg_imperialedicttrick", id, "imperial_edict_card", physical);
                roots << QVariantMap{{"owner", holder->objectName()}, {"instance", id}, {"physical", physical}};
            }
        }
        room->setTag("HImperialEdictSources", roots);
        for (ServerPlayer *actor : room->getAllPlayers(true)) {
            QList<SkillInstanceRef> desired;
            if (actor->isAlive()) for (const QVariant &value : roots) {
                const QVariantMap receipt = value.toMap();
                ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("owner").toString());
                if (holder && actor->isFriendWith(holder)) desired << SkillInstanceRef(holder->objectName(),
                    SkillInstanceKey("heg_imperialedicttrick", receipt.value("instance").toInt()));
            }
            for (const SkillInstance &instance : actor->getSkillInstances()) {
                if (instance.skillName != "heg_imperialedictattach" || instance.source != SourceAttached
                    || instance.parentRef.key.skillName != "heg_imperialedicttrick") continue;
                if (!desired.contains(instance.parentRef)) room->detachAttachedSkill(SkillInstanceRef(actor->objectName(), instance.key()));
            }
            for (const SkillInstanceRef &parent : desired) {
                const SkillInstanceRef child = room->attachSkillToPlayer(actor, "heg_imperialedictattach", parent);
                if (!child.isValid()) continue;
                ServerPlayer *holder = room->findPlayerByObjectName(parent.ownerObjectName);
                if (holder) actor->setSkillInstanceStateValue(child.key.skillName, child.key.instanceID, "imperial_edict_card",
                    holder->getSkillInstanceStateValue(parent.key.skillName, parent.key.instanceID, "imperial_edict_card", -1));
            }
        }
        return true;
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
                Slash slash(Card::NoSuit, 0);
                slash.setSkillName("_heg_rule_the_world");
                if (p->isAlive() && effect.to->isAlive() && p->canSlash(effect.to, &slash, false))
                    room->useCard(CardUseStruct(&slash, p, effect.to), false);
            }
        }
        if (choice.startsWith("discard")) {
            if (completeEffect && p->canGet(effect.to, "he")) {
                int card_id = room->askForCardChosen(p, effect.to, "he", objectName(), false, Card::MethodGet);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
                if (room->getCardOwner(card_id) == effect.to && p->canGet(effect.to, card_id)
                    && (room->getCardPlace(card_id) == Player::PlaceHand || room->getCardPlace(card_id) == Player::PlaceEquip))
                    room->obtainCard(p, Sanguosha->getCard(card_id), reason, false);
            } else if (!completeEffect && p->canDiscard(effect.to, "he")) {
                int card_id = room->askForCardChosen(p, effect.to, "he", objectName(), false, Card::MethodDiscard);
                if (room->getCardOwner(card_id) == effect.to && p->canDiscard(effect.to, card_id)
                    && (room->getCardPlace(card_id) == Player::PlaceHand || room->getCardPlace(card_id) == Player::PlaceEquip))
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
            Slash slash(Card::NoSuit, 0);
            slash.setSkillName("_heg_conquering");
            CardUseStruct slashUse(&slash, effect.to, target);
            if (completeEffect) slash.setFlags("heg_conquering_damage");
            if (effect.to->isAlive() && target->isAlive() && effect.to->canSlash(target, &slash)) room->useCard(slashUse, false);

            return;
        }
    }

    effect.to->drawCards(completeEffect ? 2 : 1, objectName());
}

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
    ServerPlayer *source = effect.to;
    source->drawCards(8, objectName());
    if (!source->isAlive() || source->isKongcheng()) return;
    QList<int> available = source->forceToDiscard(998, false);
    QList<int> remaining = source->forceToDiscard(6, false);
    if (remaining.isEmpty()) return;
    if (available.size() > remaining.size()) {
        const QList<int> selected = lordExExchange(room, source, objectName(), 998, remaining.size(),
            "@consolidate_country-discard", QString(), ListI2S(available).join(','));
        if (!selected.isEmpty()) remaining = selected;
    }
    for (int id : QList<int>(remaining))
        if (!source->handCards().contains(id) || !source->canDiscard(source, id)) remaining.removeOne(id);
    QList<CardsMoveStruct> moves;
    if (hasFlag("CompleteEffect") && source->getSeemingKingdom() == "wu") {
        QMap<ServerPlayer *, QList<int>> assignments;
        int distributed = 0;
        while (source->isAlive() && !remaining.isEmpty() && distributed < 6) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *other : room->getOtherPlayers(source))
                if (source->isFriendWith(other) && assignments.value(other).size() < 2) candidates << other;
            if (candidates.isEmpty()) break;
            QList<int> offered = remaining;
            // Native selection carries no independent skill activation or global arrangement state.
            const CardsMoveStruct choice = room->askForYijiStruct(source, offered, objectName(), false, false, true,
                qMin(2, 6 - distributed), candidates, CardMoveReason(), "@consolidate_country-give", false, false);
            ServerPlayer *recipient = choice.to ? room->findPlayerByObjectName(choice.to->objectName()) : nullptr;
            if (!recipient || choice.card_ids.isEmpty() || !candidates.contains(recipient)) break;
            for (int id : choice.card_ids) {
                if (assignments.value(recipient).size() >= 2 || distributed >= 6) break;
                if (!source->handCards().contains(id) || !remaining.removeOne(id)) continue;
                assignments[recipient] << id;
                ++distributed;
            }
        }
        for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
            QList<int> ids;
            for (int id : it.value()) if (source->handCards().contains(id)) ids << id;
            if (it.key()->isAlive() && !ids.isEmpty())
                moves << CardsMoveStruct(ids, source, it.key(), Player::PlaceHand, Player::PlaceHand,
                    CardMoveReason(CardMoveReason::S_REASON_GIVE, source->objectName(), it.key()->objectName(), objectName(), QString()));
            else remaining << ids;
        }
    }
    QList<int> discard;
    for (int id : remaining) if (source->handCards().contains(id) && source->canDiscard(source, id)) discard << id;
    if (!discard.isEmpty()) moves << CardsMoveStruct(discard, source, nullptr, Player::PlaceHand, Player::DiscardPile,
        CardMoveReason(CardMoveReason::S_REASON_THROW, source->objectName(), objectName(), QString()));
    if (!moves.isEmpty()) room->moveCardsAtomic(moves, false);
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
    bool galleryOpen = true;
    const auto clearGallery = qScopeGuard([&] { if (galleryOpen) room->clearAG(effect.from); });
    QString choice = lordExChoice(room, effect.from, objectName(), choices.join("+"), QVariant::fromValue(effect.to), QString(), allchoices.join("+"));
    room->clearAG(effect.from);
    galleryOpen = false;

    if (choice == choice1) {
        QList<int> chosen;
        QSet<int> types;
        for (int n = 0; n < 2 && effect.to->isAlive(); ++n) {
            QList<int> candidates;
            for (const Card *card : effect.to->getHandcards())
                if (!types.contains(card->getTypeId()) && effect.to->canDiscard(effect.to, card->getEffectiveId())) candidates << card->getEffectiveId();
            if (candidates.isEmpty()) break;
            const QList<int> selection = lordExExchange(room, effect.to, objectName(), 1, 1, "@chaos-select", QString(), ListI2S(candidates).join(','));
            int selected = !selection.isEmpty() ? selection.first() : -1;
            candidates.clear();
            for (const Card *card : effect.to->getHandcards())
                if (!types.contains(card->getTypeId()) && effect.to->canDiscard(effect.to, card->getEffectiveId())) candidates << card->getEffectiveId();
            if (candidates.isEmpty()) break;
            if (!candidates.contains(selected)) selected = candidates.first();
            chosen << selected;
            types.insert(Sanguosha->getCard(selected)->getTypeId());
        }
        for (int id : QList<int>(chosen))
            if (!effect.to->handCards().contains(id) || !effect.to->canDiscard(effect.to, id)) chosen.removeOne(id);
        if (!chosen.isEmpty()) {
            DummyCard cards(chosen);
            room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_THROW, effect.to->objectName(), QString(), objectName(), QString()), effect.to);
        }
    }

    if (choice == choice2 && effect.from->canDiscard(effect.to, "h")) {
        int card_id = room->askForCardChosen(effect.from, effect.to, "h", objectName(), true, Card::MethodDiscard);
        if (effect.to->handCards().contains(card_id) && effect.from->canDiscard(effect.to, card_id))
            room->throwCard(card_id, effect.to, effect.from);
    }

    if (hasFlag("CompleteEffect") && effect.to->getSeemingKingdom() == "qun" && effect.to->isKongcheng())
        effect.to->drawCards(qMax(0, effect.to->getHp() - effect.to->getHandcardNum()), objectName());
}

class HSuzhi : public TriggerSkillV2
{
public:
    HSuzhi() : TriggerSkillV2("heg_suzhi")
    { events << DamageCaused << CardUsed << CardsMoveBatch << EventPhaseChanging << EventPhaseStart << EventSkillInvoking; frequency = Compulsory; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 3; }
    void commit(SkillContext &ctx) const
    {
        QVariantMap receipt = ctx.interceptor_data.value(objectName());
        if (receipt.value("committed").toBool()) return;
        if (ctx.original_data && ctx.original_data->canConvert<PhaseChangeStruct>()) return;
        receipt.insert("committed", true); ctx.interceptor_data.insert(objectName(), receipt); addUsage(ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking && data.canConvert<SkillContext>()) {
            SkillContext ctx = data.value<SkillContext>();
            if (ctx.activationRef.key.skillName == objectName()) { commit(ctx); data = QVariant::fromValue(ctx); }
        }
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            const QList<int> ids = ListS2I(player->getTag("HSuzhiGrantedFankui").toStringList());
            player->removeTag("HSuzhiGrantedFankui");
            for (int id : ids) room->detachSkillFromPlayer(player, SkillInstanceKey("heg_fankui_simazhao", id).toString(), false, false, false);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() == Player::NotActive) return {};
        bool matches = false;
        if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            matches = damage.from == player && damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))
                && damage.by_user && !damage.chain && !damage.transfer;
        } else if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            matches = use.from == player && use.card && use.card->getTypeId() == Card::TypeTrick
                && (!use.card->isVirtualCard() || use.card->getSubcards().isEmpty());
        } else if (event == CardsMoveBatch) {
            for (const QVariant &value : data.toList()) {
                const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
                if (discardedByOther(move, player) && player->canGet(move.from, "he")) matches = true;
            }
        } else if (event == EventPhaseChanging) matches = data.value<PhaseChangeStruct>().to == Player::NotActive;
        return matches ? TriggerList{{player, lordExUsableEntries(this, room, player)}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ctx.interceptor_data.insert(objectName(), {{"counts", event != EventPhaseChanging}, {"committed", false}});
        return ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID) || ctx.owner->askForSkillInvoke(this, *ctx.original_data);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { if (!isUsable(ctx)) return false; commit(ctx); return true; }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        commit(ctx);
        if (event == DamageCaused) ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        else if (event == CardsMoveBatch) {
            for (const QVariant &value : ctx.original_data->toList()) {
                const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
                if (discardedByOther(move, ctx.owner) && !ctx.targets.contains(move.from)) ctx.targets << move.from;
            }
        } else ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageCaused) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to == target) { damage.damage += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage); }
        } else if (event == CardUsed) target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (event == EventPhaseChanging) {
            const int id = room->acquireSkillFromEffect(target, "heg_fankui_simazhao", ctx, true, false, false);
            if (id <= 0) return false;
            target->setSkillInstanceStateValue("heg_fankui_simazhao", id, "suzhi_source", QVariantMap{
                {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});
            QStringList ids = target->getTag("HSuzhiGrantedFankui").toStringList(); ids << QString::number(id);
            target->setTag("HSuzhiGrantedFankui", ids);
        } else if (ctx.choice == "receive") {
            const QVariantMap selection = ctx.extra_data.toMap();
            ServerPlayer *victim = room->findPlayerByObjectName(selection.value("victim").toString());
            const int id = selection.value("card", -1).toInt();
            if (victim && room->getCardOwner(id) == victim && target->canGet(victim, id)
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) room->obtainCard(target, id, false);
        } else {
            for (int n = 0; n < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive() && ctx.owner->canGet(target, "he"); ++n) {
                const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodGet);
                SkillContext receive = ctx; receive.choice = "receive";
                receive.extra_data = QVariantMap{{"victim", target->objectName()}, {"card", id}};
                skillEffect(event, room, player, receive, ctx.owner);
            }
        }
        return false;
    }
private:
    static bool discardedByOther(const CardsMoveOneTimeStruct &move, const ServerPlayer *owner)
    {
        return move.from && move.from != owner && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            && move.to_place == Player::DiscardPile && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD;
    }
};

class HSuzhiTarget : public TargetModSkillV2
{
public:
    HSuzhiTarget() : TargetModSkillV2("#heg_suzhi-target") { pattern = "TrickCard"; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.holder || !ctx.card
            || ctx.primary->getPhase() == Player::NotActive
            || ctx.card->getTypeId() != Card::TypeTrick || (ctx.card->isVirtualCard() && !ctx.card->getSubcards().isEmpty())) return CorrectSkillResult::noEffect();
        const SkillInstance *helper = ctx.holder->findSkillInstance(ctx.instanceRef.key.skillName, ctx.instanceRef.key.instanceID);
        if (!helper || !helper->parentRef.isValid() || helper->parentRef.ownerObjectName != ctx.primary->objectName()) return CorrectSkillResult::noEffect();
        const SkillInstanceRef &root = helper->parentRef;
        return ctx.primary->getMark(SkillInstanceUtils::formatUsageMarkKey(root.key.skillName, root.key.instanceID, "-Clear")) < 3
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
    }
};

class HZhaoxin : public TriggerSkillV2
{
public:
    HZhaoxin() : TriggerSkillV2("heg_zhaoxin") { events << Damaged; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && !player->isKongcheng() ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.owner->isKongcheng()) return false; room->showAllCards(ctx.owner); return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (other->getHandcardNum() <= ctx.owner->getHandcardNum()) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *other = room->askForPlayerChosen(ctx.owner, candidates, "zhaoxin-exchange", "@zhaoxin-exchange");
        if (!other) return false;
        SkillContext first = ctx, second = ctx;
        first.extra_data = false; second.extra_data = false;
        skillEffect(event, room, player, first, ctx.owner);
        skillEffect(event, room, player, second, other);
        // Exchange is atomic and requires both affected players to accept their own hook.
        if (first.extra_data.toBool() && second.extra_data.toBool() && ctx.owner->isAlive() && other->isAlive()
            && other->getHandcardNum() <= ctx.owner->getHandcardNum()) room->swapCards(ctx.owner, other, "h", objectName(), false);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    { ctx.extra_data = getEffectiveAmount(ctx) > 0; return false; }
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
            if (damage.damage > 1 && player->forceToDiscard(1, true).isEmpty())
                return TriggerList();
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        bool invoke = false;
        if (player->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)) {
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
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.original_data->value<DamageStruct>().damage > 1)
            room->askForDiscard(target, "shicai_discard", 2 * getEffectiveAmount(ctx), 2 * getEffectiveAmount(ctx), false, true);
        else target->drawCards(getEffectiveAmount(ctx), objectName());
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        SkillContext draw = ctx; draw.choice = "draw";
        skillEffect(event, room, player, draw, ctx.invoker);
        const QVariantMap history = room->queryCardUseDamage();
        if (!history.value("complete").toBool()) return false;
        bool injured = false;
        for (const QVariant &entry : history.value("items").toList())
            if (entry.toMap().value("data").toMap().value("to").toString() == ctx.owner->objectName()) injured = true;
        if (!injured || !ctx.owner->isAlive()) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers()) if (eligible(ctx.owner, target)) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, "chenglve_mark", "@chenglve-mark", true);
        if (target) { SkillContext token = ctx; token.choice = "token"; skillEffect(event, room, player, token, target); }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (eligible(ctx.owner, target)) room->addPlayerMark(target, "@halfmaxhp", getEffectiveAmount(ctx));
        return false;
    }
private:
    static bool eligible(ServerPlayer *owner, ServerPlayer *target)
    {
        return owner && target && target->isAlive() && owner->isFriendWith(target) && target->hasShownAllGenerals()
            && target->getMark("@companion") + target->getMark("@halfmaxhp") + target->getMark("@firstshow") + target->getMark("@careerist") == 0;
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        bool invoke = false;
        if (player->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)) {
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
        for (ServerPlayer *other : room->getAlivePlayers())
            if (other->hasShownOneGeneral() && !other->isFriendWith(ctx.owner) && other->inMyAttackRange(ctx.owner)) ctx.targets << other;
        room->sortByActionOrder(ctx.targets);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int n = 0; n < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive(); ++n) {
            RoomState *state = Sanguosha->currentRoomState();
            const auto reason = state->getCurrentCardUseReason();
            const QString pattern = state->getCurrentCardUsePattern();
            const auto prompt = qScopeGuard([=] {
                state->setCurrentCardUseReason(reason);
                state->setCurrentCardUsePattern(pattern);
            });
            if (target->canSlash(ctx.owner) && room->askForUseSlashTo(target, ctx.owner, "@baolie-slash:" + ctx.owner->objectName())) continue;
            if (!ctx.owner->isAlive() || !target->isAlive() || !ctx.owner->canDiscard(target, "he")) break;
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target && ctx.owner->canDiscard(target, id)
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) room->throwCard(id, target, ctx.owner);
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
        if (!Sanguosha->matchExpPattern(pattern, from, card))
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
        return context.modType == TargetModSkill::Residue && value > 0
            ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::useAmount(value);
    }
};


class HAocaiViewAsSkill : public ViewAsSkillV2
{
public:
    HAocaiViewAsSkill() : ViewAsSkillV2("heg_aocai", 0) { response_or_use = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    static QString failureKey(int id) { return SkillInstanceUtils::formatUsageMarkKey("heg_aocai_failed", id, "-Clear"); }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.initiator && ctx.initiator->getMark(failureKey(ctx.activationRef.key.instanceID)) == 0; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getPhase() != Player::NotActive
            || request.initiator->getMark(failureKey(request.activationRef.key.instanceID)) > 0
            || (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)) return false;
        return request.pattern == "slash" || request.pattern == "jink" || request.pattern == "peach" || request.pattern.contains("analeptic");
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const QString name = request.pattern.split('+').first();
        Card *card = Sanguosha->cloneCard(name);
        if (card) {
            card->setSkillName(objectName()); card->setShowSkill(objectName());
            card->setTag("HAocaiRequest", QVariantMap{{"pattern", request.pattern}, {"reason", int(request.reason)}});
        }
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        QScopedPointer<Card> card(Sanguosha->cloneCard(request.pattern.split('+').first()));
        return card ? card->getClassName() : QString();
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { ctx.extra_data = QVariantMap{{"pattern", request.pattern}, {"reason", int(request.reason)}}; return true; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.extra_data = ctx.use_card ? ctx.use_card->getTag("HAocaiRequest") : QVariant();
        ctx.updated_card = nullptr;
        const EffectFlow flow = skillEffect(ctx, ctx.invoker);
        // A canceled private reveal must not fall through with the empty-material preview.
        if (flow == FinishSkill || !ctx.updated_card) { ctx.is_canceled = true; return FinishSkill; }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *) const override
    {
        if (ctx.is_canceled || getEffectiveAmount(ctx) <= 0) { ctx.is_canceled = true; return FinishSkill; }
        Room *room = ctx.invoker->getRoom();
        const QString pattern = ctx.extra_data.toMap().value("pattern").toString();
        const int reason = ctx.extra_data.toMap().value("reason").toInt();
        const QList<int> ids = room->getNCards(2 * getEffectiveAmount(ctx), false);
        const auto restoreCards = qScopeGuard([&] {
            QList<int> pending;
            for (int id : ids)
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) pending << id;
            if (!pending.isEmpty()) room->returnToTopDrawPile(pending);
        });
        QList<int> enabled, disabled;
        const Card::HandlingMethod method = reason == CardUseStruct::CARD_USE_REASON_RESPONSE ? Card::MethodResponse : Card::MethodUse;
        for (int id : ids) {
            const Card *card = Sanguosha->getCard(id);
            if (Sanguosha->matchPattern(pattern, ctx.invoker, card) && !ctx.invoker->isCardLimited(card, method)) enabled << id;
            else disabled << id;
        }
        LogMessage log; log.type = "$ViewDrawPile"; log.from = ctx.invoker; log.card_str = ListI2S(ids).join('+'); room->sendLog(log, ctx.invoker);
        room->fillAG(ids, ctx.invoker, disabled);
        const auto clear = qScopeGuard([&] { room->clearAG(ctx.invoker); });
        const int id = enabled.isEmpty() ? -1 : room->askForAG(ctx.invoker, enabled, true, objectName());
        if (!enabled.contains(id) || room->getCardOwner(id) || room->getCardPlace(id) != Player::DrawPile || room->getDrawPile().contains(id)) {
            room->setPlayerMark(ctx.initiator, failureKey(ctx.activationRef.key.instanceID), 1);
            ctx.is_canceled = true; return FinishSkill;
        }
        const Card *selected = Sanguosha->getCard(id);
        if (!Sanguosha->matchPattern(pattern, ctx.invoker, selected) || ctx.invoker->isCardLimited(selected, method)) { ctx.is_canceled = true; return FinishSkill; }
        Card *replacement = Sanguosha->cloneCard(selected->objectName(), selected->getSuit(), selected->getNumber());
        if (!replacement) { ctx.is_canceled = true; return FinishSkill; }
        replacement->addSubcard(id); replacement->setSkillName(objectName()); replacement->setShowSkill(objectName()); replacement->deleteLater();
        ctx.updated_card = replacement;
        return ContinueEffects;
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



};

HAocaiCard::HAocaiCard()
{
    setSkillName("heg_aocai");
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

HDuwuCard::HDuwuCard()
{
    setSkillName("heg_duwu");
    mute = true;
    target_fixed = true;
}

class HDuwuViewAsSkill : public ViewAsSkillV2
{
public:
    HDuwuViewAsSkill() : ViewAsSkillV2("heg_duwu", 0) { frequency = Limited; limit_mark = "@duwu"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    TargetMode targetMode() const override { return NoTarget; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HDuwuCard"; }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { ctx.extra_data = ctx.invoker->startCommand(objectName()); return true; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    { if (ctx.invoker->getMark("@duwu") > 0) room->removePlayerMark(ctx.invoker, "@duwu"); return true; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        room->doSuperLightbox("heg_zhugeke", objectName());
        const QVariantMap before = room->queryHistoryEvents({{"kind", "dying"}, {"limit", 1}});
        QList<ServerPlayer *> recipients;
        for (ServerPlayer *other : room->getAlivePlayers())
            if (ctx.invoker->inMyAttackRange(other) && !ctx.invoker->willBeFriendWith(other)) recipients << other;
        room->sortByActionOrder(recipients);
        for (ServerPlayer *target : recipients) {
            if (!ctx.invoker->isAlive()) break;
            SkillContext command = ctx; command.choice = "command";
            skillEffect(command, target);
        }
        if (!ctx.invoker->isAlive() || !before.value("complete").toBool()) return FinishSkill;
        QVariantMap filter{{"kind", "dying"}, {"after", before.value("watermark")}};
        bool survivor = false;
        for (;;) {
            const QVariantMap history = room->queryHistoryEvents(filter);
            if (!history.value("complete").toBool()) return FinishSkill;
            for (const QVariant &value : history.value("items").toList()) {
                ServerPlayer *target = room->findPlayerByObjectName(value.toMap().value("data").toMap().value("player").toString(), true);
                if (target && target->isAlive()) survivor = true;
            }
            if (!history.value("has_more").toBool()) break;
            filter.insert("after", history.value("next_after")); filter.insert("watermark", history.value("watermark"));
        }
        if (survivor) { SkillContext loss = ctx; loss.choice = "losehp"; skillEffect(loss, ctx.invoker); }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (ctx.choice == "losehp") room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        else if (!target->doCommand(objectName(), ctx.extra_data.toInt(), ctx.invoker)) {
            room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
            SkillContext draw = ctx; draw.choice = "draw";
            skillEffect(draw, ctx.invoker);
        }
        return ContinueEffects;
    }
};

class HDuwu : public TriggerSkillV2
{
public:
    HDuwu() : TriggerSkillV2("heg_duwu")
    { frequency = Limited; limit_mark = "@duwu"; view_as_skill = new HDuwuViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class HShilu : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    HShilu() : TriggerSkillV2("heg_shilu")
    {
        events << BuryVictim << EventPhaseStart; global = true;
        // Finish burial/rewards (GameRule priority -1) before collecting the deceased generals.
    }

    static QStringList unclaimedGenerals(Room *room, ServerPlayer *victim)
    {
        if (!victim) return {};
        const QStringList claimed = victim->tag.value("HShiluClaimed").toStringList();
        QStringList result;
        for (const QString &name : QStringList{victim->getActualGeneral1Name(), victim->getActualGeneral2Name()}) {
            if (name.isEmpty() || name.contains("sujiang") || claimed.contains(name) || result.contains(name)) continue;
            bool inPile = false;
            for (ServerPlayer *other : room->getAllPlayers(true))
                for (const QString &pile : other->getGeneralPileNames()) if (other->getGeneralPile(pile).contains(name)) inPile = true;
            if (!inPile) result << name;
        }
        return result;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == BuryVictim && player && player == data.value<DeathStruct>().who)
            player->tag.remove("HShiluClaimed");
        return true;
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
        if (unclaimedGenerals(room, death.who).isEmpty()) return result;

        // Burial is dispatched to the victim; owner-indexed candidates retain each
        // surviving holder's exact source, and the effect uses ctx.owner.
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if ((owner && owner->isAlive() && owner->hasSkill(objectName())))
                result.insert(owner, QStringList(objectName()));
        }
        return result;
    }



    bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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
            const QList<int> ids = lordExExchange(room, player, objectName(), player->getGeneralPile("massacre").size(), 0,
                "@shilu:::" + QString::number(player->getGeneralPile("massacre").size()));
            if (ids.isEmpty()) return false;
            ctx.extra_data = ListI2V(ids);
            return true;
        }

        return false;
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (id < 0 || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
            ids << id;
        }
        if (ids.isEmpty() || ids.size() > ctx.owner->getGeneralPile("massacre").size()) return false;
        DummyCard cards(ids); room->throwCard(&cards, objectName(), ctx.owner); return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        QVariant &data = *ctx.original_data;
        if (triggerEvent == BuryVictim) {
            DeathStruct death = data.value<DeathStruct>();
            ServerPlayer *target = death.who;

            int x = (death.damage && death.damage->from == player) ? 2 * getEffectiveAmount(ctx) : 0;

            const QStringList corpse = unclaimedGenerals(room, target);
            if (corpse.isEmpty()) return false;
            QStringList generals = corpse;

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
            // addGeneralToPile commits synchronously; only actual received corpse cards are claimed.
            QStringList claimed = target->tag.value("HShiluClaimed").toStringList();
            for (const QString &name : corpse)
                if (player->getGeneralPile("massacre").contains(name) && !claimed.contains(name)) claimed << name;
            target->tag.insert("HShiluClaimed", claimed);

        } else if (triggerEvent == EventPhaseStart) {
            player->drawCards(ctx.extra_data.toList().size() * getEffectiveAmount(ctx), objectName());
        }

        return false;
    }
};

class HXiongnve : public TriggerSkillV2
{
public:
    HXiongnve() : TriggerSkillV2("heg_xiongnve")
    { events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player) return true;
        const bool turnStart = event == EventPhaseStart && player->getPhase() == Player::RoundStart;
        if (!turnStart && event != EventPhaseChanging) return true;
        QVariantList retained;
        for (const QVariant &value : player->property("heg_xiongnve_receipts").toList()) {
            const bool defence = value.toMap().value("kind").toString() == "defence";
            if ((defence && !turnStart) || (!defence && event != EventPhaseChanging)) retained << value;
        }
        room->setPlayerProperty(player, "heg_xiongnve_receipts", retained);
        if (turnStart) room->setPlayerMark(player, "##xiongnve_avoid", 0);
        if (event == EventPhaseChanging) room->setPlayerMark(player, "##xiongnve", 0);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((event != EventPhaseStart && event != EventPhaseEnd) || !player || !player->isAlive()
            || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
        return player->getGeneralPile("massacre").size() >= (event == EventPhaseEnd ? 2 : 1)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const bool defence = event == EventPhaseEnd;
        if (!ctx.owner->askForSkillInvoke(this, defence ? "defence" : "attack")) return false;
        QStringList available = ctx.owner->getGeneralPile("massacre"), selected;
        for (int n = 0; n < (defence ? 2 : 1); ++n) {
            if (available.isEmpty()) return false;
            const QString name = room->askForGeneral(ctx.owner, available, QString(), true, defence ? "xiongnve_defence" : "xiongnve_attack");
            if (!available.removeOne(name)) return false;
            selected << name;
        }
        ctx.extra_data = selected;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList available = ctx.owner->getGeneralPile("massacre");
        for (const QString &name : ctx.extra_data.toStringList()) if (!available.removeOne(name)) return false;
        for (const QString &name : ctx.extra_data.toStringList()) ctx.owner->removeGeneralFromPile("massacre", name);
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList kingdoms;
        QString kind = "defence";
        if (event == EventPhaseStart) {
            for (const QString &name : ctx.extra_data.toStringList())
                if (const General *general = Sanguosha->getGeneral(name)) kingdoms << general->getKingdoms().split('+', Qt::SkipEmptyParts);
            kind = lordExChoice(room, target, objectName(), "adddamage+extraction+nolimit", QVariant(), "@xiongnve-choice");
            if (kind != "adddamage" && kind != "extraction" && kind != "nolimit") return false;
        }
        QVariantList receipts = target->property("heg_xiongnve_receipts").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"kind", kind}, {"kingdoms", kingdoms},
            {"amount", getEffectiveAmount(ctx)}, {"dispatch", lordExReceiptId(target)}};
        room->setPlayerProperty(target, "heg_xiongnve_receipts", receipts);
        room->addPlayerMark(target, kind == "defence" ? "##xiongnve_avoid" : "##xiongnve");
        return false;
    }
};

class HXiongnveEffect : public TriggerSkillV2
{
public:
    HXiongnveEffect() : TriggerSkillV2("#heg_xiongnve-effect")
    { events << DamageCaused << DamageInflicted; frequency = Compulsory; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !damage.to) return true;
        for (const QVariant &value : player->property("heg_xiongnve_receipts").toList()) {
            const QVariantMap receipt = value.toMap();
            const QString kind = receipt.value("kind").toString();
            if (event == DamageInflicted) {
                if (damage.to != player || kind != "defence" || !damage.from || damage.from == player) continue;
            } else {
                if (damage.from != player || !damage.to->hasShownOneGeneral()
                    || !receipt.value("kingdoms").toStringList().contains(damage.to->getSeemingKingdom())
                    || (kind != "adddamage" && kind != "extraction")) continue;
                if (kind == "extraction" && !player->canGet(damage.to, "he")) continue;
            }
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.setModifiedAmount(receipt.value("amount").toInt());
            ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = value; ctx.choice = kind; ctx.targets = {damage.to};
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->property("heg_xiongnve_receipts").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (ctx.choice == "defence" || ctx.choice == "adddamage") {
            if (damage.to != target) return false;
            damage.damage += (ctx.choice == "defence" ? -1 : 1) * getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(damage);
            return damage.damage <= 0;
        }
        if (ctx.choice == "receive") {
            const int id = ctx.extra_data.toMap().value("card", -1).toInt();
            if (damage.to && room->getCardOwner(id) == damage.to && target->canGet(damage.to, id)
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) room->obtainCard(target, id, false);
            return false;
        }
        for (int n = 0; n < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && target->isAlive() && ctx.invoker->canGet(target, "he"); ++n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", "heg_xiongnve", false, Card::MethodGet);
            SkillContext receive = ctx; receive.choice = "receive";
            QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("card", id); receive.extra_data = receipt;
            skillEffect(event, room, player, receive, ctx.invoker);
        }
        return false;
    }
};

class HXiongnveTarget : public TargetModSkillV2
{
public:
    HXiongnveTarget() : TargetModSkillV2("#heg_xiongnve-target")
    { pattern = "^SkillCard"; setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.secondary || !ctx.card || ctx.modType != TargetModSkill::Residue
            || ctx.card->getTypeId() == Card::TypeSkill || !ctx.secondary->hasShownOneGeneral()) return CorrectSkillResult::noEffect();
        for (const QVariant &value : ctx.primary->property("heg_xiongnve_receipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("kind").toString() == "nolimit" && receipt.value("amount").toInt() > 0
                && receipt.value("kingdoms").toStringList().contains(ctx.secondary->getSeemingKingdom())) return CorrectSkillResult::unlimitedResidue();
        }
        return CorrectSkillResult::noEffect();
    }
};

class HCongcha : public TriggerSkillV2
{
public:
    HCongcha() : TriggerSkillV2("heg_congcha") { events << DrawNCards << EventPhaseStart; global = true; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::RoundStart) return true;
        const QVariantList expired = player->tag.take("HCongchaReceipts").toList();
        for (const QVariant &value : expired) {
            ServerPlayer *target = room->findPlayerByObjectName(value.toMap().value("target").toString());
            if (target) room->removePlayerMark(target, "##congcha");
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == DrawNCards && data.value<DrawStruct>().reason == "draw_phase") {
            for (ServerPlayer *other : room->getAlivePlayers()) if (!other->hasShownOneGeneral()) return {};
            return {{player, {objectName()}}};
        }
        if (event == EventPhaseStart && player->getPhase() == Player::Start)
            for (ServerPlayer *other : room->getOtherPlayers(player)) if (!other->hasShownOneGeneral()) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DrawNCards) { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this); }
        QList<ServerPlayer *> choices;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) if (!other->hasShownOneGeneral()) choices << other;
        if (choices.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, choices, objectName(), "@congcha-target", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num += 2 * getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(draw);
        } else if (!target->hasShownOneGeneral()) {
            QVariantList receipts = ctx.owner->tag.value("HCongchaReceipts").toList();
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"target", target->objectName()},
                {"amount", getEffectiveAmount(ctx)}, {"dispatch", lordExReceiptId(ctx.owner)}};
            ctx.owner->tag.insert("HCongchaReceipts", receipts);
            room->addPlayerMark(target, "##congcha");
        }
        return false;
    }
};

class HCongchaEffect : public TriggerSkillV2
{
public:
    HCongchaEffect() : TriggerSkillV2("#heg_congcha-effect") { events << GeneralShowed; frequency = Compulsory; global = true; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    static QString eventKey(Room *room)
    { return "HCongchaRevealed:" + QString::number(room->currentHistoryEventId()); }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player) return true;
        QVariantList pending;
        const QString key = eventKey(room);
        int consumed = 0;
        // Consume on the first reveal even if the eventual continuation is intercepted.
        for (ServerPlayer *owner : room->getAllPlayers(true)) {
            QVariantList kept;
            for (const QVariant &value : owner->tag.value("HCongchaReceipts").toList()) {
                QVariantMap receipt = value.toMap();
                if (receipt.value("target").toString() != player->objectName()) { kept << value; continue; }
                receipt.insert("actor", owner->objectName()); receipt.insert("event", key); pending << receipt;
                ++consumed;
            }
            owner->tag.insert("HCongchaReceipts", kept);
        }
        player->tag.insert(key, pending);
        // Publish all consumption before MarkChange can re-enter another reveal.
        if (consumed > 0) room->removePlayerMark(player, "##congcha", consumed);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive()) return true;
        for (const QVariant &value : player->tag.value(eventKey(room)).toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("actor").toString());
            if (!owner || !owner->isAlive()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.setModifiedAmount(receipt.value("amount").toInt());
            ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = value;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->isAlive() && ctx.invoker && ctx.invoker->tag.value(ctx.extra_data.toMap().value("event").toString()).toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.choice = ctx.owner->isFriendWith(ctx.invoker) ? "draw" : "losehp";
        ctx.targets = {ctx.invoker};
        if (ctx.choice == "draw") ctx.targets << ctx.owner;
        room->sortByActionOrder(ctx.targets);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(2 * getEffectiveAmount(ctx), "heg_congcha");
        else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), "heg_congcha", ctx.owner));
        return false;
    }
};

class HGongqing : public TriggerSkillV2
{
public:
    HGongqing() : TriggerSkillV2("heg_gongqing") { events << DamageInflicted; frequency = Compulsory; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.from && damage.from->isAlive()
            && damage.from->getAttackRange() > 3 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return ctx.owner->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)
            || ctx.owner->askForSkillInvoke("heg_gongqing", *ctx.original_data);
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage += getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(damage); return false;
    }
};

class HGongqingDecrease : public HGongqing
{
public:
    HGongqingDecrease() { setObjectName("#heg_gongqing-decrease"); }
    int getPriority(TriggerEvent) const override { return -2; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill("heg_gongqing") && damage.from && damage.from->isAlive()
            && damage.from->getAttackRange() < 3 && damage.damage > 1 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage = qMin(damage.damage, 1);
        *ctx.original_data = QVariant::fromValue(damage); return false;
    }
};

HJinfaCard::HJinfaCard()
{
    setSkillName("heg_jinfa");
}

bool HJinfaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isNude();
}

class HJinfa : public ViewAsSkillV2
{
public:
    HJinfa() : ViewAsSkillV2("heg_jinfa", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && (request.initiator->getHandcards().contains(card) || request.initiator->getEquips().contains(card)) && !request.initiator->isJilei(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && !target->isNude() && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        ServerPlayer *source = ctx.invoker;
        if (ctx.choice == "receive") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            ServerPlayer *donor = room->findPlayerByObjectName(receipt.value("donor").toString());
            const int id = receipt.value("card", -1).toInt();
            if (!donor || room->getCardOwner(id) != donor || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
            if (!receipt.value("gift").toBool() && !target->canGet(donor, id)) return ContinueEffects;
            CardMoveReason reason(receipt.value("gift").toBool() ? CardMoveReason::S_REASON_GIVE : CardMoveReason::S_REASON_EXTRACTION,
                donor->objectName(), target->objectName(), objectName(), QString());
            room->obtainCard(target, Sanguosha->getCard(id), reason, false);
            ctx.extra_data = QVariantMap{{"spade", receipt.value("gift").toBool() && room->getCardOwner(id) == target
                && room->getCardPlace(id) == Player::PlaceHand && Sanguosha->getCard(id)->getSuit() == Card::Spade}};
            return ContinueEffects;
        }
        if (ctx.choice == "slash") {
            ServerPlayer *attacker = room->findPlayerByObjectName(ctx.extra_data.toString());
            Slash slash(Card::NoSuit, 0); slash.setSkillName("_heg_jinfa");
            if (attacker && attacker->isAlive() && attacker->canSlash(target, &slash, false)) room->useCardFromSkillEffect(CardUseStruct(&slash, attacker, target), ctx, false);
            return ContinueEffects;
        }
        for (int n = 0; n < getEffectiveAmount(ctx) && source->isAlive() && target->isAlive() && !target->isNude(); ++n) {
            const QList<int> gift = lordExExchange(room, target, "_heg_jinfa", 1, 0, "@jinfa-give:" + source->objectName(), "", "EquipCard");
            int id = -1; bool given = false;
            if (gift.size() == 1 && room->getCardOwner(gift.first()) == target && Sanguosha->getCard(gift.first())->getTypeId() == Card::TypeEquip
                && (room->getCardPlace(gift.first()) == Player::PlaceHand || room->getCardPlace(gift.first()) == Player::PlaceEquip)) { id = gift.first(); given = true; }
            else if (source->canGet(target, "he")) id = room->askForCardChosen(source, target, "he", objectName(), false, Card::MethodGet);
            if (id < 0) continue;
            SkillContext receive = ctx; receive.choice = "receive";
            receive.extra_data = QVariantMap{{"donor", target->objectName()}, {"card", id}, {"gift", given}};
            skillEffect(receive, source);
            if (receive.extra_data.toMap().value("spade").toBool()) {
                SkillContext attack = ctx; attack.choice = "slash"; attack.extra_data = target->objectName();
                skillEffect(attack, source);
            }
        }
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HJinfaCard"; }
};

class HXishe : public TriggerSkillV2
{
public:
    HXishe() : TriggerSkillV2("heg_xishe") { events << EventPhaseStart << Death << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death) {
            const DeathStruct death = data.value<DeathStruct>();
            if (player != death.who || !death.damage || !death.damage->card || !death.damage->from) return true;
            const QVariantMap receipt = death.damage->card->getTag("HXisheReceipt").toMap();
            if (receipt.isEmpty()) return true;
            ServerPlayer *actor = death.damage->from;
            QVariantList receipts = actor->tag.value("HXisheKills").toList();
            if (!receipts.contains(receipt)) receipts << receipt;
            actor->tag.insert("HXisheKills", receipts);
        } else if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            for (ServerPlayer *actor : room->getAllPlayers(true)) actor->tag.remove("HXisheKills");
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Start) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) if (owner != player && owner->canDiscard(owner, "e")) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<int> selected = lordExExchange(room, ctx.owner, objectName(), 1, 0, "@xishe-slash:" + ctx.invoker->objectName(), "", ".|.|.|equipped");
        if (selected.size() != 1) return false;
        ctx.extra_data = selected.first(); ctx.targets = {ctx.invoker}; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceEquip || !ctx.owner->canDiscard(ctx.owner, id) || Sanguosha->getCard(id)->hasFlag("using")) return false;
        room->throwCard(id, ctx.owner); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // A repeated payment belongs to this accepted effect; each Slash still uses the ordinary pipeline.
        const QVariantMap receipt{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}, {"dispatch", lordExReceiptId(ctx.owner)}};
        do {
            for (int n = 0; n < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive(); ++n) {
                Slash slash(Card::NoSuit, 0); slash.setSkillName("_heg_xishe"); slash.setTag("HXisheReceipt", receipt);
                if (!ctx.owner->canSlash(target, &slash, false)) break;
                CardUseStruct use(&slash, ctx.owner, target);
                if (target->getHp() < ctx.owner->getHp()) use.no_respond_list << "_ALL_TARGETS";
                room->useCardFromSkillEffect(use, ctx, false);
            }
            if (!ctx.owner->isAlive() || !target->isAlive() || !ctx.owner->canDiscard(ctx.owner, "e")) break;
            const QList<int> selected = lordExExchange(room, ctx.owner, objectName(), 1, 0, "@xishe-slash:" + target->objectName(), "", ".|.|.|equipped");
            if (selected.size() != 1) break;
            const int id = selected.first();
            if (room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceEquip || !ctx.owner->canDiscard(ctx.owner, id) || Sanguosha->getCard(id)->hasFlag("using")) break;
            room->throwCard(id, ctx.owner);
        } while (ctx.owner->isAlive() && target->isAlive());
        return false;
    }
};

class HXisheTransform : public TriggerSkillV2
{
public:
    HXisheTransform() : TriggerSkillV2("#heg_xishe-transform") { events << EventPhaseChanging; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (ServerPlayer *actor : room->getAlivePlayers()) {
            if (!actor->canTransform() || actor->getMark("xishetransformUsed") > 0) continue;
            for (const QVariant &value : actor->tag.value("HXisheKills").toList()) {
                const QVariantMap receipt = value.toMap();
                SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = actor;
                ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
                ctx.instanceID = receipt.value("dispatch").toInt(); ctx.setModifiedAmount(receipt.value("amount").toInt());
                ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = value; ctx.targets = {actor}; contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getMark("xishetransformUsed") == 0 && ctx.owner->tag.value("HXisheKills").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return lordExChoice(room, ctx.owner, "transform_xishe", "yes+no", QVariant(), "@transform-ask:::xishe") == "yes"; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) > 0 && target->canTransform() && target->getMark("xishetransformUsed") == 0) {
            room->addPlayerMark(target, "xishetransformUsed"); room->transformDeputyGeneral(target, QString(), false);
        }
        return false;
    }
};

HHuaiyiCard::HHuaiyiCard()
{
    setSkillName("heg_huaiyi");
    target_fixed = true;
}

class HHuaiyi : public ViewAsSkillV2
{
public:
    HHuaiyi() : ViewAsSkillV2("heg_huaiyi", 0) {}
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && !request.initiator->isKongcheng() && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.initiator && request.selectedCardIds.isEmpty(); }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    { if (!ctx.invoker || ctx.invoker->isKongcheng()) return false; room->showAllCards(ctx.invoker); return true; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        skillEffect(ctx, ctx.invoker); return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); ServerPlayer *source = ctx.invoker;
        if (ctx.choice == "receive") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            ServerPlayer *donor = room->findPlayerByObjectName(receipt.value("donor").toString());
            const int id = receipt.value("card", -1).toInt();
            if (!donor || room->getCardOwner(id) != donor || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
            if (Sanguosha->getCard(id)->getTypeId() == Card::TypeEquip) target->addToPile("disloyalty", id);
            else room->obtainCard(target, id, false);
            return ContinueEffects;
        }
        if (ctx.choice == "take") {
            for (int n = 0; n < getEffectiveAmount(ctx) && source->isAlive() && target->isAlive() && !target->isNude(); ++n) {
                const int id = room->askForCardChosen(source, target, "he", objectName(), false, Card::MethodNone);
                SkillContext receive = ctx; receive.choice = "receive";
                receive.extra_data = QVariantMap{{"donor", target->objectName()}, {"card", id}};
                skillEffect(receive, source);
            }
            return ContinueEffects;
        }
        bool red = false, black = false;
        for (const Card *card : source->getHandcards()) { red |= card->isRed(); black |= card->isBlack(); }
        if (!red || !black) return ContinueEffects;
        const QString color = lordExChoice(room, source, objectName(), "black+red", QVariant(), "@huaiyi-choose");
        QList<int> ids;
        for (const Card *card : source->getHandcards())
            if (!source->isJilei(card) && !card->hasFlag("using") && ((color == "red" && card->isRed()) || (color == "black" && card->isBlack()))) ids << card->getEffectiveId();
        if (ids.isEmpty()) return ContinueEffects;
        DummyCard discard(ids); room->throwCard(&discard, source);
        if (!source->isAlive()) return ContinueEffects;
        QList<ServerPlayer *> choices;
        for (ServerPlayer *other : room->getOtherPlayers(source)) if (!other->isNude()) choices << other;
        if (choices.isEmpty()) return ContinueEffects;
        QList<ServerPlayer *> selected = room->askForPlayersChosen(source, choices, "huaiyi_snatch", 0, ids.size(), "@huaiyi-snatch:::" + QString::number(ids.size()));
        room->sortByActionOrder(selected);
        for (ServerPlayer *other : selected) {
            if (!source->isAlive()) break;
            SkillContext take = ctx; take.choice = "take"; skillEffect(take, other);
        }
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HHuaiyiCard"; }
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

    bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Preserve nested payment state; V2 owns source activation after consent.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QVariant &data = *ctx.original_data;
        ServerPlayer *player = ctx.owner;
        bool invoke = false;
        if (player->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) {
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

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += target->getPile("disloyalty").size() * getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(draw);
        } else if (getEffectiveAmount(ctx) > 0) room->killPlayer(target);
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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

            ctx.targets = {to};
            return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(qMax(0, target->getMaxHp() - target->getHandcardNum()) * getEffectiveAmount(ctx), objectName()); return false; }
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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
        if (!ask_who->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) return false;
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

        ctx.choice = choice;
        ctx.targets = {ask_who};

        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (ctx.choice == "recover") room->recover(target, RecoverStruct(actor, nullptr, getEffectiveAmount(ctx)));
        else for (int n = 0; n < getEffectiveAmount(ctx) && actor->isAlive() && target->isAlive() && actor->canDiscard(target, "he"); ++n) {
            const int id = room->askForCardChosen(actor, target, "he", "heg_lianpian", false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target && actor->canDiscard(target, id)
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) room->throwCard(id, target, actor);
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = lordExDiscardCount(target, true);
        if (count > 0) target->drawCards(qMin(count, 3) * getEffectiveAmount(ctx), "heg_tongdu");
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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
        if (!ask_who->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) return false;
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
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = lordExDiscardCount(target, true);
        if (count > 0) target->drawCards(qMin(count, 3) * getEffectiveAmount(ctx), "heg_tongdu");
        return false;
    }
};

HQingyinCard::HQingyinCard()
{
    setSkillName("heg_qingyin");
    mute = true;
    target_fixed = true;
}

class HQingyin : public ViewAsSkillV2
{
public:
    HQingyin() : ViewAsSkillV2("heg_qingyin", 0) { frequency = Limited; limit_mark = "@qingyin"; }
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        Card *card = const_cast<Card *>(ViewAsSkillV2::createCard(request));
        if (!card || !request.initiator) return card;
        const SkillInstance *entry = request.initiator->findSkillInstance(request.activationRef.key.skillName, request.activationRef.key.instanceID);
        const int side = entry && request.activationRef.ownerObjectName == request.initiator->objectName() ? entry->bindHead : 0;
        // This is effect metadata, available even when payment is bypassed.
        card->setTag("HQingyinOrigin", QVariantMap{{"side", side}, {"general", side == 1 ? request.initiator->getActualGeneral1Name()
            : side == 2 ? request.initiator->getActualGeneral2Name() : QString()}});
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    { if (ctx.invoker->getMark(limit_mark) > 0) room->removePlayerMark(ctx.invoker, limit_mark); return true; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.extra_data = ctx.use_card ? ctx.use_card->getTag("HQingyinOrigin") : QVariant();
        Room *room = ctx.invoker->getRoom();
        for (ServerPlayer *target : room->getAlivePlayers()) if (ctx.invoker->willBeFriendWith(target)) {
            SkillContext recovery = ctx; recovery.choice = "recover"; skillEffect(recovery, target);
        }
        SkillContext remove = ctx; remove.choice = "remove"; skillEffect(remove, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "recover") {
            if (target->isWounded()) target->getRoom()->recover(target, RecoverStruct(ctx.invoker, nullptr, target->getLostHp() * getEffectiveAmount(ctx)));
        } else {
            // The source's binding was frozen before payment, so losing the skill cannot redirect removal.
            const QVariantMap origin = ctx.extra_data.toMap();
            const int side = origin.value("side").toInt();
            const QString name = side == 1 ? target->getActualGeneral1Name() : side == 2 ? target->getActualGeneral2Name() : QString();
            if ((side == 1 || side == 2) && name == origin.value("general").toString()) target->removeGeneral(side == 1);
        }
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HQingyinCard"; }
};

class HJuejue : public TriggerSkillV2
{
public:
    HJuejue() : TriggerSkillV2("heg_juejue") { events << EventPhaseStart << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseChanging && player) player->tag.remove("HJuejueReceipts");
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Discard && player->getHp() > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke(this); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.owner->getHp() <= 0) return false; room->loseHp(HpLostStruct(ctx.owner, 1, objectName(), ctx.owner)); return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList receipts = target->tag.value("HJuejueReceipts").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}, {"dispatch", lordExReceiptId(target)}};
        target->tag.insert("HJuejueReceipts", receipts);
        return false;
    }
};

class HJuejueEffect : public TriggerSkillV2
{
public:
    HJuejueEffect() : TriggerSkillV2("#heg_juejue-effect") { events << EventPhaseEnd; frequency = Compulsory; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Discard) return true;
        const int discarded = lordExDiscardCount(player, true);
        if (discarded <= 0) return true;
        for (const QVariant &value : player->tag.value("HJuejueReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.setModifiedAmount(receipt.value("amount").toInt());
            ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = value; ctx.choice = QString::number(discarded);
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->tag.value("HJuejueReceipts").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = room->getOtherPlayers(ctx.owner); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = ctx.choice.toInt() * getEffectiveAmount(ctx);
        if (count <= 0) return false;
        const Card *reply = room->askForExchange(target, "heg_juejue", count, count, false,
            "@juejue-discard:" + ctx.owner->objectName() + "::" + QString::number(count), true, ".|.|.|hand");
        QList<int> ids = reply ? reply->getSubcards() : QList<int>();
        if (reply) reply->deleteLater();
        bool valid = ids.size() == count;
        QSet<int> unique;
        for (int id : ids) {
            if (unique.contains(id) || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) valid = false;
            unique.insert(id);
        }
        if (valid) {
            DummyCard put(ids);
            CardMoveReason reason(CardMoveReason::S_REASON_PUT, target->objectName(), "heg_juejue", QString());
            room->throwCard(&put, reason, nullptr);
        } else room->damage(DamageStruct("heg_juejue", ctx.owner, target, getEffectiveAmount(ctx)));
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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
        if (player->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) {
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
        QList<ServerPlayer *> choices;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (target->inSiegeRelation(target, ctx.owner) && ctx.owner->canSlash(target, false)) choices << target;
        if (!choices.isEmpty()) {
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, choices, "_heg_fangyuan", "@fangyuan-slash");
            if (target) ctx.targets = {target};
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int n = 0; n < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive(); ++n) {
            Slash slash(Card::NoSuit, 0); slash.setSkillName("_heg_fangyuan");
            if (!ctx.owner->canSlash(target, &slash, false)) break;
            room->useCardFromSkillEffect(CardUseStruct(&slash, ctx.owner, target), ctx, false);
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
        return CorrectSkillResult::signedAmount(int(to_count.length()) + x);
    }
};

HTonglingCard::HTonglingCard()
{
    setSkillName("heg_tongling_usecard");
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
        if (request.initiator->getHandcards().contains(to_select) && to_select->isAvailable(request.initiator)) {
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
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = Sanguosha->cloneCard(original->objectName(), original->getSuit(), original->getNumber());
        if (!card) return nullptr;
        card->addSubcard(original); card->setSkillName(objectName()); card->setShowSkill("heg_tongling");
        return card;
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target || request.selectedCardIds.size() != 1) return false;
        if (selected.isEmpty() && target->objectName() != request.initiator->property("tongling_usetarget").toString()) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        return card->targetFilter(selected, target, request.initiator) && !request.initiator->isProhibited(target, card, selected);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        if (card->targetFixed()) return selected.isEmpty() && (card->isKindOf("AOE") || card->isKindOf("GlobalEffect"));
        return !selected.isEmpty() && selected.first()->objectName() == request.initiator->property("tongling_usetarget").toString()
            && card->targetsFeasible(selected, request.initiator);
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override { return cardSelectionFeasible(request); }
};

class HTongling : public TriggerSkillV2
{
public:
    HTongling() : TriggerSkillV2("heg_tongling") { events << Damage << EventSkillInvoking; global = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    void commit(SkillContext &ctx) const
    {
        QVariantMap receipt = ctx.interceptor_data.value(objectName());
        if (receipt.value("committed").toBool()) return;
        receipt.insert("committed", true); ctx.interceptor_data.insert(objectName(), receipt); addUsage(ctx);
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking && data.canConvert<SkillContext>()) {
            SkillContext ctx = data.value<SkillContext>();
            if (ctx.activationRef.key.skillName == objectName()) { commit(ctx); data = QVariant::fromValue(ctx); }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage || !player || !player->isAlive() || !player->hasSkill(objectName())
            || !player->hasShownOneGeneral() || player->getPhase() != Player::Play) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.to && damage.to->isAlive() && damage.to->hasShownOneGeneral() && !player->isFriendWith(damage.to)
            ? TriggerList{{player, lordExUsableEntries(this, room, player)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        QList<ServerPlayer *> choices;
        for (ServerPlayer *other : room->getAlivePlayers()) if (ctx.owner->isFriendWith(other)) choices << other;
        if (choices.isEmpty()) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const QVariant old = ctx.owner->tag.value("tongling-damage");
        ctx.owner->tag.insert("tongling-damage", *ctx.original_data);
        const auto restore = qScopeGuard([&] { if (old.isValid()) ctx.owner->tag.insert("tongling-damage", old); else ctx.owner->tag.remove("tongling-damage"); });
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, choices, objectName(), "@tongling-invoke::" + damage.to->objectName(), true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { if (!isUsable(ctx)) return false; commit(ctx); return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { commit(ctx); return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(2 * getEffectiveAmount(ctx), objectName()); return false; }
        if (ctx.choice == "obtain") {
            QList<int> ids = ListV2I(ctx.extra_data.toList());
            if (ids.isEmpty()) return false;
            for (int id : ids) if (room->getCardPlace(id) != Player::PlaceTable) return false;
            DummyCard card(ids); room->obtainCard(target, &card); return false;
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || !damage.to->isAlive()) return false;
        const QList<int> originalIds = lordExCardIds(damage.card);
        Room::AcceptedViewAsEffectScope borrowed(room, target, "heg_tongling_usecard", ctx);
        if (!borrowed.isValid()) return false;
        const auto prompt = lordExPromptScope(room, target, borrowed.activationRef());
        const QVariant oldTarget = target->property("tongling_usetarget"), oldDamage = target->tag.value("tongling-damage");
        room->setPlayerProperty(target, "tongling_usetarget", damage.to->objectName()); target->tag.insert("tongling-damage", *ctx.original_data);
        const auto restore = qScopeGuard([&] {
            room->setPlayerProperty(target, "tongling_usetarget", oldTarget);
            if (oldDamage.isValid()) target->tag.insert("tongling-damage", oldDamage); else target->tag.remove("tongling-damage");
        });
        const CardUseStruct used = room->askForUseCardStruct(target, "@@heg_tongling_usecard", "@tongling-usecard::" + damage.to->objectName(), -1, Card::MethodUse, false);
        const qint64 useId = used.targetModReveal.useHistoryEventId;
        if (!used.card || useId <= 0) return false;
        const QVariantMap history = room->queryCardUseDamage(useId);
        if (!history.value("complete").toBool() || history.value("has_more").toBool()) return false;
        if (history.value("items").toList().isEmpty()) {
            SkillContext obtain = ctx; obtain.choice = "obtain"; obtain.extra_data = ListI2V(originalIds);
            skillEffect(event, room, player, obtain, damage.to);
        } else {
            QList<ServerPlayer *> recipients{target}; if (damage.from && damage.from != target) recipients << damage.from;
            for (ServerPlayer *recipient : recipients) { SkillContext draw = ctx; draw.choice = "draw"; skillEffect(event, room, player, draw, recipient); }
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

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstance *source = ctx.owner->findSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        return source && ctx.owner->isSkillInstanceEffectAvailable(source->skillName, source->instanceID)
            && ctx.original_data->toStringList().contains(source->bindHead == 1 ? "head" : source->bindHead == 2 ? "deputy" : QString());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getAlivePlayers()) if (ctx.owner->distanceTo(target) <= 1) ctx.targets << target;
        room->sortByActionOrder(ctx.targets); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->getGeneral2() || getEffectiveAmount(ctx) <= 0) return false;
        if (target->hasShownAllGenerals()) {
            QStringList choices;
            if (!target->getActualGeneral1Name().contains("sujiang") && !target->isLord()) choices << "head";
            if (!target->getGeneral2Name().contains("sujiang")) choices << "deputy";
            if (choices.isEmpty()) return false;
            const QString choice = lordExChoice(room, target, "jinxian_hide", choices.join('+'), QVariant(), "@jinxian-hide", "head+deputy");
            if (choices.contains(choice) && target->hasShownAllGenerals()) target->hideGeneral(choice == "head");
        } else room->askForDiscard(target, "jinxian_discard", 2 * getEffectiveAmount(ctx), 2 * getEffectiveAmount(ctx), false, true);
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
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
        if (player->isSkillInstanceEffectAvailable(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) {
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
    { ctx.targets = {ctx.original_data->value<DamageStruct>().to}; return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    { return getEffectiveAmount(ctx) > 0; }
};

HJianyanCard::HJianyanCard()
{
    setSkillName("heg_jianyan");
    target_fixed = true;
}

class HJianyan : public ViewAsSkillV2
{
public:
    HJianyan() : ViewAsSkillV2("heg_jianyan", 0) {}
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.initiator->getKingdom() == "shu" && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.initiator && request.selectedCardIds.isEmpty(); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        const QStringList choices{"basic", "trick", "equip", "red", "black"};
        const QStringList patterns{"BasicCard", "TrickCard", "EquipCard", ".|red", ".|black"};
        const QString choice = lordExChoice(room, ctx.invoker, objectName(), choices.join('+'), QVariant(), "@jianyan-choice");
        if (!choices.contains(choice)) return FinishSkill;
        const QString pattern = patterns.at(choices.indexOf(choice));
        for (int n = 0; n < getEffectiveAmount(ctx) && ctx.invoker->isAlive(); ++n) {
            int id = -1;
            for (int pass = 0; pass < 2 && id < 0; ++pass) {
                const QList<int> pile = room->getDrawPile();
                for (int i = pile.size() - 1; i >= 0; --i) if (Sanguosha->matchExpPattern(pattern, nullptr, Sanguosha->getCard(pile.at(i)))) { id = pile.at(i); break; }
                if (id >= 0 || pass == 1) break;
                bool match = false;
                for (int discarded : room->getDiscardPile()) if (Sanguosha->matchExpPattern(pattern, nullptr, Sanguosha->getCard(discarded))) { match = true; break; }
                if (!match) break;
                room->swapPile();
            }
            if (id < 0) break;
            const Card *card = Sanguosha->getCard(id);
            CardMoveReason reason(CardMoveReason::S_REASON_TURNOVER, ctx.invoker->objectName(), objectName(), QString());
            room->moveCardTo(card, nullptr, Player::PlaceTable, reason, true);
            const auto cleanup = qScopeGuard([&] {
                if (room->getCardPlace(id) == Player::PlaceTable) {
                    CardMoveReason finish(CardMoveReason::S_REASON_NATURAL_ENTER, ctx.invoker->objectName(), objectName(), QString());
                    room->throwCard(Sanguosha->getCard(id), finish, nullptr);
                }
            });
            if (room->getCardPlace(id) != Player::PlaceTable || !ctx.invoker->isAlive()) continue;
            QList<ServerPlayer *> males;
            for (ServerPlayer *other : room->getAlivePlayers()) if (other->isMale()) males << other;
            if (males.isEmpty()) continue;
            const int old = ctx.invoker->getMark(objectName()); ctx.invoker->setMark(objectName(), id);
            const auto restore = qScopeGuard([&] { ctx.invoker->setMark(objectName(), old); });
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, males, objectName(),
                QString("@jianyan-give:::%1:%2\\%3").arg(card->objectName()).arg(card->getSuitString() + "_char").arg(card->getNumberString()));
            if (target) { SkillContext receive = ctx; receive.extra_data = id; skillEffect(receive, target); }
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (target->getRoom()->getCardPlace(id) == Player::PlaceTable) target->getRoom()->obtainCard(target, id);
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HJianyanCard"; }
};

HJujianCard::HJujianCard()
{
    setSkillName("heg_jujian");
}

bool HJujianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->isFriendWith(to_select) && to_select->getMark("jujiantransformUsed") == 0;
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
        return (request.initiator->getHandcards().contains(to_select) || request.initiator->getEquips().contains(to_select))
            && to_select->getTypeId() != Card::TypeBasic;
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
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return selected.isEmpty() && target && target->isAlive() && request.initiator->isFriendWith(target) && target->getMark("jujiantransformUsed") == 0; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(2 * getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
        if (target->getMark("jujiantransformUsed") > 0 || !target->canTransform() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        room->addPlayerMark(target, "jujiantransformUsed"); room->transformDeputyGeneral(target);
        const General *general = target->getGeneral2();
        if (!general) return ContinueEffects;
        bool compulsory = false;
        for (const Skill *skill : general->getVisibleSkillList()) if (skill->getFrequency() == Skill::Compulsory) { compulsory = true; break; }
        if (compulsory) {
            QList<ServerPlayer *> recipients{ctx.invoker}; if (target != ctx.invoker) recipients << target;
            for (ServerPlayer *recipient : recipients) { SkillContext draw = ctx; draw.choice = "draw"; skillEffect(draw, recipient); }
        }
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HJujianCard"; }
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

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke(this); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        Room::AcceptedViewAsEffectScope borrowed(room, ctx.owner, objectName(), ctx);
        if (!borrowed.isValid()) return false;
        const auto prompt = lordExPromptScope(room, ctx.owner, borrowed.activationRef());
        room->askForUseCard(ctx.owner, "@@heg_jujian", "@jujian-card", -1, Card::MethodDiscard);
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
            if (use.card && use.card->hasFlag("GlobalCardUseDisresponsive")) {
                use.no_respond_list << "_ALL_TARGETS";
                data = QVariant::fromValue(use);
            }
        } else if (player && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            room->setPlayerMark(player, "heg_xingzhao_maxcards", 0);
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
            for (const QString &general : player->getGeneralPile("massacre"))
                player->removeGeneralFromPile("massacre", general);
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

    skills << new HLordEXRecord << new HQuanjinTargets << new HFankuiSimazhao << new HTonglingUseCard;
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

    addMetaObject<HImperialEdictAttachCard>();
    addMetaObject<HImperialEdictTrickCard>();

    skills << new HImperialEdictSkill << new HImperialEdictAttach << new HImperialEdictTrick;
}

ADD_PACKAGE(HLordEXCard)
