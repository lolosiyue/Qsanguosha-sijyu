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

// Original HEG content: see docs/hegemony-original-names.json for the import namespace.
#include "h-standard-shu-generals.h"
#include "standard.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "skill-instance-utils.h"
#include "util.h"

namespace {
bool hasShouyue(const Player *player)
{
    const Player *lord = player ? player->getLord() : nullptr;
    return lord && lord->hasLordSkill("heg_shouyue") && lord->hasShownGeneral1();
}

bool ownsTrigger(const ServerPlayer *player, const QString &skill)
{
    return player && player->isAlive() && player->hasSkill(skill);
}

bool invokeShuSkill(const TriggerSkill *skill, Room *room, ServerPlayer *owner,
                    const QVariant &data = QVariant())
{
    if (!owner->askForSkillInvoke(skill, data)) return false;
    room->broadcastSkillInvoke(skill->objectName(), owner);
    return true;
}

void preventJink(Room *room, ServerPlayer *owner, ServerPlayer *target, const CardUseStruct &use)
{
    const int index = use.to.indexOf(target);
    const QString key = "Jink_" + use.card->toString();
    QVariantList jinks = owner->getTag(key).toList();
    if (index < 0 || index >= jinks.size()) return;
    // Re-read after each target's judgement: nested effects may update this use.
    jinks[index] = 0;
    owner->setTag(key, jinks);
    LogMessage log;
    log.type = "#NoJink";
    log.from = target;
    room->sendLog(log);
}
}

class HRendeViewAsSkill : public ViewAsSkillV2 {
public:
    HRendeViewAsSkill() : ViewAsSkillV2("heg_rende") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.initiator && !card->isEquipped()
            && request.initiator->handCards().contains(card->getEffectiveId())
            && !request.selectedCardIds.contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty();
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return selected.isEmpty() && target && target->isAlive() && target != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HRendeCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.invoker || ctx.targets.size() != 1 || request.selectedCardIds.isEmpty()) return false;
        ServerPlayer *target = ctx.targets.first();
        if (!target || !target->isAlive() || target == ctx.invoker) return false;
        for (int id : request.selectedCardIds)
            if (!ctx.invoker->handCards().contains(id)) return false;
        // Giving is the cost; do not let the generic proxy discard these cards.
        DummyCard gift(request.selectedCardIds);
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(),
                              target->objectName(), objectName(), QString());
        room->obtainCard(target, &gift, reason, false);
        ctx.extra_data = request.selectedCardIds.size();
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner) return FinishSkill;
        const int oldCount = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "given", 0).toInt();
        const int newCount = oldCount + ctx.extra_data.toInt();
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "given", newCount);
        // Keep the legacy AI's aggregate hint; authority is the selected instance.
        owner->getRoom()->addPlayerMark(owner, objectName(), ctx.extra_data.toInt());
        if (oldCount < 3 && newCount >= 3 && owner->isWounded())
            owner->getRoom()->recover(owner, RecoverStruct(owner, ctx.use_card, getEffectiveAmount(ctx), objectName()));
        return ContinueEffects;
    }
};

class HRende : public TriggerSkillV2 {
public:
    HRende() : TriggerSkillV2("heg_rende")
    {
        events << EventPhaseChanging;
        view_as_skill = new HRendeViewAsSkill;
    }
    void record(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !ctx.original_data
            || ctx.original_data->value<PhaseChangeStruct>().to != Player::NotActive) return;
        for (int id : player->getSkillInstanceIds(objectName()))
            player->removeSkillInstanceStateValue(objectName(), id, "given");
        room->setPlayerMark(player, objectName(), 0);
    }
};

class HWusheng : public ViewAsSkillV2 {
public:
    HWusheng() : ViewAsSkillV2("heg_wusheng", 1) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? Slash::IsAvailable(request.initiator) : request.pattern == "slash";
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !request.initiator
            || (!card->isRed() && !hasShouyue(request.initiator))) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcard(card);
        return slash.isAvailable(request.initiator);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        Slash *slash = new Slash(original->getSuit(), original->getNumber());
        slash->addSubcard(original);
        slash->setSkillName(objectName());
        slash->setShowSkill(objectName());
        return slash;
    }
};

