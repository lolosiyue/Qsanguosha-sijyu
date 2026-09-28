/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the MOL General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    Mogara
    *********************************************************************/

#include "h-mol.h"
#include "skill-instance-utils.h"
#include "h-formation.h"
#include <QScopeGuard>
#include "standard.h"
#include "h-standard-tricks.h"
#include "h-standard-shu-generals.h"
#include "client.h"
#include "engine.h"
#include "structs.h"
#include "gamerule.h"
#include "settings.h"
#include "roomthread.h"
#include "json.h"
#include "room.h"
#include "serverplayer.h"
#include "general.h"
#include "util.h"
#include <memory>
#include "skill-declaration.h"
// New HEG donor: TODO/QSanguosha-For-Hegemony-xxyheaven @ cf61c15.
// Namespace mapping: docs/hegemony-xxy-names.json.

namespace {
QStringList molState(const Player *player, const char *property)
{
    return player->property(property).toString().split('+', Qt::SkipEmptyParts);
}

void setMolState(Room *room, ServerPlayer *player, const char *property,
                 const QStringList &values, const QString &markStem)
{
    const QStringList previous = molState(player, property);
    if (previous == values) return;
    // String properties use the native reconnect transport; composite marks
    // render public values through the existing translated mark display.
    if (!previous.isEmpty())
        room->setPlayerMark(player, markStem + "+" + previous.join("+"), 0);
    room->setPlayerProperty(player, property, values.join("+"));
    if (!values.isEmpty())
        room->setPlayerMark(player, markStem + "+" + values.join("+"), 1);

}

void appendMolState(Room *room, ServerPlayer *player, const char *property,
                    const QString &value, const QString &markStem)
{
    QStringList values = molState(player, property);
    if (!values.contains(value)) values << value;
    setMolState(room, player, property, values, markStem);
}

QList<int> jiansuMoney(const Player *player)
{
    QList<int> ids;
    for (const QString &value : molState(player, "heg_jiansu_money"))
        ids << value.toInt();
    return ids;
}

void setJiansuMoney(Room *room, ServerPlayer *player, const QList<int> &ids)
{
    QStringList values;
    for (int id : ids) values << QString::number(id);
    // Eligible card ids are owner-only, including reconnect. PlayerStateService
    // rejects spectator delivery of this private property.
    player->setProperty("heg_jiansu_money", values.join("+"));
    player->addProperty("heg_jiansu_money");
    room->notifyProperty(player, player, "heg_jiansu_money");
    room->setPlayerMark(player, "@money", ids.length());
}
}

class HMOLStateRecord : public TriggerSkillV2
{
public:
    explicit HMOLStateRecord(const QString &name) : TriggerSkillV2(name) { global = true; events << EventLoseSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *target, QVariant &data) const override
    {
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        // Piles are bearer resources; removing one grant must preserve every other copy.
        if (!target || change.instanceID <= 0 || target->ownsSkill(change.skillName)
            || target->hasSkillInstance(change.skillName, change.instanceID)) return false;
        if (change.skillName == "heg_tunchu") target->clearOnePrivatePile("heg_food");
        else if (change.skillName == "heg_sidi") target->clearOnePrivatePile("heg_drive");
        else if (change.skillName == "heg_yinbingx") target->clearOnePrivatePile("kerchief");
        return false;
    }
};
class HJuzhanProhibit : public ProhibitSkill
{
public:
    HJuzhanProhibit() : ProhibitSkill("#heg_juzhan-prohibit") { }
    bool isProhibited(const Player *from, const Player *to, const Card *card,
                      const QList<const Player *> &) const override
    {
        // Donor GlobalProhibit applies only to non-skill cards for this turn.
        return from && to && card && card->getTypeId() != Card::TypeSkill
            && molState(from, "heg_juzhan_prohibited").contains(to->objectName());
    }
};

class HWuku : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HWuku() : TriggerSkillV2("heg_wuku")
    {
        events << CardUsed << EventLoseSkill;
        frequency = Compulsory; global = true;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventLoseSkill && player && data.value<SkillChangeStruct>().skillName == objectName()
            && !player->ownsSkill(objectName())) {
            room->setPlayerMark(player, "#heg_wuku", 0);
        }
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardUsed && player != NULL && player->hasShownOneGeneral()) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() == Card::TypeEquip) {
                QList<ServerPlayer *> owners = room->findPlayersBySkillName(objectName());
                TriggerList skill_list;
                foreach (ServerPlayer *owner, owners)
                    if (owner->hasShownOneGeneral() && !player->isFriendWith(owner) && owner->getMark("#heg_wuku") < 2)
                        skill_list.insert(owner, QStringList(objectName()));
                return skill_list;
            }
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
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
    {
        ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = qMin(getEffectiveAmount(ctx), qMax(0, 2 - target->getMark("#heg_wuku")));
        if (count > 0) target->gainMark("#heg_wuku", count);
        return false;
    }
};

// These declarations produce native cards. V2 owns payment, the exact source,
// and reveal; previews never consume Wuku or the alternating Guishu state.
class HMiewu : public ViewAsSkillV2
{
public:
    HMiewu() : ViewAsSkillV2("heg_miewu", 1) { response_or_use = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::guhuo(objectName(), true, true, false, false, true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || player->getMark("#heg_wuku") < 1) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && !usableNames(request).isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    { return request.initiator && ViewAsSkillV2::canSelectCard(request, candidate) && candidate && !candidate->hasFlag("using")
        && (request.initiator->handCards().contains(candidate->getEffectiveId()) || request.initiator->hasEquip(candidate)); }
    bool pay(Room *room, SkillContext &context, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)
            || !ViewAsSkillV2::pay(room, context, request)) return false;
        ServerPlayer *player = context.initiator;
        if (!player) return false;
        player->loseMark("#heg_wuku");
        return true;
    }

protected:
    Card *buildCard(const ActiveSkillRequest &request, const QString &name) const override
    {
        Card *card = ViewAsSkillV2::buildCard(request, name);
        if (card) card->setShowSkill(objectName());
        return card;
    }
};

class HMiewuDraw : public TriggerSkillV2
{
public:
    HMiewuDraw() : TriggerSkillV2("#heg_miewu-draw") { events << EventSkillInvoking; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &data, QList<SkillContext> &out) const override
    {
        const SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName != "heg_miewu" || !accepted.sourceRef.isValid() || accepted.is_canceled
            || !accepted.initiator || !accepted.use_card || accepted.use_card->getSkillName() != "heg_miewu") return true;
        // This is the accepted conversion's draw, not a new activation of a live helper.
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = accepted.initiator;
        ctx.sourceRef = accepted.sourceRef; ctx.instanceID = accepted.sourceRef.key.instanceID;
        ctx.amount = getEffectiveAmount(accepted); ctx.current_event = event; ctx.original_data = &data; ctx.is_forced = true;
        ctx.extra_data = QVariantMap{{"activation_owner", accepted.activationRef.ownerObjectName}, {"activation_skill", accepted.activationRef.key.skillName},
            {"activation_instance", accepted.activationRef.key.instanceID}};
        ctx.preferredTarget = accepted.initiator; ctx.targets = {accepted.initiator}; out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.sourceRef.isValid() && !ctx.extra_data.toMap().isEmpty(); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), "heg_miewu"); return false; }
};
class HGuishuViewAsSkill : public ViewAsSkillV2
{
public:
    HGuishuViewAsSkill() : ViewAsSkillV2("heg_guishu", 1) { response_or_use = true; }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::juguan(objectName(), "befriend_attacking,known_both"); }
    SkillDeclarationReason declarationReason(const Player *self, const QString &value, const Card *) const override
    {
        if (!self || (value != "befriend_attacking" && value != "known_both"))
            return SkillDeclarationReason::CandidateUnavailable;
        // The legacy presenter has no selected reference; exact admission is below.
        for (int id : self->getValidSkillInstanceIds(objectName()))
            if (self->getSkillInstanceStateValue(objectName(), id, "last_card").toString() != value)
                return SkillDeclarationReason::None;
        return SkillDeclarationReason::CandidateUnavailable;
    }
    SkillDeclarationReason declarationReason(const ActiveSkillRequest &request,
        const QString &value, const Card *) const override
    {
        if (!request.initiator || !request.activationRef.isValid()
            || request.activationRef.key.skillName != objectName()
            || (value != "befriend_attacking" && value != "known_both"))
            return SkillDeclarationReason::CandidateUnavailable;
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator)) {
            Room *room = server->getRoom();
            const QVariant turn = room->historyScopes().value("turn_id");
            if (turn.toLongLong() <= 0) return SkillDeclarationReason::CandidateUnavailable;
            QVariantMap query{{"kind", "use_card"}, {"turn_id", turn}, {"from", server->objectName()}};
            QString previous; qint64 last = 0;
            while (true) {
                const QVariantMap page = room->queryHistoryFacts(query);
                if (!page.value("complete").toBool()) return SkillDeclarationReason::CandidateUnavailable;
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap(), used = fact.value("data").toMap(), card = used.value("card").toMap();
                    if (card.value("skill_name").toString() != objectName()) continue;
                    if (!used.contains("activation_instance_id")) return SkillDeclarationReason::CandidateUnavailable;
                    if (used.value("activation_owner").toString() != request.activationRef.ownerObjectName
                        || used.value("activation_skill").toString() != request.activationRef.key.skillName
                        || used.value("activation_instance_id").toInt() != request.activationRef.key.instanceID) continue;
                    if (fact.value("sequence").toLongLong() > last) { last = fact.value("sequence").toLongLong(); previous = card.value("name").toString(); }
                }
                if (!page.value("has_more").toBool()) break;
                query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
            }
            return previous != value ? SkillDeclarationReason::None : SkillDeclarationReason::CandidateUnavailable;
        }
        // Clients use the synchronized declaration projection; server admission uses facts.
        for (const Player *holder : request.initiator->getSiblings(true)) {
            if (holder->objectName() != request.activationRef.ownerObjectName) continue;
            if (holder->hasSkillInstance(objectName(), request.activationRef.key.instanceID)
                && holder->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID,
                    "last_card").toString() != value) return SkillDeclarationReason::None;
        }
        return SkillDeclarationReason::CandidateUnavailable;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return ViewAsSkillV2::canSelectCard(request, candidate) && candidate
            && candidate->getSuit() == Card::Spade && !candidate->hasFlag("using")
            && request.initiator && request.initiator->handCards().contains(candidate->getEffectiveId());
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)
            || declarationReason(request, request.userString, nullptr) != SkillDeclarationReason::None)
            return nullptr;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        if (!canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()))) return nullptr;
        Card *card = Sanguosha->cloneCard(request.userString);
        if (!card) return nullptr;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        card->setShowSkill(objectName());
        card->setCanRecast(false);
        return card;
    }
    bool pay(Room *room, SkillContext &context, const ActiveSkillRequest &request) const override
    {
        if (declarationReason(request, request.userString, nullptr) != SkillDeclarationReason::None
            || !ViewAsSkillV2::pay(room, context, request)) return false;
        return true;
    }
};

class HGuishu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    SkillDialogInfo getDialogInfo() const override
    { return view_as_skill->getDialogInfo(); }
    SkillDeclarationReason declarationReason(const Player *self, const QString &value, const Card *card) const override
    { return view_as_skill->declarationReason(self, value, card); }
    SkillDeclarationReason declarationReason(const ActiveSkillRequest &request,
        const QString &value, const Card *card) const override
    { return view_as_skill->declarationReason(request, value, card); }
    HGuishu() : TriggerSkillV2("heg_guishu")
    {
        view_as_skill = new HGuishuViewAsSkill;
        events << EventPhaseStart << CardUsed; global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.activationRef.key.skillName != objectName() || !use.card) return false;
            ServerPlayer *holder = room->findPlayerByObjectName(use.activationRef.ownerObjectName, true);
            if (holder && holder->hasSkillInstance(objectName(), use.activationRef.key.instanceID))
                holder->setSkillInstanceStateValue(objectName(), use.activationRef.key.instanceID, "last_card", use.card->objectName());
            return false;
        }
        // Rebuild only the UI projection from the current turn, including resumed outer turns.
        const QVariant turn = room->historyScopes().value("turn_id"); if (turn.toLongLong() <= 0) return false;
        QVariantMap query{{"kind", "use_card"}, {"turn_id", turn}};
        QHash<QString, QPair<qint64, QString>> latest;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query); if (!page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), used = fact.value("data").toMap();
                if (used.value("activation_skill").toString() != objectName()) continue;
                const QString key = used.value("activation_owner").toString() + ":" + used.value("activation_instance_id").toString();
                const qint64 sequence = fact.value("sequence").toLongLong();
                if (sequence > latest.value(key).first) latest[key] = qMakePair(sequence, used.value("card").toMap().value("name").toString());
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        for (ServerPlayer *holder : room->getAllPlayers(true)) for (int id : holder->getSkillInstanceIds(objectName()))
            holder->setSkillInstanceStateValue(objectName(), id, "last_card", latest.value(holder->objectName() + ":" + QString::number(id)).second);
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};
class HYuanyu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYuanyu() : TriggerSkillV2("heg_yuanyu")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->isAlive() && !damage.from->inMyAttackRange(player))
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)) {
            room->sendCompulsoryTriggerLog(player, objectName());
            invoke = true;
        } else invoke = player->askForSkillInvoke(this, data);
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to != target) return false;
        damage.damage -= getEffectiveAmount(ctx);
        data = QVariant::fromValue(damage);
        if (damage.damage <= 0)
            return true;
        return false;
    }
};

