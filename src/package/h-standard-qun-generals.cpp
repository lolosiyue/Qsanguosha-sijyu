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

#include "h-standard-qun-generals.h"
#include "skill.h"
#include "skill-instance-utils.h"
#include "room.h"
#include "serverplayer.h"
#include "engine.h"
#include "general.h"
#include "standard.h"
#include "util.h"
#include "roomthread.h"

// Qun-specific rules ported from QSanguosha-For-Hegemony-xxyheaven cf61c15.
namespace {
const char *const LuanjiSuits = "heg_luanji_suits_phase";

QStringList publicHistory(const Player *player, const char *key)
{
    return player->property(key).toString().split('+', Qt::SkipEmptyParts);
}

bool hasGeneralSlot(const Player *player, bool head)
{
    const General *general = head ? player->getGeneral() : player->getGeneral2();
    return general && !general->objectName().contains("sujiang");
}

class HLuanjiViewAsSkill : public ViewAsSkillV2
{
public:
    HLuanjiViewAsSkill() : ViewAsSkillV2("heg_luanji", 2) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        // Suits need not match each other; only previously paid suits are excluded.
        return request.initiator && ViewAsSkillV2::canSelectCard(request, card)
            && card->getEffectiveId() >= 0 && !request.selectedCardIds.contains(card->getEffectiveId())
            && !card->isEquipped()
            && !publicHistory(request.initiator, LuanjiSuits).contains(card->getSuitString() + "_char");
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2) return false;
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
        auto *card = new ArcheryAttack(Card::SuitToBeDecided, 0);
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        card->setShowSkill(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "ArcheryAttack"; }
};

class HLuanji : public TriggerSkillV2
{
public:
    HLuanji() : TriggerSkillV2("heg_luanji")
    {
        events << PreCardUsed;
        view_as_skill = new HLuanjiViewAsSkill;
    }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || use.card->getSkillName() != objectName()) return true;
        QStringList suits = publicHistory(player, LuanjiSuits);
        for (int id : use.card->getSubcards()) {
            const QString suit = Sanguosha->getCard(id)->getSuitString() + "_char";
            if (!suits.contains(suit)) suits << suit;
        }
        room->setPlayerProperty(player, LuanjiSuits, suits.join('+'));
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    { return {}; }
};

class HLuanjiDraw : public TriggerSkillV2
{
public:
    HLuanjiDraw() : TriggerSkillV2("#heg_luanji-draw")
    {
        events << CardResponded;
        frequency = Compulsory;
        global = true;
    }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return -2; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive()) return true;
        const CardResponseStruct response = data.value<CardResponseStruct>();
        // The responding ally receives this card effect without owning Luanji.
        if (response.m_card && response.m_card->isKindOf("Jink")
            && response.m_toCard && response.m_toCard->getSkillName() == "heg_luanji"
            && response.m_who && player->isFriendWith(response.m_who)) {
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = player;
            ctx.invoker = player;
            ctx.initiator = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const CardResponseStruct response = ctx.original_data->value<CardResponseStruct>();
        return response.m_card && response.m_card->isKindOf("Jink") && response.m_toCard
            && response.m_toCard->getSkillName() == "heg_luanji"
            && response.m_who && ctx.owner->isFriendWith(response.m_who);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *player = ctx.owner;
        LogMessage log;
        log.type = "#LuanjiDraw";
        log.from = player;
        log.arg = "heg_luanji";
        room->sendLog(log);
        return room->askForChoice(player, "luanji_draw", "yes+no", *ctx.original_data, QString(), "@heg_luanji-draw") == "yes";
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(1, "heg_luanji");
        return false;
    }
};