class HPaoxiao : public TargetModSkillV2 {
public:
    HPaoxiao() : TargetModSkillV2("heg_paoxiao") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::Residue
            ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
    bool requiresShowForUse(const CardUseStruct &use) const override
    {
        return use.from && use.card && use.card->isKindOf("Slash")
            && use.from->getPhase() == Player::Play && use.from->getSlashCount() > 1
            && use.from->hasSkill(objectName()) && !use.from->hasShownSkill(objectName());
    }
};

class HPaoxiaoArmorNullification : public TriggerSkillV2 {
public:
    HPaoxiaoArmorNullification() : TriggerSkillV2("#heg_paoxiao-null")
    {
        events << TargetSpecified;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!ownsTrigger(player, "heg_paoxiao") || !hasShouyue(player)
            || !use.card || !use.card->isKindOf("Slash")) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.original_data || !hasShouyue(ctx.owner)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card && use.to.contains(target)) {
            room->notifySkillInvoked(room->getLord(ctx.owner->getKingdom()), "heg_shouyue");
            target->addQinggangTag(use.card);
        }
        return false;
    }
};

HGuanxing::HGuanxing(const QString &name) : TriggerSkillV2(name)
{
    events << EventPhaseStart;
    frequency = name == "heg_yizhi" ? Compulsory : Frequent;
    if (name == "heg_yizhi") relate_to_place = "deputy";
}

bool HGuanxing::canPreshow() const { return false; }

void HGuanxing::record(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const
{
    if (player && player->getPhase() == Player::Start)
        player->removeTag("heg_guanxing_consumed");
}

TriggerList HGuanxing::triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const
{
    if (!ownsTrigger(player, objectName()) || player->getPhase() != Player::Start) return {};
    const QStringList consumed = player->getTag("heg_guanxing_consumed").toStringList();
    QStringList candidates;
    for (int id : player->getValidSkillInstanceIds(objectName())) {
        const QString key = SkillInstanceUtils::formatName(objectName(), id);
        if (!consumed.contains(key)) candidates << key;
    }
    return candidates.isEmpty() ? TriggerList() : TriggerList{{player, candidates}};
}

bool HGuanxing::cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    if (!ctx.owner || !invokeShuSkill(this, room, ctx.owner)) return false;
    const QString other = objectName() == "heg_yizhi" ? "heg_guanxing" : "heg_yizhi";
    // The trigger menu chooses the actual source (head or deputy). The other
    // source is optional and is revealed only during pay, never during selection.
    if (ctx.owner->hasSkill(other) && !ctx.owner->hasShownSkill(other)) {
        for (int id : ctx.owner->getValidSkillInstanceIds(other)) {
            SkillInstanceRef ref(ctx.owner->objectName(), SkillInstanceKey(other, id));
            if (!room->canShowGeneralForSkill(ref)) continue;
            ctx.extra_data = id;
            ctx.choice = room->askForChoice(ctx.owner, "GuanxingShowGeneral", "show_both_generals+cancel");
            break;
        }
    }
    return true;
}

bool HGuanxing::pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    if (ctx.choice != "show_both_generals") return true;
    const QString other = objectName() == "heg_yizhi" ? "heg_guanxing" : "heg_yizhi";
    return room->showGeneralForSkill(SkillInstanceRef(ctx.owner->objectName(), SkillInstanceKey(other, ctx.extra_data.toInt())));
}

bool HGuanxing::effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *owner = ctx.owner;
    if (!owner) return false;
    // Guanxing and Yizhi are two reveal choices for the same observation. Pair
    // instances by stable ID order so additional acquired instances still work.
    const QString other = objectName() == "heg_yizhi" ? "heg_guanxing" : "heg_yizhi";
    const int ordinal = owner->getValidSkillInstanceIds(objectName()).indexOf(ctx.instanceID);
    const QList<int> others = owner->getValidSkillInstanceIds(other);
    if (ordinal >= 0 && ordinal < others.size()) {
        QStringList consumed = owner->getTag("heg_guanxing_consumed").toStringList();
        consumed << SkillInstanceUtils::formatName(other, others.at(ordinal));
        owner->setTag("heg_guanxing_consumed", consumed);
    }
    const int count = owner->hasShownSkill("heg_guanxing") && owner->hasShownSkill("heg_yizhi")
        ? 5 : qMin(5, owner->aliveCount());
    const QList<int> cards = room->getNCards(count);
    LogMessage log;
    log.type = "$ViewDrawPile";
    log.from = owner;
    log.card_str = ListI2S(cards).join("+");
    room->sendLog(log, owner);
    room->askForGuanxing(owner, cards, Room::GuanxingBothSides);
    return false;
}

