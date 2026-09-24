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

// Source: QSanguosha-For-Hegemony-xxyheaven cf61c15, standard-shu-generals.cpp.
#include "h-standard-shu-generals.h"
#include "standard.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "general.h"
#include "skill-instance-utils.h"
#include "util.h"
#include <QScopeGuard>

namespace {
bool ownsHegemonySkill(const ServerPlayer *player, const QString &skillName)
{
    return player && player->isAlive() && player->hasSkill(skillName);
}

bool invokeHegemonySkill(const TriggerSkill *skill, Room *room, ServerPlayer *owner,
                         const QVariant &data = QVariant())
{
    if (!owner || !owner->askForSkillInvoke(skill, data)) return false;
    room->broadcastSkillInvoke(skill->objectName(), owner);
    return true;
}

bool hasShouyue(const Player *player)
{
    const Player *lord = player ? player->getLord() : nullptr;
    return player && player->getSeemingKingdom() == "shu"
        && lord && lord->hasLordSkill("heg_shouyue") && lord->hasShownGeneral1();
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

class HPaoxiaoDraw : public TriggerSkillV2 {
public:
    HPaoxiaoDraw() : TriggerSkillV2("#heg_paoxiao-draw") { events << CardUsed; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<CardUseStruct>().card;
        return ownsHegemonySkill(player, "heg_paoxiao") && card && card->isKindOf("Slash")
            && player->getRoom()->countHistoryCards(player, "turn", "Slash") == 2
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner) ctx.owner->drawCards(getEffectiveAmount(ctx), "heg_paoxiao");
        return false;
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
        if (!ownsHegemonySkill(player, "heg_paoxiao") || !hasShouyue(player)
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

class HKongcheng : public TriggerSkillV2 {
public:
    HKongcheng() : TriggerSkillV2("heg_kongcheng")
    {
        events << TargetConfirming << BeforeCardsMove << EventPhaseStart;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!ownsHegemonySkill(player, objectName())) return {};
        if (event == BeforeCardsMove) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            return player->getPhase() == Player::NotActive && player->isKongcheng()
                && move.to == player && move.to_place == Player::PlaceHand
                && (move.reason.m_reason == CardMoveReason::S_REASON_GIVE
                    || move.reason.m_reason == CardMoveReason::S_REASON_PREVIEWGIVE)
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        if (event == EventPhaseStart)
            return player->getPhase() == Player::Draw && !player->getPile("zither").isEmpty()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player->isKongcheng() || !use.card
            || (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel")) || !use.to.contains(player)) return {};
        return {{player, {objectName()}}};
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        if (event == BeforeCardsMove) {
            CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            // Rewrite the pending destination; the movement service commits the pile atomically.
            move.to_place = Player::PlaceSpecial;
            move.to_pile_name = "zither";
            *ctx.original_data = QVariant::fromValue(move);
            return false;
        }
        if (event == EventPhaseStart) {
            DummyCard cards(ctx.owner->getPile("zither"));
            room->obtainCard(ctx.owner, &cards,
                CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, ctx.owner->objectName(), objectName(), QString()), false);
            return false;
        }
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
            ? Slash::IsAvailable(request.initiator)
            : (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
                && (request.pattern == "slash" || request.pattern == "jink");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !ViewAsSkillV2::canSelectCard(request, card)) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "slash"
            ? card->isKindOf("Jink") : request.pattern == "jink" && card->isKindOf("Slash");
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
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
        events << CardUsed << CardResponded << CardOffset;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == CardOffset) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card || !effect.card->isKindOf("Slash") || !effect.offset_card
                || !effect.offset_card->isKindOf("Jink")) return {};
            TriggerList result;
            if (effect.card->getSkillName() == objectName() && ownsHegemonySkill(effect.from, objectName()))
                result[effect.from] << objectName();
            if (effect.offset_card->getSkillName() == objectName() && ownsHegemonySkill(effect.to, objectName()))
                result[effect.to] << objectName();
            return result;
        }
        if (!ownsHegemonySkill(player, objectName()) || !hasShouyue(player)) return {};
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        return card && card->getSkillName() == objectName() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        if (event != CardOffset) return true;
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        const bool damage = effect.from == ctx.owner && effect.card
            && effect.card->getSkillName() == objectName();
        QList<ServerPlayer *> targets;
        // XXY excludes the dodger for damage, and both duel participants for recovery.
        for (ServerPlayer *target : room->getAlivePlayers()) {
            if (damage ? target != effect.to
                       : target != effect.from && target != ctx.owner && target->isWounded())
                targets << target;
        }
        if (targets.isEmpty()) return false;
        ctx.choice = damage ? "damage" : "recover";
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets,
            objectName() + "_" + ctx.choice, objectName() + "-" + ctx.choice, true);
        if (!target) return false;
        ctx.targets = {target};
        room->notifySkillInvoked(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName(), damage ? 1 : 2, ctx.owner);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), target->objectName());
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        // Offset bonuses are separate from Shouyue's use/response draw.
        if (event != CardOffset && hasShouyue(ctx.owner)) {
            room->notifySkillInvoked(room->getLord(ctx.owner->getKingdom()), "heg_shouyue");
            ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (event != CardOffset || !ctx.owner || !target || !target->isAlive()) return false;
        if (ctx.choice == "damage")
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        else
            room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *card) const override
    {
        return card && card->isKindOf("Slash") ? 1 : 2;
    }
};

