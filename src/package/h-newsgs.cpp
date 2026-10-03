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

#include "h-newsgs.h"
#include "skill.h"
#include "h-strategic-advantage.h"
#include "standard.h"
#include "h-standard-tricks.h"
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
#include <QScopeGuard>
#include "skill-declaration.h"
#include "skill-instance-utils.h"
// New HEG donor: TODO/QSanguosha-For-Hegemony-xxyheaven @ cf61c15.
// Namespace mapping: docs/hegemony-xxy-names.json.

namespace {
QVariantMap newsgsReceipt(Room *room, const SkillContext &ctx)
{
    const qint64 serial = room->getTag("HNewsgsReceiptSequence").toLongLong() + 1;
    room->setTag("HNewsgsReceiptSequence", serial);
    return {{"serial", serial}, {"owner", ctx.activationRef.ownerObjectName},
        {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
        {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
        {"source_instance", ctx.sourceRef.key.instanceID}};
}

SkillInstanceRef newsgsRoot(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("source_owner").toString(),
        SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
}

SkillInstanceRef newsgsActivation(const QVariantMap &receipt)
{
    return SkillInstanceRef(receipt.value("owner").toString(),
        SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
}

void newsgsProjectApplied(Room *room)
{
    for (ServerPlayer *player : room->getAllPlayers(true)) {
        for (const QString &mark : {QStringLiteral("##mingfa"), QStringLiteral("##zhuihuan"),
                QStringLiteral("#quanjian-turn"), QStringLiteral("@heg_kuangcai"), QStringLiteral("#guowu")}) {
            int count = 0;
            for (const QVariant &value : room->getTag("HNewsgsApplied").toList()) {
                const QVariantMap receipt = value.toMap(); if (receipt.value("target").toString() != player->objectName()) continue;
                const QString type = receipt.value("type").toString();
                if (mark == "##mingfa" && type == "mingfa") ++count;
                if (mark == "##zhuihuan" && type.startsWith("zhuihuan")) ++count;
                if ((mark == "#quanjian-turn" && type == "quanjian") || (mark == "@heg_kuangcai" && type == "kuangcai")) count += receipt.value("amount").toInt();
                if (mark == "#guowu" && type == "guowu") count = qMax(count, receipt.value("extra_used").toBool() ? qMin(2, receipt.value("types").toInt()) : receipt.value("types").toInt());
            }
            if (player->getMark(mark) != count) room->setPlayerMark(player, mark, count);
        }
    }
}

void newsgsApply(Room *room, const SkillContext &ctx, ServerPlayer *target, const QString &type,
    int amount, const QVariantMap &details = QVariantMap())
{
    QVariantMap receipt = newsgsReceipt(room, ctx);
    for (auto it = details.cbegin(); it != details.cend(); ++it) receipt[it.key()] = it.value();
    receipt["target"] = target->objectName(); receipt["actor"] = ctx.invoker->objectName();
    receipt["type"] = type; receipt["amount"] = amount; receipt["entered"] = false;
    receipt["created_phase"] = room->historyScopes().value("phase_id");
    QVariantList receipts = room->getTag("HNewsgsApplied").toList();
    receipts << receipt; room->setTag("HNewsgsApplied", receipts); newsgsProjectApplied(room);
}

void newsgsConsume(Room *room, const QVariantMap &receipt)
{
    QVariantList kept;
    for (const QVariant &value : room->getTag("HNewsgsApplied").toList())
        if (value.toMap().value("serial") != receipt.value("serial")) kept << value;
    room->setTag("HNewsgsApplied", kept); newsgsProjectApplied(room);
}

void newsgsExpireApplied(TriggerEvent event, Room *room, ServerPlayer *actor, const QVariant &data)
{
    if (event != EventPhaseChanging && event != EventPhaseStart && event != Death && event != TurnBroken) return;
    ServerPlayer *subject = event == Death ? data.value<DeathStruct>().who : actor;
    if (!subject) return;
    const PhaseChangeStruct change = event == EventPhaseChanging ? data.value<PhaseChangeStruct>() : PhaseChangeStruct();
    const QVariant turn = room->historyScopes().value("turn_id");
    const bool turnEnd = event == TurnBroken || (event == EventPhaseStart && subject->getPhase() == Player::NotActive);
    QVariantList kept;
    for (const QVariant &value : room->getTag("HNewsgsApplied").toList()) {
        QVariantMap receipt = value.toMap(); const QString type = receipt.value("type").toString();
        const bool target = receipt.value("target").toString() == subject->objectName();
        const bool issuer = receipt.value("owner").toString() == subject->objectName();
        const bool beneficiary = receipt.value("actor").toString() == subject->objectName();
        bool remove = false;
        if (type != "kuangcai") {
            // Applied restrictions do not disappear merely because their source dies.
            if (event == Death && (target || (type == "mingfa" && beneficiary)
                || (type.startsWith("zhuihuan") && issuer))) remove = true;
            if (event == EventPhaseChanging) {

                const QVariantMap phase = room->historyEvent(receipt.value("phase").toLongLong());
                if (type == "guowu" && receipt.value("turn") == turn
                    && phase.value("status").toString() == "finished"
                    && phase.value("data").toMap().value("player").toString() == subject->objectName()) remove = true;
                if (type == "mingfa" && target && change.from == Player::NotActive && !receipt.value("entered").toBool()
                    && turn.toLongLong() > 0) { receipt["entered"] = true; receipt["turn"] = turn; }
            }
            if ((type == "quanjian" || type == "guowu") && turnEnd && receipt.value("turn") == turn) remove = true;
            if (type.startsWith("zhuihuan") && issuer && event == EventPhaseStart && subject->getPhase() == Player::RoundStart
                && receipt.value("created_phase") != room->historyScopes().value("phase_id")) remove = true;
        }
        if (!remove) kept << receipt;
    }
    room->setTag("HNewsgsApplied", kept); newsgsProjectApplied(room);
}

SkillContext newsgsContinuation(Room *room, const QString &skill, const QVariantMap &receipt,
    ServerPlayer *invoker, TriggerEvent event, QVariant &data)
{
    SkillContext ctx; ctx.skill_name = skill; ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
    ctx.initiator = ctx.owner; ctx.invoker = invoker; ctx.sourceRef = newsgsRoot(receipt);
    ctx.instanceID = receipt.value("serial").toInt(); ctx.amount = receipt.value("amount").toInt();
    ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
    return ctx;
}

void newsgsProjectZhiwei(Room *room)
{
    for (ServerPlayer *player : room->getAllPlayers(true)) {
        int count = 0;
        for (const QVariant &value : room->getTag("HZhiweiRelations").toList())
            if (value.toMap().value("target").toString() == player->objectName()) ++count;
        if (player->getMark("##zhiwei") != count) room->setPlayerMark(player, "##zhiwei", count);
    }
}

void newsgsCleanZhiwei(Room *room)
{
    QVariantList kept;
    for (const QVariant &value : room->getTag("HZhiweiRelations").toList()) {
        const QVariantMap receipt = value.toMap(); const SkillInstanceRef ref = newsgsActivation(receipt);
        ServerPlayer *owner = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (owner && owner->isAlive() && owner->hasSkillInstance(ref.key.skillName, ref.key.instanceID)
            && !room->isGeneralHiddenForSkill(ref)) kept << value;
    }
    room->setTag("HZhiweiRelations", kept); newsgsProjectZhiwei(room);
}

void newsgsGrant(Room *room, ServerPlayer *target, const QString &skill,
    const SkillContext &ctx, const QString &expiry)
{
    if (!target || target->isDead()) return;
    if (expiry == "permanent") { room->acquireSkillFromEffect(target, skill, ctx); return; }
    QVariantMap receipt = newsgsReceipt(room, ctx);
    receipt["recipient"] = target->objectName(); receipt["granted_skill"] = skill;
    receipt["expiry"] = expiry; receipt["entered"] = false;
    receipt["created_phase"] = room->historyScopes().value("phase_id");
    if (expiry == "phase") {
        const QVariantMap scopes = room->historyScopes();
        if (scopes.value("phase_id").toLongLong() <= 0 || scopes.value("turn_id").toLongLong() <= 0) return;
        receipt["phase"] = scopes.value("phase_id"); receipt["turn"] = scopes.value("turn_id");
    }
    // Publish the committed ID before acquire notifications can end its lifetime.
    room->acquireSkillFromEffect(target, skill, ctx, [&](int id) {
        receipt["grant"] = id;
        QVariantList grants = room->getTag("HNewsgsGrants").toList();
        grants << receipt; room->setTag("HNewsgsGrants", grants);
    });
}

void newsgsExpireGrants(TriggerEvent event, Room *room, ServerPlayer *actor, const QVariant &data)
{
    if (event != EventPhaseChanging && event != EventPhaseStart && event != Death && event != TurnBroken) return;
    ServerPlayer *subject = event == Death ? data.value<DeathStruct>().who : actor;
    if (!subject) return;
    const PhaseChangeStruct change = event == EventPhaseChanging ? data.value<PhaseChangeStruct>() : PhaseChangeStruct();
    const QVariant turn = room->historyScopes().value("turn_id");
    QVariantList kept, expired;
    for (const QVariant &value : room->getTag("HNewsgsGrants").toList()) {
        QVariantMap receipt = value.toMap();
        const bool recipient = receipt.value("recipient").toString() == subject->objectName();
        const bool issuer = receipt.value("owner").toString() == subject->objectName();
        const QString expiry = receipt.value("expiry").toString();
        bool remove = event == Death && (recipient || (issuer && expiry == "issuer_round"));
        if (event == EventPhaseStart && issuer && expiry == "issuer_round" && subject->getPhase() == Player::RoundStart
            && receipt.value("created_phase") != room->historyScopes().value("phase_id")) remove = true;
        if (expiry == "phase" && receipt.value("turn") == turn) {
            const QVariantMap phase = room->historyEvent(receipt.value("phase").toLongLong());
            if (event == TurnBroken || (event == EventPhaseChanging && phase.value("status").toString() == "finished"
                && phase.value("data").toMap().value("player").toString() == subject->objectName())) remove = true;
        }
        if (expiry == "next_turn") {
            if (event == EventPhaseChanging && recipient && change.from == Player::NotActive
                && !receipt.value("entered").toBool() && turn.toLongLong() > 0) {
                receipt["entered"] = true; receipt["turn"] = turn;
            }
            if (receipt.value("entered").toBool() && receipt.value("turn") == turn
                && (event == TurnBroken || (event == EventPhaseChanging && recipient && change.to == Player::NotActive))) remove = true;
        }
        if (remove) expired << receipt; else kept << receipt;
    }
    // Detach callbacks may append another grant; never overwrite their ledger afterwards.
    room->setTag("HNewsgsGrants", kept);
    for (const QVariant &value : expired) {
        const QVariantMap receipt = value.toMap();
        ServerPlayer *target = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
        const QString skill = receipt.value("granted_skill").toString(); const int id = receipt.value("grant").toInt();
        if (target && target->hasSkillInstance(skill, id))
            room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(skill, id));
    }
}
}

class HWanggui : public TriggerSkillV2
{
public:
    HWanggui() : TriggerSkillV2("heg_wanggui") { events << Damage << Damaged << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        // This rule requires the particular granting general to be shown.
        return TriggerSkillV2::isSourceAvailable(room, ctx) && ctx.owner
            && ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID);
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        if (event == Damage && damage.to && damage.to->hasFlag("Global_DFDebut")) return {};
        if (player->hasShownAllGenerals()) return {{player, {objectName()}}};
        for (ServerPlayer *other : room->getAlivePlayers())
            if (!player->isFriendWith(other) && other->hasShownOneGeneral()) return {{player, {objectName()}}};
        return {};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !isUsable(ctx)) return false;
        if (ctx.owner->hasShownAllGenerals()) {
            if (!ctx.owner->askForSkillInvoke(this, "prompt")) return false;
            ctx.choice = "draw";
            for (ServerPlayer *other : room->getAlivePlayers())
                if (ctx.owner->isFriendWith(other)) ctx.targets << other;
            room->sortByActionOrder(ctx.targets);
        } else {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *other : room->getAlivePlayers())
                if (!ctx.owner->isFriendWith(other) && other->hasShownOneGeneral()) candidates << other;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "wanggui-invoke", true, true);
            if (!target) return false;
            ctx.choice = "damage";
            ctx.targets = {target};
        }
        return !ctx.targets.isEmpty();
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Reserve the exact instance before any effect interception or nested callbacks.
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};
class HXibing : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HXibing() : TriggerSkillV2("heg_xibing")
    {
        events << TargetSpecified << EventPhaseStart;

    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player && player->getPhase() ==  Player::NotActive) {
            QList<ServerPlayer *> alls = room->getAllPlayers(true);
            foreach (ServerPlayer *p, alls) {
                const QStringList expired = p->getTag("HXibingDisableReasons").toStringList();
                p->removeTag("HXibingDisableReasons");
                room->setPlayerMark(p, "##xibing", 0);
                for (const QString &reason : expired) room->removePlayerDisableShow(p, reason);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == TargetSpecified && player && player->isAlive() && player->getPhase() == Player::Play) {
            TriggerList skill_list;
            CardUseStruct use = data.value<CardUseStruct>();
            // The current accepted use is already in history at TargetSpecified.
            const QVariantMap history = room->queryCardHistory(player, "turn", QString(), false, true);
            if (!history.value("complete").toBool()) return {};
            int blackUses = 0;
            for (const QVariant &entry : history.value("items").toList()) {
                const QVariantMap card = entry.toMap();
                if (card.value("black").toBool() && (card.value("ndtrick").toBool()
                    || card.value("classes").toStringList().contains("Slash"))) ++blackUses;
            }
            if (use.card && use.card->isBlack() && (use.card->isKindOf("Slash") || use.card->isNDTrick())
                && blackUses == 1 && use.to.size() == 1) {
                QList<ServerPlayer *> skill_owners = room->findPlayersBySkillName(objectName());
                foreach (ServerPlayer *skill_owner, skill_owners) {
                    if (skill_owner == player) continue;
                    skill_list.insert(skill_owner, QStringList(objectName()));
                }
            }
            return skill_list;
        }

        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *huaxin = ctx.owner;
        if (huaxin->askForSkillInvoke(this, QVariant::fromValue(player))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, huaxin->objectName(), player->objectName());
            room->broadcastSkillInvoke(objectName(), huaxin);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.owner) return false;
        ctx.manual_effect = true;
        const qint64 serial = newsgsReceipt(room, ctx).value("serial").toLongLong();
        SkillContext part = ctx;
        part.extra_data = QVariantMap{{"serial", serial}, {"hidden", false}};
        part.choice = "refill";
        skillEffect(event, room, player, part, ctx.invoker);
        if (ctx.owner->hasShownAllGenerals() && ctx.owner->getGeneral2()
            && ctx.invoker->hasShownAllGenerals() && ctx.invoker->getGeneral2()) {
            part = ctx;
            part.choice = "hide-optional";
            part.extra_data = QVariantMap{{"serial", serial}, {"hidden", false}};
            skillEffect(event, room, player, part, ctx.owner);
            if (part.extra_data.toMap().value("hidden").toBool()) {
                part = ctx;
                part.extra_data = QVariantMap{{"serial", serial}, {"hidden", false}};
                part.choice = "hide";
                skillEffect(event, room, player, part, ctx.invoker);
            }
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QString reason = objectName() + ":" + ctx.sourceRef.ownerObjectName + ":"
            + QString::number(ctx.sourceRef.key.instanceID) + ":" + QString::number(ctx.extra_data.toMap().value("serial").toLongLong());
        if (ctx.choice == "refill") {
            // A positive effect fills to the printed endpoint; it does not multiply that endpoint.
            const int n = getEffectiveAmount(ctx) > 0 ? qMax(0, target->getHp() - target->getHandcardNum()) : 0;
            if (n > 0) {
                target->drawCards(n, objectName());
                if (target->isAlive()) {
                    room->setPlayerCardLimitation(target, "use", ".|.|.|hand", true, reason);
                    room->addPlayerMark(target, "##xibing");
                }
            }
        } else if (getEffectiveAmount(ctx) > 0) {
            QVariantMap result = ctx.extra_data.toMap();
            result["hidden"] = doXiBing(ctx.owner, target, ctx.choice == "hide-optional", reason);
            ctx.extra_data = result;
        }
        return false;
    }