class HKongcheng : public TriggerSkillV2 {
public:
    HKongcheng() : TriggerSkillV2("heg_kongcheng")
    {
        events << TargetConfirming;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!ownsTrigger(player, objectName()) || !player->isKongcheng() || !use.card
            || (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel")) || !use.to.contains(player)) return {};
        return {{player, {objectName()}}};
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        room->cancelTarget(use, ctx.owner);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class HLongdanVS : public ViewAsSkillV2 {
public:
    HLongdanVS() : ViewAsSkillV2("heg_longdan", 1) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? Slash::IsAvailable(request.initiator) : request.pattern == "slash" || request.pattern == "jink";
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card)) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "slash"
            ? card->isKindOf("Jink") : request.pattern == "jink" && card->isKindOf("Slash");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = original->isKindOf("Slash")
            ? static_cast<Card *>(new Jink(original->getSuit(), original->getNumber()))
            : static_cast<Card *>(new Slash(original->getSuit(), original->getNumber()));
        card->addSubcard(original);
        card->setSkillName(objectName());
        card->setShowSkill(objectName());
        return card;
    }
};

class HLongdan : public TriggerSkillV2 {
public:
    HLongdan() : TriggerSkillV2("heg_longdan")
    {
        view_as_skill = new HLongdanVS;
        events << CardUsed << CardResponded;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!ownsTrigger(player, objectName()) || !hasShouyue(player)) return {};
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        return card && card->getSkillName() == objectName() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner && hasShouyue(ctx.owner)) {
            room->notifySkillInvoked(room->getLord(ctx.owner->getKingdom()), "heg_shouyue");
            ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *card) const override
    {
        return card && card->isKindOf("Slash") ? 1 : 2;
    }
};

class HTieqi : public TriggerSkillV2 {
public:
    HTieqi() : TriggerSkillV2("heg_tieqi") { events << TargetSpecified; }
    void record(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || ctx.owner != player || !ctx.original_data) return;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return;
        QStringList targets;
        for (ServerPlayer *target : use.to) targets << target->objectName();
        // Preserve seat order while nested effects change the live target list.
        use.card->setTag(objectName() + "_targets", targets);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!ownsTrigger(player, objectName()) || !use.card || !use.card->isKindOf("Slash")) return {};
        const int count = use.card->getTag(objectName() + "_targets").toStringList().size();
        return count > 0 ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        const QStringList targets = use.card->getTag(objectName() + "_targets").toStringList();
        if (ctx.trigger_count < 0 || ctx.trigger_count >= targets.size()) return false;
        ServerPlayer *target = room->findPlayerByObjectName(targets.at(ctx.trigger_count));
        if (!target || !target->isAlive() || !use.to.contains(target)) return false;
        ctx.targets = {target};
        return invokeShuSkill(this, room, ctx.owner, QVariant::fromValue(target));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.to.contains(target)) return false;
        JudgeStruct judge;
        const bool shouyue = hasShouyue(ctx.owner);
        judge.pattern = shouyue ? ".|spade" : ".|red";
        judge.good = !shouyue;
        judge.reason = objectName();
        judge.who = ctx.owner;
        if (shouyue) room->notifySkillInvoked(room->getLord(ctx.owner->getKingdom()), "heg_shouyue");
        room->judge(judge);
        if (judge.isGood()) {
            preventJink(room, ctx.owner, target, use);
            room->broadcastSkillInvoke(objectName(), 2, ctx.owner);
        }
        return false;
    }
};

