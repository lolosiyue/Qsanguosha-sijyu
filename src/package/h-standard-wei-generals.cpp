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

#include "h-standard-wei-generals.h"
#include "general.h"
#include "protocol.h"
#include "room.h"
#include "card.h"
#include "serverplayer.h"
#include "skill.h"
#include "engine.h"
#include "standard.h"
#include "skill-instance-utils.h"
#include "roomthread.h"

// Rules in this section follow xxyheaven cf61c15, not the older standard set.
class HWeiDamagedSkill : public TriggerSkillV2 {
public:
    explicit HWeiDamagedSkill(const QString &name) : TriggerSkillV2(name) { events << Damaged; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner && ctx.owner->askForSkillInvoke(this,
            ctx.original_data ? *ctx.original_data : QVariant());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        return false;
    }
};

class HGuicai : public TriggerSkillV2 {
public:
    HGuicai() : TriggerSkillV2("heg_guicai") { events << AskForRetrial; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        // The donor permits the response from the private hand pile as well as he.
        return player && player->isAlive() && player->hasSkill(objectName()) && judge
            && (!player->isNude() || !player->getHandPile().isEmpty())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        // Keep cost selection free of prompts and movement; pay owns the retrial choice.
        return ctx.owner && ctx.original_data;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.original_data) return false;
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->who || !judge->card) return false;
        const QString prompt = QStringList{"@guicai-card", judge->who->objectName(), objectName(),
            judge->reason, QString::number(judge->card->getEffectiveId())}.join(":");
        // Native MethodResponse prompts expose hand, equipment and hand-pile cards.
        const Card *selected = room->askForCard(ctx.owner, "..", prompt, *ctx.original_data,
                                                Card::MethodResponse, judge->who, true);
        if (!selected || selected->getEffectiveId() < 0) return false;
        const int id = selected->getEffectiveId();
        if (room->getCardOwner(id) != ctx.owner) return false;
        const bool inHand = ctx.owner->handCards().contains(id);
        const bool inEquip = ctx.owner->getEquipsId().contains(id);
        const bool inHandPile = ctx.owner->getHandPile().contains(id);
        if (!inHand && !inEquip && !inHandPile) return false;
        ctx.extra_data = id;

        LogMessage invoke;
        invoke.type = "#InvokeSkill";
        invoke.from = ctx.owner;
        invoke.arg = objectName();
        room->sendLog(invoke);
        LogMessage response;
        response.card_str = selected->toString();
        response.from = ctx.owner;
        response.type = QString("#%1_Resp").arg(selected->getClassName());
        room->sendLog(response);
        room->notifySkillInvoked(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.original_data) return false;
        // A waived payment has no selected retrial card; QVariant's default 0
        // must not accidentally select physical card zero.
        if (!ctx.extra_data.isValid()) return false;
        const int id = ctx.extra_data.toInt();
        if (id < 0 || room->getCardOwner(id) != ctx.owner
            || (!ctx.owner->handCards().contains(id) && !ctx.owner->getEquipsId().contains(id)
                && !ctx.owner->getHandPile().contains(id))) return false;
        const Card *card = Sanguosha->getCard(id);
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (card && judge) {
            room->retrial(card, ctx.owner, judge, objectName(), false);
            judge->updateResult();
        }
        return false;
    }
};

class HGanglie : public HWeiDamagedSkill {
public:
    HGanglie() : HWeiDamagedSkill("heg_ganglie") {}
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        JudgeStruct judge;
        judge.pattern = ".|red";
        judge.good = true;
        judge.reason = objectName();
        judge.who = ctx.owner;
        room->judge(judge);
        ServerPlayer *from = damage.from;
        if (!from || !from->isAlive()) return false;
        if (judge.isGood())
            room->damage(DamageStruct(objectName(), ctx.owner, from, getEffectiveAmount(ctx)));
        else if (judge.card && judge.card->isBlack() && ctx.owner->isAlive() && ctx.owner->canDiscard(from, "he")) {
            int id = room->askForCardChosen(ctx.owner, from, "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(id, objectName(), from, ctx.owner);
        }
        return false;
    }
};