class HQunHistory : public TriggerSkillV2
{
public:
    HQunHistory() : TriggerSkillV2("#heg_qun-variant-history")
    { events << EventPhaseChanging; global = true; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        for (ServerPlayer *player : room->getAllPlayers(true)) {
            if (!publicHistory(player, LuanjiSuits).isEmpty()) room->setPlayerProperty(player, LuanjiSuits, QString());

        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    { return {}; }
};


class HDuanchang : public TriggerSkillV2
{
public:
    HDuanchang() : TriggerSkillV2("heg_duanchang") { events << Death; frequency = Compulsory; }
    bool canPreshow() const override { return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->hasSkill(objectName())) return {};
        const DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        return death.who == player && killer && killer->isAlive()
            && (hasGeneralSlot(killer, true) || hasGeneralSlot(killer, false))
            ? TriggerList{{player, QStringList(objectName())}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (!killer || !killer->isAlive()) return false;
        QStringList choices;
        if (hasGeneralSlot(killer, true)) choices << "head_general";
        if (hasGeneralSlot(killer, false)) choices << "deputy_general";
        if (choices.isEmpty()) return false;
        // Slot names disclose no hidden general identity to the deceased chooser.
        ctx.choice = choices.size() == 1 ? choices.first()
            : room->askForChoice(ctx.owner, objectName(), choices.join('+'), QVariant::fromValue(killer));
        ctx.targets = {killer};
        return choices.contains(ctx.choice);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target || !target->isAlive()) return false;
        const bool head = ctx.choice == "head_general";
        if (!hasGeneralSlot(target, head)) return false;
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), target->objectName());
        LogMessage log;
        log.type = head ? "#DuanchangLoseHeadSkills" : "#DuanchangLoseDeputySkills";
        log.from = ctx.owner;
        log.to << target;
        log.arg = objectName();
        room->sendLog(log);
        QStringList severed = target->property("Duanchang").toString().split(',', Qt::SkipEmptyParts);
        const QString slot = head ? "head" : "deputy";
        if (!severed.contains(slot)) severed << slot;
        room->setPlayerProperty(target, "Duanchang", severed.join(','));

        // Detach only this general's innate instances. The runtime cascades their
        // helpers; acquired or opposite-slot copies of the same skill survive.
        const QList<SkillInstance> instances = target->getSkillInstances();
        for (const SkillInstance &instance : instances) {
            const Skill *skill = Sanguosha->getSkill(instance.skillName);
            if (instance.source == SourceInnate && instance.bindHead == (head ? 1 : 2)
                && skill && skill->isVisible() && !skill->isAttachedLordSkill())
                room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(instance.skillName, instance.instanceID),
                                           false, false, true);
        }
        if (target->isAlive()) target->gainMark("@heg_duanchang");
        return false;
    }
};
}

class HWeimu : public TriggerSkillV2
{
public:
    HWeimu() : TriggerSkillV2("heg_weimu")
    {
        events << TargetConfirming << BeforeCardsMoveBatch;
        frequency = Compulsory;
    }

    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (triggerEvent == TargetConfirming) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isNDTrick() || !use.card->isBlack()) return {};
            if (use.to.contains(player))
                return TriggerList{{player, {objectName()}}};
        } else if (triggerEvent == BeforeCardsMoveBatch) {

            QVariantList move_datas = data.toList();
            if (move_datas.size() != 1) return {};
            QVariant move_data = move_datas.first();

            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceDelayedTrick && move.card_ids.size() == 1) {

                if (Sanguosha->getCard(move.card_ids.first())->isBlack())
                   return TriggerList{{player, {objectName()}}};

            }

        }
        return {};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data
            && (ctx.owner->hasShownSkill(this) || ctx.owner->askForSkillInvoke(this, *ctx.original_data));
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Update the native event payload so targeting and delayed-trick movement see the cancellation.
        room->broadcastSkillInvoke(objectName(), player);
        room->sendCompulsoryTriggerLog(player, objectName());
        if (triggerEvent == TargetConfirming) {
            CardUseStruct use = data.value<CardUseStruct>();
            room->cancelTarget(use, player); // Room::cancelTarget(use, player);
            data = QVariant::fromValue(use);
        } else if (triggerEvent == BeforeCardsMoveBatch) {

            QVariantList move_datas = data.toList();
            if (move_datas.size() != 1) return false;

            QVariant move_data = move_datas.first();
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            move.to = NULL;
            move.to_place = Player::DiscardPile;
            move.reason = CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, QString());

            move_data = QVariant::fromValue(move);
            QVariantList new_datas;
            new_datas << move_data;
            data = QVariant::fromValue(new_datas);

            return false;
        }
        return false;
    }
};