private:
    static bool doXiBing(ServerPlayer *huaxin, ServerPlayer *player, bool optional, const QString &reason)
    {
        if (huaxin->isDead() || player->isDead()) return false;
        Room *room = huaxin->getRoom();
        QStringList generals, allchoices;
        allchoices << "head" << "deputy";
        if (!player->getActualGeneral1Name().contains("sujiang") && !player->isLord())
            generals << "head";

        if (player->getGeneral2() != NULL && !player->getGeneral2Name().contains("sujiang"))
            generals << "deputy";

        if (generals.isEmpty()) return false;

        if (optional) {
            generals << "cancel";
            allchoices << "cancel";
        }

        QString choice = room->askForChoice(huaxin, "heg_xibing", generals.join("+"), QVariant(), allchoices.join("+"),
                                            "@xibing-hide::" + player->objectName());

        if (choice == "cancel") return false;

        bool head = (choice == "head");

        if (huaxin->isDead() || player->isDead() || !generals.contains(choice)) return false;
        QStringList reasons = player->getTag("HXibingDisableReasons").toStringList();
        if (!reasons.contains(reason)) reasons << reason;
        player->setTag("HXibingDisableReasons", reasons);
        // Register before hide callbacks; an expired receipt must not be resurrected.
        player->hideGeneral(head);
        if (player->isAlive() && player->getTag("HXibingDisableReasons").toStringList().contains(reason))
            room->setPlayerDisableShow(player, head ? "h":"d", reason);

        return true;
    }

};

class HZhente : public TriggerSkillV2
{
public:
    HZhente() : TriggerSkillV2("heg_zhente") { events << TargetConfirmed << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && use.card->isBlack() && use.to.contains(player)
            && (use.card->getTypeId() == Card::TypeBasic || use.card->isNDTrick())
            && use.from && use.from != player && use.from->isAlive()) return {{player, {objectName()}}};
        return {};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !isUsable(ctx)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return ctx.owner->askForSkillInvoke(this, QVariant::fromValue(use.from));
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Reserve the exact instance before any effect interception or nested callbacks.
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || !use.from->isAlive() || !use.card) return false;
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), use.from->objectName());
        ctx.choice = room->askForChoice(use.from, objectName(), "nullified+cardlimited", *ctx.original_data,
            QString(), "zhente-ask:" + ctx.owner->objectName() + "::" + use.card->objectName());
        ctx.targets = {ctx.choice == "nullified" ? ctx.owner : use.from};
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        LogMessage log;
        log.from = use.from;
        if (ctx.choice == "nullified") {
            log.type = "#ZhenteChoice1";
            log.to << target;
            log.arg = use.card->objectName();
            if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        } else if (ctx.choice == "cardlimited") {
            log.type = "#ZhenteChoice2";
            const QString reason = objectName() + ":" + ctx.activationRef.ownerObjectName
                + ":" + QString::number(ctx.activationRef.key.instanceID);
            room->setPlayerCardLimitation(target, "use", ".|black|.|.", true, reason);
        } else return false;
        room->sendLog(log);
        return false;
    }
};
class HZhiwei : public TriggerSkillV2
{
public:
    HZhiwei() : TriggerSkillV2("heg_zhiwei") { global = true; events << GeneralShowed << GeneralHidden << GeneralRemoved << EventLoseSkill << Death; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool canPreshow() const override { return false; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    { if (event != GeneralShowed) newsgsCleanZhiwei(room); return false; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (!TriggerSkillV2::prepareSource(room, ctx) || !ctx.original_data) return false;
        const SkillInstance *instance = ctx.owner->findSkillInstance(objectName(), ctx.activationRef.key.instanceID);
        const QStringList shown = ctx.original_data->toStringList();
        return instance && ((instance->bindHead == 1 && shown.contains("head")) || (instance->bindHead == 2 && shown.contains("deputy")));
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    { return event == GeneralShowed && player && player->isAlive() && player->cheakSkillLocation(objectName(), data) ? TriggerList{{player,{objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "zhiwei-invoke", true, true);
        if (!target) return false; ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        QVariantList relations;
        for (const QVariant &value : room->getTag("HZhiweiRelations").toList())
            if (newsgsActivation(value.toMap()) != ctx.activationRef) relations << value;
        QVariantMap receipt = newsgsReceipt(room, ctx); receipt["target"] = target->objectName();
        receipt["actor"] = ctx.invoker->objectName(); receipt["amount"] = amount;
        relations << receipt; room->setTag("HZhiweiRelations", relations); newsgsProjectZhiwei(room);
        return false;
    }
};

class HZhiweiEffect : public TriggerSkillV2
{
public:
    HZhiweiEffect() : TriggerSkillV2("#heg_zhiwei-effect") { global = true; frequency = Compulsory; events << Damage << Damaged << Death << EventPhaseEnd; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!actor) return true;
        for (const QVariant &value : room->getTag("HZhiweiRelations").toList()) {
            QVariantMap receipt = value.toMap(); ServerPlayer *beneficiary = room->findPlayerByObjectName(receipt.value("actor").toString());
            ServerPlayer *assist = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (!beneficiary || beneficiary->isDead() || !assist) continue;
            QList<ServerPlayer *> targets; QString choice;
            if ((event == Damage || event == Damaged) && assist == actor) {
                if (event == Damaged && beneficiary->isKongcheng()) continue;
                choice = event == Damage ? "draw" : "discard"; targets << beneficiary;
            } else if (event == EventPhaseEnd && actor == beneficiary && actor->getPhase() == Player::Discard && assist->isAlive()) {
                QVariantList ids;
                const QVariant phase = room->historyScopes().value("phase_id");
                if (phase.toLongLong() <= 0) continue;
                QVariantMap query{{"phase_id", phase}, {"from", actor->objectName()}};
                bool complete = true;
                // The printed transfer occurs once at phase end, using committed discards from that exact phase.
                for (;;) {
                    const QVariantMap page = room->queryHistoryMoves(query);
                    if (page.contains("error") || !page.value("complete").toBool()) { complete = false; break; }
                    if (!query.contains("watermark")) query["watermark"] = page.value("watermark");
                    for (const QVariant &entry : page.value("items").toList()) {
                        const QVariantMap move = entry.toMap().value("data").toMap();
                        const int id = move.value("card_id", -1).toInt();
                        if (id >= 0 && move.value("to_place", -1).toInt() == Player::DiscardPile
                            && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                            && room->getCardPlace(id) == Player::DiscardPile && !ids.contains(id)) ids << id;
                    }
                    if (!page.value("has_more").toBool()) break;
                    query["after"] = page.value("next_after");
                }
                if (!complete) continue;
                if (ids.isEmpty()) continue; receipt["move_ids"] = ids; choice = "obtain"; targets << assist;
            } else if (event == Death && actor == beneficiary && data.value<DeathStruct>().who == assist) {
                choice = "hide"; targets << room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            } else continue;
            SkillContext ctx = newsgsContinuation(room, objectName(), receipt, beneficiary, event, data);
            ctx.choice = choice; ctx.targets = targets;
            if (ctx.owner && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap(); const SkillInstanceRef ref = newsgsActivation(receipt);
        ServerPlayer *owner = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!owner || owner->isDead() || !owner->hasSkillInstance(ref.key.skillName, ref.key.instanceID)
            || owner->isSkillInvalid(ref.key.skillName, ref.key.instanceID) || room->isGeneralHiddenForSkill(ref)) return false;
        for (const QVariant &value : room->getTag("HZhiweiRelations").toList())
            if (value.toMap().value("serial") == receipt.value("serial")) return true;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "draw") target->drawCards(amount, "heg_zhiwei");
        else if (ctx.choice == "discard") {
            for (int n = amount; n > 0 && target->isAlive(); --n) {
                const QList<int> ids = target->forceToDiscard(10086, false); if (ids.isEmpty()) break;
                const int id = ids.at(qsanRandomBounded(ids.size()));
                if (target->handCards().contains(id) && !Sanguosha->getCard(id)->hasFlag("using")
                    && target->canDiscard(target, id)) room->throwCard(id, target, target);
            }
        } else if (ctx.choice == "obtain") {
            QList<int> ids; for (const QVariant &value : ctx.extra_data.toMap().value("move_ids").toList())
                if (room->getCardPlace(value.toInt()) == Player::DiscardPile) ids << value.toInt();
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards); }
        } else if (target->hasShownAllGenerals()) {
            const SkillInstanceRef activation = newsgsActivation(ctx.extra_data.toMap());
            const SkillInstance *instance = target->findSkillInstance(activation.key.skillName, activation.key.instanceID);
            if (instance && instance->bindHead != 0) target->hideGeneral(instance->bindHead == 1);
        }
        return false;
    }
};

class HQiao : public TriggerSkillV2
{
public:
    HQiao() : TriggerSkillV2("heg_qiao") { events << TargetConfirmed << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.to.contains(player)) return {};
        return use.from && !player->willBeFriendWith(use.from) && !use.from->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from) return false;
        return ctx.owner->askForSkillInvoke(this, QVariant::fromValue(use.from));
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Reserve the exact instance before any effect interception or nested callbacks.
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        SkillContext discard = ctx;
        discard.choice = "other";
        skillEffect(event, room, owner, discard, ctx.original_data->value<CardUseStruct>().from);
        SkillContext self = ctx;
        self.choice = "self";
        skillEffect(event, room, owner, self, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = getEffectiveAmount(ctx); if (count <= 0) return false;
        if (ctx.choice == "self") {
            room->askForDiscard(target, "qiao_discard", count, count, false, true, "@qiao-discard");
        } else {
            for (int i = 0; i < count && target->isAlive() && ctx.owner->isAlive()
                && ctx.owner->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
                if (id < 0 || room->getCardOwner(id) != target
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                    || Sanguosha->getCard(id)->hasFlag("using") || !ctx.owner->canDiscard(target, id)) break;
                CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, ctx.owner->objectName(),
                    target->objectName(), objectName(), QString());
                room->throwCard(Sanguosha->getCard(id), reason, target, ctx.owner);
            }
        }
        return false;
    }
};
class HChengshang : public TriggerSkillV2
{
public:
    HChengshang() : TriggerSkillV2("heg_chengshang") { events << CardFinished << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished) return {};
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.from != player || use.card->getTypeId() == Card::TypeSkill
            || (use.card->isVirtualCard() && use.card->subcardsLength() != 1)) return {};
        const QVariantMap damage = room->queryCardUseDamage();
        if (!damage.value("complete").toBool()
            || !damage.value("items").toList().isEmpty()) return {};
        for (ServerPlayer *to : use.to)
            if (!to->willBeFriendWith(player)) return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return isUsable(ctx) && ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        QList<int> ids;
        for (int id : room->getDrawPile()) {
            const Card *card = Sanguosha->getCard(id);
            if (card->getSuit() == use.card->getSuit() && card->getNumber() == use.card->getNumber()) ids << id;
        }
        if (ids.isEmpty()) return false;
        DummyCard obtained(ids);
        room->obtainCard(target, &obtained, true);
        return false;
    }
};
class HKuangcai : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HKuangcai() : TriggerSkillV2("heg_kuangcai")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Discard) {
            const int x = player->getRoom()->countHistoryCards(player);
            if (x < 0) return {};
            const QVariantMap damage = player->getRoom()->queryActualDamage({
                {"turn_id", player->getRoom()->historyScopes().value("turn_id")},
                {"from", player->objectName()}, {"limit", 1}});
            if (!damage.value("complete").toBool()) return {};
            if (x == 0 || damage.value("items").toList().isEmpty())
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        bool invoke = false;
        if (!room->isGeneralHiddenForSkill(ctx.activationRef)) {
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

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int used = room->countHistoryCards(ctx.owner); if (used < 0) return false;
        const int change = (used == 0 ? 1 : -1) * getEffectiveAmount(ctx);
        if (change) newsgsApply(room, ctx, target, "kuangcai", change);
        return false;
    }
};

