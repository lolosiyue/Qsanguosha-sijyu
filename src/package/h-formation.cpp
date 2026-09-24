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
#include "h-formation.h"
#include "standard-generals.h"
#include "standard.h"
#include "maneuvering.h"
#include "h-standard-tricks.h"
#include "client.h"
#include "engine.h"
#include "structs.h"
#include "gamerule.h"
#include "settings.h"
#include "json.h"
#include "roomthread.h"
#include "room.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
#include <QSet>


namespace {
bool canSummonHegemonyArray(const Player *player, const QString &skillName,
                                    const QString &arrayType, int exactInstanceID)
{
    if (!player) return false;
    if (player->getAliveSiblings().size() < 3
        || player->hasFlag("Global_SummonFailed")) return false;

    bool canReveal = false;
    for (const SkillInstance &instance : player->getSkillInstances()) {
        if (instance.skillName != skillName
            || (exactInstanceID >= 0 && instance.instanceID != exactInstanceID)
            || player->isSkillInvalid(instance.skillName, instance.instanceID)) continue;
        const bool slotShown = instance.bindHead == 1 ? player->hasShownGeneral()
            : (instance.bindHead == 2 && player->hasShownGeneral2());
        // A visible flag is only authoritative after its bound general is
        // shown; concealed innate sources must still pass the slot contract.
        if (instance.bindHead == 0 || (slotShown ? instance.visible
            : player->canShowGeneral(instance.bindHead == 1 ? "h" : "d"))) {
            canReveal = true;
            break;
        }
    }
    if (!canReveal) return false;

    if (arrayType == QLatin1String("Siege")) {
        if (player->willBeFriendWith(player->getNextAlive())
            && player->willBeFriendWith(player->getLastAlive())) return false;
        if (!player->willBeFriendWith(player->getNextAlive())
            && !player->getNextAlive(2)->hasShownOneGeneral()
            && player->getNextAlive()->hasShownOneGeneral()) return true;
        if (!player->willBeFriendWith(player->getLastAlive()))
            return !player->getLastAlive(2)->hasShownOneGeneral()
                && player->getLastAlive()->hasShownOneGeneral();
    } else if (arrayType == QLatin1String("Formation")) {
        int count = player->aliveCount(false);
        int asked = count;
        for (int i = 1; i < count; ++i) {
            const Player *target = player->getNextAlive(i);
            if (player->isFriendWith(target)) continue;
            if (!target->hasShownOneGeneral()) return true;
            asked = i;
            break;
        }
        count -= asked;
        for (int i = 1; i < count; ++i) {
            const Player *target = player->getLastAlive(i);
            if (player->isFriendWith(target)) continue;
            return !target->hasShownOneGeneral();
        }
    }
    return false;
}

}

HArraySummon::HArraySummon(const QString &name, const QString &type)
    : ViewAsSkillV2(name), m_type(type)
{
}

bool HArraySummon::canActivate(const ActiveSkillRequest &request) const
{
    if (request.activationRef.isValid()
        && (!request.initiator
            || request.activationRef.ownerObjectName != request.initiator->objectName()
            || request.activationRef.key.skillName != objectName()))
        return false;
    return Config.EnableHegemony && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
        && canSummonHegemonyArray(request.initiator, objectName(), m_type,
            request.activationRef.isValid() ? request.activationRef.key.instanceID : -1);
}

ViewAsSkillV2::TargetMode HArraySummon::targetMode() const
{
    return NoTarget;
}

bool HArraySummon::targetsFeasible(const ActiveSkillRequest &,
                                   const QList<const Player *> &targets) const
{
    return targets.isEmpty();
}

ViewAsSkillV2::EffectFlow HArraySummon::effect(SkillContext &ctx) const
{
    // V2 has already authorized and revealed activationRef before this call.
    if (ctx.invoker && ctx.invoker->isAlive()) ctx.invoker->summonFriends(m_type);
    return ContinueEffects;
}

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

SkillInstanceRef shownFormationSource(ServerPlayer *owner, const QString &name)
{
    if (!ownsHegemonySkill(owner, name)) return {};
    for (const SkillInstance &instance : owner->getSkillInstances()) {
        if (instance.skillName != name || owner->isSkillInvalid(name, instance.instanceID)) continue;
        const bool shown = instance.bindHead == 1 ? owner->hasShownGeneral1()
            : instance.bindHead == 2 ? owner->hasShownGeneral2() : instance.visible;
        if (shown) return SkillInstanceRef(owner->objectName(), instance.key());
    }
    return {};
}

void projectFormationGrant(Room *room, ServerPlayer *target, const QString &skill,
                           const QString &provider, const SkillInstanceRef &source)
{
    // Keep a single aura grant, bound to its actual provider. Losing the provider
    // then uses the existing attachment registry's automatic recursive cleanup.
    bool hasOtherSource = false;
    bool hasGrant = false;
    const auto instances = target->getSkillInstances();
    for (const SkillInstance &instance : instances) {
        if (instance.skillName != skill) continue;
        if (instance.source == SourceAttached && instance.parentRef.key.skillName == provider) {
            const SkillInstanceRef ref(target->objectName(), instance.key());
            if (!source.isValid() || instance.parentRef != source) room->detachAttachedSkill(ref);
            else hasGrant = true;
        } else hasOtherSource = true;
    }
    if (source.isValid() && !hasGrant && !hasOtherSource)
        room->attachSkillToPlayer(target, skill, source);
}

}