class HWushuang : public TriggerSkillV2
{
public:
    HWushuang() : TriggerSkillV2("heg_wushuang")
    {
        events << PreCardUsed << TargetSpecifying << TargetSpecified << TargetConfirmed << CardEffected;
        frequency = Compulsory;
    }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return -1; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) {
                use.card->removeTag("heg_wushuang_source_targets");
                use.card->removeTag("heg_wushuang_receivers");
            }
        } else if (event == CardEffected) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card || !effect.card->isKindOf("Duel") || !effect.from || !effect.to) return true;
            if (effect.card->getTag("heg_wushuang_source_targets").toStringList().isEmpty()
                && effect.card->getTag("heg_wushuang_receivers").toStringList().isEmpty()) return true;
            QStringList doubled;
            if (effect.from->hasShownSkill("wushuang")
                || effect.card->getTag("heg_wushuang_source_targets").toStringList().contains(effect.to->objectName()))
                doubled << effect.to->objectName();
            if (effect.to->hasShownSkill("wushuang")
                || effect.card->getTag("heg_wushuang_receivers").toStringList().contains(effect.to->objectName()))
                doubled << effect.from->objectName();
            // Native Duel's response consumer operates on the current pair.
            // Do not let one added target's Wushuang affect the other duels.
            room->setTag("Wushuang_" + effect.card->toString(), doubled);
            if (!doubled.isEmpty()) room->setTag("wushuangData", data);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event != TargetSpecifying && event != TargetSpecified && event != TargetConfirmed) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card) return {};
        if (event == TargetSpecifying) {
            if (use.from != player || !use.card->isKindOf("Duel")
                || (use.card->isVirtualCard() && !use.card->getSubcards().isEmpty())
                || room->getUseExtraTargets(use).isEmpty()) return {};
            return {{player, {objectName()}}};
        }
        if (event == TargetSpecified && use.from == player
            && (use.card->isKindOf("Slash") || use.card->isKindOf("Duel")) && !use.to.isEmpty())
            return {{player, {objectName() + "*" + QString::number(use.to.size())}}};
        if (event == TargetConfirmed && use.card->isKindOf("Duel") && use.to.contains(player) && use.from)
            return {{player, {objectName()}}};
        return {};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetSpecifying)
            return !room->isGeneralHiddenForSkill(ctx.activationRef) || ctx.owner->askForSkillInvoke(this);
        ServerPlayer *target = event == TargetConfirmed ? use.from : use.to.value(ctx.trigger_count);
        if (!target || !target->isAlive()) return false;
        ctx.targets = {target};
        return !room->isGeneralHiddenForSkill(ctx.activationRef)
            || ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target));
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != TargetSpecifying) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        const QList<ServerPlayer *> extra = room->askForPlayersChosen(ctx.owner,
            room->getUseExtraTargets(use), "heg_wushuang_extra", 0, 2, "@heg_wushuang-add");
        use.to << extra;
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !target) return false;
        if (use.card->isKindOf("Slash")) {
            QVariantList jinks = ctx.owner->getTag("Jink_" + use.card->toString()).toList();
            const int index = use.to.indexOf(target);
            if (index >= 0 && index < jinks.size() && jinks.at(index).toInt() == 1) jinks[index] = 2;
            ctx.owner->setTag("Jink_" + use.card->toString(), jinks);
        } else {
            const QString key = event == TargetConfirmed ? "heg_wushuang_receivers" : "heg_wushuang_source_targets";
            QStringList names = use.card->getTag(key).toStringList();
            names << (event == TargetConfirmed ? ctx.owner->objectName() : target->objectName());
            use.card->setTag(key, names);
        }
        return false;
    }
};