class HKuangcaiMaxCards : public MaxCardsSkillV2
{
public:
    HKuangcaiMaxCards() : MaxCardsSkillV2("#heg_kuangcai-maxcards")
    { setHolderSelector(CorrectSkill_System); setBaseAmount(1); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The public total projects independently retained signed applications exactly once.
        return ctx.primary ? CorrectSkillResult::useAmount(ctx.primary->getMark("@heg_kuangcai") * ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class HKuangcaiTarget : public TargetModSkillV2
{
public:
    HKuangcaiTarget() : TargetModSkillV2("#heg_kuangcai-target", "^SkillCard") { setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getPhase() == Player::NotActive || !ctx.card
            || !Sanguosha->matchExpPattern(pattern, ctx.primary, ctx.card)) return CorrectSkillResult::noEffect();
        if (ctx.modType == Residue || ctx.modType == DistanceLimit) return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class HShejian : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HShejian() : TriggerSkillV2("heg_shejian")
    {
        events << TargetConfirmed << EventSkillInvoking;
    }
    LimitScope getLimitScope() const override { return Limit_None; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking
            || !(player && player->isAlive() && player->hasSkill(objectName())) || player->isKongcheng()) return TriggerList();
        QList<ServerPlayer *> allplayers = room->getAlivePlayers();
        foreach (ServerPlayer *p, allplayers) {
            if (p->getHp() <= 0)
                return TriggerList();
        }
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() != Card::TypeSkill) {
            if (use.from && use.from->isAlive() && use.from != player) {
                foreach (ServerPlayer *to, use.to) {
                    if (to != player) return TriggerList();
                }


                return TriggerList{{player, {objectName()}}};
            }

        }
        return TriggerList();
    }

    bool cost(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!isUsable(ctx)) return false;
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(use.from));
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
            return true;
        }

        return false;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true; ctx.extra_data = 0; ctx.choice = "throw";
        skillEffect(event, room, actor, ctx, ctx.invoker);
        ServerPlayer *from = ctx.original_data->value<CardUseStruct>().from;
        if (!from || from->isDead() || ctx.invoker->isDead()) return false;
        QStringList choices{"damage"}; if (ctx.extra_data.toInt() > 0 && ctx.invoker->canDiscard(from, "he")) choices << "discard";
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"), QVariant::fromValue(from),
            "discard+damage", "@shejian-choice::" + from->objectName() + ":" + QString::number(ctx.extra_data.toInt()));
        skillEffect(event, room, actor, ctx, from); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "throw") {
            QList<int> ids; for (int id : target->handCards()) if (!Sanguosha->getCard(id)->hasFlag("using") && target->canDiscard(target, id)) ids << id;
            if (ids.isEmpty()) return false;
            const CardMoveReason reason(CardMoveReason::S_REASON_THROW, target->objectName(), QString(), objectName(), QString());
            const QVariant moved = room->moveCardsSub(CardsMoveStruct(ids, nullptr, Player::DiscardPile, reason), true); QSet<int> actual;
            for (const QVariant &value : moved.toList()) { const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
                if (move.from == target && move.to_place == Player::DiscardPile) for (int id : move.card_ids) if (ids.contains(id)) actual.insert(id); }
            ctx.extra_data = actual.size();
        } else if (ctx.choice == "damage") room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        else for (int n = ctx.extra_data.toInt() * amount; n > 0 && target->isAlive() && ctx.invoker->isAlive() && ctx.invoker->canDiscard(target, "he"); --n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using") && ctx.invoker->canDiscard(target, id)) room->throwCard(id, target, ctx.invoker);
        }
        return false;
    }
};

class HYusui : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HYusui() : TriggerSkillV2("heg_yusui")
    {
        events << TargetConfirmed << EventSkillInvoking;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking || !(player && player->isAlive() && player->hasSkill(objectName()))
                || !player->hasShownOneGeneral() || player->getHp() < 1) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() != Card::TypeSkill && use.card->isBlack()
                && (use.from && use.from->hasShownOneGeneral() && !use.from->isFriendWith(player) && use.from->isAlive()))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!isUsable(ctx)) return false;
        if (!player->askForSkillInvoke(this, QVariant::fromValue(use.from))) return false;
        room->broadcastSkillInvoke(objectName(), player);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
        return true;
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
        addUsage(ctx);
        // Payment belongs to the original activation owner, never a takeover actor.
        room->loseHp(ctx.owner);
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.original_data->value<CardUseStruct>().from}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0 || !ctx.invoker || ctx.invoker->isDead()) return false;
        // The choice follows the HP payment, so its legal branches use the post-payment state.
        QStringList choices;
        if (target->getHp() > ctx.invoker->getHp()) choices << "losehp";
        if (!target->isKongcheng()) choices << "discard";
        if (choices.isEmpty()) return false;
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"), *ctx.original_data,
            "losehp+discard", "@yusui-choice::" + target->objectName());
        if (ctx.choice == "losehp") {
            const int count = qMax(0, target->getHp() - ctx.invoker->getHp()) * amount;
            if (count > 0) room->loseHp(target, count);
        } else { const int count = qMax(0, target->getMaxHp()) * amount; if (count > 0) room->askForDiscard(target, "yusui_discard", count, count); }
        return false;
    }
};

HBoyanCard::HBoyanCard()
{
    setSkillName("heg_boyan");

}

bool HBoyanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}


class HBoyanViewAsSkill : public ViewAsSkillV2
{
public:
    HBoyanViewAsSkill() : ViewAsSkillV2("heg_boyan") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive()) return false;
        HBoyanCard preview; preview.addSubcards(request.selectedCardIds);
        return preview.targetFilter(targets, target, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        const int count = qMax(0, target->getMaxHp() - target->getHandcardNum());
        if (count > 0) target->drawCards(count, objectName());
        if (target->isDead()) return ContinueEffects;
        const QString reason = "heg_boyan:" + QString::number(newsgsReceipt(room, ctx).value("serial").toLongLong());
        room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true, reason);
        room->addPlayerMark(target, "##boyan");
        if (ctx.invoker && ctx.invoker->isAlive() && target->isAlive()
            && room->askForChoice(ctx.invoker, objectName(), "yes+no", QVariant::fromValue(target), QString(),
                "@boyan-zongheng::" + target->objectName()) == "yes")
            newsgsGrant(room, target, "heg_boyanzongheng", ctx, "next_turn");
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HBoyanCard"; }
};

class HBoyan : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HBoyan() : TriggerSkillV2("heg_boyan")
    {
        events << EventPhaseStart << EventPhaseChanging << TurnBroken << Death; global = true;
        view_as_skill = new HBoyanViewAsSkill;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        newsgsExpireGrants(event, room, player, data);
        if (event == EventPhaseStart && player && player->getPhase() == Player::NotActive)
            for (ServerPlayer *target : room->getAllPlayers(true)) room->setPlayerMark(target, "##boyan", 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

HBoyanZonghengCard::HBoyanZonghengCard()
{
    setSkillName("heg_boyanzongheng");

}

bool HBoyanZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}


class HBoyanZongheng : public ViewAsSkillV2
{
public:
    HBoyanZongheng() : ViewAsSkillV2("heg_boyanzongheng") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive()) return false;
        HBoyanZonghengCard preview; preview.addSubcards(request.selectedCardIds);
        return preview.targetFilter(targets, target, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        const QString reason = "heg_boyan:" + QString::number(newsgsReceipt(room, ctx).value("serial").toLongLong());
        room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true, reason);
        room->addPlayerMark(target, "##boyan");
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HBoyanZonghengCard"; }
};

class HJianliang : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HJianliang() : TriggerSkillV2("heg_jianliang")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->getPhase() != Player::Draw)
            return TriggerList();
        QList<ServerPlayer *> players = room->getOtherPlayers(player);
        foreach(ServerPlayer *p, players) {
            if (p->getHandcardNum() < player->getHandcardNum())
                return TriggerList();
        }

        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getAlivePlayers()) if (ctx.invoker->isFriendWith(target)) ctx.targets << target;
        room->sortByActionOrder(ctx.targets); return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};

HWeimengCard::HWeimengCard()
{
    setSkillName("heg_weimeng");

}

bool HWeimengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}


class HWeimengViewAsSkill : public ViewAsSkillV2
{
public:
    HWeimengViewAsSkill() : ViewAsSkillV2("heg_weimeng") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getHp() > 0;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { if (!request.initiator || !target || !target->isAlive()) return false; HWeimengCard preview; return preview.targetFilter(targets, target, request.initiator); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.invoker) return ContinueEffects;
        if (ctx.choice == "receive") {
            QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (!from || from->isDead()) return ContinueEffects;
            const int maximum = qMin(receipt.value("maximum").toInt(), receipt.value("base").toInt() * amount);
            if (maximum <= 0 || !target->canGet(from, "h")) return ContinueEffects;
            const QList<int> selected = room->askForCardsChosen(target, from, "h", objectName(), 1, maximum, false, Card::MethodGet);
            QList<int> ids; for (int id : selected)
                if (room->getCardOwner(id) == from && room->getCardPlace(id) == Player::PlaceHand && target->canGet(from, id)
                    && !Sanguosha->getCard(id)->hasFlag("using") && !ids.contains(id)) ids << id;
            if (ids.isEmpty()) return ContinueEffects;
            const CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), objectName(), QString());
            const QVariant moved = room->moveCardsSub(CardsMoveStruct(ids, target, Player::PlaceHand, reason), false); QSet<int> got;
            for (const QVariant &value : moved.toList()) { const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
                if (move.from == from && move.to == target && move.to_place == Player::PlaceHand) for (int id : move.card_ids) if (ids.contains(id)) got.insert(id); }
            receipt["count"] = got.size(); ctx.extra_data = receipt; return ContinueEffects;
        }
        if (!target->isKongcheng() && ctx.invoker->isAlive()) {
            const int base = qMax(0, ctx.invoker->getHp());
            SkillContext gain = ctx; gain.choice = "receive";
            gain.extra_data = QVariantMap{{"from", target->objectName()}, {"maximum", base * amount}, {"base", base}, {"count", 0}};
            skillEffect(gain, ctx.invoker);
            const int count = qMin(gain.extra_data.toMap().value("count").toInt(), ctx.invoker->getCardCount(true));
            if (count > 0 && ctx.invoker->isAlive() && target->isAlive()) {
                const QList<int> chosen = room->askForExchangeCards(ctx.invoker, "weimeng_giveback", count, count,
                    QString("@weimeng-give::%1:%2").arg(target->objectName()).arg(count));
                QList<int> valid; for (int id : chosen) if (room->getCardOwner(id) == ctx.invoker
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                    && !Sanguosha->getCard(id)->hasFlag("using") && !valid.contains(id)) valid << id;
                if (!valid.isEmpty()) { DummyCard cards(valid); room->obtainCard(target, &cards,
                    CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), QString()), false); }
            }
        }
        if (ctx.invoker->isAlive() && target->isAlive() && room->askForChoice(ctx.invoker, objectName(), "yes+no",
            QVariant::fromValue(target), QString(), "@weimeng-zongheng::" + target->objectName()) == "yes")
            newsgsGrant(room, target, "heg_weimengzongheng", ctx, "next_turn");
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HWeimengCard"; }
};

class HWeimeng : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HWeimeng() : TriggerSkillV2("heg_weimeng")
    {
        events << EventPhaseStart << EventPhaseChanging << TurnBroken << Death; global = true;
        view_as_skill = new HWeimengViewAsSkill;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        newsgsExpireGrants(event, room, player, data);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

HWeimengZonghengCard::HWeimengZonghengCard()
{
    setSkillName("heg_weimengzongheng");

}

bool HWeimengZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}


class HWeimengZongheng : public ViewAsSkillV2
{
public:
    HWeimengZongheng() : ViewAsSkillV2("heg_weimengzongheng") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { if (!request.initiator || !target || !target->isAlive()) return false; HWeimengZonghengCard preview; return preview.targetFilter(targets, target, request.initiator); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.invoker) return ContinueEffects;
        if (ctx.choice == "receive") {
            QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (!from || from->isDead()) return ContinueEffects;
            const int maximum = qMin(receipt.value("maximum").toInt(), receipt.value("base").toInt() * amount);
            if (maximum <= 0 || !target->canGet(from, "h")) return ContinueEffects;
            const QList<int> selected = room->askForCardsChosen(target, from, "h", objectName(), 1, maximum, false, Card::MethodGet);
            QList<int> ids; for (int id : selected)
                if (room->getCardOwner(id) == from && room->getCardPlace(id) == Player::PlaceHand && target->canGet(from, id)
                    && !Sanguosha->getCard(id)->hasFlag("using") && !ids.contains(id)) ids << id;
            if (ids.isEmpty()) return ContinueEffects;
            const CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), objectName(), QString());
            const QVariant moved = room->moveCardsSub(CardsMoveStruct(ids, target, Player::PlaceHand, reason), false); QSet<int> got;
            for (const QVariant &value : moved.toList()) { const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
                if (move.from == from && move.to == target && move.to_place == Player::PlaceHand) for (int id : move.card_ids) if (ids.contains(id)) got.insert(id); }
            receipt["count"] = got.size(); ctx.extra_data = receipt; return ContinueEffects;
        }
        if (!target->isKongcheng() && ctx.invoker->isAlive()) {
            const int base = 1;
            SkillContext gain = ctx; gain.choice = "receive";
            gain.extra_data = QVariantMap{{"from", target->objectName()}, {"maximum", base * amount}, {"base", base}, {"count", 0}};
            skillEffect(gain, ctx.invoker);
            const int count = qMin(gain.extra_data.toMap().value("count").toInt(), ctx.invoker->getCardCount(true));
            if (count > 0 && ctx.invoker->isAlive() && target->isAlive()) {
                const QList<int> chosen = room->askForExchangeCards(ctx.invoker, "weimeng_giveback", count, count,
                    QString("@weimeng-give::%1:%2").arg(target->objectName()).arg(count));
                QList<int> valid; for (int id : chosen) if (room->getCardOwner(id) == ctx.invoker
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                    && !Sanguosha->getCard(id)->hasFlag("using") && !valid.contains(id)) valid << id;
                if (!valid.isEmpty()) { DummyCard cards(valid); room->obtainCard(target, &cards,
                    CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), QString()), false); }
            }
        }
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HWeimengZonghengCard"; }
};