class HTieqi : public TriggerSkillV2 {
public:
    HTieqi() : TriggerSkillV2("heg_tieqi") { events << TargetSpecified; frequency = Frequent; }
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
        if (!ownsHegemonySkill(player, objectName()) || !use.card || !use.card->isKindOf("Slash")) return {};
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
        return invokeHegemonySkill(this, room, ctx.owner, QVariant::fromValue(target));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.to.contains(target)) return false;
        JudgeStruct judge;
        const bool shouyue = hasShouyue(ctx.owner);
        judge.pattern = ".";
        judge.good = true;
        judge.reason = objectName();
        judge.who = ctx.owner;
        if (shouyue) room->notifySkillInvoked(room->getLord(ctx.owner->getKingdom()), "heg_shouyue");
        const bool alreadyMarked = target->hasFlag("TieqiTarget");
        target->setFlags("TieqiTarget");
        // Existing retrial AI reads this flag; preserve it across nested judges.
        const auto clearTargetFlag = qScopeGuard([target, alreadyMarked] {
            if (!alreadyMarked) target->setFlags("-TieqiTarget");
        });
        room->judge(judge);
        QStringList choices;
        if (target->hasShownGeneral1()) choices << "head_general";
        if (target->hasShownGeneral2()) choices << "deputy_general";
        const QString choice = !shouyue && !choices.isEmpty()
            ? room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(target)) : QString();
        // Invalidate exact innate sources on the chosen shown general, never acquired skills.
        for (const SkillInstance &instance : target->getSkillInstances()) {
            if (instance.source != SourceInnate || instance.bindHead == 0) continue;
            const QString slot = instance.bindHead == 1 ? "head_general" : "deputy_general";
            const Skill *skill = Sanguosha->getSkill(instance.skillName);
            if (!choices.contains(slot) || (!shouyue && slot != choice) || !skill
                || skill->getFrequency(target) == Skill::Compulsory || skill->getFrequency(target) == Skill::Wake) continue;
            room->addSkillInvalidity(target, instance.skillName, ctx.owner->objectName(), "heg_tieqi_turn", instance.instanceID);
        }
        const QString suit = judge.card ? judge.card->getSuitString() : QString();
        if (target->isAlive() && QStringList{"spade", "club", "heart", "diamond"}.contains(suit)
            && !room->askForCard(target, ".|" + suit, "@heg_tieji-discard:::" + suit, QVariant::fromValue(use)))
            preventJink(room, ctx.owner, target, use);
        return false;
    }

};

class HTieqiClear : public TriggerSkillV2 {
public:
    HTieqiClear() : TriggerSkillV2("#heg_tieqi-clear") { events << EventPhaseStart; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::NotActive) return true;
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            for (const QString &record : target->getTag("SkillInvalidityRecords").toStringList()) {
                const QStringList parts = record.split('|');
                if (parts.size() == 3 && parts.at(2) == "heg_tieqi_turn")
                    room->removeSkillInvalidity(target, parts.at(0), parts.at(1), parts.at(2));
            }
        }
        return true;
    }
};