// Selection proxies never move cards. The enclosing trigger owns payment/effect,
// and stores the result in its execution context rather than a player tag.
class HFormationSelection : public ViewAsSkillV2 {
public:
    HFormationSelection(const QString &name, const QString &pile = QString())
        : ViewAsSkillV2(name, 1), m_pile(pile) { expand_pile = pile; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern == "@@" + objectName();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using")
            || !request.selectedCardIds.isEmpty()) return false;
        const int id = card->getEffectiveId();
        if (id < 0) return false;
        if (!m_pile.isEmpty()) return request.initiator->getPile(m_pile).contains(id);
        return request.initiator->handCards().contains(id) || request.initiator->hasEquip(card);
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
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return m_pile.isEmpty() ? SelectTargets : NoTarget; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!m_pile.isEmpty() || !selected.isEmpty() || !target || !target->isAlive()
            || !cardSelectionFeasible(request)) return false;
        const auto *equip = qobject_cast<const EquipCard *>(
            Sanguosha->getCard(request.selectedCardIds.first())->getRealCard());
        return equip ? target->hasEquipArea(static_cast<int>(equip->location()))
                && !target->getEquip(static_cast<int>(equip->location()))
            : target != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return m_pile.isEmpty() ? targets.size() == 1 : targets.isEmpty();
    }
private:
    QString m_pile;
};

class HZiliang : public TriggerSkillV2 {
public:
    HZiliang() : TriggerSkillV2("heg_ziliang")
    {
        events << Damaged;
        relate_to_place = "deputy";
        view_as_skill = new HFormationSelection(objectName(), "field");
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (!owner->getPile("field").isEmpty() && (!Config.EnableHegemony || owner->isFriendWith(player)))
                result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!ctx.owner || !player || !ctx.original_data) return false;
        const QVariant previous = ctx.owner->getTag("ziliang_aidata");
        ctx.owner->setTag("ziliang_aidata", *ctx.original_data);
        const auto restore = qScopeGuard([&] { ctx.owner->setTag("ziliang_aidata", previous); });
        const Card *card = room->askForUseCard(ctx.owner, "@@heg_ziliang", "@heg_ziliang-give", -1, Card::MethodNone);
        if (!card || card->getSubcards().size() != 1) return false;
        ctx.extra_data = card->getSubcards().first();
        ctx.targets = {player};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (ctx.owner && target->isAlive() && ctx.owner->getPile("field").contains(id))
            room->obtainCard(target, id);
        return false;
    }
};

class HTuntian : public TriggerSkillV2 {
public:
    HTuntian() : TriggerSkillV2("heg_tuntian")
    {
        events << CardsMoveOneTime << FinishJudge;
        frequency = Frequent;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!ownsHegemonySkill(player, objectName())) return {};
        if (event == CardsMoveOneTime) {
            if (player->getPhase() != Player::NotActive) return {};
            const auto move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || (move.to == player
                && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))) return {};
            for (int i = 0; i < move.from_places.size(); ++i)
                if ((move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip)
                    && i < move.card_ids.size())
                    return TriggerList{{player, {objectName()}}};
            return {};
        }
        if (event == FinishJudge) {
            const JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge && judge->reason == objectName() && judge->isGood() && judge->card
                && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
                return TriggerList{{player, {objectName()}}};
        }
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        if (event == CardsMoveOneTime)
            return invokeHegemonySkill(this, room, ctx.owner, *ctx.original_data);
        const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->card) return false;
        ctx.extra_data = judge->card->getEffectiveId();
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "yes+no", *ctx.original_data, QString(),
            "@heg_tuntian-field:::" + Sanguosha->translate(judge->card->objectName()));
        if (ctx.choice == "yes") {
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = ctx.owner;
            log.arg = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName(), ctx.owner);
            room->notifySkillInvoked(ctx.owner, objectName());
        }
        return ctx.choice == "yes";
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardsMoveOneTime) {
            if (!ctx.owner || !ctx.owner->isAlive()) return false;
            JudgeStruct judge;
            judge.pattern = ".|heart";
            judge.good = false;
            judge.reason = objectName();
            judge.who = ctx.owner;
            room->judge(judge);
        } else if (ctx.owner && ctx.owner->isAlive()) {
            const int id = ctx.extra_data.toInt();
            if (id >= 0 && room->getCardPlace(id) == Player::PlaceJudge)
                ctx.owner->addToPile("field", id);
        }
        return false;
    }
};

class HTuntianDistance : public DistanceSkillV2 {
public:
    HTuntianDistance() : DistanceSkillV2("#heg_tuntian-dist")
    { setHolderSelector(CorrectSkill_Primary); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || !ctx.holder->hasShownSkill("heg_tuntian")) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(-ctx.holder->getPile("field").size());
    }
};

class HHuyuan : public TriggerSkillV2 {
public:
    HHuyuan() : TriggerSkillV2("heg_huyuan")
    {
        events << EventPhaseStart;
        view_as_skill = new HFormationSelection(objectName());
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return ownsHegemonySkill(player, objectName()) && player->getPhase() == Player::Finish
            && !player->isNude() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const CardUseStruct use = room->askForUseCardStruct(ctx.owner, "@@heg_huyuan", "@heg_huyuan-equip", -1, Card::MethodNone);
        if (!use.card || use.card->getSubcards().size() != 1 || use.to.size() != 1) return false;
        ctx.extra_data = use.card->getSubcards().first();
        ctx.targets = use.to;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.targets.size() != 1 || !ctx.targets.first()->isAlive()) return false;
        const int id = ctx.extra_data.toInt();
        const Card *card = Sanguosha->getCard(id);
        const auto *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        ServerPlayer *target = ctx.targets.first();
        if (room->getCardOwner(id) != ctx.owner
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        if (equip) {
            if (!target->hasEquipArea(static_cast<int>(equip->location()))
                || target->getEquip(static_cast<int>(equip->location()))) return false;
            room->moveCardTo(card, ctx.owner, target, Player::PlaceEquip,
                CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), target->objectName(), objectName(), QString()));
        } else {
            if (target == ctx.owner) return false;
            room->obtainCard(target, card, CardMoveReason(CardMoveReason::S_REASON_GIVE,
                ctx.owner->objectName(), target->objectName(), objectName(), QString()), false);
        }
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const auto *equip = qobject_cast<const EquipCard *>(
            Sanguosha->getCard(ctx.extra_data.toInt())->getRealCard());
        if (!equip) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getAllPlayers())
            if (target->distanceTo(other) == 1 && ctx.owner->canDiscard(other, "ej")) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *chosen = room->askForPlayerChosen(ctx.owner, candidates, objectName(),
            "@heg_huyuan-discard:" + target->objectName(), true);
        if (chosen && ctx.owner->canDiscard(chosen, "ej"))
            room->throwCard(room->askForCardChosen(ctx.owner, chosen, "ej", objectName(), false, Card::MethodDiscard), chosen, ctx.owner);
        return false;
    }
};