class HWeicheng : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HWeicheng() : TriggerSkillV2("heg_weicheng")
    {
        events << CardsMoveBatch;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->getHp() <= player->getHandcardNum()) return TriggerList();

        QVariantList move_datas = data.toList();
        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.from_places.contains(Player::PlaceHand)
                    && move.to && move.to != move.from && move.to_place == Player::PlaceHand) {
                return TriggerList{{player, {objectName()}}};
            }
        }

        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

HDaoshuCard::HDaoshuCard()
{
    setSkillName("heg_daoshu");

}

bool HDaoshuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canGet(to_select, "h") && to_select != Self;
}


class HDaoshu : public ViewAsSkillV2
{
public:
    HDaoshu() : ViewAsSkillV2("heg_daoshu") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override
    { return 1 + (ctx.owner ? ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "bonus").toInt() : 0); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { return request.initiator && target && target->isAlive() && targets.isEmpty() && target != request.initiator && request.initiator->canGet(target, "h"); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.invoker) return ContinueEffects;
        if (ctx.choice == "damage") { room->damage(DamageStruct(objectName(), ctx.invoker, target, amount)); return ContinueEffects; }
        if (ctx.choice == "gain") {
            const QVariantMap request = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(request.value("from").toString());
            if (!from || from->isDead() || !target->canGet(from, "h")) return ContinueEffects;
            const int maximum = qMin(amount, request.value("maximum").toInt());
            if (maximum <= 0) return ContinueEffects;
            const QList<int> selected = room->askForCardsChosen(target, from, "h", objectName(), 1, maximum, false, Card::MethodGet);
            QList<int> ids; for (int id : selected)
                if (room->getCardOwner(id) == from && room->getCardPlace(id) == Player::PlaceHand && target->canGet(from, id)
                    && !Sanguosha->getCard(id)->hasFlag("using") && !ids.contains(id)) ids << id;
            QVariantList got;
            if (!ids.isEmpty()) {
                const CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), objectName(), QString());
                const QVariant moved = room->moveCardsSub(CardsMoveStruct(ids, target, Player::PlaceHand, reason), true); QSet<int> seen;
                for (const QVariant &value : moved.toList()) { const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
                    if (move.from == from && move.to == target && move.to_place == Player::PlaceHand)
                        for (int id : move.card_ids) if (ids.contains(id) && !seen.contains(id)) { seen.insert(id); got << id; } }
            }
            ctx.extra_data = got; return ContinueEffects;
        }
        const Card::Suit suit = room->askForSuit(ctx.invoker, objectName());
        SkillContext gain = ctx; gain.choice = "gain";
        gain.extra_data = QVariantMap{{"from", target->objectName()}, {"maximum", amount}};
        skillEffect(gain, ctx.invoker);
        const QVariantList obtained = gain.extra_data.toList(); if (obtained.isEmpty()) return ContinueEffects;
        bool matched = false; QStringList allowed{"spade", "heart", "club", "diamond"};
        for (const QVariant &value : obtained) { const Card *card = Sanguosha->getCard(value.toInt());
            if (card->getSuit() == suit) matched = true; else allowed.removeOne(card->getSuitString()); }
        if (matched) {
            // Increase this activation's phase allowance, never rewrite accepted history.
            if (ctx.owner->hasSkillInstance(objectName(), ctx.activationRef.key.instanceID))
                ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "bonus",
                    ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "bonus").toInt() + 1);
            SkillContext damage = ctx; damage.choice = "damage"; skillEffect(damage, target);
        }
        if (allowed.size() < 4 && ctx.invoker->isAlive() && target->isAlive()) {
            QList<int> candidates; for (const Card *card : ctx.invoker->getHandcards()) if (allowed.contains(card->getSuitString()) && !card->hasFlag("using")) candidates << card->getEffectiveId();
            if (candidates.isEmpty()) room->showAllCards(ctx.invoker);
            else {
                const int count = qMin(int(candidates.size()), amount);
                const QList<int> selected = room->askForExchangeCards(ctx.invoker, objectName(), count, count,
                    "@daoshu-give::" + target->objectName(), QString(), ".|" + allowed.join(",") + "|.|hand");
                QList<int> valid; for (int id : selected) if (candidates.contains(id) && ctx.invoker->handCards().contains(id)
                    && allowed.contains(Sanguosha->getCard(id)->getSuitString()) && !Sanguosha->getCard(id)->hasFlag("using") && !valid.contains(id)) valid << id;
                if (!valid.isEmpty()) { DummyCard cards(valid); room->obtainCard(target, &cards,
                    CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), QString()), true); }
            }
        }
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HDaoshuCard"; }
};


class HDaoshuReset : public TriggerSkillV2
{
public:
    HDaoshuReset() : TriggerSkillV2("#heg_daoshu-reset") { global = true; events << EventPhaseChanging; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player && data.value<PhaseChangeStruct>().from == Player::Play)
            for (int id : player->getSkillInstanceIds("heg_daoshu")) player->removeSkillInstanceStateValue("heg_daoshu", id, "bonus");
        return false;
    }
};

class HZhukou : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhukou() : TriggerSkillV2("heg_zhukou")
    {
        events << Damage;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        DamageStruct damage = data.value<DamageStruct>();
        // Damage callbacks run after the immutable actual_damage fact is committed.
        const QVariantMap history = room->queryActualDamage({
            {"phase_id", room->historyScopes().value("phase_id")},
            {"from", player->objectName()}, {"limit", 2}});
        const QVariantList damages = history.value("items").toList();
        const QString currentDamage = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toString();
        if (history.value("complete").toBool()
            && damages.size() == 1 && damages.first().toMap().value("event_id").toString() == currentDamage
            && !damage.to->hasFlag("Global_DFDebut")) {
            if (room->getCurrent() && room->getCurrent()->getPhase() == Player::Play) {
                if (player->getRoom()->countHistoryCards(player) > 0)
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int used = room->countHistoryCards(ctx.owner);
        if (used > 0) target->drawCards(qMin(used, 5) * getEffectiveAmount(ctx), objectName());
        return false;
    }
};


class HDuannian : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HDuannian() : TriggerSkillV2("heg_duannian")
    {
        events << EventPhaseEnd;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Play && !player->isKongcheng())
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        target->throwAllHandCards();
        if (target->isAlive()) target->drawCards(qMax(0, target->getMaxHp() - target->getHandcardNum()), objectName());
        return false;
    }
};

class HLianyou : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HLianyou() : TriggerSkillV2("heg_lianyou")
    {
        events << Death;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        DeathStruct death = data.value<DeathStruct>();
        return (player && player->hasSkill(objectName()) && death.who == player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *victim;
        if ((victim = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "heg_@lianyou", true, true)) != NULL) {
            room->broadcastSkillInvoke(objectName(), player);

            ctx.targets = {victim};

            return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        room->addPlayerMark(target, "##xinghuo");
        newsgsGrant(room, target, "heg_xinghuo", ctx, "permanent");
        return false;
    }
};

class HXinghuo : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HXinghuo() : TriggerSkillV2("heg_xinghuo")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature == DamageStruct::Fire)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        bool invoke = false;
        if (!room->isGeneralHiddenForSkill(ctx.activationRef)) {
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
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class HGongxiu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HGongxiu() : TriggerSkillV2("heg_gongxiu")
    {
        events << DrawNCards;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.canConvert<DrawStruct>() && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        if (draw.num <= 0) return false;
        --draw.num; *ctx.original_data = QVariant::fromValue(draw); return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> discard;
        for (ServerPlayer *target : room->getAlivePlayers()) if (target->canDiscard(target, "he")) discard << target;
        const int previous = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "previous").toInt();
        QStringList choices; if (previous != 1) choices << "draw";
        if (previous != 2 && !discard.isEmpty()) choices << "discard";
        if (choices.isEmpty()) return false;
        ctx.choice = room->askForChoice(ctx.invoker, "gongxiu_choose", choices.join("+"), QVariant(), "draw+discard", "@gongxiu-choose");
        if (!choices.contains(ctx.choice)) return false;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "previous", ctx.choice == "draw" ? 1 : 2);
        const int count = qMax(0, ctx.invoker->getMaxHp()); if (!count) return false;
        ctx.targets = room->askForPlayersChosen(ctx.invoker, ctx.choice == "draw" ? room->getAlivePlayers() : discard,
            ctx.choice == "draw" ? "gongxiu_draw" : "gongxiu_discard", 1, count,
            (ctx.choice == "draw" ? "@gongxiu-draw:::" : "@gongxiu-discard:::") + QString::number(count));
        room->sortByActionOrder(ctx.targets); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "draw") target->drawCards(amount, objectName());
        else room->askForDiscard(target, "gongxiu_throw", amount, amount, false, true, "@gongxiu-throw");
        return false;
    }
};

HJingheCard::HJingheCard()
{
    setSkillName("heg_jinghe");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool HJingheCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.length() < subcardsLength() && to_select->hasShownOneGeneral();
}

bool HJingheCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == subcardsLength();
}



class HJingheViewAsSkill : public ViewAsSkillV2
{
public:
    HJingheViewAsSkill() : ViewAsSkillV2("heg_jinghe") {}
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId()) || candidate->isEquipped()
            || candidate->hasFlag("using") || !request.initiator->handCards().contains(candidate->getEffectiveId())
            || request.selectedCardIds.size() >= request.initiator->getMaxHp()) return false;
        QList<int> seen;
        for (int id : request.selectedCardIds) {
            if (id < 0 || seen.contains(id)) return false;
            const Card *selected = Sanguosha->getCard(id);
            if (!selected) return false;
            seen << id;
            if (selected->isKindOf("Slash") && candidate->isKindOf("Slash")) return false;
            if (selected->isKindOf("Nullification") && candidate->isKindOf("Nullification")) return false;
            if (selected->objectName() == candidate->objectName()) return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.isEmpty()) return false;
        // Replay each prefix so count and distinct card-name rules also gate submitted requests.
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive()) return false;
        HJingheCard preview; preview.addSubcards(request.selectedCardIds);
        return preview.targetFilter(targets, target, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return !targets.isEmpty() && targets.size() == request.selectedCardIds.size(); }
    bool pay(Room *room, SkillContext &, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *payer = const_cast<ServerPlayer *>(qobject_cast<const ServerPlayer *>(request.initiator));
        if (!payer || !cardSelectionFeasible(request)) return false;
        for (int id : request.selectedCardIds) if (!payer->handCards().contains(id) || Sanguosha->getCard(id)->hasFlag("using")) return false;
        room->showCard(payer, request.selectedCardIds); return true;
    }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true; if (!ctx.invoker || !ctx.use_card || getEffectiveAmount(ctx) <= 0) return FinishSkill;
        QStringList choices{"heg_leiji_tianshu", "heg_yinbing", "heg_huoqi", "heg_guizhu",
            "heg_xianshou", "heg_lundao", "heg_guanyue", "heg_yanzheng"};
        qsanShuffle(choices); choices = choices.mid(0, qMin(int(choices.size()), int(ctx.targets.size()) * getEffectiveAmount(ctx)));
        ctx.extra_data = choices;
        for (ServerPlayer *target : ctx.targets) {
            if (!ctx.invoker->isAlive() || ctx.extra_data.toStringList().isEmpty()) break;
            skillEffect(ctx, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        for (int n = getEffectiveAmount(ctx); n > 0 && target->isAlive(); --n) {
            QStringList choices = ctx.extra_data.toStringList(); if (choices.isEmpty()) break;
            const QString chosen = room->askForChoice(target, "jinghe_skill", (choices + QStringList{"cancel"}).join("+"),
                QVariant(), choices.join("+"), "@jinghe-choose");
            if (!choices.contains(chosen)) break;
            choices.removeOne(chosen); ctx.extra_data = choices;
            newsgsGrant(room, target, chosen, ctx, "issuer_round");
        }
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HJingheCard"; }
};

class HJinghe : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HJinghe() : TriggerSkillV2("heg_jinghe")
    {
        events << EventPhaseStart << EventPhaseChanging << TurnBroken << Death; global = true;
        view_as_skill = new HJingheViewAsSkill;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        newsgsExpireGrants(event, room, player, data);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

class HLeijiTianshu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HLeijiTianshu() : TriggerSkillV2("heg_leiji_tianshu")
    {
        events << CardUsed << CardResponded;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        const Card *card = NULL;
        if (triggerEvent == CardUsed)
            card = data.value<CardUseStruct>().card;
        else if (triggerEvent == CardResponded)
            card = data.value<CardResponseStruct>().m_card;

        if (card != NULL && card->isKindOf("Jink")) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "leiji-invoke", true, true);
        if (target) {
            ctx.targets = {target};
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        } else {
            return false;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        JudgeStruct judge; judge.pattern = ".|spade"; judge.good = false; judge.negative = true;
        judge.reason = objectName(); judge.who = target; room->judge(judge);
        if (judge.isBad() && target->isAlive()) room->damage(DamageStruct(objectName(), ctx.invoker, target, 2 * amount, DamageStruct::Thunder));
        return false;
    }
};

class HYinbing : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HYinbing() : TriggerSkillV2("heg_yinbing")
    {
        events << Predamage << HpLost;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == Predamage && (player && player->isAlive() && player->hasSkill(objectName()))) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("Slash") && !damage.chain && !damage.transfer) {
                TriggerList skill_list;
                skill_list.insert(player, QStringList(objectName()));
                return skill_list;
            }
        } else if (triggerEvent == HpLost) {
            QList<ServerPlayer *> owners = room->findPlayersBySkillName(objectName());
            TriggerList skill_list;
            foreach (ServerPlayer *owner, owners)
                if (player != owner)
                    skill_list.insert(owner, QStringList(objectName()));
            return skill_list;

        }
        return TriggerList();
    }

    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.invoker = ctx.owner; ctx.initiator = ctx.owner; return TriggerSkillV2::prepareSource(room, ctx); }
    bool cost(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        bool invoke = false;
        if (!room->isGeneralHiddenForSkill(ctx.activationRef)) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true; ctx.extra_data = false;
        skillEffect(event, room, player, ctx, event == Predamage ? ctx.original_data->value<DamageStruct>().to : ctx.invoker);
        return event == Predamage && ctx.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (event == Predamage) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            ctx.extra_data = true; room->loseHp(target, damage.damage * amount);
        } else target->drawCards(amount, objectName());
        return false;
    }
};