class HJizhi : public TriggerSkillV2 {
public:
    HJizhi() : TriggerSkillV2("heg_jizhi") { events << CardUsed; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<CardUseStruct>().card;
        if (!ownsHegemonySkill(player, objectName()) || !card || !card->isNDTrick()) return {};
        // The new donor accepts real tricks and material-free virtual tricks only.
        if (card->isVirtualCard() && !card->getSubcards().isEmpty()) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data && invokeHegemonySkill(this, room, ctx.owner, *ctx.original_data);
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
        if (!ownsHegemonySkill(player, objectName()) || !use.card || !use.card->isKindOf("Slash")) return {};
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
        if (target->getHp() < ctx.owner->getHp()) return false;
        ctx.targets = {target};
        return invokeHegemonySkill(this, room, ctx.owner, QVariant::fromValue(target));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (ctx.owner && ctx.original_data) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card) return false;
            const QString choice = room->askForChoice(ctx.owner, objectName(), "nojink+adddamage", *ctx.original_data);
            if (choice == "nojink") preventJink(room, ctx.owner, target, use);
            else if (choice == "adddamage") {
                QStringList targets = use.card->getTag("heg_liegong_damage").toStringList();
                targets << target->objectName();
                use.card->setTag("heg_liegong_damage", targets);
            }
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

class HLiegongTarget : public TargetModSkillV2 {
public:
    HLiegongTarget() : TargetModSkillV2("#heg_liegong-target") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.primary && ctx.secondary
            && ctx.primary->getHandcardNum() >= ctx.secondary->getHandcardNum()
            ? CorrectSkillResult::useAmount(10000) : CorrectSkillResult::noEffect();
    }
};

class HLiegongDamage : public TriggerSkillV2 {
public:
    HLiegongDamage() : TriggerSkillV2("#heg_liegong-damage")
    { events << PreCardUsed << ConfirmDamage << CardFinished; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == ConfirmDamage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.to && !damage.chain && !damage.transfer) {
                damage.damage += damage.card->getTag("heg_liegong_damage").toStringList().count(damage.to->objectName());
                data = QVariant::fromValue(damage);
            }
        } else {
            const Card *card = data.value<CardUseStruct>().card;
            if (card) card->removeTag("heg_liegong_damage");
        }
        return true;
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
        if (!ownsHegemonySkill(player, parentSkill) || !effect.card || !effect.card->isKindOf("SavageAssault")) return {};
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
            if (ownsHegemonySkill(owner, objectName())) choices[owner] << objectName();
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
        if (!card || !card->isKindOf("SavageAssault")) return;
        const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        for (int id : ids)
            room->setCardFlag(id, event == CardUsed ? "heg_juxiang_real_sa" : "-heg_juxiang_real_sa");
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardsMoveOneTime) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.card_ids.isEmpty() || !move.from_places.contains(Player::PlaceTable)
            || move.to_place != Player::DiscardPile || move.reason.m_reason != CardMoveReason::S_REASON_USE
            || room->getCardPlace(move.card_ids.first()) != Player::DiscardPile) return {};
        for (int id : move.card_ids)
            if (!Sanguosha->getCard(id)->hasFlag("heg_juxiang_real_sa")) return {};
        TriggerList choices;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != move.from && ownsHegemonySkill(owner, objectName())) choices[owner] << objectName();
        return choices;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        bool available = !move.card_ids.isEmpty();
        for (int id : move.card_ids)
            if (room->getCardPlace(id) != Player::DiscardPile) available = false;
        if (available) {
            DummyCard card(move.card_ids);
            room->broadcastSkillInvoke(objectName(), 2, ctx.owner);
            ctx.owner->obtainCard(&card);
        }
        return false;
    }
};