class HSidi : public TriggerSkillV2
{
public:
    HSidi() : TriggerSkillV2("heg_sidi") { events << Damaged << EventPhaseEnd << EventPhaseChanging; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    static QString type(const Card *card)
    { return card->getTypeId() == Card::TypeBasic ? "BasicCard" : card->getTypeId() == Card::TypeTrick ? "TrickCard" : "EquipCard"; }
    static QStringList available(ServerPlayer *holder)
    {
        QStringList types{"BasicCard", "TrickCard", "EquipCard"};
        for (int id : holder->getPile("heg_drive")) types.removeAll(type(Sanguosha->getCard(id)));
        return types;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        QVariantList kept, expired;
        for (const QVariant &entry : room->getTag("HSidiApplied").toList()) {
            if (entry.toMap().value("turn").toLongLong() == turn) expired << entry; else kept << entry;
        }
        room->setTag("HSidiApplied", kept);
        for (const QVariant &entry : expired) {
            const QVariantMap value = entry.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(value.value("target").toString(), true);
            if (!target) continue;
            if (value.value("kind").toString() == "card") room->removePlayerCardLimitationByReason(target, value.value("token").toString());
            else room->removeSkillInvalidity(target, value.value("disabled_skill").toString(), value.value("token").toString(), objectName(), value.value("disabled_instance").toInt());
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        for (ServerPlayer *holder : room->findPlayersBySkillName(objectName())) {
            if (event == Damaged && !player->isNude() && holder->isFriendWith(player) && !available(holder).isEmpty()) result[holder] << objectName();
            else if (event == EventPhaseEnd && player->getPhase() == Player::RoundStart && !holder->isFriendWith(player)
                && !holder->getPile("heg_drive").isEmpty() && room->historyScopes().value("turn_id").toLongLong() > 0) result[holder] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Damaged) {
            if (!ctx.owner->askForSkillInvoke(this, QVariant::fromValue(ctx.invoker))) return false;
            ctx.choice = "put"; ctx.targets = {ctx.invoker}; return true;
        }
        const QList<int> ids = room->askForExchangeCards(ctx.owner, objectName(), 3, 0, "@sidi-remove::" + ctx.invoker->objectName(), "heg_drive");
        if (ids.isEmpty()) return false;
        QVariantList cards; QStringList types;
        for (int id : ids) {
            if (!ctx.owner->getPile("heg_drive").contains(id) || cards.contains(id)) return false;
            cards << id; types << type(Sanguosha->getCard(id));
        }
        ctx.extra_data = QVariantMap{{"cards", cards}, {"types", types}};
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Damaged) return true;
        DummyCard cards;
        for (const QVariant &entry : ctx.extra_data.toMap().value("cards").toList()) {
            const int id = entry.toInt(); if (!ctx.owner->getPile("heg_drive").contains(id) || Sanguosha->getCard(id)->hasFlag("using")) return false; cards.addSubcard(id);
        }
        room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), ""), nullptr);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == Damaged) return false;
        ctx.manual_effect = true;
        QStringList choices{"cardlimit", "skilllimit", "recover"};
        const QStringList types = ctx.extra_data.toMap().value("types").toStringList();
        for (int i = 0; i < types.size() && !choices.isEmpty() && ctx.invoker->isAlive(); ++i) {
            const QString selected = room->askForChoice(ctx.owner, "sidi_choice", choices.join("+"), QVariant(), "cardlimit+skilllimit+recover", "@sidi-choice::" + ctx.invoker->objectName());
            if (!choices.removeOne(selected)) break;
            SkillContext child = ctx; child.choice = selected;
            if (selected == "recover") {
                QList<ServerPlayer *> friends;
                for (ServerPlayer *candidate : room->getOtherPlayers(ctx.owner)) if (candidate->isFriendWith(ctx.owner) && candidate->canRecover()) friends << candidate;
                if (friends.isEmpty()) continue;
                ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, friends, "sidi_recover", "@sidi-recover");
                if (target) skillEffect(event, room, owner, child, target);
            } else skillEffect(event, room, owner, child, ctx.invoker);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.choice == "put") {
            const QStringList types = available(ctx.owner);
            if (types.isEmpty()) return false;
            const QList<int> selected = room->askForExchangeCards(target, "sidi_put", amount, 0, "@sidi-put:" + ctx.owner->objectName(), QString(), types.join(","));
            QVariantList cards; QStringList remaining = types;
            for (int id : selected) {
                const Card *card = Sanguosha->getCard(id);
                if (card && !card->hasFlag("using") && room->getCardOwner(id) == target && (target->handCards().contains(id) || target->hasEquip(card)) && remaining.removeOne(type(card))) cards << id;
            }
            if (cards.isEmpty()) return false;
            SkillContext child = ctx; child.choice = "pile"; child.extra_data = QVariantMap{{"cards", cards}, {"from", target->objectName()}};
            skillEffect(event, room, owner, child, ctx.owner); return false;
        }
        if (ctx.choice == "pile") {
            const QVariantMap value = ctx.extra_data.toMap();
            ServerPlayer *from = room->findPlayerByObjectName(value.value("from").toString(), true);
            QList<int> cards; QStringList types = available(target);
            for (const QVariant &entry : value.value("cards").toList()) {
                const int id = entry.toInt();
                const Card *card = Sanguosha->getCard(id);
                if (from && card && !card->hasFlag("using") && room->getCardOwner(id) == from
                    && (from->handCards().contains(id) || from->hasEquip(card)) && types.removeOne(type(card))) cards << id;
            }
            if (!cards.isEmpty()) target->addToPile("heg_drive", cards);
            return false;
        }
        if (ctx.choice == "recover") { room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount)); return false; }
        const qint64 serial = room->getTag("HSidiSerial").toLongLong() + 1; room->setTag("HSidiSerial", serial);
        const QString token = QString("heg_sidi:%1").arg(serial);
        QVariantMap receipt{{"target", target->objectName()}, {"token", token}, {"turn", room->historyScopes().value("turn_id")},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
        QString selected, name; int id = 0;
        if (ctx.choice == "cardlimit") {
            const QStringList types = ctx.extra_data.toMap().value("types").toStringList();
            selected = room->askForChoice(ctx.owner, "sidi_cardtype", types.join("+"), QVariant(), "BasicCard+EquipCard+TrickCard", "@sidi-cardtype::" + target->objectName());
            if (!types.contains(selected)) return false;
            receipt.insert("kind", "card");
        } else {
            QStringList skills;
            for (const Skill *skill : target->getVisibleSkillList()) for (int candidateId : target->getSkillInstanceIds(skill->objectName())) {
                const SkillInstance *instance = target->findSkillInstance(skill->objectName(), candidateId);
                if (instance && ((instance->bindHead == 1 && target->hasShownGeneral1()) || (instance->bindHead == 2 && target->hasShownGeneral2())))
                    skills << SkillInstanceUtils::formatName(skill->objectName(), candidateId);
            }
            if (skills.isEmpty()) return false;
            selected = room->askForChoice(ctx.owner, "sidi_skill", skills.join("+"), QVariant(), QString(), "@sidi-skill::" + target->objectName());
            if (!skills.contains(selected)) return false;
            id = SkillInstanceUtils::parseName(selected, name);
            if (id <= 0 || !target->hasSkillInstance(name, id)) return false;
            receipt.insert("kind", "skill"); receipt.insert("disabled_skill", name); receipt.insert("disabled_instance", id);
        }
        QVariantList receipts = room->getTag("HSidiApplied").toList(); receipts << receipt; room->setTag("HSidiApplied", receipts);
        if (ctx.choice == "cardlimit") room->setPlayerCardLimitation(target, "use", selected, false, token);
        else room->addSkillInvalidity(target, name, token, objectName(), id);
        return false;
    }
};
class HDangxian : public TriggerSkillV2
{
public:
    HDangxian() : TriggerSkillV2("heg_dangxian")
    { events << GeneralShowed << EventPhaseEnd << EventSkillInvoking; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || event == EventSkillInvoking) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            if (event == GeneralShowed) {
                const SkillInstance *instance = player->findSkillInstance(objectName(), id);
                if (!instance || player->getSkillInstanceStateValue(objectName(), id, "invoked").toBool()) continue;
                const QStringList slot_list = data.toStringList();
                if (!((instance->bindHead == 1 && slot_list.contains("head"))
                    || (instance->bindHead == 2 && slot_list.contains("deputy")))) continue;
            } else if (event != EventPhaseEnd || player->getPhase() != Player::RoundStart) continue;
            result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.owner)
            accepted.owner->setSkillInstanceStateValue(objectName(), accepted.instanceID, "invoked", true);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && (ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)
        || ctx.owner->askForSkillInvoke(this)); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (event == GeneralShowed) room->addPlayerMark(target, "@firstshow", amount);
        else for (int i = 0; i < amount; ++i) target->insertPhase(Player::Play);
        return false;
    }
};
class HHuanshi : public TriggerSkillV2
{
public:
    HHuanshi() : TriggerSkillV2("heg_huanshi") { events << AskForRetrial; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !judge || !judge->who
            || !player->isFriendWith(judge->who) || (player->isNude() && player->getHandPile().isEmpty())) return {};
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->card || !judge->who) return false;
        const QString prompt = QString("@huanshi-card:%1:%2:%3:%4")
            .arg(judge->who->objectName(), objectName(), judge->reason).arg(judge->card->getEffectiveId());
        const Card *card = room->askForCard(ctx.owner, "..", prompt, *ctx.original_data,
            Card::MethodResponse, judge->who, true);
        if (!card || (card->isVirtualCard() && card->subcardsLength() != 1)) return false;
        const int id = card->getEffectiveId();
        const Card *physical = id >= 0 ? Sanguosha->getEngineCard(id) : nullptr;
        if (!physical || card->objectName() != physical->objectName()) return false;
        ctx.extra_data = id;
        ctx.targets = {judge->who};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        return ctx.extra_data.isValid() && id >= 0 && room->getCardOwner(id) == ctx.owner
            && !Sanguosha->getCard(id)->hasFlag("using")
            && (ctx.owner->handCards().contains(id) || ctx.owner->getEquipsId().contains(id)
                || ctx.owner->getHandPile().contains(id));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        const int id = ctx.extra_data.toInt();
        if (!judge || judge->who != target || getEffectiveAmount(ctx) <= 0 || room->getCardOwner(id) != ctx.owner
            || !(ctx.owner->handCards().contains(id) || ctx.owner->getEquipsId().contains(id)
                || ctx.owner->getHandPile().contains(id))) return false;
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        // Native retrial owns both physical moves and the single response notification.
        room->retrial(Sanguosha->getCard(id), ctx.owner, judge, objectName(), false);
        return false;
    }
};
HHongyuanCard::HHongyuanCard()
{
    setSkillName("heg_hongyuan");
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}
void HHongyuanCard::extraCost(Room *, const CardUseStruct &) const {}
void HHongyuanCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const {}

class HHongyuanViewAsSkill : public ViewAsSkillV2
{
public:
    HHongyuanViewAsSkill() : ViewAsSkillV2("heg_hongyuan", 1) {}
    bool willThrowSelectedCards() const override { return false; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !candidate->hasFlag("using")
            && request.selectedCardIds.isEmpty() && request.initiator->handCards().contains(candidate->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        return ViewAsSkillV2::createCard(request);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        room->showCard(ctx.invoker, request.selectedCardIds.first()); return true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HHongyuanCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (!target->handCards().contains(id)) return ContinueEffects;
        Room *room = target->getRoom();
        // Retain the applied physical-card identity independently of the granting instance.
        QVariantList receipts = target->getTag("HHongyuanApplied").toList();
        receipts << QVariantMap{{"card", id}, {"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_instance", ctx.activationRef.key.instanceID}, {"phase", room->historyScopes().value("phase_id")}};
        target->setTag("HHongyuanApplied", receipts);
        QStringList ids = target->property("view_as_transferable").toString().split("+", Qt::SkipEmptyParts);
        if (!ids.contains(QString::number(id))) ids << QString::number(id);
        room->setPlayerProperty(target, "view_as_transferable", ids.join("+"));
        return ContinueEffects;
    }
};

class HHongyuan : public TriggerSkillV2
{
public:
    HHongyuan() : TriggerSkillV2("heg_hongyuan")
    {
        events << BeforeCardsMoveBatch << EventPhaseChanging << CardsMoveBatch;
        global = true;
        view_as_skill = new HHongyuanViewAsSkill;
    }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging && event != CardsMoveBatch) return true;
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            const QVariantList previous = holder->getTag("HHongyuanApplied").toList();
            if (previous.isEmpty()) continue;
            QVariantList remaining;
            QStringList ids = holder->property("view_as_transferable").toString().split("+", Qt::SkipEmptyParts);
            for (const QVariant &entry : previous) {
                const int id = entry.toMap().value("card").toInt();
                bool leftHand = false;
                if (event == CardsMoveBatch) for (const QVariant &moved : data.toList()) {
                    const CardsMoveOneTimeStruct move = moved.value<CardsMoveOneTimeStruct>();
                    if (move.from != holder) continue;
                    for (int i = 0; i < move.card_ids.size(); ++i)
                        if (move.card_ids.at(i) == id && move.from_places.value(i) == Player::PlaceHand) leftHand = true;
                }
                if ((event == EventPhaseChanging && room->historyEvent(entry.toMap().value("phase").toLongLong()).value("status").toString() != "active")
                    || leftHand || !holder->handCards().contains(id))
                    ids.removeAll(QString::number(id));
                else remaining << entry;
            }
            if (previous != remaining) {
                holder->setTag("HHongyuanApplied", remaining);
                room->setPlayerProperty(holder, "view_as_transferable", ids.join("+"));
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != BeforeCardsMoveBatch || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        bool transfer = false;
        for (const QVariant &entry : data.toList()) {
            const CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceHand
                && move.reason.m_reason == CardMoveReason::S_REASON_DRAW && move.reason.m_skillName == "transfer") transfer = true;
        }
        if (transfer) for (ServerPlayer *other : room->getOtherPlayers(player))
            if (other->isFriendWith(player)) return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> choices;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (other->isFriendWith(ctx.owner)) choices << other;
        ServerPlayer *chosen = room->askForPlayerChosen(ctx.owner, choices, objectName(), "hongyuan-invoke", true, true);
        if (!chosen) return false;
        ctx.targets = {chosen};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        QVariantList moves;
        for (const QVariant &entry : ctx.original_data->toList()) {
            CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.to == ctx.owner && move.to_place == Player::PlaceHand
                && move.reason.m_reason == CardMoveReason::S_REASON_DRAW && move.reason.m_skillName == "transfer") move.to = target;
            moves << QVariant::fromValue(move);
        }
        *ctx.original_data = moves;
        return false;
    }
};
class HMingzhe : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HMingzhe() : TriggerSkillV2("heg_mingzhe")
    {
        events  << CardsMoveBatch << CardUsed << CardResponded;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->getPhase() != Player::NotActive) return {};
        if (triggerEvent == CardUsed || triggerEvent == CardResponded) {
            const Card *cardstar = NULL;
            if (triggerEvent == CardUsed) {
                CardUseStruct use = data.value<CardUseStruct>();
                cardstar = use.card;
            } else {
                CardResponseStruct resp = data.value<CardResponseStruct>();
                cardstar = resp.m_card;
            }
            if (cardstar && cardstar->getTypeId() != Card::TypeSkill && cardstar->isRed())
                return TriggerList{{player, {objectName()}}};

        } else if (triggerEvent == CardsMoveBatch) {
            const qint64 event = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id").toLongLong();
            if (event <= 0) return {};
            QVariantMap query{{"event_id", event}, {"from", player->objectName()}};
            QSet<int> red;
            while (true) {
                const QVariantMap page = room->queryHistoryMoves(query);
                if (!page.value("complete").toBool()) return {};
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap value = entry.toMap().value("data").toMap();
                    if (value.value("from_place").toInt() == Player::PlaceEquip && value.value("card_before").toMap().value("red").toBool())
                        red.insert(value.value("card_id").toInt());
                }
                if (!page.value("has_more").toBool()) break;
                query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
            }
            TriggerList result;
            for (int id : red) { Q_UNUSED(id); result[player] << objectName(); }
            return result;
        }
        return {};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if ((event != CardsMoveBatch && player->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)) || player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class HDanlao : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDanlao() : TriggerSkillV2("heg_danlao")
    {
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.to.contains(player) && use.card->getTypeId() == Card::TypeTrick && (player && player->isAlive() && player->hasSkill(objectName()))) {
            bool can_trigger = false;
            foreach (ServerPlayer *p, use.to) {
                if (p->isAlive() && p != player) {
                    can_trigger = true;
                    break;
                }
            }
            if (can_trigger)
                return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QVariant &data = *ctx.original_data;
        target->drawCards(getEffectiveAmount(ctx), objectName());
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.to.contains(target) && !use.nullified_list.contains(target->objectName()))
            use.nullified_list << target->objectName();
        data = QVariant::fromValue(use);
        return false;
    }
};