class HHeyiSelection : public HArraySummon {
public:
    HHeyiSelection() : HArraySummon("heg_heyi", "Formation") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return Config.EnableHegemony ? HArraySummon::canActivate(request)
            : request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                && request.pattern == "@@heg_heyi";
    }
    TargetMode targetMode() const override { return Config.EnableHegemony ? NoTarget : SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *target) const override
    { return !Config.EnableHegemony && target && target->isAlive() && !selected.contains(target); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (Config.EnableHegemony) return targets.isEmpty();
        if (targets.size() < 2 || !targets.contains(request.initiator)) return false;
        if (QSet<const Player *>(targets.cbegin(), targets.cend()).size() != targets.size()) return false;
        // A contiguous set on the living seating ring has at most one outgoing edge.
        int boundaries = 0;
        for (const Player *target : targets) {
            if (!target || !target->isAlive()) return false;
            if (!targets.contains(target->getNextAlive())) ++boundaries;
        }
        return boundaries <= 1;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { return Config.EnableHegemony ? HArraySummon::effect(ctx) : ContinueEffects; }
};

class HHeyi : public TriggerSkillV2 {
public:
    HHeyi() : TriggerSkillV2("heg_heyi")
    {
        events << GeneralShown << GeneralHidden << GeneralRemoved << Death << RemoveStateChanged
               << EventAcquireSkill << EventLoseSkill << EventPhaseChanging;
        view_as_skill = new HHeyiSelection;
    }
    bool canPreshow() const override { return false; }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!Config.EnableHegemony && ctx.owner == player && ctx.original_data
            && (event == Death || (event == EventPhaseChanging
                && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive)))
            ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients");
        refresh(room);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return !Config.EnableHegemony && event == EventPhaseChanging && ownsHegemonySkill(player, objectName())
            && data.value<PhaseChangeStruct>().to == Player::NotActive
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const auto use = room->askForUseCardStruct(ctx.owner, "@@heg_heyi", "@heg_heyi", -1, Card::MethodNone);
        if (!use.card || use.to.size() < 2 || !use.to.contains(ctx.owner)) return false;
        ctx.targets = use.to;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList recipients;
        for (ServerPlayer *target : ctx.targets)
            if (target != ctx.owner && target->isAlive()) recipients << target->objectName();
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "recipients", recipients);
        refresh(room);
        return false;
    }
private:
    void refresh(Room *room) const
    {
        const auto owners = room->findPlayersBySkillName(objectName());
        for (ServerPlayer *target : room->getPlayers()) {
            SkillInstanceRef source;
            if (target->isAlive() && (!Config.EnableHegemony || room->alivePlayerCount() >= 4)) {
                for (ServerPlayer *owner : owners) {
                    if (owner == target) continue;
                    if (Config.EnableHegemony) {
                        if (!owner->inFormationRalation(target)) continue;
                        source = shownFormationSource(owner, objectName());
                    } else {
                        for (int id : owner->getValidSkillInstanceIds(objectName())) {
                            if (owner->getSkillInstanceStateValue(objectName(), id, "recipients").toStringList().contains(target->objectName())) {
                                source = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
                                break;
                            }
                        }
                    }
                    if (source.isValid()) break;
                }
            }
            projectFormationGrant(room, target, Config.EnableHegemony ? "heg_feiying" : "feiying", objectName(), source);
        }
    }
};

class HFeiying : public DistanceSkillV2 {
public:
    HFeiying() : DistanceSkillV2("heg_feiying")
    {
        frequency = Compulsory;
        setBaseAmount(1);
        setHolderSelector(CorrectSkill_Secondary);
    }
};

class HTianfu : public TriggerSkillV2 {
public:
    HTianfu() : TriggerSkillV2("heg_tianfu")
    {
        events << EventPhaseStart << EventPhaseChanging << Death << EventLoseSkill << EventAcquireSkill
               << GeneralShown << GeneralHidden << GeneralRemoved << RemoveStateChanged;
        relate_to_place = "head";
        view_as_skill = new HArraySummon(objectName(), "Formation");
    }
    bool canPreshow() const override { return false; }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *current = room->getCurrent();
        const bool ending = event == EventPhaseChanging && player == current && ctx.original_data
            && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive;
        if (!Config.EnableHegemony) {
            const QString activeTurn = ctx.owner
                ? ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "active_turn").toString() : QString();
            const bool turnEnds = player && player->objectName() == activeTurn && ctx.original_data
                && (event == Death || (event == EventPhaseChanging
                    && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive));
            if (ctx.owner && (turnEnds || (event == Death && player == ctx.owner)))
                ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "active_turn");
            refreshIdentity(room);
            return;
        }
        for (ServerPlayer *owner : room->getPlayers()) {
            const bool active = !ending && current && current->isAlive() && current->getPhase() != Player::NotActive
                && room->alivePlayerCount() >= 4 && ownsHegemonySkill(owner, objectName())
                && owner->hasShownSkill(objectName()) && owner->inFormationRalation(current);
            projectFormationGrant(room, owner, "kanpo", objectName(),
                active ? shownFormationSource(owner, objectName()) : SkillInstanceRef());
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (Config.EnableHegemony || event != EventPhaseStart || !player || !player->isAlive()
            || player->getPhase() != Player::RoundStart) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner == player || owner->isAdjacentTo(player)) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The current player decides; the source remains the Tianfu owner's instance.
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->askForSkillInvoke(this, ctx.owner, false)) return false;
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner && ctx.invoker && ctx.invoker->isAlive()) {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "active_turn", ctx.invoker->objectName());
            refreshIdentity(room);
        }
        return false;
    }