class HJizhi : public TriggerSkillV2 {
public:
    HJizhi() : TriggerSkillV2("heg_jizhi") { events << CardUsed; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<CardUseStruct>().card;
        if (!ownsTrigger(player, objectName()) || !card || !card->isNDTrick()) return {};
        // Unlike nosjizhi, a converted physical card must retain its card name.
        if (card->isVirtualCard() && !card->getSubcards().isEmpty()
            && (card->subcardsLength() != 1
                || Sanguosha->getCard(card->getEffectiveId())->objectName() != card->objectName())) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data && invokeShuSkill(this, room, ctx.owner, *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner) ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class HLiegong : public TriggerSkillV2 {
public:
    HLiegong() : TriggerSkillV2("heg_liegong") { events << TargetSpecified; }
    void record(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || ctx.owner != player || !ctx.original_data) return;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return;
        QStringList targets;
        for (ServerPlayer *target : use.to) targets << target->objectName();
        // Preserve seat order while nested effects change the live target list.
        use.card->setTag(objectName() + "_targets", targets);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!ownsTrigger(player, objectName()) || player->getPhase() != Player::Play
            || !use.card || !use.card->isKindOf("Slash")) return {};
        const int count = use.card->getTag(objectName() + "_targets").toStringList().size();
        return count > 0 ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        const QStringList targets = use.card->getTag(objectName() + "_targets").toStringList();
        if (ctx.trigger_count < 0 || ctx.trigger_count >= targets.size()) return false;
        ServerPlayer *target = room->findPlayerByObjectName(targets.at(ctx.trigger_count));
        if (!target || !target->isAlive() || !use.to.contains(target)) return false;
        const int hand = target->getHandcardNum();
        if (hand < ctx.owner->getHp() && hand > ctx.owner->getAttackRange()) return false;
        ctx.targets = {target};
        return invokeShuSkill(this, room, ctx.owner, QVariant::fromValue(target));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (ctx.owner && ctx.original_data) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (use.card) preventJink(room, ctx.owner, target, use);
        }
        return false;
    }
};

class HLiegongRange : public AttackRangeSkillV2 {
public:
    HLiegongRange() : AttackRangeSkillV2("#heg_liegong-for-lord") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder && ctx.holder->hasShownSkill("heg_liegong") && hasShouyue(ctx.holder)
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class HKuanggu : public TriggerSkillV2 {
public:
    HKuanggu() : TriggerSkillV2("heg_kuanggu")
    {
        events << PreDamageDone << Damage;
        frequency = Compulsory;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != PreDamageDone || !ctx.original_data) return;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || !damage.to) return;
        const int distance = damage.from->distanceTo(damage.to);
        // Keep the pre-damage distance snapshot separate from identity Kuanggu.
        damage.from->setTag("heg_kuanggu_in_range", distance >= 0 && distance <= 1);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage || !ownsTrigger(player, objectName()) || !player->isWounded()
            || !player->getTag("heg_kuanggu_in_range").toBool()) return {};
        const int count = data.value<DamageStruct>().damage;
        return count > 0 ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner && ctx.owner->isWounded()) {
            room->broadcastSkillInvoke(objectName(), ctx.owner);
            room->recover(ctx.owner, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
        }
        return false;
    }
};

class HSavageAssaultAvoid : public TriggerSkillV2 {
public:
    explicit HSavageAssaultAvoid(const QString &parent)
        : TriggerSkillV2("#heg_sa_avoid_" + parent), parentSkill("heg_" + parent)
    {
        events << CardEffected;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!ownsTrigger(player, parentSkill) || !effect.card || !effect.card->isKindOf("SavageAssault")) return {};
        return {{player, {objectName()}}};
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        effect.nullified = true;
        *ctx.original_data = QVariant::fromValue(effect);
        room->broadcastSkillInvoke(parentSkill, 1, ctx.owner);
        LogMessage log;
        log.type = "#SkillNullify";
        log.from = ctx.owner;
        log.arg = parentSkill;
        log.arg2 = "savage_assault";
        room->sendLog(log);
        return false;
    }
private:
    QString parentSkill;
};