class HJilei : public TriggerSkillV2
{
public:
    HJilei() : TriggerSkillV2("heg_jilei") { events << Damaged << EventPhaseChanging; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    static void project(Room *room)
    {
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            QStringList types;
            for (const QVariant &entry : room->getTag("HJileiApplied").toList()) {
                const QVariantMap value = entry.toMap();
                const QString type = "log_" + value.value("type").toString();
                if (value.value("target").toString() == target->objectName() && !types.contains(type)) types << type;
            }
            setMolState(room, target, "heg_jilei", types, "&heg_jilei");
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        QVariantList kept, expired;
        for (const QVariant &entry : room->getTag("HJileiApplied").toList()) {
            if (entry.toMap().value("turn").toLongLong() == turn) expired << entry; else kept << entry;
        }
        room->setTag("HJileiApplied", kept); project(room);
        for (const QVariant &entry : expired) {
            const QVariantMap value = entry.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(value.value("target").toString(), true);
            if (target) room->removePlayerCardLimitationByReason(target, value.value("token").toString());
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damaged || !player || !player->isAlive() || !player->hasSkill(objectName())
            || room->historyScopes().value("turn_id").toLongLong() <= 0) return {};
        ServerPlayer *from = data.value<DamageStruct>().from;
        return from && from->isAlive() ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from;
        if (!from || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(from))) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "BasicCard+EquipCard+TrickCard", *ctx.original_data, QString(), "@jilei-choose::" + from->objectName());
        ctx.targets = {from}; return QStringList{"BasicCard", "EquipCard", "TrickCard"}.contains(ctx.choice);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const qint64 serial = room->getTag("HJileiSerial").toLongLong() + 1; room->setTag("HJileiSerial", serial);
        const QString token = QString("heg_jilei:%1").arg(serial);
        QVariantList receipts = room->getTag("HJileiApplied").toList();
        receipts << QVariantMap{{"target", target->objectName()}, {"type", ctx.choice}, {"token", token}, {"turn", room->historyScopes().value("turn_id")},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
        room->setTag("HJileiApplied", receipts);
        room->setPlayerCardLimitation(target, "use,response,discard", ctx.choice + "|.|.|hand", false, token); project(room);
        return false;
    }
};
class HWanglie : public TriggerSkillV2
{
public:
    HWanglie() : TriggerSkillV2("heg_wanglie")
    { events << CardUsed << CardResponded << EventPhaseChanging << EventPhaseStart << GeneralShown << EventAcquireSkill; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseChanging) {
            QVariantList kept, expired;
            for (const QVariant &entry : room->getTag("HWanglieApplied").toList()) {
                const qint64 phase = entry.toMap().value("phase").toLongLong();
                if (room->historyEvent(phase).value("status").toString() == "active") kept << entry; else expired << entry;
            }
            room->setTag("HWanglieApplied", kept);
            for (const QVariant &entry : expired) {
                const QVariantMap receipt = entry.toMap();
                ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
                if (target) room->removePlayerCardLimitationByReason(target, receipt.value("token").toString());
            }
        } else if (player) {
            // UI projection only. Server decisions use the journal's exact current-use exclusion.
            room->setPlayerMark(player, "heg_wanglie_first", room->countHistoryCards(player, "phase", QString(), false, true) == 0);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play
            || room->historyScopes().value("phase_id").toLongLong() <= 0) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && (use.card->isKindOf("Slash") || use.card->isNDTrick()) ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = room->getAlivePlayers(); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        if (target != ctx.owner) return false;
        const qint64 serial = room->getTag("HWanglieSerial").toLongLong() + 1; room->setTag("HWanglieSerial", serial);
        const QString token = QString("heg_wanglie:%1").arg(serial);
        QVariantList receipts = room->getTag("HWanglieApplied").toList();
        receipts << QVariantMap{{"target", target->objectName()}, {"token", token}, {"phase", room->historyScopes().value("phase_id")},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
        room->setTag("HWanglieApplied", receipts);
        room->setPlayerCardLimitation(target, "use", ".", false, token);
        return false;
    }
};

class HWanglieTarget : public TargetModSkillV2
{
public:
    HWanglieTarget() : TargetModSkillV2("#heg_wanglie-target", "^SkillCard") { frequency = NotFrequent; setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != DistanceLimit || !ctx.primary || ctx.primary->getPhase() != Player::Play) return CorrectSkillResult::noEffect();
        const ServerPlayer *server = qobject_cast<const ServerPlayer *>(ctx.primary);
        const bool first = server ? server->getRoom()->countHistoryCards(server, "phase", QString(), false, true) == 0
            : ctx.primary->getMark("heg_wanglie_first") > 0;
        return first ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};
class HYinbingX : public TriggerSkillV2
{
public:
    HYinbingX() : TriggerSkillV2("heg_yinbingx") { events << EventPhaseStart; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish && !player->isNude()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<int> ids = room->askForExchangeCards(ctx.owner, objectName(), ctx.owner->getCardCount(), 0, "@yinbing-put", QString(), "^BasicCard");
        if (ids.isEmpty()) return false;
        QVariantList selected;
        for (int id : ids) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || card->hasFlag("using") || selected.contains(id) || card->getTypeId() == Card::TypeBasic
                || (!ctx.owner->handCards().contains(id) && !ctx.owner->hasEquip(card))) return false;
            selected << id;
        }
        ctx.extra_data = selected; ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QList<int> selected;
        for (const QVariant &entry : ctx.extra_data.toList()) {
            const int id = entry.toInt(); const Card *card = Sanguosha->getCard(id);
            if (card && !card->hasFlag("using") && !selected.contains(id) && room->getCardOwner(id) == ctx.owner && card->getTypeId() != Card::TypeBasic
                && (ctx.owner->handCards().contains(id) || ctx.owner->hasEquip(card))) selected << id;
        }
        if (!selected.isEmpty()) target->addToPile("kerchief", selected, true);
        return false;
    }
};

class HYinbingXCompulsory : public TriggerSkillV2
{
public:
    HYinbingXCompulsory() : TriggerSkillV2("#heg_yinbingx-compulsory") { events << Damaged; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || player->getPile("kerchief").isEmpty()) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID) && !ctx.owner->askForSkillInvoke("heg_yinbingx", *ctx.original_data)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx); ++i) {
            const QList<int> cards = target->getPile("kerchief"); if (cards.isEmpty()) break;
            room->fillAG(cards, target); const int id = room->askForAG(target, cards, false, "heg_yinbingx"); room->clearAG(target);
            if (!cards.contains(id) || !target->getPile("kerchief").contains(id)) break;
            room->throwCard(Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, target->objectName(), "heg_yinbingx", ""), nullptr);
        }
        return false;
    }
};

class HJuedi : public TriggerSkillV2
{
public:
    HJuedi() : TriggerSkillV2("heg_juedi") { events << EventPhaseStart; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start && !player->getPile("kerchief").isEmpty()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID) && !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const QList<int> pile = ctx.owner->getPile("kerchief"); if (pile.isEmpty()) return false;
        QList<ServerPlayer *> others;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) if (other->getHp() <= ctx.owner->getHp()) others << other;
        ctx.choice = others.isEmpty() ? "self" : room->askForChoice(ctx.owner, objectName(), "self+give");
        ServerPlayer *target = ctx.choice == "give" ? room->askForPlayerChosen(ctx.owner, others, objectName(), "@juedi") : ctx.owner;
        if (!target) return false;
        QVariantList cards; for (int id : pile) cards << id;
        ctx.extra_data = QVariantMap{{"cards", cards}, {"given", pile.size()}}; ctx.targets = {target}; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantMap receipt = ctx.extra_data.toMap(); DummyCard cards;
        for (const QVariant &entry : receipt.value("cards").toList()) {
            const int id = entry.toInt(); const Card *card = Sanguosha->getCard(id);
            if (!card || card->hasFlag("using") || cards.getSubcards().contains(id)
                || !ctx.owner->getPile("kerchief").contains(id)) return false; cards.addSubcard(id);
        }
        if (ctx.targets.isEmpty()) return false;
        if (ctx.choice == "self") {
            room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), ""), nullptr);
            return true;
        }
        ServerPlayer *target = ctx.targets.first();
        if (!target || !target->isAlive() || target == ctx.owner || target->getHp() > ctx.owner->getHp()) return false;
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        const qint64 parent = room->currentHistoryEventId();
        room->obtainCard(target, &cards, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), ""));
        QVariantMap query{{"after", before.value("watermark")}, {"from", ctx.owner->objectName()}, {"to", target->objectName()}};
        QSet<int> actual; bool complete = before.value("complete").toBool() && parent > 0;
        while (complete) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool()) { complete = false; break; }
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
                const int id = value.value("card_id").toInt();
                if (cards.getSubcards().contains(id) && value.value("to_place").toInt() == Player::PlaceHand
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == parent) actual.insert(id);
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        receipt.insert("given", complete ? actual.size() : -1); ctx.extra_data = receipt; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "self") {
            const int count = target->getMaxHp() - target->getHandcardNum();
            if (count > 0) target->drawCards(count, objectName());
        } else {
            room->recover(target, RecoverStruct(objectName(), ctx.owner, amount));
            const int given = ctx.extra_data.toMap().value("given").toInt();
            if (given > 0) target->drawCards(given * amount, objectName());
        }
        return false;
    }
};
class HMoukui : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HMoukui() : TriggerSkillV2("heg_moukui")
    {
        events << TargetSpecified;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
        QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !use.card || !use.card->isKindOf("Slash")) return true;
        // Keep each target tied to the exact grant through target interception.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (int i = 0; i < use.to.size(); ++i) {
                ServerPlayer *target = use.to.at(i);
                if (!target) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + target->objectName() + '&' + QString::number(i + 1);
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                ctx.targets << target;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
                if (!amountOk) continue;
                contexts << ctx;
            }
        }
        return true;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *skill_target = ctx.preferredTarget;
        ServerPlayer *player = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, QVariant::fromValue(skill_target))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), skill_target->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "draw") { target->drawCards(amount, objectName()); return false; }
        QStringList choices{"draw"};
        if (ctx.owner->canDiscard(target, "he")) choices << "discard";
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(target), "draw+discard", "@moukui-choose::" + target->objectName());
        if (choice == "draw") {
            SkillContext child = ctx; child.choice = "draw"; skillEffect(event, room, owner, child, ctx.owner);
        } else if (choice == "discard") {
            const QList<int> ids = room->askForCardsChosen(ctx.owner, target, "he", objectName(), amount, amount, false, Card::MethodDiscard);
            DummyCard cards;
            for (int id : ids) {
                const Card *card = Sanguosha->getCard(id);
                if (card && !card->hasFlag("using") && !cards.getSubcards().contains(id)
                    && room->getCardOwner(id) == target && (target->handCards().contains(id) || target->hasEquip(card))
                    && ctx.owner->canDiscard(target, id)) cards.addSubcard(id);
            }
            if (cards.subcardsLength() > 0) room->throwCard(&cards, target, ctx.owner);
        } else return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return false;
        const qint64 serial = room->getTag("HMoukuiSerial").toLongLong() + 1;
        room->setTag("HMoukuiSerial", serial);
        QVariantList receipts = use.card->getTag("HMoukuiApplied").toList();
        receipts << QVariantMap{{"holder", ctx.owner->objectName()}, {"target", target->objectName()},
            {"use_event", useEvent}, {"serial", serial},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}, {"amount", amount}};
        use.card->tag.insert("HMoukuiApplied", receipts);
        return false;
    }
};

class HMoukuiEffect : public TriggerSkillV2
{
public:
    HMoukuiEffect() : TriggerSkillV2("#heg_moukui-effect")
    { events << CardOffset << CardFinished << EventSkillEffectFinished; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    const Card *receiptCard(const SkillContext &ctx) const
    { return ctx.original_data ? ctx.original_data->value<CardEffectStruct>().card : nullptr; }
    void consume(const SkillContext &ctx) const
    {
        const Card *card = receiptCard(ctx); if (!card) return;
        QVariantList receipts = card->getTag("HMoukuiApplied").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return;
        // Keep row positions stable while sibling trigger contexts are pending.
        QVariantMap receipt = receipts.at(index).toMap(); receipt.insert("consumed", true);
        receipts[index] = receipt; card->tag.insert("HMoukuiApplied", receipts);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && !finished.activationRef.isValid()) consume(finished);
        } else if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>(); if (!use.card) return false;
            const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            QVariantList receipts = use.card->getTag("HMoukuiApplied").toList();
            for (int i = receipts.size() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("use_event").toLongLong() == useEvent) receipts.removeAt(i);
            use.card->tag.insert("HMoukuiApplied", receipts);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &out) const override
    {
        if (event != CardOffset) return true;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card || !effect.card->isKindOf("Slash") || !effect.to || !effect.to->isAlive()
            || !effect.offset_card || !effect.offset_card->isKindOf("Jink")) return true;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return true;
        const QVariantList receipts = effect.card->getTag("HMoukuiApplied").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("consumed").toBool() || receipt.value("use_event").toLongLong() != useEvent
                || receipt.value("target").toString() != effect.to->objectName()) continue;
            ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString());
            if (!holder || !holder->isAlive() || !effect.to->canDiscard(holder, "he")) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = holder; ctx.invoker = ctx.initiator = effect.to;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = ctx.sourceRef.key.instanceID; ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt;
            ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true; ctx.trigger_count = i;
            ctx.preferredTarget = holder; ctx.targets = {holder}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const Card *card = receiptCard(ctx); const QVariantMap receipt = ctx.extra_data.toMap();
        return ctx.sourceRef.isValid() && card && receipt.value("serial").toLongLong() > 0
            && !receipt.value("consumed").toBool() && card->getTag("HMoukuiApplied").toList().contains(ctx.extra_data)
            && receipt.value("use_event").toLongLong() == room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { consume(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.invoker->canDiscard(target, "he")) return false;
        const QList<int> ids = room->askForCardsChosen(ctx.invoker, target, "he", "heg_moukui", amount, amount, false, Card::MethodDiscard);
        DummyCard cards;
        for (int id : ids) {
            const Card *card = Sanguosha->getCard(id);
            if (card && !card->hasFlag("using") && !cards.getSubcards().contains(id)
                && room->getCardOwner(id) == target && (target->handCards().contains(id) || target->hasEquip(card))
                && ctx.invoker->canDiscard(target, id)) cards.addSubcard(id);
        }
        if (cards.subcardsLength() > 0) room->throwCard(&cards, target, ctx.invoker);
        return false;
    }
};
class HZhenxiTrick : public ViewAsSkillV2
{
public:
    HZhenxiTrick() : ViewAsSkillV2("heg_zhenxi_trick", 1)
    {
        response_or_use = true;
        response_pattern = "@@heg_zhenxi_trick";
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@heg_zhenxi_trick"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId()) || (!request.initiator->handCards().contains(candidate->getEffectiveId()) && !request.initiator->hasEquip(candidate))) return false;
        return request.selectedCardIds.isEmpty() && !candidate->hasFlag("using")
            && Sanguosha->matchExpPattern("BasicCard,EquipCard|diamond,club", request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection order so forged requests obey the same card restrictions.
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
        Card *card = originalCard->getSuit() == Card::Diamond
            ? static_cast<Card *>(new Indulgence(originalCard->getSuit(), originalCard->getNumber()))
            : static_cast<Card *>(new SupplyShortage(originalCard->getSuit(), originalCard->getNumber()));
        card->addSubcard(originalCard->getId());
        card->setSkillName(objectName());
        card->tag.insert("HZhenxiTarget", request.initiator->property("zhenxi_target"));
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return QString();
        return Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == Card::Diamond
            ? "Indulgence" : "SupplyShortage";
    }
};

class HZhenxi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HZhenxi() : TriggerSkillV2("heg_zhenxi")
    {
        events << TargetSpecified;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
        QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !use.card || !use.card->isKindOf("Slash")) return true;
        // Keep each target tied to the exact grant through target interception.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (int i = 0; i < use.to.size(); ++i) {
                ServerPlayer *target = use.to.at(i);
                if (!target) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + target->objectName() + '&' + QString::number(i + 1);
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                ctx.targets << target;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
                if (!amountOk) continue;
                contexts << ctx;
            }
        }
        return true;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *skill_target = ctx.preferredTarget;
        ServerPlayer *player = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, QVariant::fromValue(skill_target))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), skill_target->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        ServerPlayer *player = ctx.owner;
        auto convert = [&]() {
            Room::AcceptedViewAsEffectScope continuation(room, player, "heg_zhenxi_trick", ctx);
            if (!continuation.isValid()) return false;
            const QVariant previous = player->property("zhenxi_target");
            room->setPlayerProperty(player, "zhenxi_target", target->objectName());
            const auto restore = qScopeGuard([&] { room->setPlayerProperty(player, "zhenxi_target", previous); });
            return room->askForUseCard(player, "@@heg_zhenxi_trick", "@zhenxi-trick::" + target->objectName()) != nullptr;
        };
        auto discard = [&]() {
            if (!player->canDiscard(target, "he")) return false;
            const QList<int> ids = room->askForCardsChosen(player, target, "he", objectName(), amount, amount, false, Card::MethodDiscard);
            DummyCard cards;
            for (int id : ids) {
                const Card *card = Sanguosha->getCard(id);
                if (card && !card->hasFlag("using") && !cards.getSubcards().contains(id)
                    && room->getCardOwner(id) == target && (target->handCards().contains(id) || target->hasEquip(card))
                    && player->canDiscard(target, id)) cards.addSubcard(id);
            }
            if (cards.subcardsLength() == 0) return false;
            room->throwCard(&cards, target, player); return true;
        };
        QStringList choices{"usecard"};
        if (player->canDiscard(target, "he")) choices << "discard";
        const QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target), "usecard+discard", "@zhenxi-choose::" + target->objectName());
        if (choice == "usecard") {
            if (convert() && player->hasShownAllGenerals() && !target->hasShownAllGenerals() && player->canDiscard(target, "he")
                && room->askForChoice(player, "zhenxi_discard", "yes+no", *ctx.original_data, QString(), "@zhenxi-discard::" + target->objectName()) == "yes") discard();
        } else if (choice == "discard" && discard() && player->hasShownAllGenerals() && !target->hasShownAllGenerals()) convert();
        return false;
    }
    virtual int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return (card->isKindOf("Indulgence") || card->isKindOf("SupplyShortage")) ? -2 : -1;
    }
};