class HMingshi : public TriggerSkillV2 {
public:
    HMingshi() : TriggerSkillV2("heg_mingshi") {
        events << DamageInflicted;
        frequency = Compulsory;
        m_baseAmount = 1;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        const DamageStruct damage = data.value<DamageStruct>();
        if (player && player->isAlive() && player->hasSkill(objectName())
            && damage.from && !damage.from->hasShownAllGenerals())
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.original_data) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        room->notifySkillInvoked(ctx.owner, objectName());
        LogMessage log;
        log.type = "#HMingshi";
        log.from = ctx.owner;
        log.arg = QString::number(damage.damage);
        damage.damage = qMax(0, damage.damage - getEffectiveAmount(ctx));
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        *ctx.original_data = QVariant::fromValue(damage);
        return damage.damage == 0;
    }
};

class HSuishi : public TriggerSkillV2 {
public:
    HSuishi() : TriggerSkillV2("heg_suishi") {
        events << Dying << Death;
        frequency = Compulsory;
        m_baseAmount = 1;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return result;
        const DyingStruct dying = event == Dying ? data.value<DyingStruct>() : DyingStruct();
        ServerPlayer *other = event == Dying ? (dying.damage ? dying.damage->from : nullptr)
            : data.value<DeathStruct>().who;
        if (!other) return result;
        // Dying/Death already visit every seat. The payload's victim is distinct
        // from this recipient; enumerating all owners here would multiply triggers.
        if (event == Dying && dying.who == player) return result;
        if (other->isFriendWith(player) || player->willBeFriendWith(other))
            result[player] << objectName();
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        room->broadcastSkillInvoke(objectName(), event == Dying ? 1 : 2, ctx.owner);
        if (event == Dying) ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        else room->loseHp(ctx.owner, getEffectiveAmount(ctx));
        return false;
    }
};