private:
    void refreshIdentity(Room *room) const
    {
        for (ServerPlayer *owner : room->getPlayers()) {
            SkillInstanceRef source;
            if (ownsHegemonySkill(owner, objectName())) {
                for (int id : owner->getValidSkillInstanceIds(objectName())) {
                    if (!owner->getSkillInstanceStateValue(objectName(), id, "active_turn").toString().isEmpty()) {
                        source = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
                        break;
                    }
                }
            }
            projectFormationGrant(room, owner, "kanpo", objectName(), source);
        }
    }
};

class HShengxi : public TriggerSkillV2 {
public:
    HShengxi() : TriggerSkillV2("heg_shengxi")
    {
        events << DamageDone << EventPhaseStart << EventPhaseEnd;
        frequency = Frequent;
        m_baseAmount = 2;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner) return;
        if (event == EventPhaseStart && ctx.owner == player && player->getPhase() == Player::Play)
            ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "damaged");
        if (event == DamageDone && ctx.original_data) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (damage.from == ctx.owner && ctx.owner->getPhase() == Player::Play)
                ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "damaged", true);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !ownsHegemonySkill(player, objectName()) || player->getPhase() != Player::Play) return {};
        QStringList names;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (!player->getSkillInstanceStateValue(objectName(), id, "damaged", false).toBool())
                names << SkillInstanceUtils::formatName(objectName(), id);
        return names.isEmpty() ? TriggerList() : TriggerList{{player, names}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return invokeHegemonySkill(this, room, ctx.owner); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner && ctx.owner->isAlive()) ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class HShoucheng : public TriggerSkillV2 {
public:
    HShoucheng() : TriggerSkillV2("heg_shoucheng") { events << CardsMoveOneTime; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const auto move = data.value<CardsMoveOneTimeStruct>();
        TriggerList result;
        if (!move.from || !move.from->isAlive() || move.from->getPhase() != Player::NotActive
            || !move.from_places.contains(Player::PlaceHand) || !move.is_last_handcard) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (!Config.EnableHegemony || owner->isFriendWith(move.from) || owner->willBeFriendWith(move.from))
                result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        ServerPlayer *target = qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (!target || !invokeHegemonySkill(this, room, ctx.owner, *ctx.original_data)) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive()) return false;
        // The owner invokes Shoucheng; in identity mode the recipient may refuse the draw.
        if (!Config.EnableHegemony && room->askForChoice(target, objectName(), "accept+reject",
            QVariant::fromValue(ctx.owner)) != "accept") return false;
        if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class HShangyi : public ViewAsSkillV2 {
public:
    HShangyi() : ViewAsSkillV2("heg_shangyi") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
        && (!Config.EnableHegemony || !request.initiator->isKongcheng()); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return selected.isEmpty() && target && target->isAlive() && target != request.initiator
        && (!Config.EnableHegemony || !target->isKongcheng() || !target->hasShownAllGenerals()); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HShangyiCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.invoker || ctx.targets.size() != 1 || !ctx.targets.first()->isAlive()) return false;
        if (!ctx.invoker->isKongcheng()) room->showAllCards(ctx.invoker, ctx.targets.first());
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || !target->isAlive()
            || (Config.EnableHegemony && target->isKongcheng() && target->hasShownAllGenerals())) return ContinueEffects;

    Room *room = ctx.invoker->getRoom();

    QStringList choices;
    if (!target->isKongcheng())
        choices << "handcards";
    if (!Config.EnableHegemony)
        choices << "role";
    else if (!target->hasShownAllGenerals())
        choices << "hidden_general";

    QString choice = room->askForChoice(ctx.invoker, "heg_shangyi",
        choices.join("+"), QVariant::fromValue(target));
    LogMessage log;
    log.type = "#KnownBothView";
    log.from = ctx.invoker;
    log.to << target;
    log.arg = choice;
    room->sendLog(log, room->getOtherPlayers(ctx.invoker, true));

    if (choice == "handcards") {
        room->broadcastSkillInvoke("heg_shangyi", 1, ctx.invoker);
        QList<int> blacks;
        foreach (int card_id, target->handCards()){
            if (Sanguosha->getCard(card_id)->isBlack())
                blacks << card_id;
        }
        int to_discard = room->doGongxin(ctx.invoker, target, blacks, "heg_shangyi");
        if (to_discard == -1) return ContinueEffects;

        ctx.invoker->removeTag("heg_shangyi");
        room->throwCard(to_discard, target, ctx.invoker);
    } else if (choice == "role") {
        // Role information is sent only to the viewer, never revealed room-wide.
        JsonArray arg;
        arg << target->objectName() << target->getRole();
        room->doNotify(ctx.invoker, QSanProtocol::S_COMMAND_SET_EMOTION, arg);
        LogMessage roleLog;
        roleLog.type = "$ViewRole";
        roleLog.from = ctx.invoker;
        roleLog.to << target;
        roleLog.arg = target->getRole();
        room->sendLog(roleLog, ctx.invoker);
    } else {
        room->broadcastSkillInvoke("heg_shangyi", 2, ctx.invoker);
        QStringList list;
        if (!target->hasShownGeneral1())
            list << target->getActualGeneral1Name();
        if (!target->hasShownGeneral2())
            list << target->getActualGeneral2Name();
        foreach (const QString &name, list) {
            LogMessage log;
            log.type = "$KnownBothViewGeneral";
            log.from = ctx.invoker;
            log.to << target;
            QString position = target->getActualGeneral1Name() == name ? "head_general" : "deputy_general";
            log.arg = Sanguosha->translate(position);
            log.arg2 = name;
            room->sendLog(log, ctx.invoker);
        }
        JsonArray arg;
        arg << "heg_shangyi";
        arg << JsonUtils::toJsonArray(list);
        room->doNotify(ctx.invoker, QSanProtocol::S_COMMAND_VIEW_GENERALS, arg);
    }

        return ContinueEffects;
    }
};