class HZhenxiProhibit : public ProhibitSkill
{
public:
    HZhenxiProhibit() : ProhibitSkill("#heg_zhenxi-prohibit") {}
    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return to && card && card->getSkillName(true) == "heg_zhenxi_trick"
            && card->getTag("HZhenxiTarget").toString() != to->objectName();
    }
};

class HZhenxiTargetMod : public TargetModSkillV2
{
public:
    HZhenxiTargetMod() : TargetModSkillV2("#heg_zhenxi-target", "^SkillCard")
    { setHolderSelector(CorrectSkill_System); setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit && ctx.card && ctx.card->getSkillName(true) == "heg_zhenxi_trick"
            ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};
HJiansuCard::HJiansuCard()
{
    will_throw = true;
    setSkillName("heg_jiansu");
}

bool HJiansuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->canRecover() && to_select->getHp() <= subcardsLength();
}

// The authoritative V2 active skill performs recovery through its target hook.
void HJiansuCard::onEffect(CardEffectStruct &) const {}

class HJiansuViewAsSkill : public ViewAsSkillV2
{
public:
    HJiansuViewAsSkill() : ViewAsSkillV2("heg_jiansu")
    { response_pattern = "@@heg_jiansu"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@heg_jiansu"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && candidate->getEffectiveId() >= 0
            && !request.selectedCardIds.contains(candidate->getEffectiveId()) && !candidate->hasFlag("using")
            && request.initiator->handCards().contains(candidate->getEffectiveId())
            && request.initiator->canDiscard(request.initiator, candidate->getEffectiveId())
            && jiansuMoney(request.initiator).contains(candidate->getEffectiveId());
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
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        return ViewAsSkillV2::createCard(request);
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && selected.isEmpty() && candidate->canRecover() && candidate->getHp() <= request.selectedCardIds.size(); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) target->getRoom()->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HJiansuCard"; }
};

class HJiansu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJiansu() : TriggerSkillV2("heg_jiansu")
    {
        events << CardsMoveBatch << EventPhaseStart; global = true;
        relate_to_place = "deputy"; view_as_skill = new HJiansuViewAsSkill;
    }
    bool canPreshow() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveBatch || !player) return false;
        QList<int> money = jiansuMoney(player), remaining = money;
        // A card loses gold status on leaving hand, even if a nested move returns it.
        for (const QVariant &entry : data.toList()) {
            const CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.from != player) continue;
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceHand) remaining.removeAll(move.card_ids.at(i));
        }
        if (money != remaining) setJiansuMoney(room, player, remaining);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseStart)
            return player->getPhase() == Player::Play && !jiansuMoney(player).isEmpty()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (player->getPhase() != Player::NotActive) return {};
        for (const QVariant &entry : data.toList()) {
            const CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.to != player || move.to_place != Player::PlaceHand) continue;
            for (int id : move.card_ids)
                if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                    return TriggerList{{player, {objectName()}}};
        }
        return {};
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardsMoveBatch && !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (event == EventPhaseStart) {
            Room::AcceptedViewAsEffectScope continuation(room, target, objectName(), ctx);
            if (continuation.isValid()) room->askForUseCard(target, "@@heg_jiansu", "@jiansu-card", -1, Card::MethodDiscard);
            return false;
        }
        QList<int> gained;
        for (const QVariant &entry : ctx.original_data->toList()) {
            const CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.to != target || move.to_place != Player::PlaceHand) continue;
            for (int id : move.card_ids)
                if (!gained.contains(id) && target->handCards().contains(id)) gained << id;
        }
        if (gained.isEmpty()) return false;
        room->showCard(target, gained);
        QList<int> money = jiansuMoney(target);
        for (int id : gained) if (target->handCards().contains(id) && !money.contains(id)) money << id;
        setJiansuMoney(room, target, money);
        return false;
    }
};
class HYaowu : public TriggerSkillV2
{
public:
    HYaowu() : TriggerSkillV2("heg_yaowu")
    { events << Damage << EventSkillInvoking; frequency = Limited; limit_mark = "@showoff"; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == Damage && player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstance *instance = ctx.owner->findSkillInstance(objectName(), ctx.instanceID);
        if (!instance || !isUsable(ctx) || (instance->bindHead == 1 && ctx.owner->hasShownGeneral1())
            || (instance->bindHead == 2 && ctx.owner->hasShownGeneral2())
            || (instance->bindHead == 0 && ctx.owner->hasShownSkill(objectName()))) return false;
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName != objectName()) return false;
        if (accepted.bypass_cost) addUsage(accepted);
        if (accepted.owner) room->removePlayerMark(accepted.owner, limit_mark);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        // Transformation is an applied permanent effect; death survives source removal.
        const qint64 serial = room->getTag("HYaowuSerial").toLongLong() + 1; room->setTag("HYaowuSerial", serial);
        QVariantList receipts = target->getTag("HYaowuApplied").toList();
        receipts << QVariantMap{{"serial", serial}, {"holder", target->objectName()}, {"amount", amount},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
        target->setTag("HYaowuApplied", receipts);
        room->setPlayerMark(target, "##yaowu", receipts.size());
        room->gainMaxHp(target, 2 * amount, objectName());
        room->recover(target, RecoverStruct(objectName(), target, 2 * amount));
        return false;
    }
};

class HYaowuDeath : public TriggerSkillV2
{
public:
    HYaowuDeath() : TriggerSkillV2("#heg_yaowu-death")
    { events << Death << BuryVictim; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent event) const override { return event == BuryVictim ? -2 : 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == Death) {
            const DeathStruct death = data.value<DeathStruct>();
            if (death.who) death.who->setTag("HYaowuDeathApplied", death.who->getTag("HYaowuApplied"));
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &out) const override
    {
        if (event != BuryVictim) return true;
        const DeathStruct death = data.value<DeathStruct>();
        if (!death.who || !death.who->isDead()) return true;
        const QVariantList receipts = death.who->getTag("HYaowuDeathApplied").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = death.who;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = ctx.sourceRef.key.instanceID; ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true; ctx.trigger_count = i;
            for (ServerPlayer *ally : room->getAlivePlayers()) if (ally->isFriendWith(death.who)) ctx.targets << ally;
            if (!ctx.targets.isEmpty()) out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.sourceRef.isValid() && ctx.owner->getTag("HYaowuDeathApplied").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (getEffectiveAmount(ctx) > 0) room->loseHp(target, getEffectiveAmount(ctx)); return false; }
};

class HShiyong : public TriggerSkillV2
{
public:
    HShiyong() : TriggerSkillV2("heg_shiyong") { events << DamageInflicted; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    static int yaowuInvoked(Room *room, const ServerPlayer *player)
    {
        QVariantMap query{{"kind", "skill_invoked"}, {"player", player->objectName()}};
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList())
                if (entry.toMap().value("data").toMap().value("invoked_skill").toString() == "heg_yaowu") return 1;
            if (!page.value("has_more").toBool()) return 0;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card) return {};
        const int invoked = yaowuInvoked(room, player); if (invoked < 0) return {};
        const bool transformed = invoked > 0;
        return (transformed ? !damage.card->isBlack() && damage.from && damage.from->isAlive() : !damage.card->isRed())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID) && !ctx.owner->askForSkillInvoke(this)) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const int invoked = yaowuInvoked(room, ctx.owner); if (invoked < 0) return false;
        ServerPlayer *recipient = invoked == 0 ? ctx.owner : damage.from;
        if (!recipient) return false;
        ctx.targets = {recipient}; return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (getEffectiveAmount(ctx) > 0) target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};

class HZhuidu : public TriggerSkillV2
{
public:
    HZhuidu() : TriggerSkillV2("heg_zhuidu") { events << DamageCaused << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return event == DamageCaused && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Play && damage.to && damage.to->isAlive()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!ctx.owner->askForSkillInvoke(this, QVariant::fromValue(damage.to))) return false;
        ctx.targets = {damage.to};
        if (damage.to->isFemale()) {
            const Card *card = room->askForExchange(ctx.owner, "zhuidu_discard", 1, 1, true, "@zhuidu-both::" + damage.to->objectName(), true);
            if (card) ctx.extra_data = card->getSubcards().value(0, -1);
        }
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        if (ctx.extra_data.isValid()) {
            const int id = ctx.extra_data.toInt(); const Card *card = Sanguosha->getCard(id);
            if (!card || card->hasFlag("using") || (!ctx.owner->handCards().contains(id) && !ctx.owner->hasEquip(card))
                || !ctx.owner->canDiscard(ctx.owner, id)) return false;
            addUsage(ctx); room->throwCard(card, ctx.owner, ctx.owner); return true;
        }
        addUsage(ctx); return true;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.activationRef.key.skillName == objectName()) addUsage(accepted);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        const bool both = ctx.extra_data.isValid();
        const bool discard = both || (!target->getEquips().isEmpty()
            && room->askForChoice(target, "zhuidu_choice", "throw+damage", *ctx.original_data) == "throw");
        if (discard) target->throwAllEquips();
        if (both || !discard) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>(); damage.damage += amount;
            *ctx.original_data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class HShigong : public TriggerSkillV2
{
public:
    HShigong() : TriggerSkillV2("heg_shigong")
    { events << Dying << EventSkillInvoking; frequency = Limited; limit_mark = "@handover"; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == Dying && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::NotActive && data.value<DyingStruct>().who == player
            && player->getHp() < 1 && player->getGeneral2() && !player->getActualGeneral2Name().contains("sujiang")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.extra_data = ctx.owner->getActualGeneral2Name();
        if (room->getCurrent() && room->getCurrent()->isAlive()) ctx.targets = {room->getCurrent()};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || ctx.owner->getActualGeneral2Name() != ctx.extra_data.toString()
            || ctx.extra_data.toString().contains("sujiang")) return false;
        addUsage(ctx); ctx.owner->removeGeneral(false); return true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName != objectName()) return false;
        if (accepted.bypass_cost) addUsage(accepted);
        if (accepted.owner) room->removePlayerMark(accepted.owner, limit_mark);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "recover") {
            const int endpoint = ctx.extra_data.toInt() == 1 ? target->getMaxHp() : 1;
            if (endpoint > target->getHp()) room->recover(target, RecoverStruct(objectName(), ctx.owner, endpoint - target->getHp()));
            return false;
        }
        const General *general = Sanguosha->getGeneral(ctx.extra_data.toString());
        QStringList choices;
        if (general) for (const Skill *skill : general->getVisibleSkillList()) if (isNormalSkill(skill)) choices << skill->objectName();
        bool acquired = false;
        if (!choices.isEmpty()) {
            choices << "cancel";
            const QString selected = room->askForChoice(target, "shigong_skill", choices.join("+"), *ctx.original_data, QString(), "@shigong-choose:::" + ctx.extra_data.toString());
            if (selected != "cancel" && choices.contains(selected)) acquired = room->acquireSkillFromEffect(target, selected, ctx) > 0;
        }
        SkillContext child = ctx; child.choice = "recover"; child.extra_data = acquired ? 1 : 0;
        skillEffect(event, room, owner, child, ctx.owner); return false;
    }
private:
    static bool isNormalSkill(const Skill *skill)
    {
        if (skill->isAttachedLordSkill() || skill->isLordSkill()) return false;
        if (skill->relateToPlace(true) || skill->relateToPlace(false)
            || dynamic_cast<const HArraySummon *>(ViewAsSkill::parseViewAsSkill(skill))) return false;
        return skill->getFrequency() == Skill::Frequent || skill->getFrequency() == Skill::NotFrequent;
    }
};