HHuoqiCard::HHuoqiCard()
{
    setSkillName("heg_huoqi");
}

bool HHuoqiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    QList<const Player *> players = Self->getAliveSiblings();
    int min_hp = Self->getHp();
    foreach (const Player *p, players) {
        if (min_hp > p->getHp())
            min_hp = p->getHp();
    }
    return to_select->getHp() == min_hp && to_select->isWounded();
}


class HHuoqi : public ViewAsSkillV2
{
public:
    HHuoqi() : ViewAsSkillV2("heg_huoqi", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && Sanguosha && request.selectedCardIds.isEmpty()
            && candidate && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
            && (request.initiator->handCards().contains(candidate->getEffectiveId()) || request.initiator->hasEquip(candidate))
            && request.initiator->canDiscard(request.initiator, candidate->getEffectiveId())
            && !request.initiator->isJilei(candidate)
            && Sanguosha->matchExpPattern(".", request.initiator, candidate);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1
            || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive()) return false;
        HHuoqiCard preview; preview.addSubcards(request.selectedCardIds);
        return preview.targetFilter(targets, target, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return ContinueEffects;
        RecoverStruct recover; recover.who = ctx.invoker; recover.card = ctx.use_card; recover.recover = amount;
        target->getRoom()->recover(target, recover);
        if (target->isAlive()) target->drawCards(amount, objectName());
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HHuoqiCard"; }
};

class HGuizhu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HGuizhu() : TriggerSkillV2("heg_guizhu")
    {
        events << Dying << EventSkillInvoking;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventSkillInvoking && player && player->isAlive() && player->hasSkill(objectName()))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (isUsable(ctx) && player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

HXianshouCard::HXianshouCard()
{
    setSkillName("heg_xianshou");
}

bool HXianshouCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}


class HXianshou : public ViewAsSkillV2
{
public:
    HXianshou() : ViewAsSkillV2("heg_xianshou") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive()) return false;
        HXianshouCard preview; preview.addSubcards(request.selectedCardIds);
        return preview.targetFilter(targets, target, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards((target->isWounded() ? 1 : 2) * getEffectiveAmount(ctx), objectName()); return ContinueEffects; }

    QString historyKey(const ActiveSkillRequest &) const override { return "HXianshouCard"; }
};


class HLundao : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HLundao() : TriggerSkillV2("heg_lundao")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            ServerPlayer *from = data.value<DamageStruct>().from;
            if(from && from->isAlive()) {
                int x = player->getHandcardNum(), y = from->getHandcardNum();
                if (x > y || (x < y && !from->isNude())) return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && player->askForSkillInvoke(this, QVariant::fromValue(from))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), from->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *from = ctx.original_data->value<DamageStruct>().from; if (!from || from->isDead()) return false;
        if (ctx.invoker->getHandcardNum() < from->getHandcardNum()) { ctx.choice = "discard"; ctx.targets = {from}; }
        else if (ctx.invoker->getHandcardNum() > from->getHandcardNum()) { ctx.choice = "draw"; ctx.targets = {ctx.invoker}; }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else for (int n = getEffectiveAmount(ctx); n > 0 && target->isAlive() && ctx.invoker->isAlive() && ctx.invoker->canDiscard(target, "he"); --n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using") && ctx.invoker->canDiscard(target, id)) room->throwCard(id, target, ctx.invoker);
        }
        return false;
    }
};

class HGuanyue : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HGuanyue() : TriggerSkillV2("heg_guanyue")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Finish) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        const QList<int> ids = room->getNCards(2 * amount);
        const auto restore = qScopeGuard([&] {
            room->clearAG(target); QList<int> remaining;
            for (int id : ids) if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
            room->returnToTopDrawPile(remaining);
        });
        room->fillAG(ids, target); const int chosen = room->askForAG(target, ids, false, objectName());
        if (ids.contains(chosen) && room->getCardPlace(chosen) == Player::DrawPile) room->obtainCard(target, chosen, false);
        return false;
    }
};

class HYanzheng : public TriggerSkillV2
{
public:
    HYanzheng() : TriggerSkillV2("heg_yanzheng") { events << EventPhaseStart; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start && player->getHandcardNum() > 1
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<int> kept = room->askForExchangeCards(ctx.owner, objectName(), 1, 0,
            "@yanzheng", "", ".|.|.|hand");
        if (kept.size() != 1 || !ctx.owner->handCards().contains(kept.first())) return false;
        QVariantList ids;
        for (int id : ctx.owner->handCards())
            if (id != kept.first() && ctx.owner->canDiscard(ctx.owner, id)) ids << id;
        ctx.extra_data = QVariantMap{{"cards", ids}, {"count", ids.size()}};
        return !ids.isEmpty();
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantMap receipt = ctx.extra_data.toMap(); QList<int> ids;
        for (const QVariant &value : receipt.value("cards").toList()) {
            const int id = value.toInt();
            if (!ctx.owner->handCards().contains(id) || !ctx.owner->canDiscard(ctx.owner, id)
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            ids << id;
        }
        if (ids.isEmpty()) return false;
        const CardMoveReason reason(CardMoveReason::S_REASON_THROW, ctx.owner->objectName(), QString(), objectName(), QString());
        const QVariant moved = room->moveCardsSub(CardsMoveStruct(ids, nullptr, Player::DiscardPile, reason), true);
        QSet<int> paid;
        for (const QVariant &value : moved.toList()) {
            const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
            if (move.from != ctx.owner || move.to_place != Player::DiscardPile) continue;
            for (int id : move.card_ids) if (ids.contains(id)) paid.insert(id);
        }
        receipt["count"] = paid.size(); ctx.extra_data = receipt;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = ctx.extra_data.toMap().value("count").toInt();
        if (count > 0) ctx.targets = room->askForPlayersChosen(ctx.invoker, room->getAlivePlayers(),
            "yanzheng_damage", 1, count, "@yanzheng-damage:::" + QString::number(count));
        room->sortByActionOrder(ctx.targets); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        return false;
    }
};

HFenglveCard::HFenglveCard()
{
    setSkillName("heg_fenglve");
}

bool HFenglveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}


class HFenglveViewAsSkill : public ViewAsSkillV2
{
public:
    HFenglveViewAsSkill() : ViewAsSkillV2("heg_fenglve") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { if (!request.initiator || !target || !target->isAlive()) return false; HFenglveCard preview; return preview.targetFilter(targets, target, request.initiator); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.invoker) return ContinueEffects;
        if (ctx.choice == "receive") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (!from || from->isDead()) return ContinueEffects;
            const int count = qMin(from->getCards(receipt.value("zone").toString()).size(), qsizetype(receipt.value("base").toInt() * amount));
            if (count <= 0) return ContinueEffects;
            const QList<int> selected = room->askForCardsChosen(from, from, receipt.value("zone").toString(), objectName(), count, count);
            QList<int> ids; for (int id : selected) if (room->getCardOwner(id) == from && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip || (receipt.value("zone").toString() == "hej" && room->getCardPlace(id) == Player::PlaceDelayedTrick))) ids << id;
            if (!ids.isEmpty() && target->isAlive()) { DummyCard cards(ids); room->obtainCard(target, &cards,
                CardMoveReason(CardMoveReason::S_REASON_GIVE, from->objectName(), target->objectName(), objectName(), QString()), false); }
            return ContinueEffects;
        }
        if (ctx.invoker->canPindian(target)) {
            std::unique_ptr<PindianStruct> result(ctx.invoker->PinDian(target, objectName(), nullptr));
            if (!result || ctx.invoker->isDead() || target->isDead()) return ContinueEffects;
            SkillContext transfer = ctx; transfer.choice = "receive";
            if (result->from_number > result->to_number) {
                transfer.extra_data = QVariantMap{{"from", target->objectName()}, {"zone", "hej"}, {"base", 2}};
                skillEffect(transfer, ctx.invoker);
            } else if (result->from_number < result->to_number) {
                transfer.extra_data = QVariantMap{{"from", ctx.invoker->objectName()}, {"zone", "he"}, {"base", 1}};
                skillEffect(transfer, target);
            }
        }
        if (ctx.invoker->isAlive() && target->isAlive() && room->askForChoice(ctx.invoker, objectName(), "yes+no",
            QVariant::fromValue(target), QString(), "@fenglve-zongheng::" + target->objectName()) == "yes")
            newsgsGrant(room, target, "heg_fenglvezongheng", ctx, "next_turn");
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HFenglveCard"; }
};

class HFenglve : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HFenglve() : TriggerSkillV2("heg_fenglve")
    {
        events << EventPhaseStart << EventPhaseChanging << TurnBroken << Death; global = true;
        view_as_skill = new HFenglveViewAsSkill;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        newsgsExpireGrants(event, room, player, data);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

HFenglveZonghengCard::HFenglveZonghengCard()
{
    setSkillName("heg_fenglvezongheng");
}

bool HFenglveZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}


class HFenglveZongheng : public ViewAsSkillV2
{
public:
    HFenglveZongheng() : ViewAsSkillV2("heg_fenglvezongheng") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { if (!request.initiator || !target || !target->isAlive()) return false; HFenglveZonghengCard preview; return preview.targetFilter(targets, target, request.initiator); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.invoker) return ContinueEffects;
        if (ctx.choice == "receive") {
            const QVariantMap receipt = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (!from || from->isDead()) return ContinueEffects;
            const int count = qMin(from->getCards(receipt.value("zone").toString()).size(), qsizetype(receipt.value("base").toInt() * amount));
            if (count <= 0) return ContinueEffects;
            const QList<int> selected = room->askForCardsChosen(from, from, receipt.value("zone").toString(), objectName(), count, count);
            QList<int> ids; for (int id : selected) if (room->getCardOwner(id) == from && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip || (receipt.value("zone").toString() == "hej" && room->getCardPlace(id) == Player::PlaceDelayedTrick))) ids << id;
            if (!ids.isEmpty() && target->isAlive()) { DummyCard cards(ids); room->obtainCard(target, &cards,
                CardMoveReason(CardMoveReason::S_REASON_GIVE, from->objectName(), target->objectName(), objectName(), QString()), false); }
            return ContinueEffects;
        }
        if (ctx.invoker->canPindian(target)) {
            std::unique_ptr<PindianStruct> result(ctx.invoker->PinDian(target, objectName(), nullptr));
            if (!result || ctx.invoker->isDead() || target->isDead()) return ContinueEffects;
            SkillContext transfer = ctx; transfer.choice = "receive";
            if (result->from_number > result->to_number) {
                transfer.extra_data = QVariantMap{{"from", target->objectName()}, {"zone", "hej"}, {"base", 1}};
                skillEffect(transfer, ctx.invoker);
            } else if (result->from_number < result->to_number) {
                transfer.extra_data = QVariantMap{{"from", ctx.invoker->objectName()}, {"zone", "he"}, {"base", 2}};
                skillEffect(transfer, target);
            }
        }
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HFenglveZonghengCard"; }
};


class HAnyong : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HAnyong() : TriggerSkillV2("heg_anyong")
    {
        events << DamageCaused << EventSkillInvoking;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to && player != damage.to) {
            QList<ServerPlayer *> owners = room->findPlayersBySkillName(objectName());
            TriggerList skill_list;
            foreach (ServerPlayer *owner, owners)
                if (owner != damage.to && owner->isFriendWith(player))
                    skill_list.insert(owner, QStringList(objectName()));
            return skill_list;
        }

        return TriggerList();
    }

    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.invoker = ctx.owner; ctx.initiator = ctx.owner; return TriggerSkillV2::prepareSource(room, ctx); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().from;
        ServerPlayer *player = ctx.owner;
        if (!isUsable(ctx)) return false;
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(target));
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);

            return true;
        }

        return false;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true; ctx.choice = "damage"; ctx.extra_data = false;
        ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
        skillEffect(event, room, actor, ctx, victim);
        if (!ctx.extra_data.toBool()) return false;
        if (victim->hasShownAllGenerals()) {
            ctx.choice = "losehp"; skillEffect(event, room, actor, ctx, ctx.invoker);
            ctx.choice = "retire"; skillEffect(event, room, actor, ctx, ctx.owner);
        } else if (victim->hasShownOneGeneral()) {
            ctx.choice = "discard"; skillEffect(event, room, actor, ctx, ctx.invoker);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "damage") {
            DamageStruct damage = ctx.original_data->value<DamageStruct>(); damage.damage += damage.damage * amount;
            *ctx.original_data = QVariant::fromValue(damage); ctx.extra_data = true;
        } else if (ctx.choice == "losehp") room->loseHp(target, amount);
        else if (ctx.choice == "discard") room->askForDiscard(target, "anyong_discard", 2 * amount, 2 * amount, false, false, "@anyong-discard");
        else if (target->hasSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID))
            room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID));
        return false;
    }
};