class HHuoshou : public TriggerSkillV2 {
public:
    HHuoshou() : TriggerSkillV2("heg_huoshou")
    {
        events << TargetSpecified << ConfirmDamage << CardFinished;
        frequency = Compulsory;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return;
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("SavageAssault")) return;
            const QString source = damage.card->getTag("heg_huoshou_source").toString();
            if (source.isEmpty()) return;
            ServerPlayer *owner = room->findPlayerByObjectName(source, true);
            damage.from = owner && owner->isAlive() ? owner : nullptr;
            *ctx.original_data = QVariant::fromValue(damage);
        } else if (event == CardFinished) {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            if (card && card->isKindOf("SavageAssault")) card->removeTag("heg_huoshou_source");
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const Card *card = event == TargetSpecified ? data.value<CardUseStruct>().card : nullptr;
        if (!card || !card->isKindOf("SavageAssault")) return {};
        TriggerList choices;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (ownsTrigger(owner, objectName())) choices[owner] << objectName();
        return choices;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        if (card) {
            // Card-local provenance survives nested Savage Assaults without
            // overwriting the outer use's source or persisting a player pointer.
            card->setTag("heg_huoshou_source", ctx.owner->objectName());
            room->notifySkillInvoked(ctx.owner, objectName());
            room->broadcastSkillInvoke(objectName(), 2, ctx.owner);
        }
        return false;
    }
};

class HJuxiang : public TriggerSkillV2 {
public:
    HJuxiang() : TriggerSkillV2("heg_juxiang")
    {
        events << CardUsed << CardsMoveOneTime << CardFinished;
        frequency = Compulsory;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data || (event != CardUsed && event != CardFinished)) return;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        if (!card || !card->isKindOf("SavageAssault") || (card->isVirtualCard() && card->subcardsLength() != 1)) return;
        const Card *physical = Sanguosha->getEngineCard(card->getEffectiveId());
        if (physical && physical->isKindOf("SavageAssault"))
            room->setCardFlag(card->getEffectiveId(), event == CardUsed ? "heg_juxiang_real_sa" : "-heg_juxiang_real_sa");
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardsMoveOneTime) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.card_ids.size() != 1 || !move.from_places.contains(Player::PlaceTable)
            || move.to_place != Player::DiscardPile || move.reason.m_reason != CardMoveReason::S_REASON_USE
            || room->getCardPlace(move.card_ids.first()) != Player::DiscardPile
            || !Sanguosha->getCard(move.card_ids.first())->hasFlag("heg_juxiang_real_sa")) return {};
        TriggerList choices;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != move.from && ownsTrigger(owner, objectName())) choices[owner] << objectName();
        return choices;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.card_ids.size() == 1 && room->getCardPlace(move.card_ids.first()) == Player::DiscardPile) {
            DummyCard card(move.card_ids);
            room->broadcastSkillInvoke(objectName(), 2, ctx.owner);
            ctx.owner->obtainCard(&card);
        }
        return false;
    }
};

class HShushen : public TriggerSkillV2 {
public:
    HShushen() : TriggerSkillV2("heg_shushen") { events << HpRecover; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!ownsTrigger(player, objectName())) return {};
        bool hasFriend = false;
        for (ServerPlayer *other : room->getOtherPlayers(player))
            if (player->willBeFriendWith(other)) { hasFriend = true; break; }
        if (!hasFriend) return {};
        const int count = data.value<RecoverStruct>().recover;
        return count > 0 ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> friends;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (ctx.owner->willBeFriendWith(other)) friends << other;
        if (friends.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, friends, objectName(), "heg_shushen-invoke", true, true);
        if (!target) return false;
        // Execution-local targets replace the old player-tag stack.
        ctx.targets = {target};
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class HShenzhi : public TriggerSkillV2 {
public:
    HShenzhi() : TriggerSkillV2("heg_shenzhi") { events << EventPhaseStart; frequency = Frequent; }
    bool canPreshow() const override { return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return ownsTrigger(player, objectName()) && player->getPhase() == Player::Start && !player->isKongcheng()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !invokeShuSkill(this, room, ctx.owner)) return false;
        int discardable = 0;
        for (const Card *card : ctx.owner->getHandcards())
            if (!ctx.owner->isJilei(card)) ++discardable;
        ctx.extra_data = discardable;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->throwAllHandCards();
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner && ctx.extra_data.toInt() >= ctx.owner->getHp())
            room->recover(ctx.owner, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
        return false;
    }
};