class HTanfeng : public TriggerSkillV2
{
public:
    HTanfeng() : TriggerSkillV2("heg_tanfeng") { events << EventPhaseStart; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return {};
        for (ServerPlayer *other : room->getAlivePlayers())
            if (!player->willBeFriendWith(other) && player->canDiscard(other, "hej")) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getAlivePlayers())
            if (!ctx.owner->willBeFriendWith(other) && ctx.owner->canDiscard(other, "hej")) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@tanfeng-target", true, true);
        if (!target) return false;
        ctx.targets = {target};
        ctx.extra_data = room->askForCardChosen(ctx.owner, target, "hej", objectName(), false, Card::MethodDiscard);
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.targets.isEmpty()) return false;
        ServerPlayer *target = ctx.targets.first(); const int id = ctx.extra_data.toInt(); const Card *card = Sanguosha->getCard(id);
        if (!card || card->hasFlag("using") || room->getCardOwner(id) != target || !ctx.owner->canDiscard(target, id)
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip && room->getCardPlace(id) != Player::PlaceDelayedTrick)) return false;
        room->throwCard(card, target, ctx.owner); return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "skip") { target->skip(static_cast<Player::Phase>(ctx.extra_data.toInt())); return false; }
        const QStringList phases{"judge", "draw", "play", "discard", "finish", "cancel"};
        const QString choice = room->askForChoice(target, objectName(), phases.join("+"), QVariant(), QString(), "@tanfeng-choose:" + ctx.owner->objectName());
        const int index = phases.indexOf(choice);
        if (index < 0 || choice == "cancel") return false;
        room->damage(DamageStruct(objectName(), ctx.owner, target, amount, DamageStruct::Fire));
        SkillContext child = ctx; child.choice = "skip"; child.extra_data = index + 2;
        skillEffect(event, room, owner, child, ctx.owner); return false;
    }
};
class HJinwu : public TriggerSkillV2
{
public:
    HJinwu() : TriggerSkillV2("heg_jinwu") { events << EventPhaseStart; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (!target->askCommandto(objectName(), target)) {
            room->setPlayerFlag(target, "Global_PlayPhaseTerminated"); return false;
        }
        for (int i = 0; i < amount && target->isAlive(); ++i) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *other : room->getAlivePlayers()) if (target->canSlash(other)) candidates << other;
            if (candidates.isEmpty()) break;
            ServerPlayer *victim = room->askForPlayerChosen(target, candidates, "jinwu-slash", "@jinwu-slash");
            if (!victim) break;
            auto slash = std::make_unique<Slash>(Card::NoSuit, 0); slash->setSkillName("_heg_jinwu");
            CardUseStruct use(slash.get(), target, victim); use.setOwnedCard(slash.release());
            room->useCardFromSkillEffect(use, ctx, false);
        }
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 0; }
};
class HZhuke : public TriggerSkillV2
{
public:
    HZhuke() : TriggerSkillV2("heg_zhuke")
    { events << CommandVerifying << TurnedOver << ChainStateChanged; relate_to_place = "head"; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && (event == CommandVerifying || (event == TurnedOver && !player->faceUp()) || (event == ChainStateChanged && player->isChained()))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CommandVerifying) {
            const QVariant previous = ctx.owner->getTag("ZhukeCommanddata");
            ctx.owner->setTag("ZhukeCommanddata", *ctx.original_data);
            const auto restore = qScopeGuard([&] { ctx.owner->setTag("ZhukeCommanddata", previous); });
            if (!ctx.owner->askForSkillInvoke(this)) return false;
            // Command selection is intent, so bypass payment preserves this choice.
            ctx.extra_data = ctx.owner->startCommand(objectName()); ctx.targets = {ctx.owner}; return true;
        }
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getAlivePlayers()) if (ctx.owner->isFriendWith(other) && other->canRecover()) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "zhuke-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (event == CommandVerifying) {
            QStringList fields = ctx.original_data->toString().split(":"); const int index = ctx.extra_data.toInt();
            if (fields.size() >= 2 && index >= 0 && index < 6) { fields[1] = QString("command%1").arg(index + 1); *ctx.original_data = fields.join(":"); }
        } else room->recover(target, RecoverStruct(objectName(), ctx.owner, amount));
        return false;
    }
};
class HQuanjia : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HQuanjia() : TriggerSkillV2("heg_quanjia")
    {
        relate_to_place = "deputy";
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *, QVariant &) const override
    {
        return {};
    }
};

class HQuanjiaCompulsory : public TriggerSkillV2
{
public:
    HQuanjiaCompulsory() : TriggerSkillV2("#heg_quanjia-compulsory")
    { events << GeneralShowed << EventSkillInvoking; frequency = Compulsory; relate_to_place = "deputy"; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == GeneralShowed && player && player->cheakSkillLocation("heg_quanjia", data)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstance *instance = ctx.owner->findSkillInstance(objectName(), ctx.instanceID);
        if (!instance || !isUsable(ctx)) return false;
        const QStringList shown = ctx.original_data->toStringList();
        return instance->bindHead == 0 || (instance->bindHead == 1 && shown.contains("head"))
            || (instance->bindHead == 2 && shown.contains("deputy"));
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.activationRef.key.skillName == objectName()) addUsage(accepted);
        }
        return false;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        ctx.targets.clear();
        QList<ServerPlayer *> players = room->getAlivePlayers(); room->sortByActionOrder(players);
        for (ServerPlayer *target : players) {
            if (ctx.owner->getRole().startsWith("careerist")) break;
            if (!target->hasShownOneGeneral()) {
                SkillContext child = ctx; child.choice = "show"; skillEffect(event, room, owner, child, target);
            }
        }
        players = room->getAlivePlayers(); room->sortByActionOrder(players);
        for (ServerPlayer *target : players) if (target->isFriendWith(ctx.owner)) {
            SkillContext child = ctx; child.choice = "draw"; skillEffect(event, room, owner, child, target);
        }
        for (ServerPlayer *target : room->getAlivePlayers()) {
            if ((target->hasShownGeneral1() && target->getGeneral()->hasSkill("rende"))
                || (target->hasShownGeneral2() && target->getGeneral2() && target->getGeneral2()->hasSkill("rende"))) {
                SkillContext child = ctx; child.choice = "grant"; skillEffect(event, room, owner, child, target);
            }
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "draw") { target->drawCards(amount, "heg_quanjia"); return false; }
        if (ctx.choice == "grant") {
            room->acquireSkillFromEffect(target, "heg_zhangwu", ctx);
            room->acquireSkillFromEffect(target, "heg_shouyue", ctx);
            room->addPlayerMark(target, "##quanjia");
            return false;
        }
        const bool sameKingdom = target->getKingdom() == ctx.owner->getKingdom();
        const bool head = sameKingdom && target->getActualGeneral1()->getKingdom() != "careerist";
        QStringList choices;
        if (head && target->disableShow(true).isEmpty() && !target->hasShownGeneral1()) choices << "show_head_general";
        if (sameKingdom && target->disableShow(false).isEmpty() && !target->hasShownGeneral2()) choices << "show_deputy_general";
        if (choices.contains("show_head_general") && choices.contains("show_deputy_general")) choices << "show_both_generals";
        choices << "cancel";
        // The room flag modifies this reveal only; nested effects restore their caller.
        const QVariant previous = room->getTag("GlobalQuanjiaShow"); room->setTag("GlobalQuanjiaShow", true);
        const auto restore = qScopeGuard([&] { room->setTag("GlobalQuanjiaShow", previous); });
        const QString selected = room->askForChoice(target, "heg_quanjia", choices.join("+"), QVariant(), QString(), "@generalshow-choose");
        if (!choices.contains(selected) || selected == "cancel") return false;
        if (selected == "show_head_general" || selected == "show_both_generals") target->showGeneral(true, true, false);
        if (selected == "show_deputy_general" || selected == "show_both_generals") target->showGeneral(false, true, false);
        LogMessage log; log.type = "#BasaraReveal"; log.from = target; log.arg = target->getGeneralName(); log.arg2 = target->getGeneral2Name(); room->sendLog(log);
        return false;
    }
};
class HTunchu : public TriggerSkillV2
{
public:
    HTunchu() : TriggerSkillV2("heg_tunchu") { events << DrawNCards; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && data.canConvert<DrawStruct>()
            && data.value<DrawStruct>().reason == "draw_phase" ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>(); if (draw.who != target) return false;
        const QVariantMap scopes = room->historyScopes();
        if (scopes.value("phase_id").toLongLong() <= 0 || scopes.value("turn_id").toLongLong() <= 0) return false;
        const qint64 serial = room->getTag("HTunchuSerial").toLongLong() + 1; room->setTag("HTunchuSerial", serial);
        const QString token = QString("heg_tunchu:%1").arg(serial);
        QVariantList receipts = room->getTag("HTunchuApplied").toList();
        receipts << QVariantMap{{"serial", serial}, {"token", token}, {"holder", target->objectName()}, {"amount", amount},
            {"phase", scopes.value("phase_id")}, {"turn", scopes.value("turn_id")},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
        room->setTag("HTunchuApplied", receipts);
        room->setPlayerCardLimitation(target, "use", "Slash", false, token);
        room->addPlayerMark(target, "##tunchu");
        draw.num += 2 * amount; *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class HTunchuEffect : public TriggerSkillV2
{
public:
    HTunchuEffect() : TriggerSkillV2("#heg_tunchu-effect")
    { events << EventPhaseEnd << EventPhaseChanging << EventSkillEffectFinished; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    void consume(Room *room, const SkillContext &ctx) const
    {
        QVariantList receipts = room->getTag("HTunchuApplied").toList();
        const int index = receipts.indexOf(ctx.extra_data); if (index < 0) return;
        QVariantMap receipt = receipts.at(index).toMap(); receipt.insert("consumed", true);
        receipts[index] = receipt; room->setTag("HTunchuApplied", receipts);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && !finished.activationRef.isValid()) consume(room, finished);
            return false;
        }
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        QVariantList retained, expired;
        for (const QVariant &entry : room->getTag("HTunchuApplied").toList())
            (entry.toMap().value("turn").toLongLong() == turn ? expired : retained) << entry;
        // Publish expiry before native limitation/mark callbacks can reenter.
        room->setTag("HTunchuApplied", retained);
        for (const QVariant &entry : expired) {
            const QVariantMap receipt = entry.toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
            if (!holder) continue;
            room->removePlayerCardLimitationByReason(holder, receipt.value("token").toString());
            room->removePlayerMark(holder, "##tunchu");
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &out) const override
    {
        if (event != EventPhaseEnd || !player || player->getPhase() != Player::Draw || !player->isAlive()) return true;
        const qint64 phase = room->historyScopes().value("phase_id").toLongLong();
        const QVariantList receipts = room->getTag("HTunchuApplied").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("consumed").toBool() || receipt.value("holder").toString() != player->objectName()
                || receipt.value("phase").toLongLong() != phase) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = ctx.sourceRef.key.instanceID; ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true; ctx.trigger_count = i;
            ctx.preferredTarget = player; ctx.targets = {player}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.sourceRef.isValid() && room->getTag("HTunchuApplied").toList().contains(ctx.extra_data) && !ctx.extra_data.toMap().value("consumed").toBool(); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { consume(room, ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0 || target->isKongcheng()) return false;
        const QList<int> selected = room->askForExchangeCards(target, "tunchu_push", 2 * amount, amount, "@tunchu-push", "", ".|.|.|hand");
        QList<int> payable;
        for (int id : selected) {
            const Card *card = Sanguosha->getCard(id);
            if (card && !card->hasFlag("using") && target->handCards().contains(id) && !payable.contains(id)) payable << id;
        }
        if (!payable.isEmpty()) target->addToPile("heg_food", payable);
        return false;
    }
};
class HShuliang : public TriggerSkillV2
{
public:
    HShuliang() : TriggerSkillV2("heg_shuliang") { events << EventPhaseStart; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            const int distance = owner->distanceTo(player);
            if (owner != player && owner->isFriendWith(player) && distance >= 0
                && distance <= owner->getPile("heg_food").size() && !owner->getPile("heg_food").isEmpty()) result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<int> ids = room->askForExchangeCards(ctx.owner, objectName(), 1, 0,
            "@shuliang:" + ctx.invoker->objectName(), "heg_food");
        if (ids.size() != 1) return false;
        ctx.extra_data = ids.first();
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.extra_data.isValid() || !ctx.owner->getPile("heg_food").contains(id) || Sanguosha->getCard(id)->hasFlag("using")) return false;
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), ctx.owner->objectName(), objectName(), QString());
        room->throwCard(Sanguosha->getCard(id), reason, nullptr);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class HDujin : public TriggerSkillV2
{
public:
    HDujin() : TriggerSkillV2("heg_dujin") { events << DrawNCards << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.owner)
            accepted.owner->setSkillInstanceStateValue(objectName(), accepted.instanceID, "invoked", true);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DrawNCards || !player || !player->isAlive() || !player->hasSkill(objectName()) || !player->hasEquip()
            || !data.canConvert<DrawStruct>() || data.value<DrawStruct>().reason != "draw_phase") return {};
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner && ctx.owner->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        if (draw.who != target) return false;
        draw.num += ((target->getEquips().size() + 1) / 2) * getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};
class HDujinCompulsory : public TriggerSkillV2
{
public:
    HDujinCompulsory() : TriggerSkillV2("#heg_dujin-compulsory")
    { events << GeneralShowed << EventSkillInvoking; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    static SkillInstanceRef parent(const Player *holder, int id)
    {
        const SkillInstance *helper = holder ? holder->findSkillInstance("#heg_dujin-compulsory", id) : nullptr;
        if (!helper) return {};
        return helper->parentRef.isValid() ? helper->parentRef : SkillInstanceRef(holder->objectName(), helper->parent);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef != ctx.activationRef) return;
        const SkillInstanceRef ref = parent(ctx.owner, ctx.instanceID);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder && ref.isValid()) holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invoked", true);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != GeneralShowed || !player || !player->isAlive()) return result;
        for (ServerPlayer *other : room->getAllPlayers(true)) if (other != player && other->isFriendWith(player)) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref = parent(player, id);
            if (ref.ownerObjectName != player->objectName()) continue;
            const SkillInstance *instance = player->findSkillInstance(ref.key.skillName, ref.key.instanceID);
            if (!instance || player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invoked").toBool()) continue;
            const QStringList slot_list = data.toStringList();
            if ((instance->bindHead == 1 && slot_list.contains("head")) || (instance->bindHead == 2 && slot_list.contains("deputy")))
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { room->addPlayerMark(target, "@firstshow", getEffectiveAmount(ctx)); return false; }
};
class HKenshang : public ViewAsSkillV2
{
public:
    HKenshang() : ViewAsSkillV2("heg_kenshang") { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && ((request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                    && Slash::IsAvailable(request.initiator))
                || (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                    && request.pattern == "slash"));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || candidate->hasFlag("using") || request.selectedCardIds.contains(candidate->getEffectiveId()) || (!request.initiator->handCards().contains(candidate->getEffectiveId()) && !request.initiator->hasEquip(candidate))) return false;
        QList<int> selected;
        for (int id : request.selectedCardIds) {
            if (id < 0 || selected.contains(id) || !Sanguosha->getCard(id)) return false;
            selected.append(id);
        }
        // The candidate and already selected materials affect Slash legality;
        // keep this query local and side-effect free.
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcards(request.selectedCardIds);
        slash.addSubcard(candidate);
        return slash.isAvailable(request.initiator);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.isEmpty()) return false;
        // Replay selection order so forged requests obey the same card restrictions.
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
        Card *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        slash->setShowSkill(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class HKenshangEffect : public TriggerSkillV2
{
public:
    HKenshangEffect() : TriggerSkillV2("#heg_kenshang-effect")
    { events << EventSkillInvoking << TargetSpecifying << CardFinished << EventSkillEffectFinished; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && finished.choice == "finish" && finished.original_data) {
                const CardUseStruct use = finished.original_data->value<CardUseStruct>();
                if (use.card && use.card->getTag("HKenshangApplied") == finished.extra_data) use.card->removeTag("HKenshangApplied");
            }
            return false;
        }
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName != "heg_kenshang" || !accepted.sourceRef.isValid() || !accepted.use_card) return false;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return false;
        accepted.use_card->tag.insert("HKenshangApplied", QVariantMap{{"use_event", useEvent}, {"owner", accepted.sourceRef.ownerObjectName},
            {"skill", accepted.sourceRef.key.skillName}, {"instance", accepted.sourceRef.key.instanceID},
            {"activation_owner", accepted.activationRef.ownerObjectName}, {"activation_skill", accepted.activationRef.key.skillName},
            {"activation_instance", accepted.activationRef.key.instanceID}, {"amount", getEffectiveAmount(accepted)}});
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != TargetSpecifying && event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.from || !use.from->isAlive() || !use.card->isKindOf("Slash")) return true;
        const QVariantMap receipt = use.card->getTag("HKenshangApplied").toMap();
        if (receipt.isEmpty() || receipt.value("use_event").toLongLong() != room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = use.from; ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.instanceID = ctx.sourceRef.key.instanceID; ctx.amount = receipt.value("amount").toInt();
        ctx.extra_data = receipt; ctx.choice = event == CardFinished ? "finish" : "targets"; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
        ctx.preferredTarget = use.from; ctx.targets = {use.from}; out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.sourceRef.isValid() && ctx.original_data && ctx.original_data->value<CardUseStruct>().card
        && ctx.original_data->value<CardUseStruct>().card->getTag("HKenshangApplied") == ctx.extra_data; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || getEffectiveAmount(ctx) <= 0) return false;
        if (event == TargetSpecifying) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *other : room->getUseExtraTargets(use, false))
                if (!target->inMyAttackRange(other)) candidates << other;
            for (ServerPlayer *other : use.to)
                if (!target->inMyAttackRange(other) && !candidates.contains(other)) candidates << other;
            if (candidates.isEmpty() || !target->askForSkillInvoke("_heg_kenshang", "prompt")) return false;
            use.to = candidates; room->sortByActionOrder(use.to);
            LogMessage log; log.type = "#KenshangTarget"; log.from = target; log.to = use.to; room->sendLog(log);
            *ctx.original_data = QVariant::fromValue(use);
            return false;
        }
        const QVariantMap history = room->queryCardUseDamage();
        if (!history.value("complete").toBool()) return false;
        int damage = 0;
        for (const QVariant &entry : history.value("items").toList()) damage += entry.toMap().value("data").toMap().value("amount").toInt();
        if (damage < use.card->subcardsLength()) target->drawCards(damage * getEffectiveAmount(ctx), "heg_kenshang");
        else {
            QStringList choices;
            for (const Skill *skill : target->getVisibleSkillList()) {
                if (skill->objectName() != "heg_kenshang" && !skill->objectName().startsWith("mashu")) continue;
                for (int id : target->getSkillInstanceIds(skill->objectName())) {
                    const SkillInstance *instance = target->findSkillInstance(skill->objectName(), id);
                    if (!instance || (instance->bindHead == 1 && !target->hasShownGeneral1())
                        || (instance->bindHead == 2 && !target->hasShownGeneral2())) continue;
                    choices << SkillInstanceUtils::formatName(skill->objectName(), id);
                }
            }
            if (!choices.isEmpty()) {
                const QString selected = room->askForChoice(target, "heg_kenshang", choices.join("+"), QVariant(), QString(), "@kenshang-choose");
                if (choices.contains(selected)) room->detachSkillFromPlayer(target, selected);
            }
        }
        return false;
    }
};
class HQizhi : public TriggerSkillV2
{
public:
    HQizhi() : TriggerSkillV2("heg_qizhi") { events << TargetSpecified << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 3; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified || !player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() == Player::NotActive) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || (use.card->getTypeId() != Card::TypeBasic && use.card->getTypeId() != Card::TypeTrick)) return {};
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!use.to.contains(target) && player->canDiscard(target, player->isFriendWith(target) ? "he" : "h")) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (!use.to.contains(target) && ctx.owner->canDiscard(target, ctx.owner->isFriendWith(target) ? "he" : "h")) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "heg_qizhi-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QString zone = ctx.owner->isFriendWith(target) ? "he" : "h";
        if (!ctx.owner->canDiscard(target, zone)) return false;
        const QList<int> selected = room->askForCardsChosen(ctx.owner, target, zone, objectName(), amount, amount, false, Card::MethodDiscard);
        DummyCard discarded;
        for (int id : selected) if (room->getCardOwner(id) == target && ctx.owner->canDiscard(target, id)) discarded.addSubcard(id);
        if (discarded.subcardsLength() == 0) return false;
        room->throwCard(&discarded, target, ctx.owner);
        if (target->isAlive()) target->drawCards(amount, objectName());
        return false;
    }
};