class HLuoyi : public TriggerSkillV2 {
public:
    HLuoyi() : TriggerSkillV2("heg_luoyi") { events << EventPhaseEnd << DamageCaused << EventPhaseChanging; }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override {
        if (event == EventPhaseChanging && player == ctx.owner && ctx.original_data
            && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive)
            player->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "active");
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseEnd && player->getPhase() == Player::Draw && player->canDiscard(player, "he"))
            return {{player, {objectName()}}};
        if (event == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))
                && !damage.chain && !damage.transfer) {
                QStringList active;
                for (int id : player->getValidSkillInstanceIds(objectName()))
                    if (player->getSkillInstanceStateValue(objectName(), id, "active", false).toBool())
                        active << SkillInstanceUtils::formatName(objectName(), id);
                if (!active.isEmpty()) return {{player, active}};
            }
        }
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (event == DamageCaused)
            return ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "active", false).toBool();
        const Card *card = room->askForCard(ctx.owner, "..", "@heg_luoyi", QVariant(), Card::MethodNone,
                                           nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (event == DamageCaused) return true;
        int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (event == DamageCaused) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            damage.damage += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(damage);
        } else {
            room->broadcastSkillInvoke(objectName(), ctx.owner);
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "active", true);
        }
        return false;
    }
};

class HYiji : public HWeiDamagedSkill {
public:
    HYiji() : HWeiDamagedSkill("heg_yiji") { frequency = Frequent; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        // One packet of two per damage event; damage magnitude does not repeat it.
        QList<int> ids = room->getNCards(2);
        ctx.owner->assignmentCards(ids, objectName(), room->getAlivePlayers());
        if (!ids.isEmpty()) {
            DummyCard remaining(ids);
            room->obtainCard(ctx.owner, &remaining, false);
        }
        return false;
    }
};

class HDuanliangVS : public ViewAsSkillV2 {
public:
    HDuanliangVS() : ViewAsSkillV2("heg_duanliang", 1) {
        setResponseOrUse(true);
    }
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasFlag("heg_DuanliangCannot");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override {
        return request.initiator && ViewAsSkillV2::canSelectCard(request, card)
            && card->getEffectiveId() >= 0 && !card->hasFlag("using")
            && Sanguosha->matchExpPattern("BasicCard,EquipCard|black", request.initiator, card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "SupplyShortage"; }
    const Card *createCard(const ActiveSkillRequest &request) const override {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        // The ordinary delayed trick owns target checks, material movement and effects.
        Card *card = Sanguosha->cloneCard("supply_shortage", original->getSuit(), original->getNumber());
        if (!card) return nullptr;
        card->addSubcard(original);
        card->setSkillName(objectName());
        card->setShowSkill(objectName());
        return card;
    }
};

class HDuanliang : public TriggerSkillV2 {
public:
    HDuanliang() : TriggerSkillV2("heg_duanliang") { events << PreCardUsed << EventPhaseChanging; view_as_skill = new HDuanliangVS; }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (!player || player != ctx.owner || !ctx.original_data) return;
        if (event == EventPhaseChanging) room->setPlayerFlag(player, "-heg_DuanliangCannot");
        else {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (use.card && use.card->getSkillName() == objectName() && !use.to.isEmpty()
                && player->distanceTo(use.to.first()) > 2)
                room->setPlayerFlag(player, "heg_DuanliangCannot");
        }
    }
};

class HDuanliangDistance : public TargetModSkillV2 {
public:
    HDuanliangDistance() : TargetModSkillV2("#heg_duanliang-distance") { pattern = "SupplyShortage"; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillName() == "heg_duanliang"
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
    }
};