class HNiaoxiang : public TriggerSkillV2 {
public:
    HNiaoxiang() : TriggerSkillV2("heg_niaoxiang")
    {
        events << TargetSpecified << TargetConfirmed;
        frequency = Compulsory;
        m_baseAmount = 2;
        view_as_skill = new HArraySummon(objectName(), "Siege");
    }
    bool canPreshow() const override { return false; }
    Frequency getFrequency(const Player *) const override { return Config.EnableHegemony ? Compulsory : NotFrequent; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const auto use = data.value<CardUseStruct>();
        TriggerList result;
        if (!player || !use.from || !use.card || !use.card->isKindOf("Slash")) return result;
        if (Config.EnableHegemony ? event != TargetSpecified || room->alivePlayerCount() < 4
                                 : event != TargetConfirmed || !use.to.contains(player)) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            if (Config.EnableHegemony && !owner->hasShownSkill(objectName())) continue;
            QStringList targets;
            for (ServerPlayer *target : use.to)
                if (Config.EnableHegemony ? use.from->inSiegeRelation(owner, target)
                    : target == player && owner->isAdjacentTo(target) && use.from->isAdjacentTo(target))
                    targets << target->objectName();
            if (!targets.isEmpty()) result.insert(owner, {objectName() + "->" + targets.join("+")});
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && !ctx.targets.isEmpty() && (Config.EnableHegemony ? ctx.owner->hasShownSkill(objectName())
            : invokeHegemonySkill(this, room, ctx.owner, QVariant::fromValue(ctx.targets.first())));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data) return false;
        const auto use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || !use.card) return false;
        const int index = use.to.indexOf(target);
        const QString key = "Jink_" + use.card->toString();
        QVariantList jinks = use.from->getTag(key).toList();
        if (index >= 0 && index < jinks.size() && jinks.at(index).toInt() == 1) {
            jinks[index] = getEffectiveAmount(ctx);
            use.from->setTag(key, jinks);
            if (Config.EnableHegemony) room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true);
        }
        return false;
    }
};

class HYicheng : public TriggerSkillV2 {
public:
    HYicheng() : TriggerSkillV2("heg_yicheng") { events << TargetConfirmed << TargetSpecified; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const auto use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !use.card || !use.card->isKindOf("Slash")) return result;
        if (event == TargetConfirmed && !use.to.contains(player)) return result;
        if (event == TargetSpecified && (!Config.EnableHegemony || use.from != player || use.to.isEmpty())) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            if (owner == player) {
                // The skill owner can use the ordinary trigger while still hidden.
                result.insert(owner, {objectName()});
            } else if (Config.EnableHegemony
                ? owner->hasShownSkill(objectName()) && player->inFormationRalation(owner)
                : event == TargetConfirmed && owner->willBeFriendWith(player)) {
                result.insert(owner, {objectName()});
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !ctx.owner) return false;
        if (Config.EnableHegemony && player != ctx.owner) {
            const QString choice = room->askForChoice(player, objectName(), "yes+no", QVariant::fromValue(ctx.owner), QString(),
                "@heg_yicheng-ally:" + ctx.owner->objectName());
            if (choice != "yes") return false;
            LogMessage log;
            log.type = "#InvokeOthersSkill";
            log.from = player;
            log.to << ctx.owner;
            log.arg = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName(), ctx.owner);
            room->notifySkillInvoked(ctx.owner, objectName());
        } else if (!invokeHegemonySkill(this, room, ctx.owner, QVariant::fromValue(player))) return false;
        ctx.targets = {player};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        if (target->isAlive() && target->canDiscard(target, "he")) room->askForDiscard(target, objectName(), 1, 1, false, true);
        return false;
    }
};