class HJinqu : public TriggerSkillV2
{
public:
    HJinqu() : TriggerSkillV2("heg_jinqu") { events << EventPhaseStart; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    static int qizhiCount(Room *room, ServerPlayer *actor)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return -1;
        QVariantMap query{{"kind", "skill_invoked"}, {"turn_id", turn}};
        int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                if (value.value("invoked_skill").toString() != "heg_qizhi") continue;
                // Count accepted invocations by their actor, including retired grants.
                if (value.value("activation_owner").toString() == actor->objectName()) ++count;
                else if (value.value("activation_owner").toString().isEmpty() && value.value("player").toString() == actor->objectName()) return -1;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        return count;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish && qizhiCount(room, player) >= 0
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = qizhiCount(room, ctx.owner);
        if (count < 0 || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.extra_data = count; ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (!target->isAlive()) return false;
        const int excess = target->getHandcardNum() - ctx.extra_data.toInt();
        if (excess > 0) room->askForDiscard(target, objectName(), excess, excess);
        return false;
    }
};
class HJuzhan : public TriggerSkillV2
{
public:
    HJuzhan() : TriggerSkillV2("heg_juzhan") { events << TargetSpecified << TargetConfirmed << EventPhaseChanging; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    static int parity(Room *room, const SkillInstanceRef &activation)
    {
        QVariantMap query{{"kind", "skill_invoked"}};
        int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap value = entry.toMap().value("data").toMap();
                if (value.value("activation_owner").toString() == activation.ownerObjectName
                    && value.value("activation_skill").toString() == activation.key.skillName
                    && value.value("activation_instance_id").toInt() == activation.key.instanceID) ++count;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        return count % 2;
    }
    static void project(Room *room)
    {
        for (ServerPlayer *actor : room->getAllPlayers(true)) {
            QStringList blocked;
            for (const QVariant &entry : room->getTag("HJuzhanApplied").toList()) {
                const QVariantMap value = entry.toMap();
                if (value.value("kind").toString() == "prohibit" && value.value("holder").toString() == actor->objectName()
                    && !blocked.contains(value.value("target").toString())) blocked << value.value("target").toString();
            }
            room->setPlayerProperty(actor, "heg_juzhan_prohibited", blocked.join("+"));
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        QVariantList kept, expired;
        for (const QVariant &entry : room->getTag("HJuzhanApplied").toList()) {
            if (entry.toMap().value("turn").toLongLong() == turn) expired << entry; else kept << entry;
        }
        room->setTag("HJuzhanApplied", kept); project(room);
        for (const QVariant &entry : expired) {
            const QVariantMap receipt = entry.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (target && receipt.value("kind").toString() == "hide") room->removePlayerDisableShow(target, receipt.value("token").toString());
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if ((event != TargetSpecified && event != TargetConfirmed) || !player || !player->isAlive()
            || room->historyScopes().value("turn_id").toLongLong() <= 0) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || !use.from) return result;
        if (event == TargetConfirmed && (!use.to.contains(player) || !use.from->isAlive())) return result;
        if (event == TargetSpecified && use.from != player) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const int state = parity(room, SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id)));
            if ((event == TargetConfirmed && state == 0) || (event == TargetSpecified && state == 1))
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) return ctx.owner->askForSkillInvoke(this, QVariant::fromValue(use.from));
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : use.to) if (ctx.owner->canGet(target, "he")) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "heg_juzhan-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    static QVariantMap receipt(Room *room, const SkillContext &ctx, ServerPlayer *target, const QString &kind)
    {
        const qint64 serial = room->getTag("HJuzhanSerial").toLongLong() + 1; room->setTag("HJuzhanSerial", serial);
        return {{"holder", ctx.owner->objectName()}, {"target", target->objectName()}, {"kind", kind},
            {"turn", room->historyScopes().value("turn_id")}, {"token", QString("heg_juzhan:%1").arg(serial)},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != TargetConfirmed) return false;
        ctx.manual_effect = true;
        SkillContext self = ctx; self.choice = "draw"; skillEffect(event, room, owner, self, ctx.owner);
        ServerPlayer *attacker = ctx.original_data->value<CardUseStruct>().from;
        if (attacker && attacker->isAlive()) {
            SkillContext other = ctx; other.choice = "draw-hide"; skillEffect(event, room, owner, other, attacker);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.choice == "draw" || ctx.choice == "draw-hide") {
            target->drawCards(amount, objectName());
            if (ctx.choice == "draw" || !ctx.owner->isAlive() || !target->isAlive() || !target->hasShownAllGenerals()) return false;
            QStringList choices;
            if (!target->getActualGeneral1Name().contains("sujiang") && !target->isLord()) choices << "head";
            if (target->getGeneral2() && !target->getGeneral2Name().contains("sujiang")) choices << "deputy";
            if (choices.isEmpty()) return false;
            choices << "cancel";
            const QString selected = room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant(), "head+deputy+cancel", "@juzhan-hide::" + target->objectName());
            if (!choices.contains(selected) || selected == "cancel") return false;
            QVariantMap applied = receipt(room, ctx, target, "hide");
            QVariantList list = room->getTag("HJuzhanApplied").toList(); list << applied; room->setTag("HJuzhanApplied", list);
            room->setPlayerDisableShow(target, selected == "head" ? "h" : "d", applied.value("token").toString());
            target->hideGeneral(selected == "head"); return false;
        }
        if (ctx.choice == "obtain") {
            const QVariantMap value = ctx.extra_data.toMap();
            ServerPlayer *victim = room->findPlayerByObjectName(value.value("victim").toString(), true);
            if (!victim) return false;
            DummyCard cards;
            for (const QVariant &entry : value.value("cards").toList()) {
                const int id = entry.toInt(); const Card *card = Sanguosha->getCard(id);
                if (card && !card->hasFlag("using") && room->getCardOwner(id) == victim
                    && (victim->handCards().contains(id) || victim->hasEquip(card)) && target->canGet(victim, id))
                    cards.addSubcard(id);
            }
            if (cards.subcardsLength() == 0) return false;
            room->obtainCard(target, &cards, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), victim->objectName(), objectName(), ""), false);
            QVariantList applied = room->getTag("HJuzhanApplied").toList(); applied << receipt(room, ctx, victim, "prohibit");
            room->setTag("HJuzhanApplied", applied); project(room); return false;
        }
        if (!ctx.owner->canGet(target, "he")) return false;
        const QList<int> ids = room->askForCardsChosen(ctx.owner, target, "he", objectName(), amount, amount, false, Card::MethodGet);
        QVariantList cards; for (int id : ids) cards << id;
        SkillContext child = ctx; child.choice = "obtain"; child.extra_data = QVariantMap{{"victim", target->objectName()}, {"cards", cards}};
        skillEffect(event, room, owner, child, ctx.owner); return false;
    }
};
class HDanshou : public TriggerSkillV2
{
public:
    HDanshou() : TriggerSkillV2("heg_danshou") { events << EventPhaseStart << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Round; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Start
            || room->historyScopes().value("turn_id").toLongLong() <= 0) return result;
        for (ServerPlayer *holder : room->getAlivePlayers())
            if (!holder->isAllNude() && holder->hasSkill(objectName())) result[holder] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(ctx.invoker))) return false;
        QVariantList ids;
        for (const Card *card : ctx.owner->getCards("hej"))
            if (!card->hasFlag("using") && ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) ids << card->getEffectiveId();
        if (ids.isEmpty()) return false;
        ctx.extra_data = QVariantMap{{"cards", ids}, {"discarded", ids.size()}};
        ctx.targets = {ctx.owner}; return true;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        QVariantMap receipt = ctx.extra_data.toMap();
        DummyCard discarded;
        for (const QVariant &entry : receipt.value("cards").toList()) {
            const int id = entry.toInt();
            if (room->getCardOwner(id) != ctx.owner || Sanguosha->getCard(id)->hasFlag("using") || !ctx.owner->canDiscard(ctx.owner, id)
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip && room->getCardPlace(id) != Player::PlaceDelayedTrick)) return false;
            discarded.addSubcard(id);
        }
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        const qint64 parent = room->currentHistoryEventId();
        addUsage(ctx);
        room->throwCard(&discarded, ctx.owner);
        QVariantMap query{{"after", before.value("watermark")}, {"from", ctx.owner->objectName()}};
        QSet<int> actual;
        bool complete = before.value("complete").toBool() && parent > 0;
        while (complete) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool()) { complete = false; break; }
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
                const int id = value.value("card_id").toInt();
                if (discarded.getSubcards().contains(id) && value.value("to_place").toInt() == Player::DiscardPile
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == parent) actual.insert(id);
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        receipt.insert("discarded", complete ? actual.size() : 0); ctx.extra_data = receipt;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx), discarded = ctx.extra_data.toMap().value("discarded").toInt();
        if (amount <= 0 || discarded <= 0) return false;
        const qint64 serial = room->getTag("HDanshouSerial").toLongLong() + 1; room->setTag("HDanshouSerial", serial);
        QVariantList receipts = room->getTag("HDanshouApplied").toList();
        receipts << QVariantMap{{"serial", serial}, {"holder", target->objectName()}, {"turn", room->historyScopes().value("turn_id")},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
            {"amount", amount}, {"discarded", discarded}, {"draw", 1}};
        room->setTag("HDanshouApplied", receipts);
        return false;
    }
};