class HGuowu : public TriggerSkillV2
{
public:
    HGuowu() : TriggerSkillV2("heg_guowu") { events << EventPhaseStart; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Play && !player->isKongcheng() ? TriggerList{{player,{objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantMap scopes = room->historyScopes();
        if (scopes.value("phase_id").toLongLong() <= 0 || scopes.value("turn_id").toLongLong() <= 0
            || !ctx.owner->askForSkillInvoke(this)) return false;
        QSet<QString> types;
        for (const Card *card : ctx.owner->getHandcards()) if (!card->hasFlag("using")) types.insert(card->getType());
        ctx.extra_data = QVariantMap{{"phase", scopes.value("phase_id")}, {"turn", scopes.value("turn_id")},
            {"types", types.size()}, {"extra_used", false}};
        ctx.targets = {ctx.owner}; return !types.isEmpty();
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.extra_data.toMap().value("phase") != room->historyScopes().value("phase_id")) return false;
        QSet<QString> types;
        for (const Card *card : ctx.owner->getHandcards()) {
            if (card->hasFlag("using")) return false;
            types.insert(card->getType());
        }
        if (types.isEmpty()) return false;
        QVariantMap receipt = ctx.extra_data.toMap(); receipt["types"] = types.size(); ctx.extra_data = receipt;
        // Disclosure is the payment; freeze its categories before disclosure callbacks mutate the hand.
        room->showAllCards(ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        const QVariantMap receipt = ctx.extra_data.toMap();
        if (receipt.value("types").toInt() <= 0 || receipt.value("phase") != room->historyScopes().value("phase_id")) return false;
        newsgsApply(room, ctx, target, "guowu", amount, receipt);
        for (int n = amount; n > 0 && target->isAlive(); --n) {
            const int id = room->getRandomCardInPile("Slash", false);
            if (id < 0) break;
            target->obtainCard(Sanguosha->getCard(id));
        }
        return false;
    }
};

class HGuowuEffect : public TriggerSkillV2
{
public:
    HGuowuEffect() : TriggerSkillV2("#heg_guowu-effect") { global = true; events << TargetSpecifying << EventSkillInvoking << EventPhaseChanging << EventPhaseStart << TurnBroken << Death; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        newsgsExpireApplied(event, room, actor, data);
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name != objectName()) return false;
            const QVariant serial = accepted.extra_data.toMap().value("serial"); QVariantList receipts = room->getTag("HNewsgsApplied").toList();
            for (QVariant &value : receipts) { QVariantMap receipt = value.toMap();
                if (receipt.value("serial") == serial) { receipt["extra_used"] = true; value = receipt; } }
            room->setTag("HNewsgsApplied", receipts); newsgsProjectApplied(room);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != TargetSpecifying || !actor || actor->isDead()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || room->getUseExtraTargets(use).isEmpty()) return true;
        for (const QVariant &value : room->getTag("HNewsgsApplied").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("type").toString() != "guowu" || receipt.value("target").toString() != actor->objectName()
                || receipt.value("types").toInt() < 3 || receipt.value("extra_used").toBool()
                || receipt.value("phase") != room->historyScopes().value("phase_id")) continue;
            SkillContext ctx = newsgsContinuation(room, objectName(), receipt, actor, event, data); ctx.is_forced = false;
            if (ctx.owner && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        for (const QVariant &value : room->getTag("HNewsgsApplied").toList())
            if (value.toMap().value("serial") == ctx.extra_data.toMap().value("serial")) return true;
        return false;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.invoker, room->getUseExtraTargets(ctx.original_data->value<CardUseStruct>()), objectName(),
            0, 2 * getEffectiveAmount(ctx), "@guowu-add:::" + ctx.original_data->value<CardUseStruct>().card->objectName());
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (room->getUseExtraTargets(use).contains(target)) { use.to << target; room->sortByActionOrder(use.to); *ctx.original_data = QVariant::fromValue(use); }
        return false;
    }
};

class HGuowuTargetMod : public TargetModSkillV2
{
public:
    HGuowuTargetMod() : TargetModSkillV2("#heg_guowu-targetmod", "^SkillCard") { setHolderSelector(CorrectSkill_System); setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (const ServerPlayer *player = qobject_cast<const ServerPlayer *>(ctx.primary)) {
            Room *room = player->getRoom(); bool active = false;
            for (const QVariant &value : room->getTag("HNewsgsApplied").toList()) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("type").toString() == "guowu" && receipt.value("target").toString() == player->objectName()
                    && receipt.value("phase") == room->historyScopes().value("phase_id") && receipt.value("types").toInt() > 1) active = true;
            }
            if (!active) return CorrectSkillResult::noEffect();
        }
        return ctx.modType == DistanceLimit && ctx.primary && ctx.card && ctx.primary->getMark("#guowu") > 1
            && Sanguosha->matchExpPattern(pattern, ctx.primary, ctx.card)
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class HWushuangLvlingqi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent event) const override { return event == TargetSpecified ? 1 : 3; }
    bool usesEventPriority() const override { return true; }

    HWushuangLvlingqi() : TriggerSkillV2("heg_wushuang_lvlingqi")
    {
        events << TargetSpecified << TargetConfirmed << TargetSpecifying;
        // Duel responses share the native exact-use receipt arbitration.
        frequency = Compulsory;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
        QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card) return true;
        QList<ServerPlayer *> targets;
        if (event == TargetSpecified && (use.card->isKindOf("Slash") || use.card->isKindOf("Duel")))
            targets = use.to;
        else if (event == TargetConfirmed && use.card->isKindOf("Duel") && use.from)
            targets << use.from;
        else if (event == TargetSpecifying && use.card->isKindOf("Duel")
            && (!use.card->isVirtualCard() || use.card->getSubcards().isEmpty())
            && !room->getUseExtraTargets(use).isEmpty())
            targets << nullptr;
        // Each ordered target retains the exact skill source and the V2 target hook.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (int i = 0; i < targets.size(); ++i) {
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.original_data = &data;
                ctx.current_event = event;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                if (ServerPlayer *target = targets.at(i)) {
                    ctx.skill_name += "->" + target->objectName() + '&' + QString::number(i + 1);
                    ctx.preferredTarget = target;
                    ctx.preferredTargetSeat = target->getSeat();
                    ctx.targets << target;
                }
                contexts << ctx;
            }
        }
        return true;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.preferredTarget ? ctx.preferredTarget : ctx.owner;
        ServerPlayer *ask_who = ctx.owner;
        QVariant &data = *ctx.original_data;
        bool invoke = false;
        if (!room->isGeneralHiddenForSkill(ctx.activationRef)) {
            room->sendCompulsoryTriggerLog(ask_who, objectName());
            invoke = true;
        } else invoke = ask_who->askForSkillInvoke(this, QVariant::fromValue(target));

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), ask_who);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), target->objectName());
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (ctx.preferredTarget) return false;
        ctx.manual_effect = true;
        if (event != TargetSpecifying) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const QList<ServerPlayer *> candidates = room->getUseExtraTargets(use);
        if (candidates.isEmpty() || getEffectiveAmount(ctx) <= 0) return false;
        const QList<ServerPlayer *> selected = room->askForPlayersChosen(ctx.invoker, candidates, "wushuang_extra", 0,
            2 * getEffectiveAmount(ctx), "@wushuang-add");
        for (ServerPlayer *target : selected) skillEffect(event, room, actor, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from) return false;
        if (event == TargetSpecifying) {
            if (room->getUseExtraTargets(use).contains(target)) {
                use.to << target; room->sortByActionOrder(use.to); *ctx.original_data = QVariant::fromValue(use);
            }
        } else if (use.card->isKindOf("Slash")) {
            QVariantList jinks = use.from->getTag("Jink_" + use.card->toString()).toList();
            const int index = use.to.indexOf(target);
            if (index >= 0 && index < jinks.size() && jinks.at(index).toInt() > 0) jinks[index] = qMax(jinks.at(index).toInt(), 1 + amount);
            use.from->setTag("Jink_" + use.card->toString(), jinks);
        } else if (use.card->isKindOf("Duel")) {
            const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
            const TriggerSkill *native = Sanguosha->getTriggerSkill("wushuang");
            if (useId <= 0 || !native) return false;
            // Share native strongest-response arbitration instead of stacking a second responder.
            room->getThread()->addTriggerSkill(native);
            const int dispatch = target->property("wushuang_v2_receipt_sequence").toInt() + 1;
            room->setPlayerProperty(target, "wushuang_v2_receipt_sequence", dispatch);
            const QString key = "wushuang:" + QString::number(useId);
            QVariantMap receipt = use.card->getTag(key).toMap(); QVariantList sources = receipt.value("sources").toList();
            sources << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"responders", QStringList{target->objectName()}},
                {"amount", amount}, {"dispatch", dispatch}};
            receipt["sources"] = sources; use.card->setTag(key, receipt);
        }
        return false;
    }
};

HZhuangrongCard::HZhuangrongCard()
{
    setSkillName("heg_zhuangrong");
    target_fixed = true;
}


class HZhuangrongViewAsSkill : public ViewAsSkillV2
{
public:
    HZhuangrongViewAsSkill() : ViewAsSkillV2("heg_zhuangrong", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && Sanguosha && request.selectedCardIds.isEmpty()
            && candidate && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
            && (request.initiator->handCards().contains(candidate->getEffectiveId()) || request.initiator->hasEquip(candidate))
            && request.initiator->canDiscard(request.initiator, candidate->getEffectiveId())
            && !request.initiator->isJilei(candidate)
            && Sanguosha->matchExpPattern("TrickCard", request.initiator, candidate);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1
            || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    { ctx.manual_effect = true; if (ctx.invoker) skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) > 0) newsgsGrant(target->getRoom(), target, "heg_wushuang_lvlingqi", ctx, "phase");
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HZhuangrongCard"; }
};