class HQianhuan : public TriggerSkillV2 {
public:
    HQianhuan() : TriggerSkillV2("heg_qianhuan")
    {
        events << Damaged << TargetConfirming << BeforeCardsMoveBatch;
        view_as_skill = new HFormationSelection(objectName(), "sorcery");
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        const auto use = data.value<CardUseStruct>();
        if (event == TargetConfirming && (!use.card || (!use.card->isKindOf("BasicCard")
            && !(Config.EnableHegemony ? use.card->isNDTrick() : use.card->isKindOf("TrickCard")))
            || use.to.size() != 1 || use.to.first() != player)) return result;
        CardsMoveOneTimeStruct delayedMove;
        if (event == BeforeCardsMoveBatch) {
            if (!Config.EnableHegemony) return result;
            const QVariantList moves = data.toList();
            if (moves.size() != 1) return result;
            delayedMove = moves.first().value<CardsMoveOneTimeStruct>();
            if (!delayedMove.to || delayedMove.to_place != Player::PlaceDelayedTrick || delayedMove.card_ids.size() != 1)
                return result;
            // The batch is dispatched once per seat; only its current seat owns this cancellation.
            if (!player->isFriendWith(delayedMove.to)
                || !player->hasSkill(objectName()) || player->getPile("sorcery").isEmpty()) return result;
            return TriggerList{{player, {objectName()}}};
        }
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if ((!Config.EnableHegemony || owner->isFriendWith(player))
                && (event == Damaged ? (!Config.EnableHegemony || (!owner->isNude() && owner->getPile("sorcery").size() < 4))
                                     : !owner->getPile("sorcery").isEmpty()))
                result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!ctx.owner || !ctx.original_data || !player) return false;
        if (event == Damaged) {
            if (Config.EnableHegemony) {
                QStringList availableSuits{"spade", "club", "heart", "diamond"};
                for (int id : ctx.owner->getPile("sorcery")) {
                    switch (Sanguosha->getCard(id)->getSuit()) {
                    case Card::Spade: availableSuits.removeAll("spade"); break;
                    case Card::Club: availableSuits.removeAll("club"); break;
                    case Card::Heart: availableSuits.removeAll("heart"); break;
                    case Card::Diamond: availableSuits.removeAll("diamond"); break;
                    default: break;
                    }
                }
                if (availableSuits.isEmpty()) return false;
                const Card *chosen = room->askForCard(ctx.owner,
                    QString(".|%1|.|he").arg(availableSuits.join(",")), "@heg_qianhuan-put",
                    *ctx.original_data, Card::MethodNone);
                if (!chosen) return false;
                ctx.extra_data = chosen->getEffectiveId();
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = ctx.owner;
                log.arg = objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), ctx.owner);
                room->notifySkillInvoked(ctx.owner, objectName());
                return true;
            }
            // In identity mode the surviving damaged player offers the top card.
            if (!player->isAlive() || !player->askForSkillInvoke(this, ctx.owner, false)) return false;
            room->broadcastSkillInvoke(objectName(), ctx.owner);
            return true;
        }
        QString targetName;
        QString cardName;
        if (event == TargetConfirming) {
            const auto use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || use.to.size() != 1) return false;
            targetName = use.to.first()->objectName();
            cardName = use.card->objectName();
        } else {
            const QVariantList moves = ctx.original_data->toList();
            if (moves.size() != 1) return false;
            const auto move = moves.first().value<CardsMoveOneTimeStruct>();
            if (!move.to || move.to_place != Player::PlaceDelayedTrick || move.card_ids.size() != 1) return false;
            targetName = move.to->objectName();
            cardName = Sanguosha->getCard(move.card_ids.first())->objectName();
        }
        const QVariant previous = ctx.owner->getTag("qianhuan_data");
        ctx.owner->setTag("qianhuan_data", *ctx.original_data);
        const auto restore = qScopeGuard([&] { ctx.owner->setTag("qianhuan_data", previous); });
        const Card *card = room->askForUseCard(ctx.owner, "@@heg_qianhuan",
            "@heg_qianhuan-cancel::" + targetName + ":" + cardName, -1, Card::MethodNone);
        if (!card || card->getSubcards().size() != 1) return false;
        ctx.extra_data = card->getSubcards().first();
        ctx.targets = {player};
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Damaged) {
            if (!Config.EnableHegemony) return true;
            const int id = ctx.extra_data.toInt();
            if (!ctx.owner || id < 0 || room->getCardOwner(id) != ctx.owner
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || ctx.owner->getPile("sorcery").size() >= 4) return false;
            const Card::Suit suit = Sanguosha->getCard(id)->getSuit();
            for (int other : ctx.owner->getPile("sorcery"))
                if (Sanguosha->getCard(other)->getSuit() == suit) return false;
            ctx.owner->addToPile("sorcery", id);
            return true;
        }
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || !ctx.owner->getPile("sorcery").contains(id)) return false;
        room->throwCard(Sanguosha->getCard(id),
            CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), QString()), nullptr);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == BeforeCardsMoveBatch && ctx.original_data) {
            QVariantList moves = ctx.original_data->toList();
            if (moves.size() != 1) return false;
            auto move = moves.first().value<CardsMoveOneTimeStruct>();
            if (!move.to || move.to_place != Player::PlaceDelayedTrick || move.card_ids.size() != 1) return false;
            // Keep origin and pending-pile fields from the copied movement intact.
            move.to = nullptr;
            move.to_place = Player::DiscardPile;
            move.reason = CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, QString());
            moves[0] = QVariant::fromValue(move);
            *ctx.original_data = QVariant::fromValue(moves);
            return false;
        }
        if (event != Damaged || Config.EnableHegemony || !ctx.owner || !ctx.owner->isAlive()) return false;
        const int id = room->drawCard();
        bool duplicate = false;
        for (int other : ctx.owner->getPile("sorcery"))
            if (Sanguosha->getCard(id)->getSuit() == Sanguosha->getCard(other)->getSuit()) duplicate = true;
        ctx.owner->addToPile("sorcery", id);
        if (duplicate && ctx.owner->getPile("sorcery").contains(id))
            room->throwCard(Sanguosha->getCard(id),
                CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), QString()), nullptr);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == TargetConfirming && ctx.original_data) {
            auto use = ctx.original_data->value<CardUseStruct>();
            if (use.to.size() == 1 && use.to.contains(target)) {
                room->cancelTarget(use, target);
                *ctx.original_data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

class HZhangwu : public TriggerSkillV2 {
public:
    HZhangwu() : TriggerSkillV2("heg_zhangwu")
    { events << CardsMoveOneTime << BeforeCardsMove; frequency = Compulsory; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const auto move = data.value<CardsMoveOneTimeStruct>();
        TriggerList result;
        if (move.to_place == Player::DrawPileBottom) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            for (int id : move.card_ids) {
                if (!Sanguosha->getCard(id)->isKindOf("DragonPhoenix")) continue;
                const int index = move.card_ids.indexOf(id);
                const bool leaving = move.from == owner && index < move.from_places.size()
                    && (move.from_places.at(index) == Player::PlaceHand || move.from_places.at(index) == Player::PlaceEquip)
                    && (move.to != owner || (move.to_place != Player::PlaceHand && move.to_place != Player::PlaceEquip));
                if ((event == BeforeCardsMove && leaving) || (event == CardsMoveOneTime
                    && (move.to_place == Player::DiscardPile || (move.to_place == Player::PlaceEquip && move.to != owner)))) {
                    result.insert(owner, {objectName()});
                    break;
                }
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && (ctx.owner->hasShownSkill(objectName()) || invokeHegemonySkill(this, room, ctx.owner)); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        auto move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        for (int id : move.card_ids) {
            const Card *card = Sanguosha->getCard(id);
            if (!card->isKindOf("DragonPhoenix")) continue;
            if (event == CardsMoveOneTime) {
                if (room->getCardPlace(id) == Player::DiscardPile || (room->getCardPlace(id) == Player::PlaceEquip
                    && room->getCardOwner(id) != ctx.owner)) ctx.owner->obtainCard(card);
            } else if (room->getCardOwner(id) == ctx.owner
                       && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) {
                room->showCard(ctx.owner, id);
                // Keep all parallel move metadata aligned; no shared removal flag.
                move.removeCardIds({id});
                *ctx.original_data = QVariant::fromValue(move);
                room->moveCardTo(card, nullptr, Player::DrawPileBottom);
                if (ctx.owner->isAlive() && room->getCardPlace(id) == Player::DrawPile)
                    ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
            }
            break;
        }
        return false;
    }
};

class HShouyue : public TriggerSkillV2 {
public:
    HShouyue() : TriggerSkillV2("heg_shouyue$") { frequency = Compulsory; }
    bool canPreshow() const override { return false; }
};

class HJizhao : public TriggerSkillV2 {
public:
    HJizhao() : TriggerSkillV2("heg_jizhao")
    { events << AskForPeaches; frequency = Limited; limit_mark = "@heg_jizhao"; }
    bool canPreshow() const override { return false; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return ownsHegemonySkill(player, objectName()) && player->getHp() <= 0 && player->getMark(limit_mark) > 0
            && data.value<DyingStruct>().who == player ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.original_data && invokeHegemonySkill(this, room, ctx.owner, *ctx.original_data); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner->getMark(limit_mark) < 1) return false;
        ctx.owner->loseMark(limit_mark);
        room->doSuperLightbox("heg_lord_liubei", objectName());
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !owner->isAlive()) return false;
        if (owner->getHandcardNum() < owner->getMaxHp()) owner->drawCards(owner->getMaxHp() - owner->getHandcardNum(), objectName());
        if (!owner->isAlive()) return false;
        if (owner->getHp() < 2) room->recover(owner, RecoverStruct(owner, nullptr, 2 - owner->getHp(), objectName()));
        if (owner->isAlive()) room->handleAcquireDetachSkills(owner, "-heg_shouyue|heg_rende");
        return false;
    }
};

HFormationPackage::HFormationPackage()
    : Package("heg_formation")
{
    General *dengai = new General(this, "heg_dengai", "wei"); // WEI 015
    // Tuntian is a shared identity skill; keep one registered definition in both modes.
    dengai->addSkill("tuntian");
    dengai->addSkill("jixi");
    dengai->setHeadMaxHpAdjustedValue(-1);
    dengai->addSkill(new HZiliang);

    General *caohong = new General(this, "heg_caohong", "wei"); // WEI 018
    caohong->addCompanion("heg_caoren");
    caohong->addSkill(new HHuyuan);
    caohong->addSkill(new HHeyi);

    General *jiangwei = new General(this, "heg_jiangwei", "shu"); // SHU 012 G
    jiangwei->addSkill("tiaoxin");
    jiangwei->addSkill(new Guanxing("heg_yizhi"));
    jiangwei->setDeputyMaxHpAdjustedValue(-1);
    jiangwei->addSkill(new HTianfu);

    General *jiangwanfeiyi = new General(this, "heg_jiangwanfeiyi", "shu", 3); // SHU 018
    jiangwanfeiyi->addSkill(new HShengxi);
    jiangwanfeiyi->addSkill(new HShoucheng);

    General *jiangqin = new General(this, "heg_jiangqin", "wu"); // WU 017
    jiangqin->addCompanion("heg_zhoutai");
    jiangqin->addSkill(new HShangyi);
    jiangqin->addSkill(new HNiaoxiang);

    General *xusheng = new General(this, "heg_xusheng", "wu"); // WU 020
    xusheng->addCompanion("heg_dingfeng");
    xusheng->addSkill(new HYicheng);

    General *yuji = new General(this, "heg_yuji", "qun", 3); // QUN 011 G
    yuji->addSkill(new HQianhuan);

    General *hetaihou = new General(this, "heg_hetaihou", "qun", 3, false); // QUN 020
    hetaihou->addSkill("zhendu");
    hetaihou->addSkill("qiluan");

    General *liubei = new General(this, "heg_lord_liubei$", "shu", 4, true, true);
    liubei->addSkill(new HZhangwu);
    liubei->addSkill(new HShouyue);
    liubei->addSkill(new HJizhao);

    skills << new HFeiying;
}

ADD_PACKAGE(HFormation)


HDragonPhoenix::HDragonPhoenix(Suit suit, int number) : Weapon(suit, number, 2)
{
    setObjectName("DragonPhoenix");
}

class HDragonPhoenixSkill : public WeaponSkillV2{
public:
    HDragonPhoenixSkill() : WeaponSkillV2("heg_DragonPhoenix", "DragonPhoenix") {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        CardUseStruct use = data.value<CardUseStruct>();
        if (WeaponSkillV2::triggerable(player) && use.card != NULL && use.card->isKindOf("Slash")) {
            QStringList targets;
            foreach (ServerPlayer *to, use.to) {
                if (player->hasWeapon("DragonPhoenix", to) && player->canDiscard(to, "he"))
                    targets << to->objectName();
            }
            if (!targets.isEmpty())
                // V2 dispatch expands the ordered target syntax and stores the
                // selected target in ctx.targets for each resolution.
                result.insert(player, QStringList(objectName() + "->" + targets.join("+")));
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (ctx.targets.isEmpty() || !player->canDiscard(ctx.targets.first(), "he"))
            return false;
        ServerPlayer *target = ctx.targets.first();
        ctx.preferredTarget = target;
        ctx.manual_effect = true;
        if (player->askForSkillInvoke(this, QVariant::fromValue(target))) {
            room->setEmotion(player, "weapon/dragonphoenix");
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        Q_UNUSED(player);
        if (ctx.targets.isEmpty())
            return false;
        room->askForDiscard(ctx.targets.first(), objectName(), 1, 1, false, true,
                            "@dragonphoenix-discard");
        return false;
    }
};

class HDragonPhoenixSkill2 : public WeaponSkillV2{
public:
    HDragonPhoenixSkill2() : WeaponSkillV2("#heg_DragonPhoenix", "DragonPhoenix"){
        events << BuryVictim;
    }

    int getPriority(TriggerEvent) const override {
        return -4;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *target, QVariant &) const override {
        TriggerList result;
        if (Config.EnableHegemony)
            return result;
        if (!target)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if (owner->hasWeapon("DragonPhoenix", target)) {
                result[owner] << objectName();
                break;
            }
        }
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (!Config.EnableHegemony)
            return false;

        ServerPlayer *victim = ctx.invoker;
        if (!victim || !ctx.original_data)
            return false;

        ServerPlayer *dfowner = player;
        if (dfowner == NULL || dfowner->getRole() == "careerist" || !dfowner->hasShownOneGeneral())
            return false;

        DeathStruct death = ctx.original_data->value<DeathStruct>();
        DamageStruct *damage = death.damage;
        if (!damage || !damage->from || damage->from != dfowner) return false;
        if (!damage->card || !damage->card->isKindOf("Slash")) return false;

        QStringList kingdom_list = Sanguosha->getKingdoms();
        kingdom_list << "careerist";
        bool broken = false;
        int n = dfowner->getPlayerNumWithSameKingdom("DragonPhoenix", QString(), MaxCardsType::Min); // could be canceled later
        foreach (const QString &kingdom, Sanguosha->getKingdoms()) {
            if (kingdom == "god") continue;
            if (dfowner->getRole() == "careerist") {
                if (kingdom == "careerist")
                    continue;
            } else if (dfowner->getKingdom() == kingdom)
                continue;
            int other_num = dfowner->getPlayerNumWithSameKingdom("DragonPhoenix", kingdom, MaxCardsType::Normal);
            if (other_num > 0 && other_num < n) {
                broken = true;
                break;
            }
        }

        if (broken)
            return false;

        QStringList generals = room->getLimitedGeneralNames();
        QStringList avaliable_generals;

        foreach (const QString &general, generals){
            if (Sanguosha->getGeneral(general)->getKingdom() != dfowner->getKingdom())
                continue;

            bool continue_flag = false;
            foreach (ServerPlayer *p, room->getAlivePlayers()){
                QStringList generals_of_player = room->getTag(p->objectName()).toStringList();
                if (generals_of_player.contains(general)){
                    continue_flag = true;
                    break;
                }
            }

            if (continue_flag)
                continue;

            avaliable_generals << general;
        }

        if (avaliable_generals.isEmpty())
            return false;

        bool invoke = false;
        {
            const int aidelay = Config.AIDelay;
            const auto restoreDelay = qScopeGuard([aidelay] { Config.AIDelay = aidelay; });
            Config.AIDelay = 0;
            invoke = room->askForSkillInvoke(dfowner, "heg_DragonPhoenix", *ctx.original_data)
                && room->askForSkillInvoke(victim, "heg_DragonPhoenix", "revive");
        }
        if (invoke){
            room->setEmotion(dfowner, "weapon/dragonphoenix");
            room->setPlayerProperty(victim, "Duanchang", QVariant());
            QString to_change = room->askForGeneral(victim, avaliable_generals, QString(), true, "DragonPhoenix", dfowner->getKingdom());

            if (!to_change.isEmpty()){
                bool changed = false;
                {
                    // changeHero owns exact-instance retirement and attachment;
                    // the scope restores this internal flag on trigger exceptions.
                    const bool alreadyReviving = victim->hasFlag("OriginalHegemonyDragonPhoenixReviving");
                    const QVariant previousResult = victim->getTag("OriginalHegemonyDragonPhoenixResult");
                    victim->setFlags("OriginalHegemonyDragonPhoenixReviving");
                    victim->removeTag("OriginalHegemonyDragonPhoenixResult");
                    const auto restoreReviving = qScopeGuard([victim, alreadyReviving, previousResult] {
                        if (!alreadyReviving)
                            victim->setFlags("-OriginalHegemonyDragonPhoenixReviving");
                        if (previousResult.isValid())
                            victim->setTag("OriginalHegemonyDragonPhoenixResult", previousResult);
                        else
                            victim->removeTag("OriginalHegemonyDragonPhoenixResult");
                    });
                    room->changeHero(victim, to_change, false, true, false, true);
                    changed = victim->getTag("OriginalHegemonyDragonPhoenixResult").toBool();
                }
                if (!changed || victim->getActualGeneral1Name() != to_change
                    || !victim->getActualGeneral2Name().startsWith(QStringLiteral("sujiang")))
                    return false;

                room->revivePlayer(victim);
                if (victim->isDead())
                    return false;
                room->setPlayerFlag(victim, "Global_DFDebut");

                room->setPlayerProperty(victim, "hp", 2);

                victim->setChained(false);
                room->broadcastProperty(victim, "chained");

                victim->setFaceUp(true);
                room->broadcastProperty(victim, "faceup");

                room->setPlayerProperty(victim, "kingdom", dfowner->getKingdom());
                room->setPlayerProperty(victim, "role", HegemonyRule::getMappedRole(dfowner->getKingdom()));

                foreach (const Skill *skill, Sanguosha->getGeneral(to_change)->getSkillList()) {
                    // Revived generals occupy the head position.
                    if (skill->relateToPlace(false)) continue;
                    if (skill->getFrequency() == Skill::Limited && !skill->getLimitMark().isEmpty())
                        room->setPlayerMark(victim, skill->getLimitMark(), 1);
                }

                victim->drawCards(1);
            }
        }
        return false;
    }
};


HFormationEquipPackage::HFormationEquipPackage() : Package("heg_formation_equip", CardPack){
    HDragonPhoenix *dp = new HDragonPhoenix();
    dp->setParent(this);

    skills << new HDragonPhoenixSkill << new HDragonPhoenixSkill2;
    insertRelatedSkills("heg_DragonPhoenix", "#heg_DragonPhoenix");
}

ADD_PACKAGE(HFormationEquip)