void HStandardPackage::addShuGenerals()
{
    // Rule-equivalent skills reference their existing definitions; only HEG
    // differences above own new V2 implementations.
    General *liubei = new General(this, "heg_liubei", "shu"); // SHU 001
    liubei->addCompanion("heg_guanyu");
    liubei->addCompanion("heg_zhangfei");
    liubei->addCompanion("heg_ganfuren");
    liubei->addSkill(new HRende);

    General *guanyu = new General(this, "heg_guanyu", "shu", 5); // SHU 002
    guanyu->addSkill(new HWusheng);

    General *zhangfei = new General(this, "heg_zhangfei", "shu"); // SHU 003
    zhangfei->addSkill(new HPaoxiao);
    zhangfei->addSkill(new HPaoxiaoArmorNullification);
    insertRelatedSkills("heg_paoxiao", "#heg_paoxiao-null");

    General *zhugeliang = new General(this, "heg_zhugeliang", "shu", 3); // SHU 004
    zhugeliang->addCompanion("heg_huangyueying");
    zhugeliang->addSkill(new HGuanxing);
    zhugeliang->addSkill(new HKongcheng);

    General *zhaoyun = new General(this, "heg_zhaoyun", "shu"); // SHU 005
    zhaoyun->addCompanion("heg_liushan");
    zhaoyun->addSkill(new HLongdan);

    General *machao = new General(this, "heg_machao", "shu"); // SHU 006
    machao->addSkill(new HTieqi);
    machao->addSkill("mashu");

    General *huangyueying = new General(this, "heg_huangyueying", "shu", 3, false); // SHU 007
    huangyueying->addSkill(new HJizhi);
    huangyueying->addSkill("nosqicai");

    General *huangzhong = new General(this, "heg_huangzhong", "shu"); // SHU 008
    huangzhong->addCompanion("heg_weiyan");
    huangzhong->addSkill(new HLiegong);
    huangzhong->addSkill(new HLiegongRange);
    insertRelatedSkills("heg_liegong", "#heg_liegong-for-lord");

    General *weiyan = new General(this, "heg_weiyan", "shu"); // SHU 009
    weiyan->addSkill(new HKuanggu);

    General *pangtong = new General(this, "heg_pangtong", "shu", 3); // SHU 010
    pangtong->addSkill("lianhuan");
    pangtong->addSkill("niepan");

    General *wolong = new General(this, "heg_wolong", "shu", 3); // SHU 011
    wolong->addCompanion("heg_huangyueying");
    wolong->addCompanion("heg_pangtong");
    wolong->addSkill("huoji");
    wolong->addSkill("kanpo");
    wolong->addSkill("bazhen");

    General *liushan = new General(this, "heg_liushan", "shu", 3); // SHU 013
    liushan->addSkill("xiangle");
    liushan->addSkill("fangquan");

    General *menghuo = new General(this, "heg_menghuo", "shu"); // SHU 014
    menghuo->addCompanion("heg_zhurong");
    menghuo->addSkill(new HSavageAssaultAvoid("huoshou"));
    menghuo->addSkill(new HHuoshou);
    menghuo->addSkill("zaiqi");
    insertRelatedSkills("heg_huoshou", "#heg_sa_avoid_huoshou");

    General *zhurong = new General(this, "heg_zhurong", "shu", 4, false); // SHU 015
    zhurong->addSkill(new HSavageAssaultAvoid("juxiang"));
    zhurong->addSkill(new HJuxiang);
    zhurong->addSkill("lieren");
    insertRelatedSkills("heg_juxiang", "#heg_sa_avoid_juxiang");

    General *ganfuren = new General(this, "heg_ganfuren", "shu", 3, false); // SHU 016
    ganfuren->addSkill(new HShushen);
    ganfuren->addSkill(new HShenzhi);

}