class HZhuangrong : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhuangrong() : TriggerSkillV2("heg_zhuangrong")
    {
        events << EventPhaseStart << EventPhaseChanging << TurnBroken << Death; global = true;
        view_as_skill = new HZhuangrongViewAsSkill;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        newsgsExpireGrants(event, room, player, data);
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

class HShenwei : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HShenwei() : TriggerSkillV2("heg_shenwei")
    {
        events << DrawNCards;
        relate_to_place = "head";
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!data.canConvert<DrawStruct>() || data.value<DrawStruct>().reason != "draw_phase") return TriggerList();
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getHp() > player->getHp())
                return TriggerList();
        }
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        bool invoke = false;
        if (!room->isGeneralHiddenForSkill(ctx.activationRef)) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += 2 * getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class HShenweiMaxCards : public MaxCardsSkillV2
{
public:
    HShenweiMaxCards() : MaxCardsSkillV2("#heg_shenwei-maxcards") { setBaseAmount(2); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    { return CorrectSkillResult::useAmount(ctx.currentAmount); }
};

class HDeshao : public TriggerSkillV2
{
public:
    HDeshao() : TriggerSkillV2("heg_deshao") { events << TargetSpecified << EventSkillInvoking; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &ctx) const override { return ctx.owner ? qMax(0, ctx.owner->getHp()) : 0; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { ctx.invoker = ctx.owner; ctx.initiator = ctx.owner; return TriggerSkillV2::prepareSource(room, ctx); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != TargetSpecified || !actor || actor->isDead()) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() == Card::TypeSkill || !use.card->isBlack() || use.to.isEmpty()) return {};
        ServerPlayer *owner = use.to.first();
        for (ServerPlayer *target : use.to) if (target != owner) return {};
        if (owner == actor || owner->isDead() || !owner->hasSkill(objectName()) || !owner->canDiscard(actor, "he")) return {};
        const int shown = int(owner->hasShownGeneral1()) + int(owner->hasShownGeneral2());
        const int otherShown = int(actor->hasShownGeneral1()) + int(actor->hasShownGeneral2());
        return shown >= otherShown ? TriggerList{{owner, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<CardUseStruct>().from;
        if (!isUsable(ctx) || !target || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets = {target}; return true;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int n = getEffectiveAmount(ctx); n > 0 && ctx.invoker->isAlive() && target->isAlive() && ctx.invoker->canDiscard(target, "he"); --n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using") && ctx.invoker->canDiscard(target, id)) room->throwCard(id, target, ctx.invoker);
        }
        return false;
    }
};

HMingfaCard::HMingfaCard()
{
    setSkillName("heg_mingfa");
}

bool HMingfaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->willBeFriendWith(to_select);
}


class HMingfaViewAsSkill : public ViewAsSkillV2
{
public:
    HMingfaViewAsSkill() : ViewAsSkillV2("heg_mingfa") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { if (!request.initiator || !target || !target->isAlive()) return false; HMingfaCard preview; return preview.targetFilter(targets, target, request.initiator); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) newsgsApply(target->getRoom(), ctx, target, "mingfa", amount);
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HMingfaCard"; }
};

class HMingfa : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HMingfa() : TriggerSkillV2("heg_mingfa")
    {
        events << EventPhaseStart;
        view_as_skill = new HMingfaViewAsSkill;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    { newsgsExpireGrants(event, room, player, data); return false; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

class HMingfaEffect : public TriggerSkillV2
{
public:
    HMingfaEffect() : TriggerSkillV2("#heg_mingfa-effect")
    { global = true; frequency = Compulsory; events << EventPhaseChanging << EventPhaseStart << TurnBroken << Death << EventSkillEffectFinished; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        newsgsExpireApplied(event, room, actor, data);
        if (event == EventSkillEffectFinished) {
            const SkillContext done = data.value<SkillContext>();
            if (done.skill_name == objectName()) newsgsConsume(room, done.extra_data.toMap());
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if ((event != EventPhaseChanging && event != TurnBroken) || !actor || actor->isDead()) return true;
        for (const QVariant &value : room->getTag("HNewsgsApplied").toList()) {
            const QVariantMap receipt = value.toMap(); const QString type = receipt.value("type").toString();
            if (type != "mingfa" || receipt.value("target").toString() != actor->objectName()) continue;
            if (!receipt.value("entered").toBool() || receipt.value("turn") != room->historyScopes().value("turn_id")
                || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)) continue;
            ServerPlayer *invoker = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            if (!invoker || invoker->isDead()) continue;
            SkillContext ctx = newsgsContinuation(room, objectName(), receipt, invoker, event, data);
            ctx.choice = type;
            if (ctx.owner && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return room->getTag("HNewsgsApplied").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        // Consume before hooks: cancellation cannot postpone a one-shot consequence.
        newsgsConsume(room, ctx.extra_data.toMap()); ctx.manual_effect = true;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toMap().value("target").toString());
        if (!target) return false;
        const int difference = target->getHandcardNum() - ctx.invoker->getHandcardNum();
        if (difference > 0) { ctx.choice = "draw"; skillEffect(event, room, actor, ctx, ctx.invoker); }
        else if (difference < 0) { ctx.choice = "damage"; skillEffect(event, room, actor, ctx, target);
            if (target->isAlive() && ctx.invoker->isAlive()) { ctx.choice = "obtain"; skillEffect(event, room, actor, ctx, ctx.invoker); } }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        ServerPlayer *other = room->findPlayerByObjectName(ctx.extra_data.toMap().value("target").toString());
        if (!other) return false;
        if (ctx.choice == "draw") target->drawCards(qMin(qMax(0, other->getHandcardNum() - target->getHandcardNum()), 5) * amount, "heg_mingfa");
        else if (ctx.choice == "damage") room->damage(DamageStruct("heg_mingfa", ctx.invoker, target, amount));
        else for (int n = amount; n > 0 && other->isAlive() && target->isAlive() && target->canGet(other, "h"); --n) {
            const int id = room->askForCardChosen(target, other, "h", "heg_mingfa", false, Card::MethodGet);
            if (room->getCardOwner(id) == other && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using") && target->canGet(other, id)) room->obtainCard(target, id, false);
        }
        return false;
    }
};

HMingfaZonghengCard::HMingfaZonghengCard()
{
    setSkillName("heg_mingfazongheng");
}

bool HMingfaZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->isFriendWith(to_select);
}


class HMingfaZongheng : public ViewAsSkillV2
{
public:
    HMingfaZongheng() : ViewAsSkillV2("heg_mingfazongheng", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && Sanguosha && request.selectedCardIds.isEmpty()
            && candidate && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
            && (request.initiator->handCards().contains(candidate->getEffectiveId()) || request.initiator->hasEquip(candidate))
            && request.initiator->canDiscard(request.initiator, candidate->getEffectiveId())
            && !request.initiator->isJilei(candidate)
            && Sanguosha->matchExpPattern(".", request.initiator, candidate);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1
            || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { if (!request.initiator || !target || !target->isAlive()) return false; HMingfaZonghengCard preview; return preview.targetFilter(targets, target, request.initiator); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) newsgsApply(target->getRoom(), ctx, target, "mingfa", amount);
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HMingfaZonghengCard"; }
};

class HYouyan : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HYouyan() : TriggerSkillV2("heg_youyan")
    {
        events << CardsMoveBatch << EventSkillInvoking;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking
            || !(player && player->isAlive() && player->hasSkill(objectName()))
            || player->getPhase() == Player::NotActive) return TriggerList();
        QVariantList move_datas = data.toList();
        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.to_place == Player::DiscardPile
                    && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                if (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) {
                    return TriggerList{{player, {objectName()}}};
                }
            }
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        QSet<int> ids;
        for (const QVariant &value : ctx.original_data->toList()) {
            const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
            if (move.from != ctx.owner || move.to_place != Player::DiscardPile
                || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) continue;
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip) ids.insert(move.card_ids.at(i));
        }
        const QVariant event = room->historyParent(room->currentHistoryEventId(), "move_cards", true).value("id");
        if (ids.isEmpty() || event.toLongLong() <= 0) return false;
        QVariantMap query{{"event_id", event}, {"from", ctx.owner->objectName()}}; QVariantList suits; QSet<int> seen;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query["watermark"] = page.value("watermark");
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap fact = value.toMap().value("data").toMap(); const int id = fact.value("card_id", -1).toInt();
                const int place = fact.value("from_place", -1).toInt();
                if (!ids.contains(id) || seen.contains(id) || (place != Player::PlaceHand && place != Player::PlaceEquip)) continue;
                const QVariantMap card = fact.value("card_before").toMap(); if (!card.contains("suit")) return false;
                suits << card.value("suit"); seen.insert(id);
            }
            if (!page.value("has_more").toBool()) break;
            query["after"] = page.value("next_after");
        }
        if (seen != ids || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.extra_data = suits; ctx.targets = {ctx.owner}; return true;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        const QList<int> ids = room->getNCards(4 * amount);
        const auto settle = qScopeGuard([&] {
            QList<int> unexposed, table;
            for (int id : ids) {
                if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) unexposed << id;
                else if (room->getCardPlace(id) == Player::PlaceTable) table << id;
            }
            room->returnToTopDrawPile(unexposed);
            if (!table.isEmpty()) { try { DummyCard rest(table); room->throwCard(&rest, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, target->objectName(), objectName(), QString()), nullptr); } catch (...) {} }
        });
        room->moveCardsAtomic(CardsMoveStruct(ids, target, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, target->objectName(), objectName(), QString())), true);
        QList<int> gain;
        for (int id : ids) if (room->getCardPlace(id) == Player::PlaceTable
            && !ctx.extra_data.toList().contains(int(Sanguosha->getCard(id)->getSuit()))) gain << id;
        if (!gain.isEmpty()) { DummyCard cards(gain); room->obtainCard(target, &cards, true); }
        return false;
    }
};

class HZhuihuan : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhuihuan() : TriggerSkillV2("heg_zhuihuan")
    {
        events << EventPhaseChanging << EventPhaseStart << Death;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    { newsgsExpireApplied(event, room, actor, data); return false; }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging && (player && player->isAlive() && player->hasSkill(objectName()))) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QList<ServerPlayer *> choosees = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 2, "@zhuihuan-invoke", true);
        if (choosees.length() > 0) {
            room->sortByActionOrder(choosees);
            ctx.targets = choosees;
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true; if (ctx.targets.isEmpty()) return false;
        const QString choice = room->askForChoice(ctx.invoker, objectName(), "damage+discard", QVariant::fromValue(ctx.targets.first()), QString(),
            "@zhuihuan-choose::" + ctx.targets.first()->objectName());
        ctx.choice = choice == "damage" ? "zhuihuan_damage" : "zhuihuan_discard";
        skillEffect(event, room, actor, ctx, ctx.targets.first());
        if (ctx.targets.size() > 1) {
            ctx.choice = choice == "damage" ? "zhuihuan_discard" : "zhuihuan_damage";
            skillEffect(event, room, actor, ctx, ctx.targets.at(1));
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { const int amount = getEffectiveAmount(ctx); if (amount > 0) newsgsApply(room, ctx, target, ctx.choice, amount); return false; }
};

class HZhuihuanEffect : public TriggerSkillV2
{
public:
    HZhuihuanEffect() : TriggerSkillV2("#heg_zhuihuan-effect")
    { global = true; frequency = Compulsory; events << Damaged << EventPhaseChanging << EventPhaseStart << Death << EventSkillEffectFinished; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        newsgsExpireApplied(event, room, actor, data);
        if (event == EventSkillEffectFinished) {
            const SkillContext done = data.value<SkillContext>();
            if (done.skill_name == objectName()) newsgsConsume(room, done.extra_data.toMap());
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != Damaged || !actor || actor->isDead()) return true;
        for (const QVariant &value : room->getTag("HNewsgsApplied").toList()) {
            const QVariantMap receipt = value.toMap(); const QString type = receipt.value("type").toString();
            if (!type.startsWith("zhuihuan") || receipt.value("target").toString() != actor->objectName()) continue;
            ServerPlayer *invoker = actor;
            SkillContext ctx = newsgsContinuation(room, objectName(), receipt, invoker, event, data);
            ctx.choice = type;
            if (ctx.owner && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return room->getTag("HNewsgsApplied").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        // Consume before hooks: cancellation cannot postpone a one-shot consequence.
        newsgsConsume(room, ctx.extra_data.toMap()); ctx.manual_effect = true;
        ServerPlayer *source = ctx.original_data->value<DamageStruct>().from;
        if (source) skillEffect(event, room, actor, ctx, source);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "zhuihuan_damage") room->damage(DamageStruct("heg_zhuihuan", ctx.invoker, target, amount));
        else room->askForDiscard(target, "zhuihuan_discard", 2 * amount, 2 * amount);
        return false;
    }
};

HJianguoCard::HJianguoCard()
{
    setSkillName("heg_jianguo");
}

bool HJianguoCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}


class HJianguo : public ViewAsSkillV2
{
public:
    HJianguo() : ViewAsSkillV2("heg_jianguo") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        if (!request.initiator || !target || !target->isAlive()) return false;
        HJianguoCard preview; preview.addSubcards(request.selectedCardIds);
        return preview.targetFilter(targets, target, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        QStringList choices{"d1tx"}; if (!target->isNude()) choices << "t1dx";
        const QString choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"), QVariant(),
            "d1tx+t1dx", "#jianguo-choice::" + target->objectName());
        if (choice == "d1tx") {
            target->drawCards(amount, objectName());
            const int discard = qMin(target->getHandcardNum() / 2, 5) * amount;
            if (target->isAlive() && discard > 0) room->askForDiscard(target, objectName(), discard, discard);
        } else if (choice == "t1dx") {
            room->askForDiscard(target, objectName(), amount, amount, false, true);
            if (target->isAlive()) target->drawCards(qMin(target->getHandcardNum() / 2, 5) * amount, objectName());
        }
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HJianguoCard"; }
};

class HQingshi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HQingshi() : TriggerSkillV2("heg_qingshi")
    {
        events << TargetSpecified << CardUsed << CardResponded << GeneralShown << EventPhaseStart << EventSkillInvoking;
    }

    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::NotActive) {
            for (ServerPlayer *p : room->getAllPlayers(true)) {
                room->setPlayerMark(p, "#qingshi-turn", 0);
                room->setPlayerMark(p, "qingshi-turn", 0);
            }
        } else if (player && player->hasShownSkill(objectName()) && player->getPhase() != Player::NotActive) {
            const int used = room->countHistoryCards(player);
            if (used >= 0) room->setPlayerMark(player, "#qingshi-turn", used);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != TargetSpecified || !(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::NotActive) return TriggerList();
        if (player->getRoom()->countHistoryCards(player) != player->getHandcardNum()) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if ((use.card->isKindOf("Slash") || use.card->getTypeId() == Card::TypeTrick)) {
            foreach (ServerPlayer *p, use.to) {
                if (p->isAlive() && p != player)
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        QList<ServerPlayer *> to_choose;
        foreach (ServerPlayer *p, use.to) {
            if (p->isAlive() && p != player)
                to_choose << p;
        }
        if (to_choose.isEmpty()) return false;
        ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "qingshi-invoke", true, true);
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);
            ctx.targets = {to};
            return true;
        }
        return false;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        return false;
    }
};

HQuanjianCard::HQuanjianCard()
{
    setSkillName("heg_quanjian");

}

bool HQuanjianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->isFriendWith(to_select);
}