class HHuoshuiVS : public ViewAsSkillV2 {
public:
    HHuoshuiVS() : ViewAsSkillV2("heg_huoshui") {}
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isSkillInstanceEffectAvailable(objectName(), request.activationRef.key.instanceID);
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override {
        return targets.isEmpty();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HHuoshuiCard"; }
    // The common V2 payment/reveal path activates the passive aura; no extra effect.
};

class HHuoshui : public TriggerSkillV2 {
public:
    HHuoshui() : TriggerSkillV2("heg_huoshui") {
        events << GeneralShown << GeneralHidden << GeneralRemoved << EventPhaseStart
               << Death << EventAcquireSkill << EventLoseSkill << EventSkillInvalidated << EventSkillValidityRestored;
        view_as_skill = new HHuoshuiVS;
        // Cleanup must still run after the last owning instance has disappeared.
        global = true;
    }
    void record(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override {
        ServerPlayer *current = room->getCurrent();
        bool active = false;
        if (current && current->isAlive() && current->getPhase() != Player::NotActive) {
            for (int id : current->getValidSkillInstanceIds(objectName())) {
                if (current->isSkillInstanceEffectAvailable(objectName(), id)) { active = true; break; }
            }
        }
        // This is a derived continuous restriction, not a triggerable side effect.
        // Recompute the union so hiding/removing one of two copies retains the other.
        for (ServerPlayer *p : room->getAllPlayers(true)) {
            const bool head = p->disableShow(true).contains(objectName());
            const bool deputy = p->disableShow(false).contains(objectName());
            if (active && p != current) {
                if (!head || !deputy) room->setPlayerDisableShow(p, "hd", objectName());
            } else if (head || deputy) {
                room->removePlayerDisableShow(p, objectName());
            }
        }
    }
};

class HQingcheng : public ViewAsSkillV2 {
public:
    HQingcheng() : ViewAsSkillV2("heg_qingcheng", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override {
        return request.initiator && card && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && card->isKindOf("EquipCard")
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    static QStringList choices(const Player *target) {
        QStringList result;
        if (target->getGeneral() && !target->isLord() && !target->getGeneralName().contains("sujiang"))
            result << target->getGeneralName();
        if (target->getGeneral2() && !target->getGeneral2Name().contains("sujiang"))
            result << target->getGeneral2Name();
        return result;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override {
        return target && selected.isEmpty() && target != request.initiator && target->isAlive()
            && target->hasShownAllGenerals() && !choices(target).isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override {
        return targets.size() == 1;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HQingchengCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        if (!ctx.invoker || !ctx.invoker->isAlive() || !target->hasShownAllGenerals()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        const QStringList generals = choices(target);
        if (generals.isEmpty()) return ContinueEffects;
        const QString choice = generals.size() == 1 ? generals.first()
            : room->askForGeneral(ctx.invoker, generals, QString(), true, objectName());
        if (generals.contains(choice)) target->hideGeneral(choice == target->getGeneralName());
        return ContinueEffects;
    }
};

void HStandardPackage::addQunGenerals()
{
    // Shared skills are upgraded at their native definitions; both modes use the same V2 instance type.
    // Keep local heg_* implementations only where the current hegemony rules differ.
    General *huatuo = new General(this, "heg_huatuo", "qun", 3); // QUN 001
    huatuo->addSkill("jijiu");
    huatuo->addSkill("chuli");

    General *lvbu = new General(this, "heg_lvbu", "qun", 5); // QUN 002
    lvbu->addCompanion("heg_diaochan");
    lvbu->addSkill(new HWushuang);

    General *diaochan = new General(this, "heg_diaochan", "qun", 3, false); // QUN 003
    diaochan->addSkill("lijian");
    diaochan->addSkill("biyue");

    General *yuanshao = new General(this, "heg_yuanshao", "qun"); // QUN 004
    yuanshao->addCompanion("heg_yanliangwenchou");
    yuanshao->addSkill(new HLuanji);
    // Keep Luanji's response effect and phase-history cleanup with its owning general.
    yuanshao->addSkill(new HLuanjiDraw);
    yuanshao->addSkill(new HQunHistory);
    insertRelatedSkills("heg_luanji", "#heg_luanji-draw");
    insertRelatedSkills("heg_luanji", "#heg_qun-variant-history");

    General *yanliangwenchou = new General(this, "heg_yanliangwenchou", "qun"); // QUN 005
    yanliangwenchou->addSkill("shuangxiong");

    General *jiaxu = new General(this, "heg_jiaxu", "qun", 3); // QUN 007
    jiaxu->addSkill("wansha");
    jiaxu->addSkill("luanwu");
    jiaxu->addSkill(new HWeimu);

    General *pangde = new General(this, "heg_pangde", "qun"); // QUN 008
    pangde->addSkill("mashu");
    pangde->addSkill("tenyearjianchu");

    General *zhangjiao = new General(this, "heg_zhangjiao", "qun", 3); // QUN 010
    zhangjiao->addSkill("nosleiji");
    zhangjiao->addSkill("guidao");

    General *caiwenji = new General(this, "heg_caiwenji", "qun", 3, false); // QUN 012
    caiwenji->addSkill("beige");
    caiwenji->addSkill(new HDuanchang);

    General *mateng = new General(this, "heg_mateng", "qun"); // QUN 013
    mateng->addSkill("mashu");
    mateng->addSkill("xiongyi");

    General *kongrong = new General(this, "heg_kongrong", "qun", 3); // QUN 014
    kongrong->addSkill(new HMingshi);
    kongrong->addSkill("lirang");

    General *jiling = new General(this, "heg_jiling", "qun"); // QUN 015
    jiling->addSkill("shuangren");

    General *tianfeng = new General(this, "heg_tianfeng", "qun", 3); // QUN 016
    tianfeng->addSkill("sijian");
    tianfeng->addSkill(new HSuishi);

    General *panfeng = new General(this, "heg_panfeng", "qun"); // QUN 017
    panfeng->addSkill("kuangfu");

    General *zoushi = new General(this, "heg_zoushi", "qun", 3, false); // QUN 018
    zoushi->addSkill(new HHuoshui);
    zoushi->addSkill(new HQingcheng);

}