class HJushouSelect : public ViewAsSkillV2 {
public:
    HJushouSelect() : ViewAsSkillV2("heg_jushou_select", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override {
        // MethodNone selection uses UNKNOWN and must not become a play action.
        return request.initiator && request.pattern == "@@heg_jushou_select!"
            && (request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && request.initiator->handCards().contains(card->getEffectiveId())
            && (card->isKindOf("EquipCard") ? card->isAvailable(request.initiator) : !request.initiator->isJilei(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "DummyCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override {
        if (!cardSelectionFeasible(request)) return nullptr;
        // Return only the selection; HJushou still owns equipment use/discard.
        DummyCard *selected = new DummyCard;
        selected->addSubcard(request.selectedCardIds.first());
        return selected;
    }
};

class HJushou : public TriggerSkillV2 {
public:
    HJushou() : TriggerSkillV2("heg_jushou") { events << EventPhaseStart; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *p, QVariant &) const override {
        return p && p->isAlive() && p->hasSkill(objectName()) && p->getPhase() == Player::Finish
            ? TriggerList{{p, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { return ctx.owner->askForSkillInvoke(this); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        QList<ServerPlayer *> factions;
        for (ServerPlayer *p : room->getAlivePlayers()) {
            if (!p->hasShownOneGeneral()) continue;
            bool present = false;
            for (ServerPlayer *representative : factions) if (representative->isFriendWith(p)) { present = true; break; }
            if (!present) factions << p;
        }
        const int count = factions.size();
        ServerPlayer *owner = ctx.owner;
        owner->drawCards(count, objectName());
        if (!owner->isAlive()) return false;
        const Card *fallback = nullptr;
        for (const Card *card : owner->getHandcards())
            if (card->isKindOf("EquipCard") ? card->isAvailable(owner) : !owner->isJilei(card)) { fallback = card; break; }
        if (!fallback) return false;
        const Card *selection = room->askForCard(owner, "@@heg_jushou_select!", "@heg_jushou", QVariant(), Card::MethodNone);
        const Card *card = selection ? Sanguosha->getCard(selection->getEffectiveId()) : fallback;
        if (card->isKindOf("EquipCard")) room->useCard(CardUseStruct(card, owner, owner));
        else room->throwCard(card, objectName(), owner);
        if (count > 2 && owner->isAlive()) owner->turnOver();
        return false;
    }
};

class HQiangxi : public ViewAsSkillV2 {
public:
    HQiangxi() : ViewAsSkillV2("heg_qiangxi", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->hasUsed("HQiangxiCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override {
        return ViewAsSkillV2::canSelectCard(request, card) && card->isKindOf("Weapon") && !request.initiator->isJilei(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override {
        if (request.selectedCardIds.isEmpty()) return true;
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest empty = request; empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override {
        // The new rule has no attack-range requirement.
        return target && target->isAlive() && target != request.initiator && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HQiangxiCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override {
        if (request.selectedCardIds.isEmpty()) { room->loseHp(ctx.initiator, 1, true, ctx.initiator, objectName()); return true; }
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        ctx.owner->getRoom()->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class HJieming : public HWeiDamagedSkill {
public:
    HJieming() : HWeiDamagedSkill("heg_jieming") {}
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "heg_jieming-invoke", true, true);
        if (target) ctx.targets = {target};
        return target;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override {
        const int n = qMin(5, target->getMaxHp()) - target->getHandcardNum();
        if (n > 0) target->drawCards(n, objectName());
        return false;
    }
};

class HShensu : public TriggerSkillV2 {
public:
    HShensu() : TriggerSkillV2("heg_shensu") { events << EventPhaseChanging; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *p, QVariant &data) const override {
        if (!p || !p->isAlive() || !p->hasSkill(objectName()) || !Slash::IsAvailable(p)) return {};
        Player::Phase next = data.value<PhaseChangeStruct>().to;
        if (p->isSkipped(next)) return {};
        if ((next == Player::Judge && !p->isSkipped(Player::Draw))
            || (next == Player::Play && p->canDiscard(p, "he")) || next == Player::Discard)
            return {{p, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *owner = ctx.owner;
        const Player::Phase next = ctx.original_data->value<PhaseChangeStruct>().to;
        int material = -1;
        if (next == Player::Play) {
            const Card *card = room->askForCard(owner, ".Equip", "@heg_shensu2", QVariant(), Card::MethodNone,
                                               nullptr, false, objectName());
            if (!card || card->isVirtualCard() || !owner->canDiscard(owner, card->getEffectiveId())) return false;
            material = card->getEffectiveId();
        }
        Slash slash(Card::NoSuit, 0);
        slash.setSkillName(objectName());
        QList<ServerPlayer *> available;
        for (ServerPlayer *p : room->getOtherPlayers(owner))
            if (owner->canSlash(p, &slash, false)) available << p;
        int maximum = qMax(1, 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, owner, &slash));
        const QString prompt = next == Player::Judge ? "@heg_shensu1" : next == Player::Play ? "@heg_shensu2" : "@heg_shensu3";
        QList<ServerPlayer *> chosen = room->askForPlayersChosen(owner, available, objectName(), 0, maximum, prompt);
        QList<const Player *> selected;
        for (ServerPlayer *p : chosen) {
            if (!slash.targetFilter(selected, p, owner)) return false;
            selected << p;
        }
        if (chosen.isEmpty() || !slash.targetsFeasible(selected, owner)) return false;
        ctx.targets = chosen;
        ctx.extra_data = material;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *owner = ctx.owner;
        Player::Phase next = ctx.original_data->value<PhaseChangeStruct>().to;
        if (next == Player::Play) {
            int id = ctx.extra_data.toInt();
            if (room->getCardOwner(id) != owner || !owner->canDiscard(owner, id)
                || !Sanguosha->getCard(id)->isKindOf("EquipCard")) return false;
            room->throwCard(id, objectName(), owner);
        } else if (next == Player::Discard) room->loseHp(owner, 1, true, owner, objectName());
        owner->skip(next, true);
        if (next == Player::Judge) owner->skip(Player::Draw, true);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_heg_shensu");
        room->useCard(CardUseStruct(slash, ctx.owner, ctx.targets), false);
        return false;
    }
};

class HShensuDistance : public TargetModSkillV2 {
public:
    HShensuDistance() : TargetModSkillV2("#heg_shensu-distance") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillName() == "heg_shensu"
            ? CorrectSkillResult::useAmount(1000) : CorrectSkillResult::noEffect();
    }
};

class HLuoshen : public TriggerSkillV2 {
public:
    HLuoshen() : TriggerSkillV2("heg_luoshen") {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    bool canPreshow() const override { return false; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        if (player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()))
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner && ctx.owner->askForSkillInvoke(this);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *zhenji = ctx.owner;
        if (!zhenji || !zhenji->isAlive()) return false;
        room->broadcastSkillInvoke(objectName(), zhenji);

        // Keep each invocation's cards local: repeated skill instances and nested
        // judgments must not share a player tag or take each other's results.
        QList<int> cards;
        const auto remainingCards = [&]() {
            QList<int> remaining;
            for (int id : cards)
                if (room->getCardPlace(id) == Player::PlaceTable)
                    remaining << id;
            return remaining;
        };
        JudgeStruct judge;
        try {
            judge.pattern = ".|black";
            judge.good = true;
            judge.reason = objectName();
            judge.play_animation = false;
            judge.who = zhenji;
            judge.time_consuming = true;
            do {
                room->judge(judge);
                if (judge.isGood() && judge.card) {
                    const int id = judge.card->getEffectiveId();
                    if (room->getCardPlace(id) == Player::PlaceTable && !cards.contains(id))
                        cards << id;
                }
            } while (judge.isGood() && zhenji->isAlive() && zhenji->askForSkillInvoke(this));
        } catch (...) {
            // A broken turn must not leave the completed judgments on the table.
            if (judge.card && judge.isGood() && !cards.contains(judge.card->getEffectiveId()))
                cards << judge.card->getEffectiveId();
            const QList<int> remaining = remainingCards();
            if (!remaining.isEmpty())
                room->throwCard(remaining, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                    zhenji->objectName(), objectName(), QString()), nullptr);
            throw;
        }
        const QList<int> remaining = remainingCards();
        if (!remaining.isEmpty()) {
            DummyCard result(remaining);
            if (zhenji->isAlive())
                room->obtainCard(zhenji, &result);
            else
                room->throwCard(&result, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                    zhenji->objectName(), objectName(), QString()), nullptr);
        }
        return false;
    }
};

class HLuoshenMove : public TriggerSkillV2 {
public:
    HLuoshenMove() : TriggerSkillV2("#heg_luoshen-move") {
        events << FinishJudge;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (player && player->isAlive() && player->hasSkill(objectName()) && judge
            && judge->who == player && judge->reason == "heg_luoshen" && judge->isGood()
            && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        const JudgeStruct *judge = ctx.original_data ? ctx.original_data->value<JudgeStruct *>() : nullptr;
        if (judge && judge->who == ctx.owner && judge->reason == "heg_luoshen" && judge->isGood()
            && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
            // Defer acquisition until the owning Luoshen invocation has finished.
            CardMoveReason reason(CardMoveReason::S_REASON_JUDGEDONE,
                ctx.owner->objectName(), QString(), judge->reason);
            room->moveCardTo(judge->card, nullptr, Player::PlaceTable, reason, true);
        }
        return false;
    }
};

void HStandardPackage::addWeiGenerals()
{
    // Reuse only rule-equivalent definitions; xxyheaven's changed rules use heg_*.
    General *caocao = new General(this, "heg_caocao", "wei"); // WEI 001
    caocao->addCompanion("heg_dianwei");
    caocao->addCompanion("heg_xuchu");
    caocao->addSkill("nosjianxiong");

    General *simayi = new General(this, "heg_simayi", "wei", 3); // WEI 002
    simayi->addSkill("nosfankui");
    simayi->addSkill(new HGuicai);

    // Donor standard package keeps Xiahou Dun at five base HP.
    General *xiahoudun = new General(this, "heg_xiahoudun", "wei", 5); // WEI 003
    xiahoudun->addCompanion("heg_xiahouyuan");
    xiahoudun->addSkill(new HGanglie);

    General *zhangliao = new General(this, "heg_zhangliao", "wei"); // WEI 004
    zhangliao->addSkill("tenyeartuxi");

    General *xuchu = new General(this, "heg_xuchu", "wei"); // WEI 005
    xuchu->addSkill(new HLuoyi);

    General *guojia = new General(this, "heg_guojia", "wei", 3); // WEI 006
    guojia->addSkill("tiandu");
    guojia->addSkill(new HYiji);

    General *zhenji = new General(this, "heg_zhenji", "wei", 3, false); // WEI 007
    zhenji->addSkill("qingguo");
    zhenji->addSkill(new HLuoshen);
    zhenji->addSkill(new HLuoshenMove);
    insertRelatedSkills("heg_luoshen", "#heg_luoshen-move");

    General *xiahouyuan = new General(this, "heg_xiahouyuan", "wei"); // WEI 008
    xiahouyuan->addSkill(new HShensu);
    xiahouyuan->addSkill(new HShensuDistance);
    insertRelatedSkills("heg_shensu", "#heg_shensu-distance");

    General *zhanghe = new General(this, "heg_zhanghe", "wei"); // WEI 009
    zhanghe->addSkill("qiaobian");

    General *xuhuang = new General(this, "heg_xuhuang", "wei"); // WEI 010
    xuhuang->addSkill(new HDuanliang);
    xuhuang->addSkill(new HDuanliangDistance);
    insertRelatedSkills("heg_duanliang", "#heg_duanliang-distance");

    General *caoren = new General(this, "heg_caoren", "wei"); // WEI 011
    caoren->addSkill(new HJushou);
    skills << new HJushouSelect;
    insertRelatedSkills("heg_jushou", "heg_jushou_select");

    General *dianwei = new General(this, "heg_dianwei", "wei"); // WEI 012
    dianwei->addSkill(new HQiangxi);

    General *xunyu = new General(this, "heg_xunyu", "wei", 3); // WEI 013
    xunyu->addSkill("quhu");
    xunyu->addSkill(new HJieming);

    General *caopi = new General(this, "heg_caopi", "wei", 3); // WEI 014
    caopi->addCompanion("heg_zhenji");
    caopi->addSkill("xingshang");
    caopi->addSkill("mobilefangzhu");

    General *yuejin = new General(this, "heg_yuejin", "wei", 4); // WEI 016
    yuejin->addSkill("xiaoguo");
}