class HQuanjian : public ViewAsSkillV2
{
public:
    HQuanjian() : ViewAsSkillV2("heg_quanjian") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { if (!request.initiator || !target || !target->isAlive()) return false; HQuanjianCard preview; return preview.targetFilter(targets, target, request.initiator); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        Room *room = target->getRoom(); const QVariant turn = room->historyScopes().value("turn_id");
        if (amount > 0 && turn.toLongLong() > 0 && !ctx.invoker->askCommandto(objectName(), target) && target->isAlive()
            && room->historyScopes().value("turn_id") == turn && room->historyEvent(turn.toLongLong()).value("status").toString() == "active")
            newsgsApply(room, ctx, target, "quanjian", amount, {{"turn", turn}});
        return ContinueEffects;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HQuanjianCard"; }
};

class HQuanjianEffect : public TriggerSkillV2
{
public:
    HQuanjianEffect() : TriggerSkillV2("#heg_quanjian-effect")
    { global = true; frequency = Compulsory; events << DamageInflicted << EventPhaseChanging << EventPhaseStart << TurnBroken << Death << EventSkillEffectFinished; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        newsgsExpireApplied(event, room, actor, data);
        if (event == EventSkillEffectFinished) {
            const SkillContext done = data.value<SkillContext>();
            if (done.skill_name == objectName()) newsgsConsume(room, done.extra_data.toMap());
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageInflicted || !actor || actor->isDead()) return true;
        for (const QVariant &value : room->getTag("HNewsgsApplied").toList()) {
            const QVariantMap receipt = value.toMap(); const QString type = receipt.value("type").toString();
            if (type != "quanjian" || receipt.value("target").toString() != actor->objectName()
                || receipt.value("turn") != room->historyScopes().value("turn_id")) continue;
            // The accepted restriction belongs to its recipient until turn end.
            SkillContext ctx = newsgsContinuation(room, objectName(), receipt, actor, event, data);
            ctx.choice = type;
            if (ctx.owner && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return room->getTag("HNewsgsApplied").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        // Consume before hooks: cancellation cannot postpone a one-shot consequence.
        newsgsConsume(room, ctx.extra_data.toMap()); ctx.manual_effect = true;
        skillEffect(event, room, actor, ctx, ctx.original_data->value<DamageStruct>().to);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>(); damage.damage += amount;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class HTujue : public TriggerSkillV2
{
public:
    HTujue() : TriggerSkillV2("heg_tujue") { events << AskForPeaches << EventSkillInvoking; frequency = Limited; limit_mark = "@impasse"; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == AskForPeaches && player && player->isAlive() && player->hasSkill(objectName())
            && !player->isNude() && player->getHp() < 1 && data.value<DyingStruct>().who == player
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "tujue-invoke", true, true);
        if (!target) return false;
        QVariantList ids; for (const Card *card : ctx.owner->getCards("he")) ids << card->getEffectiveId();
        ctx.extra_data = QVariantMap{{"recipient", target->objectName()}, {"cards", ids}, {"count", qMin(ids.size(), qsizetype(3))}};
        return !ids.isEmpty();
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) {
            addUsage(accepted); room->setPlayerMark(ctx.owner, limit_mark, 0);
        }
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        QVariantMap receipt = ctx.extra_data.toMap();
        ServerPlayer *target = room->findPlayerByObjectName(receipt.value("recipient").toString());
        if (!target || target->isDead()) return false;
        QList<int> ids;
        for (const QVariant &value : receipt.value("cards").toList()) {
            const int id = value.toInt(); const Player::Place place = room->getCardPlace(id);
            if (room->getCardOwner(id) != ctx.owner || (place != Player::PlaceHand && place != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
            ids << id;
        }
        if (ids.isEmpty()) return false;
        addUsage(ctx); room->setPlayerMark(ctx.owner, limit_mark, 0);
        room->doSuperLightbox("heg_huangquan", objectName());
        const CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), QString());
        const QVariant moved = room->moveCardsSub(CardsMoveStruct(ids, target, Player::PlaceHand, reason), false);
        QSet<int> paid;
        for (const QVariant &value : moved.toList()) {
            const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
            if (move.from == ctx.owner && move.to == target && move.to_place == Player::PlaceHand)
                for (int id : move.card_ids) if (ids.contains(id)) paid.insert(id);
        }
        receipt["count"] = qMin(paid.size(), qsizetype(3)); ctx.extra_data = receipt;
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = ctx.extra_data.toMap().value("count").toInt() * getEffectiveAmount(ctx);
        if (amount > 0) {
            RecoverStruct recover; recover.recover = amount; recover.who = ctx.invoker;
            room->recover(target, recover);
            if (target->isAlive()) target->drawCards(amount, objectName());
        }
        return false;
    }
};

class HZhiren : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhiren() : TriggerSkillV2("heg_zhiren")
    {
        events << CardUsed;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && player->getPhase() != Player::NotActive) {
            const Card *card = data.value<CardUseStruct>().card;
            if (card->getTypeId() == Card::TypeSkill || !card->isRed()) return TriggerList();
            if (card->isVirtualCard() && !card->getSubcards().isEmpty()) return TriggerList();

            const QVariantMap history = player->getRoom()->queryCardHistory(player);
            if (!history.value("complete").toBool()) return {};
            const QVariantList card_list = history.value("items").toList();

            int n = 0;
            foreach (QVariant card_data, card_list) {
                const QVariantMap card = card_data.toMap();
                if (card.value("red").toBool()
                    && (!card.value("virtual").toBool() || card.value("subcards").toList().isEmpty())) {
                    n++;
                }
                if (n > 1) break;
            }
            if (n == 1)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        QStringList choices;
        choices << "busuan" << "cancel";
        QList<ServerPlayer *> to_choose;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p != player && p->isFemale() && p->hasEquip())
                to_choose << p;
        }
        if (!to_choose.isEmpty()) choices << "discard";

        const Card *card = data.value<CardUseStruct>().card;

        int x = 0;
        if (card->isKindOf("Slash"))
            x = 1;
        else if (card->isKindOf("Nullification"))
            x = 4;
        else
            x = GetHanNumFromString(Sanguosha->translate(card->objectName()));

        QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant(), "busuan+discard+cancel",
                                            "@zhiren-choice:::" +QString::number(x));
        if (choice == "busuan") {
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName(), player);
            ctx.choice = "busuan";
            return true;
        } else if (choice == "discard") {
            ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "@zhiren-target", true, true);
            if (to != NULL) {
                room->broadcastSkillInvoke(objectName(), player);
                ctx.choice = "discard";
                ctx.targets = {to};
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.choice == "busuan") ctx.targets = {ctx.invoker}; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "busuan") {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            const int length = card->isKindOf("Slash") ? 1 : card->isKindOf("Nullification") ? 4 : GetHanNumFromString(Sanguosha->translate(card->objectName()));
            const QList<int> ids = room->getNCards(length * amount);
            const auto restore = qScopeGuard([&] { QList<int> remaining;
                for (int id : ids) if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
                room->returnToTopDrawPile(remaining); });
            room->askForGuanxing(target, ids, Room::GuanxingBothSides);
        } else for (int n = amount; n > 0 && target->isAlive() && ctx.invoker->isAlive() && ctx.invoker->canDiscard(target, "e"); --n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "e", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceEquip && !Sanguosha->getCard(id)->hasFlag("using") && ctx.invoker->canDiscard(target, id)) room->throwCard(id, target, ctx.invoker);
        }
        return false;
    }

private:
    static int GetHanNumFromString(QString str)     // Count Chinese characters.
    {
       int count = 0;
       for(int i = 0; i < str.length(); i++)
       {
           if(str[i].unicode() >= 0x4E00 && str[i].unicode() <= 0x9FA5)
               count++;
       }
       return count;
    }

};

class HYaner : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HYaner() : TriggerSkillV2("heg_yaner")
    {
        events << CardsMoveBatch << EventSkillInvoking;
        frequency = Frequent;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking || !(player && player->isAlive() && player->hasSkill(objectName())))
            return TriggerList();
        QVariantList move_datas = data.toList();
        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from->isAlive() && move.from->getPhase() == Player::Play && move.from != player
                    && move.from->isFriendWith(player) && move.from_places.contains(Player::PlaceHand) && move.from->isKongcheng())
                return TriggerList{{player, {objectName()}}};

        }

        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *other = nullptr;
        for (const QVariant &value : ctx.original_data->toList()) {
            const CardsMoveOneTimeStruct move = value.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from != ctx.owner && move.from->isAlive() && move.from->getPhase() == Player::Play
                && move.from->isFriendWith(ctx.owner) && move.from_places.contains(Player::PlaceHand) && move.from->isKongcheng()) { other = qobject_cast<ServerPlayer *>(move.from); break; }
        }
        if (!other || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(other))) return false;
        ctx.targets = {other, ctx.owner}; room->sortByActionOrder(ctx.targets); return true;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
};




HManoeuvrePackage::HManoeuvrePackage()
    : Package("heg_manoeuvre")
{
    General *huaxin = new General(this, "heg_huaxin", "wei", 3);
    huaxin->addSkill(new HWanggui);
    huaxin->addSkill(new HXibing);

    General *yanghu = new General(this, "heg_yanghu", "wei", 3);
    yanghu->addSkill(new HDeshao);
    yanghu->addSkill(new HMingfa);
    yanghu->addSkill(new HMingfaEffect);
    insertRelatedSkills("heg_mingfa", "#heg_mingfa-effect");

    General *zongyux = new General(this, "heg_zongyux", "shu", 3);
    zongyux->addSkill(new HQiao);
    zongyux->addSkill(new HChengshang);

    General *dengzhi = new General(this, "heg_dengzhi", "shu", 3);
    dengzhi->addSkill(new HJianliang);
    dengzhi->addSkill(new HWeimeng);

    General *luyusheng = new General(this, "heg_luyusheng", "wu", 3, false);
    luyusheng->addSkill(new HZhente);
    luyusheng->addSkill(new HZhiwei);
    luyusheng->addSkill(new HZhiweiEffect);
    insertRelatedSkills("heg_zhiwei", "#heg_zhiwei-effect");

    General *fengxi = new General(this, "heg_fengxi", "wu", 3);
    fengxi->addSkill(new HYusui);
    fengxi->addSkill(new HBoyan);

    General *miheng = new General(this, "heg_miheng", "qun", 3);
    miheng->addSkill(new HKuangcai);
    miheng->addSkill(new HKuangcaiMaxCards);
    miheng->addSkill(new HKuangcaiTarget);
    miheng->addSkill(new HShejian);
    related_skills.insert("heg_kuangcai", "#heg_kuangcai-maxcards");
    related_skills.insert("heg_kuangcai", "#heg_kuangcai-target");

    General *xunchen = new General(this, "heg_xunchen", "qun", 3);
    xunchen->addSkill(new HFenglve);
    xunchen->addSkill(new HAnyong);

    addMetaObject<HBoyanCard>();
    addMetaObject<HBoyanZonghengCard>();
    addMetaObject<HWeimengCard>();
    addMetaObject<HWeimengZonghengCard>();
    addMetaObject<HFenglveCard>();
    addMetaObject<HFenglveZonghengCard>();
    addMetaObject<HMingfaCard>();
    addMetaObject<HMingfaZonghengCard>();

    skills << new HBoyanZongheng << new HWeimengZongheng << new HFenglveZongheng << new HMingfaZongheng;
}

HNewSGSPackage::HNewSGSPackage()
    : Package("heg_newsgs")
{
    General *jianggan = new General(this, "heg_jianggan", "wei", 3);
    jianggan->addSkill(new HWeicheng);
    jianggan->addSkill(new HDaoshu);
    jianggan->addSkill(new HDaoshuReset);
    insertRelatedSkills("heg_daoshu", "#heg_daoshu-reset");

    General *yangwan = new General(this, "heg_yangwan", "shu", 3, false);
    yangwan->addCompanion("heg_machao");
    yangwan->addSkill(new HYouyan);
    yangwan->addSkill(new HZhuihuan);
    yangwan->addSkill(new HZhuihuanEffect);
    insertRelatedSkills("heg_zhuihuan", "#heg_zhuihuan-effect");

    General *zhouyi = new General(this, "heg_zhouyi", "wu", 3, false);
    zhouyi->addSkill(new HZhukou);
    zhouyi->addSkill(new HDuannian);
    zhouyi->addSkill(new HLianyou);
    zhouyi->addRelateSkill("heg_xinghuo");

    General *lvlingqi = new General(this, "heg_lvlingqi", "qun", 4, false);
    lvlingqi->setHeadMaxHpAdjustedValue();
    lvlingqi->addSkill(new HGuowu);
    lvlingqi->addSkill(new HGuowuEffect);
    lvlingqi->addSkill(new HGuowuTargetMod);
    related_skills.insert("heg_guowu", "#heg_guowu-effect");
    related_skills.insert("heg_guowu", "#heg_guowu-targetmod");
    lvlingqi->addSkill(new HZhuangrong);
    lvlingqi->addRelateSkill("heg_wushuang_lvlingqi");
    lvlingqi->addSkill(new HShenwei);
    lvlingqi->addSkill(new HShenweiMaxCards);
    insertRelatedSkills("heg_shenwei", "#heg_shenwei-maxcards");

    General *nanhualaoxian = new General(this, "heg_nanhualaoxian", "qun");
    nanhualaoxian->addSkill(new HGongxiu);
    nanhualaoxian->addSkill(new HJinghe);
    nanhualaoxian->addRelateSkill("heg_leiji_tianshu");
    nanhualaoxian->addRelateSkill("heg_yinbing");
    nanhualaoxian->addRelateSkill("heg_huoqi");
    nanhualaoxian->addRelateSkill("heg_guizhu");
    nanhualaoxian->addRelateSkill("heg_xianshou");
    nanhualaoxian->addRelateSkill("heg_lundao");
    nanhualaoxian->addRelateSkill("heg_guanyue");
    nanhualaoxian->addRelateSkill("heg_yanzheng");

    General *duyu = new General(this, "heg_ty_duyu", "wei", 3);
    duyu->addCompanion("heg_yanghu");
    duyu->addSkill(new HJianguo);
    duyu->addSkill(new HQingshi);

    General *huangquan = new General(this, "heg_huangquan", "shu", 3);
    huangquan->addSkill(new HQuanjian);
    huangquan->addSkill(new HQuanjianEffect);
    huangquan->addSkill(new HTujue);
    related_skills.insert("heg_quanjian", "#heg_quanjian-effect");

    General *panjinshu = new General(this, "heg_panjinshu", "wu", 3, false);
    panjinshu->addCompanion("heg_sunquan");
    panjinshu->addSkill(new HZhiren);
    panjinshu->addSkill(new HYaner);

    addMetaObject<HDaoshuCard>();
    addMetaObject<HJingheCard>();
    addMetaObject<HHuoqiCard>();
    addMetaObject<HXianshouCard>();
    addMetaObject<HZhuangrongCard>();
    addMetaObject<HJianguoCard>();
    addMetaObject<HQuanjianCard>();



    skills << new HXinghuo << new HLeijiTianshu << new HYinbing << new HHuoqi << new HGuizhu << new HXianshou << new HLundao
           << new HGuanyue << new HYanzheng << new HWushuangLvlingqi;
}

ADD_PACKAGE(HManoeuvre)
ADD_PACKAGE(HNewSGS)