class HDanshouEffect : public TriggerSkillV2
{
public:
    HDanshouEffect() : TriggerSkillV2("#heg_danshou-effect")
    { events << EventPhaseStart << EventPhaseChanging; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        QVariantList kept;
        for (const QVariant &entry : room->getTag("HDanshouApplied").toList())
            if (entry.toMap().value("turn").toLongLong() != turn) kept << entry;
        room->setTag("HDanshouApplied", kept); return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &out) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() <= Player::Start || player->getPhase() >= Player::Finish) return true;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        const QVariantList receipts = room->getTag("HDanshouApplied").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString());
            if (!holder || !holder->isAlive() || turn <= 0 || receipt.value("turn").toLongLong() != turn
                || holder->getHandcardNum() > receipt.value("discarded").toInt()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = holder; ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = ctx.sourceRef.key.instanceID; ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt;
            ctx.current_event = event; ctx.original_data = &data; ctx.is_forced = true; ctx.trigger_count = i;
            ctx.preferredTarget = holder; ctx.targets = {holder}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        for (const QVariant &entry : room->getTag("HDanshouApplied").toList())
            if (entry.toMap().value("serial") == ctx.extra_data.toMap().value("serial")) return ctx.sourceRef.isValid();
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "damage") { room->damage(DamageStruct("heg_danshou", ctx.owner, target, getEffectiveAmount(ctx))); return false; }
        const QVariantMap receipt = ctx.extra_data.toMap();
        const int draw = receipt.value("draw").toInt() * getEffectiveAmount(ctx);
        const QString choice = room->askForChoice(target, "heg_danshou", "draw+exdraw", QVariant::fromValue(ctx.invoker), QString(),
            "#heg_danshou-choose:::" + QString::number(draw));
        if (choice == "exdraw") {
            QVariantList receipts = room->getTag("HDanshouApplied").toList();
            for (int i = 0; i < receipts.size(); ++i) {
                QVariantMap value = receipts.at(i).toMap();
                if (value.value("serial") != receipt.value("serial")) continue;
                value.insert("draw", value.value("draw").toInt() + 1); receipts[i] = value; break;
            }
            room->setTag("HDanshouApplied", receipts); return false;
        }
        const QVariantMap before = room->queryHistoryFacts({{"limit", 1}});
        const qint64 parent = room->currentHistoryEventId();
        target->drawCards(draw, "heg_danshou");
        if (!target->isAlive() || !ctx.invoker || !ctx.invoker->isAlive() || parent <= 0 || !before.value("complete").toBool()) return false;
        QVariantMap query{{"kind", "draw_result"}, {"after", before.value("watermark")}, {"player", target->objectName()}};
        QSet<qint64> draws;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const qint64 id = entry.toMap().value("event_id").toLongLong();
                if (room->historyEvent(id).value("parent_id").toLongLong() == parent) draws.insert(id);
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        query = {{"after", before.value("watermark")}, {"to", target->objectName()}};
        QMap<qint64, int> actual;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
                const qint64 id = room->historyParent(fact.value("event_id").toLongLong(), "draw", true).value("id").toLongLong();
                if (draws.contains(id) && value.value("from_place").toInt() == Player::DrawPile && value.value("to_place").toInt() == Player::PlaceHand) ++actual[id];
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        if (actual.values().contains(4) && room->askForChoice(target, "heg_danshou", "yes+no", QVariant::fromValue(ctx.invoker), QString(),
                "#heg_danshou-damage::" + ctx.invoker->objectName()) == "yes") {
            SkillContext child = ctx; child.choice = "damage"; skillEffect(event, room, owner, child, ctx.invoker);
        }
        return false;
    }
};
HBiaozhaoCard::HBiaozhaoCard()
{
    setSkillName("heg_biaozhao");
    mute = true;
}

bool HBiaozhaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self) return false;
    HKnownBoth knownBoth(Card::NoSuit, 0);
    const HKnownBoth *known_both = &knownBoth;
    if (targets.isEmpty()) {
        return known_both->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, known_both);
    } else if (targets.length() == 1) {
        return !to_select->isFriendWith(targets.first());
    }
    return false;
}

bool HBiaozhaoCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void HBiaozhaoCard::onUse(Room *room, CardUseStruct &card_use) const
{
    // Shared SkillCard payment/reveal dispatch owns the exact V2 source.
    SkillCard::onUse(room, card_use);
}

void HBiaozhaoCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const
{
    // V2 owns the ordered recipient effects, including their target hooks.
}

class HBiaozhao : public ViewAsSkillV2
{
public:
    HBiaozhao() : ViewAsSkillV2("heg_biaozhao") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *candidate) const override
    {
        if (!request.initiator || !candidate || !candidate->isAlive() || selected.contains(candidate)) return false;
        HBiaozhaoCard card;
        return card.targetFilter(selected, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.length() == 2; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        return ViewAsSkillV2::createCard(request);
    }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effectOnTargetGroup(SkillContext &context, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.length() != 2) return FinishSkill;
        context.choice = "inspect";
        skillEffect(context, targets.first());
        if (!context.invoker || context.invoker->isDead() || targets.last()->isDead()) return FinishSkill;
        context.choice = "give";
        skillEffect(context, targets.last());
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &context, ServerPlayer *target) const override
    {
        ServerPlayer *actor = context.invoker;
        Room *room = actor->getRoom();
        if (!target || target->isDead() || getEffectiveAmount(context) <= 0) return ContinueEffects;
        if (context.choice == "inspect") {
            auto *known = new HKnownBoth(Card::NoSuit, 0);
            known->setSkillName("_heg_biaozhao");
            CardUseStruct use(known, actor, target);
            use.setOwnedCard(known);
            room->useCardFromSkillEffect(use, context, true);
        } else if (context.choice == "give" && actor->isAlive() && !actor->isNude()) {
            const QList<int> cards = room->askForExchangeCards(actor, "biaozhao_give", 1, 1,
                "@biaozhao-give::" + target->objectName());
            if (cards.size() != 1 || room->getCardOwner(cards.first()) != actor || Sanguosha->getCard(cards.first())->hasFlag("using")
                || (!actor->handCards().contains(cards.first()) && !actor->hasEquip(Sanguosha->getCard(cards.first())))) return ContinueEffects;
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, actor->objectName(), target->objectName(),
                objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(cards, target, Player::PlaceHand, reason), false);
            if (actor->isAlive()) {
                SkillContext draw = context;
                draw.choice = "draw";
                skillEffect(draw, actor);
            }
        } else if (context.choice == "draw") {
            target->drawCards(getEffectiveAmount(context), objectName());
        }
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HBiaozhaoCard"; }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 0; }
};
class HYechou : public TriggerSkillV2
{
public:
    HYechou() : TriggerSkillV2("heg_yechou") { events << Death; frequency = Compulsory; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool canPreshow() const override { return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        return player && player->hasSkill(objectName()) && death.who == player && death.damage
            && death.damage->from && death.damage->from->isAlive()
            ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.current_event == Death && ctx.owner && ctx.owner->hasSkillInstance(objectName(), ctx.instanceID)
            && !ctx.owner->isSkillInvalid(objectName(), ctx.instanceID)) return true;
        return TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (death.who == ctx.owner && death.damage && death.damage->from) ctx.targets = {death.damage->from};
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        // Each accepted repetition retains the printed three-card ordering.
        for (int repetition = 0; repetition < amount && target->isAlive(); ++repetition) {
            for (int i = 0; i < 3 && target->isAlive(); ++i) {
                auto slash = std::make_unique<Slash>(Card::NoSuit, 0);
                slash->setSkillName("_heg_yechou");
                if (ctx.owner->isProhibited(target, slash.get())) break;
                slash->setTag("HYechouApplied", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
                    {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                    {"activation_owner", ctx.activationRef.ownerObjectName},
                    {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID},
                    {"bonus", i == 2 ? 1 : 0}});
                CardUseStruct use(slash.get(), ctx.owner, target);
                if (i == 0) use.no_respond_list << "_ALL_TARGETS";
                if (i == 1) target->addQinggangTag(slash.get());
                use.setOwnedCard(slash.release());
                room->useCardFromSkillEffect(use, ctx, false);
            }
        }
        return false;
    }
};

class HYechouEffect : public TriggerSkillV2
{
public:
    HYechouEffect() : TriggerSkillV2("#heg_yechou-effect")
    { events << Dying << QuitDying << ConfirmDamage; frequency = Compulsory; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    static qint64 dyingEvent(Room *room)
    { return room->historyParent(room->currentHistoryEventId(), "dying", true).value("id").toLongLong(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != QuitDying || !player) return false;
        const qint64 current = dyingEvent(room);
        if (current <= 0) return false;
        QVariantList kept;
        for (const QVariant &entry : player->getTag("HYechouDying").toList())
            if (entry.toMap().value("dying").toLongLong() != current) kept << entry;
        // Retire only this episode before mark callbacks; nested episodes remain restricted.
        player->setTag("HYechouDying", kept);
        room->setPlayerMark(player, "##yechou", kept.size());
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &out) const override
    {
        if (event != Dying && event != ConfirmDamage) return true;
        const DamageStruct damage = event == ConfirmDamage ? data.value<DamageStruct>()
            : (data.value<DyingStruct>().damage ? *data.value<DyingStruct>().damage : DamageStruct());
        if (!damage.card || !damage.to) return true;
        const QVariantMap receipt = damage.card->getTag("HYechouApplied").toMap();
        if (receipt.isEmpty() || (event == ConfirmDamage && (damage.chain || damage.transfer || receipt.value("bonus").toInt() <= 0))) return true;
        if (event == Dying && (data.value<DyingStruct>().who != player || dyingEvent(room) <= 0)) return true;
        ServerPlayer *source = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!source) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = source; ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(source->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.instanceID = ctx.sourceRef.key.instanceID; ctx.amount = event == ConfirmDamage ? receipt.value("bonus").toInt() : 1;
        ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
        ctx.targets = {damage.to}; ctx.preferredTarget = damage.to;
        out << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.sourceRef.isValid() && !ctx.extra_data.toMap().isEmpty(); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.to == target) { damage.damage += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage); }
        } else {
            const DyingStruct dying = ctx.original_data->value<DyingStruct>();
            const qint64 episode = dyingEvent(room);
            if (dying.who != target || episode <= 0 || getEffectiveAmount(ctx) <= 0) return false;
            QVariantMap receipt = ctx.extra_data.toMap(); receipt.insert("dying", episode);
            QVariantList receipts = target->getTag("HYechouDying").toList();
            if (!receipts.contains(receipt)) receipts << receipt;
            target->setTag("HYechouDying", receipts);
            room->setPlayerMark(target, "##yechou", receipts.size());
        }
        return false;
    }
};
class HYechouProhibit : public ProhibitSkill
{
public:
    HYechouProhibit() : ProhibitSkill("#heg_yechou-prohibit")
    {
    }

    virtual bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("Peach") && from && to && from != to && from->isFriendWith(to) && to->getMark("##yechou") > 0;
    }
};