class HZaiqi : public TriggerSkillV2 {
public:
    HZaiqi() : TriggerSkillV2("heg_zaiqi") { events << EventPhaseEnd; }
    static int redDiscardCount(Room *room)
    {
        int count = 0;
        for (const QVariant &id : room->getTag("GlobalRoundDisCardPile").toList()) {
            const Card *card = Sanguosha->getCard(id.toInt());
            if (card && card->isRed()) ++count;
        }
        return count;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return ownsHegemonySkill(player, objectName()) && player->getPhase() == Player::Discard && redDiscardCount(room) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> friends;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (ctx.owner->isFriendWith(target)) friends << target;
        const int count = qMin(redDiscardCount(room), int(friends.size()));
        if (count <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, friends, objectName(), 0, count,
            "@heg_zaiqi-target:::" + QString::number(count), true);
        room->sortByActionOrder(ctx.targets);
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList choices{"drawcard"};
        if (ctx.owner && ctx.owner->isAlive() && ctx.owner->isWounded()) choices << "recover";
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(ctx.owner)) == "recover")
            room->recover(ctx.owner, RecoverStruct(target, nullptr, 1, objectName()));
        else target->drawCards(1, objectName());
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
    liubei->addSkill("tenyearrende");

    General *guanyu = new General(this, "heg_guanyu", "shu", 5); // SHU 002
    guanyu->addSkill("tenyearwusheng");
    guanyu->addCompanion("heg_zhangfei");

    General *zhangfei = new General(this, "heg_zhangfei", "shu"); // SHU 003
    zhangfei->addSkill(new HPaoxiao);
    zhangfei->addSkill(new HPaoxiaoDraw);
    zhangfei->addSkill(new HPaoxiaoArmorNullification);
    insertRelatedSkills("heg_paoxiao", "#heg_paoxiao-draw");
    insertRelatedSkills("heg_paoxiao", "#heg_paoxiao-null");

    General *zhugeliang = new General(this, "heg_zhugeliang", "shu", 3); // SHU 004
    zhugeliang->addCompanion("heg_huangyueying");
    zhugeliang->addSkill("guanxing");
    zhugeliang->addSkill(new HKongcheng);

    General *zhaoyun = new General(this, "heg_zhaoyun", "shu"); // SHU 005
    zhaoyun->addCompanion("heg_liushan");
    zhaoyun->addSkill(new HLongdan);

    General *machao = new General(this, "heg_machao", "shu"); // SHU 006
    machao->addSkill(new HTieqi);
    machao->addSkill("mashu");
    machao->addSkill(new HTieqiClear);
    insertRelatedSkills("heg_tieqi", "#heg_tieqi-clear");

    General *huangyueying = new General(this, "heg_huangyueying", "shu", 3, false); // SHU 007
    huangyueying->addSkill(new HJizhi);
    huangyueying->addSkill("nosqicai");

    General *huangzhong = new General(this, "heg_huangzhong", "shu"); // SHU 008
    huangzhong->addCompanion("heg_weiyan");
    huangzhong->addSkill(new HLiegong);
    huangzhong->addSkill(new HLiegongRange);
    huangzhong->addSkill(new HLiegongTarget);
    huangzhong->addSkill(new HLiegongDamage);
    insertRelatedSkills("heg_liegong", "#heg_liegong-for-lord");
    insertRelatedSkills("heg_liegong", "#heg_liegong-target");
    insertRelatedSkills("heg_liegong", "#heg_liegong-damage");

    General *weiyan = new General(this, "heg_weiyan", "shu"); // SHU 009
    weiyan->addSkill("tenyearkuanggu");

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
    menghuo->addSkill(new HZaiqi);
    insertRelatedSkills("heg_huoshou", "#heg_sa_avoid_huoshou");

    General *zhurong = new General(this, "heg_zhurong", "shu", 4, false); // SHU 015
    zhurong->addSkill(new HSavageAssaultAvoid("juxiang"));
    zhurong->addSkill(new HJuxiang);
    zhurong->addSkill("lieren");
    insertRelatedSkills("heg_juxiang", "#heg_sa_avoid_juxiang");

    General *ganfuren = new General(this, "heg_ganfuren", "shu", 3, false); // SHU 016
    ganfuren->addSkill("shushen");
    ganfuren->addSkill("shenzhi");

}