class HXiaoqiViewAsSkill : public ViewAsSkillV2
{
public:
    HXiaoqiViewAsSkill() : ViewAsSkillV2("heg_xiaoqi") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE || request.pattern != "slash"
            || request.activationRef.ownerObjectName != request.initiator->objectName()) return false;
        const int id = request.activationRef.key.instanceID;
        if (!request.initiator->getSkillInstanceStateValue(objectName(), id, "response_only").toBool()
            || !request.initiator->getSkillInstanceStateValue(objectName(), id, "response_prompt").toBool()
            || request.initiator->getSkillInstanceStateValue(objectName(), id, "remaining").toInt() <= 0) return false;
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator)) {
            Room *room = server->getRoom();
            const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            for (const QVariant &entry : room->getTag("HXiaoqiApplied").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("holder").toString() == server->objectName() && receipt.value("helper").toInt() == id
                    && receipt.value("use_event").toLongLong() == useEvent && receipt.value("remaining").toInt() > 0) return true;
            }
            return false;
        }
        return true;
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return canActivate(request) && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0); slash->setSkillName(objectName()); return slash;
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request); }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class HXiaoqi : public TriggerSkillV2
{
public:
    HXiaoqi() : TriggerSkillV2("heg_xiaoqi")
    { events << CardUsed << CardAsked << CardFinished << EventSkillInvoking << EventSkillEffectFinished; global = true; view_as_skill = new HXiaoqiViewAsSkill; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool canPreshow() const override { return true; }
    static bool isResponseGrant(Room *room, const Player *holder, int id)
    {
        for (const QVariant &entry : room->getTag("HXiaoqiApplied").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("holder").toString() == holder->objectName() && receipt.value("helper").toInt() == id) return true;
        }
        return false;
    }
    static void project(Room *room, ServerPlayer *holder)
    {
        int total = 0;
        for (const QVariant &entry : room->getTag("HXiaoqiApplied").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("holder").toString() == holder->objectName()) total += receipt.value("remaining").toInt();
        }
        room->setPlayerMark(holder, "#xiaoqi", total);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardAsked && player) {
            const QStringList asked = data.toStringList();
            const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            for (const QVariant &entry : room->getTag("HXiaoqiApplied").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("holder").toString() != player->objectName()) continue;
                const bool allowed = asked.size() >= 4 && asked.at(0) == "slash" && asked.at(2) == "response"
                    && asked.at(3) == receipt.value("card").toString() && receipt.value("use_event").toLongLong() == useEvent;
                player->setSkillInstanceStateValue(objectName(), receipt.value("helper").toInt(), "response_prompt", allowed);
            }
        } else if (event == EventSkillInvoking || event == EventSkillEffectFinished) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.activationRef.key.skillName != objectName() || !accepted.use_card
                || accepted.use_card->getSkillName() != objectName()) return false;
            QVariantList receipts = room->getTag("HXiaoqiApplied").toList();
            for (int i = 0; i < receipts.size(); ++i) {
                QVariantMap receipt = receipts.at(i).toMap();
                if (receipt.value("holder").toString() != accepted.activationRef.ownerObjectName
                    || receipt.value("helper").toInt() != accepted.activationRef.key.instanceID) continue;
                // Reserve at acceptance to protect nested responses; release only on
                // complete history proving that this exact response was never played.
                if (accepted.executionID <= 0) break; // Active response IDs are nonzero.
                QVariantList reserved = receipt.value("reserved").toList();
                if (event == EventSkillInvoking) {
                    if (reserved.contains(accepted.executionID)) break;
                    reserved << accepted.executionID;
                    receipt.insert("remaining", qMax(0, receipt.value("remaining").toInt() - 1));
                } else {
                    if (!reserved.removeOne(accepted.executionID)) break;
                    const qint64 response = room->historyParent(room->currentHistoryEventId(), "respond_card", true).value("id").toLongLong();
                    if (response > 0) {
                        const QVariantMap played = room->queryHistoryFacts({{"kind", "respond_card"}, {"event_id", response}, {"limit", 1}});
                        if (played.value("complete").toBool() && played.value("items").toList().isEmpty())
                            receipt.insert("remaining", receipt.value("remaining").toInt() + 1);
                    }
                }
                receipt.insert("reserved", reserved); receipts[i] = receipt;
                room->setTag("HXiaoqiApplied", receipts);
                ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
                if (holder) { holder->setSkillInstanceStateValue(objectName(), receipt.value("helper").toInt(), "remaining", receipt.value("remaining")); project(room, holder); }
                break;
            }
        } else if (event == CardFinished) {
            const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            QVariantList retained, expired;
            for (const QVariant &entry : room->getTag("HXiaoqiApplied").toList())
                (entry.toMap().value("use_event").toLongLong() == useEvent ? expired : retained) << entry;
            room->setTag("HXiaoqiApplied", retained);
            // Effect grants expire with this exact duel, without retiring sibling grants.
            for (const QVariant &entry : expired) {
                const QVariantMap receipt = entry.toMap();
                ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
                if (!holder) continue;
                room->detachSkillFromPlayer(holder, SkillInstanceUtils::formatName(objectName(), receipt.value("helper").toInt()));
                project(room, holder);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardUsed || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash")) return {};
        TriggerList result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (!isResponseGrant(room, player, id))
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (isResponseGrant(room, ctx.owner, ctx.instanceID)
            || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (!use.card) return false;
        if (use.card->isKindOf("Slash")) {
            auto duel = std::make_unique<Duel>(use.card->getSuit(), use.card->getNumber());
            duel->addSubcard(use.card); duel->setSkillName(use.card->getSkillName()); duel->setShowSkill(use.card->showSkill());
            use.changeCard(duel.get()); use.setOwnedCard(duel.release());
            use.card->tag.insert("HXiaoqiConverted", true); *ctx.original_data = QVariant::fromValue(use);
        } else if (!use.card->isKindOf("Duel") || !use.card->getTag("HXiaoqiConverted").toBool()) return false;
        int count = 0;
        for (ServerPlayer *other : room->getAlivePlayers()) {
            const General *generals[] = {other->hasShownGeneral1() ? other->getGeneral() : nullptr,
                other->hasShownGeneral2() ? other->getGeneral2() : nullptr};
            for (const General *general : generals) {
                if (!general) continue;
                for (const Skill *skill : general->getVisibleSkillList())
                    if (skill->objectName().startsWith("mashu")) { ++count; break; }
            }
        }
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (count <= 0 || useEvent <= 0) return false;
        const qint64 serial = room->getTag("HXiaoqiSerial").toLongLong() + 1; room->setTag("HXiaoqiSerial", serial);
        const int helper = room->acquireSkillFromEffect(target, objectName(), ctx, [&](int id) {
            QVariantList receipts = room->getTag("HXiaoqiApplied").toList();
            receipts << QVariantMap{{"serial", serial}, {"use_event", useEvent}, {"helper", id}, {"holder", target->objectName()},
                {"card", use.card->toString()}, {"remaining", count * amount},
                {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
            room->setTag("HXiaoqiApplied", receipts);
        });
        if (helper > 0) {
            target->setSkillInstanceStateValue(objectName(), helper, "response_only", true);
            target->setSkillInstanceStateValue(objectName(), helper, "remaining", count * amount);
            project(room, target);
        }
        return false;
    }
};
HKanjiCard::HKanjiCard()
{
    target_fixed = true;
    setSkillName("heg_kanji");
}

void HKanjiCard::extraCost(Room *, const CardUseStruct &) const {}
void HKanjiCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const
{
    // Compatibility cards enter the authoritative V2 recipient pipeline.
}

class HKanji : public ViewAsSkillV2
{
public:
    HKanji() : ViewAsSkillV2("heg_kanji") {}
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        return ViewAsSkillV2::createCard(request);
    }
    EffectFlow effect(SkillContext &ctx) const override
    { skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        room->showAllCards(target);
        QSet<Card::Suit> suits;
        for (int id : target->handCards()) {
            const Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            if (suits.contains(suit)) return ContinueEffects;
            suits.insert(suit);
        }
        const bool alreadyFour = suits.size() == 4;
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (target->isDead() || alreadyFour) return ContinueEffects;
        suits.clear();
        for (int id : target->handCards()) suits.insert(Sanguosha->getCard(id)->getSuit());
        if (suits.size() == 4) target->skip(Player::Discard);
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HKanjiCard"; }
};
class HQianzheng : public TriggerSkillV2
{
public:
    HQianzheng() : TriggerSkillV2("heg_qianzheng") { events << TargetConfirmed << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool canPreshow() const override { return true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetConfirmed || !player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && use.to.contains(player) && use.from != player && (use.card->isKindOf("Slash") || use.card->isNDTrick())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const Card *selection = room->askForExchange(ctx.owner, objectName(), 2, 2, true, "@qianzheng-cost", true);
        const QList<int> ids = selection ? selection->getSubcards() : QList<int>();
        if (ids.size() != 2 || ids.first() == ids.last()) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QVariantList materials; bool different = true;
        for (int id : ids) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || card->hasFlag("using") || (!ctx.owner->handCards().contains(id) && !ctx.owner->hasEquip(card))
                || ctx.owner->isCardLimited(card, Card::MethodRecast)) return false;
            materials << id; if (card->getTypeId() == use.card->getTypeId()) different = false;
        }
        ctx.extra_data = QVariantMap{{"materials", materials}, {"different", different}};
        ctx.targets = {ctx.owner}; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        QList<int> ids;
        for (const QVariant &entry : ctx.extra_data.toMap().value("materials").toList()) {
            const int id = entry.toInt(); const Card *card = Sanguosha->getCard(id);
            if (!card || card->hasFlag("using") || (!ctx.owner->handCards().contains(id) && !ctx.owner->hasEquip(card))
                || ctx.owner->isCardLimited(card, Card::MethodRecast)) return false;
            ids << id;
        }
        addUsage(ctx);
        // Payment moves the materials; the draw remains an interceptable recipient effect.
        room->recastCardsWithDraw(ctx.owner, ids, 0, objectName());
        return true;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.activationRef.key.skillName == objectName()) addUsage(accepted);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (use.card && useEvent > 0 && ctx.extra_data.toMap().value("different").toBool()) {
            const QList<int> physical = use.card->isVirtualCard() ? use.card->getSubcards() : QList<int>{use.card->getEffectiveId()};
            QVariantList cards; for (int id : physical) if (id >= 0) cards << id;
            if (!cards.isEmpty()) {
                const qint64 serial = room->getTag("HQianzhengSerial").toLongLong() + 1; room->setTag("HQianzhengSerial", serial);
                QVariantList receipts = room->getTag("HQianzhengApplied").toList();
                receipts << QVariantMap{{"serial", serial}, {"use_event", useEvent}, {"cards", cards}, {"holder", target->objectName()}, {"amount", amount},
                    {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                    {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_instance", ctx.activationRef.key.instanceID}};
                room->setTag("HQianzhengApplied", receipts);
            }
        }
        target->drawCards(2 * amount, "recast"); return false;
    }
};

class HQianzhengDelay : public TriggerSkillV2
{
public:
    HQianzhengDelay() : TriggerSkillV2("#heg_qianzheng-delay")
    { events << CardFinished << CardUsed << EventSkillEffectFinished; global = true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    void consume(Room *room, const SkillContext &ctx) const
    {
        QVariantList receipts = room->getTag("HQianzhengApplied").toList();
        const int index = receipts.indexOf(ctx.extra_data); if (index < 0) return;
        QVariantMap receipt = receipts.at(index).toMap(); receipt.insert("consumed", true);
        receipts[index] = receipt; room->setTag("HQianzhengApplied", receipts);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && !finished.activationRef.isValid()) consume(room, finished);
        } else if (event == CardUsed) {
            QVariantList receipts = room->getTag("HQianzhengApplied").toList();
            for (int i = receipts.size() - 1; i >= 0; --i)
                if (room->historyEvent(receipts.at(i).toMap().value("use_event").toLongLong()).value("status").toString() != "active")
                    receipts.removeAt(i);
            room->setTag("HQianzhengApplied", receipts);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &out) const override
    {
        if (event != CardFinished) return true;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return true;
        QSet<int> discarded;
        QVariantMap query{{"turn_id", room->historyEvent(useEvent).value("turn_id")}};
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool()) return true;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                if (move.value("from_place").toInt() == Player::PlaceTable && move.value("to_place").toInt() == Player::DiscardPile
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == useEvent)
                    discarded.insert(move.value("card_id").toInt());
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("watermark", page.value("watermark")); query.insert("after", page.value("next_after"));
        }
        const QVariantList receipts = room->getTag("HQianzhengApplied").toList();
        for (int i = 0; i < receipts.size(); ++i) {
            const QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("consumed").toBool() || receipt.value("use_event").toLongLong() != useEvent) continue;
            ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("holder").toString());
            if (!holder || !holder->isAlive()) continue;
            bool eligible = true;
            for (const QVariant &id : receipt.value("cards").toList())
                if (!discarded.contains(id.toInt()) || room->getCardPlace(id.toInt()) != Player::DiscardPile) eligible = false;
            if (!eligible) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = holder;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.instanceID = ctx.sourceRef.key.instanceID; ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt;
            ctx.original_data = &data; ctx.current_event = event; ctx.trigger_count = i; ctx.is_forced = true;
            ctx.preferredTarget = holder; ctx.targets = {holder}; out << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.sourceRef.isValid() && room->getTag("HQianzhengApplied").toList().contains(ctx.extra_data) && !ctx.extra_data.toMap().value("consumed").toBool(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->askForSkillInvoke("heg_qianzheng", *ctx.original_data)) return true;
        consume(room, ctx); return false;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { consume(room, ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        DummyCard cards;
        for (const QVariant &entry : ctx.extra_data.toMap().value("cards").toList()) {
            if (room->getCardPlace(entry.toInt()) != Player::DiscardPile) return false;
            cards.addSubcard(entry.toInt());
        }
        if (cards.subcardsLength() > 0) room->obtainCard(target, &cards, true);
        return false;
    }
};
HMOLPackage::HMOLPackage()
    : Package("heg_mol")
{
    General *duyu = new General(this, "heg_duyu", "qun", 4, true, true, true);
    duyu->addSkill(new HWuku);
    duyu->addSkill(new HMiewu);
    duyu->addSkill(new HMiewuDraw);
    related_skills.insert("heg_miewu", "#heg_miewu-draw");

    General *lifeng = new General(this, "heg_lifeng", "shu", 3);
    lifeng->addSkill(new HTunchu);
    lifeng->addSkill(new HTunchuEffect);
    related_skills.insert("heg_tunchu", "#heg_tunchu-effect");
    lifeng->addSkill(new HShuliang);

    General *lingcao = new General(this, "heg_lingcao", "wu");
    lingcao->addSkill(new HDujin);
    lingcao->addSkill(new HDujinCompulsory);
    related_skills.insert("heg_dujin", "#heg_dujin-compulsory");

    General *wangji = new General(this, "heg_wangji", "wei", 3);
    wangji->addSkill(new HQizhi);
    wangji->addSkill(new HJinqu);

    General *yanyan = new General(this, "heg_yanyan", "shu");
    yanyan->addSkill(new HJuzhan);

    General *zhuran = new General(this, "heg_zhuran", "wu");
    zhuran->addSkill(new HDanshou);
    zhuran->addSkill(new HDanshouEffect);
    related_skills.insert("heg_danshou", "#heg_danshou-effect");

    General *xugong = new General(this, "heg_xugong", "wu", 3);
    xugong->setSubordinateKingdom("qun");
    xugong->addCompanion("yanbaihu");
    xugong->addSkill(new HBiaozhao);
    xugong->addSkill(new HYechou);
    xugong->addSkill(new HYechouEffect);
    xugong->addSkill(new HYechouProhibit);
    related_skills.insert("heg_yechou", "#heg_yechou-effect");
    related_skills.insert("heg_yechou", "#heg_yechou-prohibit");

    skills << new HMOLStateRecord("#heg_mol-state") << new HJuzhanProhibit;
    addMetaObject<HBiaozhaoCard>();
}

HOverseasPackage::HOverseasPackage()
    : Package("heg_overseas")
{
    General *caozhen = new General(this, "heg_caozhen", "wei");
    caozhen->addCompanion("heg_caopi");
    caozhen->addSkill(new HSidi);

    General *liaohua = new General(this, "heg_liaohua", "shu");
    liaohua->addSkill(new HDangxian);
    liaohua->addCompanion("heg_guanyu");

    General *zhugejin = new General(this, "heg_zhugejin", "wu", 3);
    zhugejin->addSkill(new HHuanshi);
    zhugejin->addSkill(new HHongyuan);
    zhugejin->addSkill(new HMingzhe);
    zhugejin->addCompanion("heg_sunquan");

    General *beimihu = new General(this, "heg_beimihu", "qun", 3, false);
    beimihu->addSkill(new HGuishu);
    beimihu->addSkill(new HYuanyu);

    General *tianyu = new General(this, "heg_tianyu", "wei");
    tianyu->setDeputyMaxHpAdjustedValue();
    tianyu->addSkill(new HZhenxi);
    tianyu->addSkill(new HZhenxiProhibit);
    tianyu->addSkill(new HZhenxiTargetMod);
    related_skills.insert("heg_zhenxi", "#heg_zhenxi-prohibit");
    related_skills.insert("heg_zhenxi", "#heg_zhenxi-target");
    tianyu->addSkill(new HJiansu);

    General *xianglang = new General(this, "heg_xianglang", "shu", 3);
    xianglang->addCompanion("masu");
    xianglang->addSkill(new HKanji);
    xianglang->addSkill(new HQianzheng);
    xianglang->addSkill(new HQianzhengDelay);
    related_skills.insert("heg_qianzheng", "#heg_qianzheng-delay");

    General *maxiumatie = new General(this, "heg_maxiumatie", "qun");
    maxiumatie->addSkill("mashu");
    maxiumatie->addSkill(new HXiaoqi);

    General *xiahoushang = new General(this, "heg_xiahoushang", "wei");
    xiahoushang->addCompanion("heg_caopi");
    xiahoushang->addSkill(new HTanfeng);

    General *liyan = new General(this, "heg_liyan", "shu");
    liyan->addCompanion("heg_chendao");
    liyan->setHeadMaxHpAdjustedValue();
    liyan->addSkill(new HJinwu);
    liyan->addSkill(new HZhuke);
    liyan->addSkill(new HQuanjia);
    liyan->addSkill(new HQuanjiaCompulsory);
    related_skills.insert("heg_quanjia", "#heg_quanjia-compulsory");

    General *huaxiong = new General(this, "heg_huaxiong", "qun");
    huaxiong->addSkill(new HYaowu);
    huaxiong->addSkill(new HYaowuDeath);
    huaxiong->addSkill(new HShiyong);
    related_skills.insert("heg_yaowu", "#heg_yaowu-death");

    General *liufuren = new General(this, "heg_liufuren", "qun", 3, false);
    liufuren->addCompanion("heg_yuanshao");
    liufuren->addSkill(new HZhuidu);
    liufuren->addSkill(new HShigong);

    General *yangxiu = new General(this, "heg_yangxiu", "wei", 3);
    yangxiu->addSkill(new HDanlao);
    yangxiu->addSkill(new HJilei);

    General *chendao = new General(this, "heg_chendao", "shu");
    chendao->addCompanion("heg_zhaoyun");
    chendao->addSkill(new HWanglie);
    chendao->addSkill(new HWanglieTarget);
    related_skills.insert("heg_wanglie", "#heg_wanglie-target");

    General *zumao = new General(this, "heg_zumao", "wu");
    zumao->addCompanion("heg_sunjian");
    zumao->addSkill(new HYinbingX);
    zumao->addSkill(new HYinbingXCompulsory);
    related_skills.insert("heg_yinbingx", "#heg_yinbingx-compulsory");
    zumao->addSkill(new HJuedi);

    General *fuwan = new General(this, "heg_fuwan", "qun");
    fuwan->addSkill(new HMoukui);
    fuwan->addSkill(new HMoukuiEffect);
    related_skills.insert("heg_moukui", "#heg_moukui-effect");

    addMetaObject<HHongyuanCard>();
    addMetaObject<HJiansuCard>();
    addMetaObject<HKanjiCard>();

    skills << new HZhenxiTrick << new HMOLStateRecord("#heg_overseas-state");
}


ADD_PACKAGE(HMOL)
ADD_PACKAGE(HOverseas)
