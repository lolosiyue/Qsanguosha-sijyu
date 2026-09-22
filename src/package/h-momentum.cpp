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
#include "h-momentum.h"
#include "original-hegemony-compat.h"
#include "general.h"
#include "serverplayer.h"
#include "standard.h"
#include "maneuvering.h"
#include "h-standard-tricks.h"
#include "engine.h"
#include "structs.h"
#include "roomthread.h"
#include "room.h"
#include "util.h"
#include "settings.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>

class HHengjiang : public TriggerSkillV2 {
public:
    HHengjiang() : TriggerSkillV2("heg_hengjiang") {
        events << Damaged << TurnStart << CardsMoveOneTime << EventPhaseStart;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override {
        if (!ctx.owner || !player) return;
        if (event == TurnStart || (event == EventPhaseStart && player->getPhase() == Player::NotActive)) {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "turn", QString());
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "penalty", 0);
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded", false);
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            if (move.from == player && player->getPhase() == Player::Discard
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                && ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "turn").toString() == player->objectName())
                ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded", true);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override {
        if (!player) return {};
        if (event == Damaged) {
            ServerPlayer *current = room->getCurrent();
            if (player->isAlive() && player->hasSkill(objectName()) && current && current->isAlive()
                && current->getPhase() != Player::NotActive)
                return TriggerList{{player, QStringList{objectName() + '*' + QString::number(data.value<DamageStruct>().damage)}}};
        }
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner->askForSkillInvoke(this, QVariant::fromValue(room->getCurrent()));
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (ServerPlayer *current = room->getCurrent()) {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "turn", current->objectName());
            const int old = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "penalty").toInt();
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "penalty", old + getEffectiveAmount(ctx));
        }
        return false;
    }
};

class HHengjiangDraw : public TriggerSkillV2 {
public:
    HHengjiangDraw() : TriggerSkillV2("#heg_hengjiang-draw") { events << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                const SkillInstance *entry = owner->findSkillInstance(objectName(), id);
                if (!entry) continue;
                const auto &parent = entry->parent;
                if (owner->getSkillInstanceStateValue(parent.skillName, parent.instanceID, "turn").toString() == player->objectName()
                    && owner->getSkillInstanceStateValue(parent.skillName, parent.instanceID, "penalty").toInt() > 0
                    && !owner->getSkillInstanceStateValue(parent.skillName, parent.instanceID, "discarded").toBool())
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        ctx.owner->drawCards(getEffectiveAmount(ctx), "heg_hengjiang");
        return false;
    }
};

class HHengjiangMaxCards : public MaxCardsSkillV2 {
public:
    HHengjiangMaxCards() : MaxCardsSkillV2("#heg_hengjiang-maxcard") { setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        if (!ctx.holder || !ctx.primary) return CorrectSkillResult::noEffect();
        const SkillInstance *helper = ctx.holder->findSkillInstance(objectName(), ctx.instanceRef.key.instanceID);
        if (!helper) return CorrectSkillResult::noEffect();
        const auto &parent = helper->parent;
        if (ctx.holder->getSkillInstanceStateValue(parent.skillName, parent.instanceID, "turn").toString()
            != ctx.primary->objectName()) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(-ctx.holder->getSkillInstanceStateValue(parent.skillName, parent.instanceID, "penalty").toInt());
    }
};

class HGuixiuVS : public ViewAsSkillV2 {
public:
    HGuixiuVS() : ViewAsSkillV2("heg_guixiu") { m_baseAmount = 2; }
    bool canActivate(const ActiveSkillRequest &request) const override {
        return !Config.EnableHegemony && request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.activationRef.isValid() && !request.initiator->getSkillInstanceStateValue(
                objectName(), request.activationRef.key.instanceID, "used").toBool();
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override {
        if (!canActivate(request)) return false;
        ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "used", true);
        if (ctx.initiator->getMark("@heg_guixiu") > 0) room->removePlayerMark(ctx.initiator, "@heg_guixiu");
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override {
        ctx.invoker->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class HGuixiu : public TriggerSkillV2 {
public:
    HGuixiu() : TriggerSkillV2("heg_guixiu") {
        events << GeneralShown << GeneralRemoved << EventPhaseStart << EventLoseSkill;
        frequency = Limited;
        limit_mark = "@heg_guixiu";
        view_as_skill = new HGuixiuVS;
        m_baseAmount = 2;
    }
    Frequency getFrequency(const Player *) const override { return Config.EnableHegemony ? Frequent : Limited; }
    bool canPreshow() const override { return false; }
    bool acceptsRemovalEvent(TriggerEvent event, const QVariant &) const override {
        return Config.EnableHegemony ? event == GeneralRemoved : event == EventLoseSkill;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override {
        if (ctx.instanceID != 0) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive() || ctx.owner != ctx.invoker || !ctx.original_data
            || !acceptsRemovalEvent(ctx.current_event, *ctx.original_data)) return false;
        if (ctx.current_event == GeneralRemoved) {
            const General *general = Sanguosha->getGeneral(ctx.original_data->toString());
            return general && general->hasSkill(objectName(), true);
        }
        SkillChangeStruct change;
        return change.tryParse(*ctx.original_data) && change.skillName == objectName() && change.instanceID > 0
            && !ctx.owner->hasSkillInstance(objectName(), change.instanceID);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override {
        if (!player || !player->isAlive()) return {};
        if (acceptsRemovalEvent(event, data)) {
            bool removed = false;
            if (event == GeneralRemoved) {
                const General *general = Sanguosha->getGeneral(data.toString());
                removed = general && general->hasSkill(objectName(), true);
            } else {
                SkillChangeStruct change;
                removed = change.tryParse(data) && change.skillName == objectName() && change.instanceID > 0
                    && !player->hasSkillInstance(objectName(), change.instanceID);
            }
            return removed && player->isWounded() ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
        }
        TriggerList result;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstance *instance = player->findSkillInstance(objectName(), id);
            if (Config.EnableHegemony && event == GeneralShown && instance && instance->source == SourceInnate
                && instance->bindHead == (data.toBool() ? 1 : 2))
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
            else if (!Config.EnableHegemony && event == EventPhaseStart && player->getPhase() == Player::Start
                     && !player->getSkillInstanceStateValue(objectName(), id, "used").toBool())
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner->askForSkillInvoke(this, *ctx.original_data);
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!Config.EnableHegemony && event == EventPhaseStart) {
            if (ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "used").toBool()) return false;
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "used", true);
            if (ctx.owner->getMark(limit_mark) > 0) room->removePlayerMark(ctx.owner, limit_mark);
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (acceptsRemovalEvent(event, *ctx.original_data)) room->recover(ctx.owner, RecoverStruct(objectName(), ctx.owner));
        else ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class HCunsi : public ViewAsSkillV2 {
public:
    HCunsi() : ViewAsSkillV2("heg_cunsi") {
        frequency = Limited;
        limit_mark = "@heg_cunsi";
        waked_skills = "heg_yongjue";
        m_baseAmount = 2;
    }
    bool canActivate(const ActiveSkillRequest &request) const override {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !request.activationRef.isValid()) return false;
        const SkillInstance *instance = request.initiator->findSkillInstance(objectName(), request.activationRef.key.instanceID);
        return instance && !request.initiator->getSkillInstanceStateValue(objectName(), instance->instanceID, "used").toBool()
            && (!Config.EnableHegemony || (instance->source == SourceInnate && instance->bindHead != 0));
    }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override {
        return selected.isEmpty() && target && target->isAlive();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HCunsiCard"; }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override {
        if (!canActivate(request)) return false;
        ctx.extra_data = request.initiator->findSkillInstance(objectName(), request.activationRef.key.instanceID)->bindHead;
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override {
        if (!canActivate(request)) return false;
        ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "used", true);
        if (ctx.initiator->getMark(limit_mark) > 0) room->removePlayerMark(ctx.initiator, limit_mark);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override {
        Room *room = ctx.initiator->getRoom();
        const SkillInstance *source = ctx.initiator->findSkillInstance(objectName(), ctx.activationRef.key.instanceID);
        if (!source) return FinishSkill;
        // Retire only the accepted source after revelation, never a replacement general.
        if (Config.EnableHegemony) {
            if (source->source != SourceInnate || source->bindHead != ctx.extra_data.toInt()) return FinishSkill;
            ctx.initiator->removeGeneral(source->bindHead == 1);
            if (ctx.initiator->hasSkillInstance(objectName(), ctx.activationRef.key.instanceID)) return FinishSkill;
        }
        else {
            if (ctx.initiator->ownsSkill("heg_guixiu")) room->detachSkillFromPlayer(ctx.initiator, "heg_guixiu");
            room->detachSkillFromPlayer(ctx.initiator, SkillInstanceUtils::formatName(objectName(), ctx.activationRef.key.instanceID));
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        Room *room = target->getRoom();
        const int id = room->acquireSkill(target, "heg_yongjue");
        if (ServerPlayer *current = room->getCurrent()) {
            if (current->getPhase() == Player::Play && current->getMark("yongjue-PlayClear") > 0) {
                QVariantMap first;
                first[current->objectName()] = QVariantList();
                target->setSkillInstanceStateValue("heg_yongjue", id, "first", first);
            }
        }
        if (target != ctx.initiator) target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class HYongjue : public TriggerSkillV2 {
public:
    HYongjue() : TriggerSkillV2("heg_yongjue") {
        events << EventPhaseStart << CardUsed << CardResponded << BeforeCardsMove << CardsMoveOneTime;
        frequency = Frequent;
    }
    bool canPreshow() const override { return false; }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override {
        if (!player || !ctx.owner) return;
        QVariantMap first = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "first").toMap();
        if (event == EventPhaseStart && player->getPhase() == Player::Play) first.remove(player->objectName());
        else if ((event == CardUsed || event == CardResponded) && player->getPhase() == Player::Play) {
            const Card *card = nullptr;
            if (event == CardUsed) {
                const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
                if (use.from != player) return;
                card = use.card;
            } else {
                const CardResponseStruct response = ctx.original_data->value<CardResponseStruct>();
                if (!response.m_isUse) return;
                card = response.m_card;
            }
            if (!card || card->isKindOf("SkillCard") || first.contains(player->objectName())) return;
            QList<int> ids;
            if (card->isKindOf("Slash")) {
                ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
                if (Config.EnableHegemony && (ids.size() != 1 || !Sanguosha->getCard(ids.first())->isKindOf("Slash"))) ids.clear();
            }
            first[player->objectName()] = ListI2V(ids); // Empty still records that the first card was used.
        } else return;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "first", first);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override {
        if ((Config.EnableHegemony && event != CardsMoveOneTime) || (!Config.EnableHegemony && event != BeforeCardsMove)) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            if (Config.EnableHegemony && !owner->isFriendWith(player)) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                const QVariantMap first = owner->getSkillInstanceStateValue(objectName(), id, "first").toMap();
                const QList<int> ids = ListV2I(first.value(player->objectName()).toList());
                if (ids.isEmpty()) continue;
                bool present = true;
                for (int cardId : ids) {
                    const int index = move.card_ids.indexOf(cardId);
                    if (index < 0 || move.from_places.value(index) != Player::PlaceTable
                        || room->getCardPlace(cardId) != (Config.EnableHegemony ? Player::DiscardPile : Player::PlaceTable)) present = false;
                }
                if (present) result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ServerPlayer *user = room->findPlayerByObjectName(move.from->objectName());
        if (!user) return false;
        ServerPlayer *chooser = Config.EnableHegemony ? user : ctx.owner;
        if (!chooser->askForSkillInvoke(this, QVariant::fromValue(user))) return false;
        ctx.targets << user;
        ctx.extra_data = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "first").toMap().value(user->objectName());
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        const QList<int> ids = ListV2I(ctx.extra_data.toList());
        for (int id : ids)
            if (room->getCardPlace(id) != (Config.EnableHegemony ? Player::DiscardPile : Player::PlaceTable)) return false;
        if (ids.isEmpty()) return false;
        QVariantMap first = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "first").toMap();
        first[ctx.original_data->value<CardsMoveOneTimeStruct>().from->objectName()] = QVariantList();
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "first", first);
        if (!Config.EnableHegemony) {
            CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            move.removeCardIds(ids);
            *ctx.original_data = QVariant::fromValue(move);
        }
        DummyCard card(ids);
        room->obtainCard(target, &card);
        return false;
    }
};

class HYingyang : public TriggerSkillV2 {
public:
    HYingyang() : TriggerSkillV2("heg_yingyang") {
        events << PindianVerifying;
        m_baseAmount = 3;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override {
        TriggerList result;
        const PindianStruct *pindian = data.value<PindianStruct *>();
        if (!pindian) return result;
        for (ServerPlayer *p : {pindian->from, pindian->to})
            if (p && p->isAlive() && p->hasSkill(objectName())) result[p] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "jia3+jian3+cancel", *ctx.original_data);
        return ctx.choice != "cancel";
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        if (!pindian) return false;
        int &number = pindian->from == ctx.owner ? pindian->from_number : pindian->to_number;
        number = qBound(1, number + (ctx.choice == "jia3" ? 1 : -1) * getEffectiveAmount(ctx), 13);
        room->broadcastSkillInvoke(objectName(), ctx.choice == "jia3" ? 1 : 2, ctx.owner);
        *ctx.original_data = QVariant::fromValue(pindian);
        return false;
    }
};

class HHunshang : public TriggerSkillV2 {
public:
    HHunshang() : TriggerSkillV2("heg_hunshang") {
        events << EventPhaseStart;
        frequency = Compulsory;
        relate_to_place = "deputy";
    }
    bool canPreshow() const override { return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        if (player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start && player->getHp() == 1)
            return TriggerList{{player, QStringList{objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return !Config.EnableHegemony || ctx.owner->hasShownSkill(this) || ctx.owner->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        SkillInstanceKey helper;
        for (const SkillInstanceKey &key : ctx.owner->getChildSkillInstanceKeys(SkillInstanceKey(objectName(), ctx.instanceID)))
            if (key.skillName == "#heg_hunshang") helper = key;
        if (!helper.isValid()) return false;
        for (const QString &name : {QStringLiteral("yinghun"), QStringLiteral("yingzi")}) {
            if (ctx.owner->ownsSkill(name)) continue;
            const int id = room->acquireSkill(ctx.owner, name);
            if (id <= 0) continue;
            const QString grant = SkillInstanceUtils::formatName(name, id);
            // Acquisition reactions may retire the source before acquireSkill returns.
            if (!ctx.owner->hasSkillInstance(helper.skillName, helper.instanceID)) {
                if (ctx.owner->hasSkillInstance(name, id)) room->detachSkillFromPlayer(ctx.owner, grant);
                break;
            }
            QVariantList granted = ctx.owner->getSkillInstanceStateValue(helper.skillName, helper.instanceID, "granted").toList();
            granted << grant;
            ctx.owner->setSkillInstanceStateValue(helper.skillName, helper.instanceID, "granted", granted);
        }
        return false;
    }
};

class HHunshangRemove : public TriggerSkillV2 {
public:
    HHunshangRemove() : TriggerSkillV2("#heg_hunshang") {
        events << EventPhaseChanging << EventLoseSkill << Death;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (!ctx.owner || player != ctx.owner) return;
        bool clear = event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive;
        if (event == Death) clear = ctx.original_data->value<DeathStruct>().who == ctx.owner;
        if (event == EventLoseSkill) {
            SkillChangeStruct change;
            const SkillInstance *helper = ctx.owner->findSkillInstance(objectName(), ctx.instanceID);
            clear = helper && change.tryParse(*ctx.original_data) && change.skillName == helper->parent.skillName
                && change.instanceID == helper->parent.instanceID;
        }
        if (!clear) return;
        const QVariantList granted = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "granted").toList();
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "granted", QVariantList());
        // Revoke exact grants only; existing innate/acquired copies are untouched.
        for (const QVariant &value : granted) {
            QString name;
            const int id = SkillInstanceUtils::parseName(value.toString(), name);
            if (ctx.owner->hasSkillInstance(name, id)) room->detachSkillFromPlayer(ctx.owner, value.toString());
        }
    }
};

class HDuanxie : public ViewAsSkillV2 {
public:
    HDuanxie() : ViewAsSkillV2("heg_duanxie") {}
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HDuanxieCard");
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HDuanxieCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override {
        return selected.isEmpty() && target && target->isAlive() && target != request.initiator
            && !target->isChained() && target->canBeChainedBy(request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override {
        return selected.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        if (!target->canBeChainedBy(ctx.invoker)) return ContinueEffects;
        Room *room = target->getRoom();
        room->setPlayerChained(target, true);
        if (ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->canBeChainedBy(ctx.invoker))
            room->setPlayerChained(ctx.invoker, true);
        return ContinueEffects;
    }
};

class HFenming : public TriggerSkillV2 {
public:
    HFenming() : TriggerSkillV2("heg_fenming") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        if (player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish && player->isChained())
            return TriggerList{{player, QStringList{objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        for (ServerPlayer *p : room->getAlivePlayers())
            if (p->isChained() && ctx.owner->canDiscard(p, "he")) ctx.targets << p;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        if (!ctx.owner->isAlive() || !target->isChained() || !ctx.owner->canDiscard(target, "he")) return false;
        if (target == ctx.owner) room->askForDiscard(target, objectName(), 1, 1, false, true);
        else {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(id, target, ctx.owner);
        }
        return false;
    }
};

class HHengzheng : public TriggerSkillV2 {
public:
    HHengzheng() : TriggerSkillV2("heg_hengzheng") { events << EventPhaseStart; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw
            || (!player->isKongcheng() && (Config.EnableHegemony ? player->getHp() != 1 : player->getHp() > 1))) return {};
        for (ServerPlayer *p : room->getOtherPlayers(player))
            if (!p->isAllNude()) return TriggerList{{player, QStringList{objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        room->setPlayerMark(ctx.owner, "HengzhengUsed", 1);
        // Replacement draw must return true after all V2 target effects complete.
        ctx.manual_effect = true;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) {
            if (!ctx.owner->isAlive()) break;
            if (!p->isAllNude()) skillEffect(event, room, player, ctx, p);
        }
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        if (!target->isAllNude() && ctx.owner->isAlive()) {
            const int id = room->askForCardChosen(ctx.owner, target, "hej", objectName());
            room->obtainCard(ctx.owner, id, false);
        }
        return false;
    }
};

class HBaoling : public TriggerSkillV2 {
public:
    HBaoling() : TriggerSkillV2("heg_baoling") {
        events << EventPhaseEnd;
        frequency = Compulsory;
        relate_to_place = "head";
        waked_skills = "benghuai";
        m_baseAmount = 3;
    }
    Frequency getFrequency(const Player *) const override { return Config.EnableHegemony ? Compulsory : Wake; }
    bool canPreshow() const override { return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return {};
        if (Config.EnableHegemony) {
            if (!player->hasShownSkill(this) || !player->getActualGeneral2()
                || player->getActualGeneral2Name().contains("sujiang")) return {};
        } else {
            if (player->getMark("HengzhengUsed") == 0 && !player->canWake(objectName())) return {};
            TriggerList result;
            for (int id : player->getValidSkillInstanceIds(objectName()))
                if (!player->getSkillInstanceStateValue(objectName(), id, "awakened").toBool())
                    result[player] << SkillInstanceUtils::formatName(objectName(), id);
            return result;
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *owner = ctx.owner;
        const int amount = getEffectiveAmount(ctx);
        if (Config.EnableHegemony) {
            owner->removeGeneral(false);
            room->setPlayerProperty(owner, "maxhp", owner->getMaxHp() + amount);
        } else {
            owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "awakened", true);
            room->setPlayerMark(owner, objectName(), 1);
            // Keep the legacy shared Benghuai audio discriminator.
            room->setPlayerMark(owner, "baoling", 1);
            if (!room->changeMaxHpForAwakenSkill(owner, amount, objectName())) return false;
        }
        room->recover(owner, RecoverStruct(owner, nullptr, amount, objectName()));
        room->acquireSkill(owner, "benghuai");
        return false;
    }
};

class HChuanxin : public TriggerSkillV2 {
public:
    HChuanxin() : TriggerSkillV2("heg_chuanxin") { events << DamageCaused; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to || !damage.to->isAlive()
            || !damage.card || !(damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))
            || damage.chain || damage.transfer || !damage.by_user) return {};
        if (Config.EnableHegemony && (player->getPhase() != Player::Play || !damage.to->hasShownOneGeneral()
            || player->isFriendWith(damage.to) || (!player->hasShownOneGeneral() && player->willBeFriendWith(damage.to))
            || !damage.to->getActualGeneral2() || damage.to->getActualGeneral2Name().contains("sujiang"))) return {};
        return TriggerList{{player, QStringList{objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<DamageStruct>().to;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        QStringList choices;
        if (target->hasEquip()) choices << "discard";
        QStringList skills;
        if (Config.EnableHegemony) choices << "remove";
        else if (target->getMark("heg_chuanxin_" + ctx.owner->objectName()) == 0) {
            for (const Skill *skill : target->getVisibleSkillList())
                if (!skill->isAttachedLordSkill()) skills << skill->objectName();
            skills.removeDuplicates();
            if (skills.size() > 1) choices << "detach";
        }
        // Even a target with neither option has its damage prevented, as in the identity version.
        if (choices.isEmpty()) return true;
        const QString choice = room->askForChoice(target, objectName(), choices.join('+'), *ctx.original_data);
        if (choice == "discard") {
            target->throwAllEquips();
            if (target->isAlive()) room->loseHp(HpLostStruct(target, 1, objectName(), ctx.owner));
        } else if (choice == "remove") target->removeGeneral(false);
        else {
            room->addPlayerMark(target, "heg_chuanxin_" + ctx.owner->objectName());
            const QString name = room->askForChoice(target, "heg_chuanxin_lose", skills.join('+'), *ctx.original_data);
            room->detachSkillFromPlayer(target, name);
        }
        return true;
    }
};

class HFengshiSummonVS : public ViewAsSkillV2 {
public:
    HFengshiSummonVS() : ViewAsSkillV2("heg_fengshi") {}
    bool canActivate(const ActiveSkillRequest &request) const override {
        return Config.EnableHegemony && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && canSummonOriginalHegemonyArray(request.initiator, objectName(), "Siege");
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    EffectFlow effect(SkillContext &ctx) const override {
        if (ctx.invoker && ctx.invoker->isAlive()) ctx.invoker->summonFriends("Siege");
        return ContinueEffects;
    }
};

class HFengshi : public TriggerSkillV2 {
public:
    HFengshi() : TriggerSkillV2("heg_fengshi") {
        events << TargetSpecified << TargetConfirmed;
        view_as_skill = new HFengshiSummonVS;
    }
    bool canPreshow() const override { return false; }
    Frequency getFrequency(const Player *) const override { return Config.EnableHegemony ? Compulsory : NotFrequent; }
    QList<ServerPlayer *> targets(ServerPlayer *owner, const CardUseStruct &use) const {
        QList<ServerPlayer *> result;
        if (!use.card || !use.card->isKindOf("Slash") || !use.from || !use.from->isAlive()) return result;
        for (ServerPlayer *to : use.to) {
            const bool relation = Config.EnableHegemony ? use.from->inSiegeRelation(owner, to)
                : to->isAdjacentTo(owner) && to->isAdjacentTo(use.from);
            if (to->isAlive() && relation && to->canDiscard(to, "e")) result << to;
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override {
        const CardUseStruct use = data.value<CardUseStruct>();
        if ((Config.EnableHegemony && event != TargetSpecified) || (!Config.EnableHegemony && event != TargetConfirmed)) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            if (!Config.EnableHegemony && player != owner) continue;
            if (Config.EnableHegemony && owner->aliveCount() < 4) continue;
            if (targets(owner, use).isEmpty()) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                const SkillInstanceRef ref(owner->objectName(), SkillInstanceKey(objectName(), id));
                if (!Config.EnableHegemony || !room->isGeneralHiddenForSkill(ref))
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        for (ServerPlayer *to : targets(ctx.owner, ctx.original_data->value<CardUseStruct>())) {
            if (Config.EnableHegemony || ctx.owner->askForSkillInvoke(this, QVariant::fromValue(to))) ctx.targets << to;
        }
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        if (target->canDiscard(target, "e")) room->askForDiscard(target, objectName(), 1, 1, false, true,
            "@heg_fengshi-discard:" + ctx.owner->objectName(), ".|.|.|equipped");
        return false;
    }
};

class HWuxin : public TriggerSkillV2 {
public:
    HWuxin() : TriggerSkillV2("heg_wuxin") { events << EventPhaseStart; frequency = Frequent; }
    bool canPreshow() const override { return false; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        if (player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw)
            return TriggerList{{player, QStringList{objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        const int count = ctx.owner->getPlayerNumWithSameKingdom(objectName(), QString(), MaxCardsType::Normal);
        if (count > 0) room->askForGuanxing(ctx.owner, room->getNCards(count), Room::GuanxingUpOnly);
        return false;
    }
};

class HHongfaSlash : public ViewAsSkillV2 {
public:
    HHongfaSlash() : ViewAsSkillV2("heg_hongfa_slash", 1) {
        attached_lord_skill = true;
        expand_pile = "heavenly_army,%heavenly_army";
        response_or_use = true;
    }
    const Player *lord(const ActiveSkillRequest &request) const {
        if (!request.initiator || !request.activationRef.isValid()) return nullptr;
        const SkillInstance *entry = request.initiator->findSkillInstance(objectName(), request.activationRef.key.instanceID);
        if (!entry || !entry->parentRef.isValid()) return nullptr;
        for (const Player *p : request.initiator->getAliveSiblings(true))
            if (p->objectName() == entry->parentRef.ownerObjectName && p->hasLordSkill("heg_hongfa")
                && p->hasSkillInstance("heg_hongfa", entry->parentRef.key.instanceID)
                && !p->isSkillInvalid("heg_hongfa", entry->parentRef.key.instanceID)) return p;
        return nullptr;
    }
    bool canActivate(const ActiveSkillRequest &request) const override {
        const Player *owner = lord(request);
        if (!owner || owner->getPile("heavenly_army").isEmpty()) return false;
        if (Config.EnableHegemony ? (!request.initiator->hasShownOneGeneral() || !owner->isFriendWith(request.initiator))
                                 : owner->getKingdom() != request.initiator->getKingdom()) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Slash::IsAvailable(request.initiator) : request.pattern == "slash";
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override {
        const Player *owner = lord(request);
        return owner && card && request.selectedCardIds.isEmpty() && owner->getPile("heavenly_army").contains(card->getEffectiveId());
    }
    const Card *createCard(const ActiveSkillRequest &request) const override {
        if (!canActivate(request) || request.selectedCardIds.size() != 1) return nullptr;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        if (!canSelectCard(empty, material)) return nullptr;
        Slash *slash = new Slash(material->getSuit(), material->getNumber());
        slash->addSubcard(material);
        slash->setSkillName(objectName());
        return slash;
    }
};

class HHongfa : public TriggerSkillV2 {
public:
    HHongfa() : TriggerSkillV2("heg_hongfa$") {
        events << ConfirmPlayerNum << EventPhaseStart << PreHpLost << GameStart
               << GeneralShown << GeneralHidden << GeneralRemoved << EventAcquireSkill << EventLoseSkill << Death
               << EventSkillInvalidated << EventSkillValidityRestored << RemoveStateChanged;
        frequency = Compulsory;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (event == ConfirmPlayerNum || event == PreHpLost || !ctx.owner) return;
        const SkillInstanceRef root(ctx.owner->objectName(), SkillInstanceKey(objectName(), ctx.instanceID));
        const bool enabled = ctx.owner->isAlive() && ctx.owner->hasLordSkill(objectName())
            && !ctx.owner->isSkillInvalid(objectName(), ctx.instanceID)
            && (!Config.EnableHegemony || !room->isGeneralHiddenForSkill(root));
        for (ServerPlayer *p : room->getAllPlayers(true)) {
            const bool friendly = Config.EnableHegemony ? p->willBeFriendWith(ctx.owner)
                : p->getKingdom() == ctx.owner->getKingdom();
            if (enabled && p->isAlive() && friendly) room->attachSkillToPlayer(p, "heg_hongfa_slash", root);
            else {
                for (const SkillInstance &entry : p->getSkillInstances())
                    if (entry.skillName == "heg_hongfa_slash" && entry.parentRef == root)
                        room->detachAttachedSkill(SkillInstanceRef(p->objectName(), entry.key()));
            }
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override {
        if (!player || !player->isAlive() || !player->hasLordSkill(objectName())) return {};
        if (event == EventPhaseStart && player->getPhase() == Player::Start && player->getPile("heavenly_army").isEmpty())
            return TriggerList{{player, QStringList{objectName()}}};
        if (event == PreHpLost && !player->getPile("heavenly_army").isEmpty())
            return TriggerList{{player, QStringList{objectName()}}};
        if (event == ConfirmPlayerNum && !player->getPile("heavenly_army").isEmpty()
            && data.value<PlayerNumStruct>().m_toCalculate == "qun"
            && (data.value<PlayerNumStruct>().m_type == MaxCardsType::Max || data.value<PlayerNumStruct>().m_type == MaxCardsType::Normal))
            return TriggerList{{player, QStringList{objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (event == EventPhaseStart) return true;
        if (event == ConfirmPlayerNum) {
            const PlayerNumStruct count = ctx.original_data->value<PlayerNumStruct>();
            const int maximum = ctx.owner->getPile("heavenly_army").size();
            if (count.m_type == MaxCardsType::Max) ctx.extra_data = maximum;
            else {
                QStringList choices;
                for (int i = 0; i <= maximum; ++i) choices << QString::number(i);
                ctx.extra_data = room->askForChoice(ctx.owner, "heg_hongfa_num", choices.join('+'), *ctx.original_data).toInt();
            }
            return ctx.extra_data.toInt() > 0;
        }
        const QList<int> pile = ctx.owner->getPile("heavenly_army");
        room->fillAG(pile, ctx.owner);
        const auto clear = qScopeGuard([&]() { room->clearAG(ctx.owner); });
        ctx.extra_data = room->askForAG(ctx.owner, pile, true, objectName(), "@heg_hongfa-prevent");
        return ctx.extra_data.toInt() >= 0;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (event != PreHpLost) return true;
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner->getPile("heavenly_army").contains(id)) return false;
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), QString());
        room->throwCard(Sanguosha->getCard(id), reason, nullptr);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (event == PreHpLost) return true;
        if (event == ConfirmPlayerNum) {
            PlayerNumStruct count = ctx.original_data->value<PlayerNumStruct>();
            count.m_num += qBound(0, ctx.extra_data.toInt(), ctx.owner->getPile("heavenly_army").size());
            *ctx.original_data = QVariant::fromValue(count);
        } else {
            const int count = ctx.owner->getPlayerNumWithSameKingdom(objectName(), QString(), MaxCardsType::Normal);
            if (count > 0) ctx.owner->addToPile("heavenly_army", room->getNCards(count));
        }
        return false;
    }
};

class HWendao : public ViewAsSkillV2 {
public:
    HWendao() : ViewAsSkillV2("heg_wendao", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HWendaoCard") && request.initiator->canDiscard(request.initiator, "he");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override {
        return request.initiator && card && request.selectedCardIds.isEmpty() && card->isRed()
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquips().contains(card))
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "HWendaoCard"; }
    EffectFlow effect(SkillContext &ctx) const override {
        Room *room = ctx.invoker->getRoom();
        for (ServerPlayer *p : room->getAlivePlayers())
            for (const Card *card : p->getEquips())
                if (Sanguosha->getCard(card->getEffectiveId())->isKindOf("PeaceSpell")) {
                    room->obtainCard(ctx.invoker, card, true);
                    return ContinueEffects;
                }
        for (int id : room->getDiscardPile())
            if (Sanguosha->getCard(id)->isKindOf("PeaceSpell")) {
                room->obtainCard(ctx.invoker, id, true);
                break;
            }
        return ContinueEffects;
    }
};

HMomentumPackage::HMomentumPackage()
    : Package("heg_momentum")
{
    // Reuse registered shared skills; only momentum-specific rules have local V2 definitions.
    General *lidian = new General(this, "heg_lidian", "wei", 3); // WEI 017
    lidian->addCompanion("heg_yuejin");
    lidian->addSkill("xunxun");
    lidian->addSkill("wangxi");

    General *zangba = new General(this, "heg_zangba", "wei", 4); // WEI 023
    zangba->addCompanion("heg_zhangliao");
    zangba->addSkill(new HHengjiang);
    zangba->addSkill(new HHengjiangDraw);
    insertRelatedSkills("heg_hengjiang", "#heg_hengjiang-draw");
    zangba->addSkill(new HHengjiangMaxCards);
    insertRelatedSkills("heg_hengjiang", "#heg_hengjiang-maxcard");

    General *madai = new General(this, "heg_madai", "shu", 4); // SHU 019
    madai->addCompanion("heg_machao");
    madai->addSkill("mashu");
    madai->addSkill("qianxi");

    General *mifuren = new General(this, "heg_mifuren", "shu", 3, false); // SHU 021
    mifuren->addSkill(new HGuixiu);
    mifuren->addSkill(new HCunsi);
    mifuren->addRelateSkill("heg_yongjue");

    General *sunce = new General(this, "heg_sunce", "wu", 4); // WU 010
    sunce->addCompanion("heg_zhouyu");
    sunce->addCompanion("heg_taishici");
    sunce->addCompanion("heg_daqiao");
    sunce->addSkill("jiang");
    sunce->addSkill(new HYingyang);
    sunce->addSkill(new HHunshang);
    sunce->addSkill(new HHunshangRemove);
    insertRelatedSkills("heg_hunshang", "#heg_hunshang");
    sunce->setDeputyMaxHpAdjustedValue(-1);
    sunce->addRelateSkill("yinghun");
    sunce->addRelateSkill("yingzi");

    General *chenwudongxi = new General(this, "heg_chenwudongxi", "wu", 4); // WU 023
    chenwudongxi->addSkill(new HDuanxie);
    chenwudongxi->addSkill(new HFenming);

    General *dongzhuo = new General(this, "heg_dongzhuo", "qun", 4); // QUN 006
    dongzhuo->addSkill(new HHengzheng);
    dongzhuo->addSkill(new HBaoling);
    dongzhuo->addRelateSkill("benghuai");

    General *zhangren = new General(this, "heg_zhangren", "qun", 4); // QUN 024
    zhangren->addSkill(new HChuanxin);
    zhangren->addSkill(new HFengshi);

    General *lord_zhangjiao = new General(this, "heg_lord_zhangjiao$", "qun", 4, true, true);
    lord_zhangjiao->addSkill(new HWuxin);
    lord_zhangjiao->addSkill(new HHongfa);
    lord_zhangjiao->addSkill(new HWendao);

    skills << new HYongjue << new HHongfaSlash;

}

ADD_PACKAGE(HMomentum)

HPeaceSpell::HPeaceSpell(Suit suit, int number)
: Armor(suit, number)
{
    setObjectName("PeaceSpell");
}

void HPeaceSpell::onUninstall(ServerPlayer *player) const{
    if (player->isAlive() && player->hasArmorEffect(objectName()))
        player->setFlags("peacespell_throwing");

    Armor::onUninstall(player);
}

class HPeaceSpellSkill : public ArmorSkillV2{
protected:
    bool usesEventSource(const SkillContext &ctx) const override
    {
        // The selector validates the moving card, independently of current equipment.
        return ctx.current_event == CardsMoveOneTime;
    }

public:
    HPeaceSpellSkill() : ArmorSkillV2("heg_PeaceSpell", "PeaceSpell") {
        events << DamageInflicted << CardsMoveOneTime;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        if (triggerEvent == DamageInflicted) {
            DamageStruct damage = data.value<DamageStruct>();
            if (ArmorSkillV2::triggerable(player) && player->hasArmorEffect("PeaceSpell", damage.from)
                && damage.nature != DamageStruct::Normal)
                result.insert(player, QStringList(objectName()));
        }
        else if (player && player->hasFlag("peacespell_throwing")) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.from_places.contains(Player::PlaceEquip)) {
                for (int i = 0; i < move.card_ids.size(); i++) {
                    if (move.from_places[i] != Player::PlaceEquip) continue;
                    const Card *card = Sanguosha->getEngineCard(move.card_ids[i]);
                    if (card->objectName() == "PeaceSpell") {
                        // onUninstall marks the former holder; card authority
                        // deliberately survives the armor's detachment.
                        result.insert(player, QStringList(objectName()));
                        break;
                    }
                }
            }
        }
        return result;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (!ctx.original_data)
            return false;
        if (triggerEvent == DamageInflicted){
            DamageStruct damage = ctx.original_data->value<DamageStruct>();

            LogMessage l;
            l.type = "#PeaceSpellNatureDamage";
            l.from = damage.from;
            l.to << damage.to;
            l.arg = QString::number(damage.damage);
            switch (damage.nature) {
            case DamageStruct::Normal: l.arg2 = "normal_nature"; break;
            case DamageStruct::Fire: l.arg2 = "fire_nature"; break;
            case DamageStruct::Thunder: l.arg2 = "thunder_nature"; break;
            }

            room->sendLog(l);
            room->setEmotion(damage.to, "armor/peacespell");

            return true;
        }
        else {
            LogMessage l;
            l.type = "#PeaceSpellLost";
            l.from = player;

            room->sendLog(l);

            player->setFlags("-peacespell_throwing");
            room->loseHp(player);
            if (player->isAlive())
                player->drawCards(2);
        }
        return false;
    }
};

class HPeaceSpellSkillMaxCards : public MaxCardsSkill{
public:
    HPeaceSpellSkillMaxCards() : MaxCardsSkill("#heg_PeaceSpell-max") {
    }

    int getExtra(const Player *target) const override {
        return getExtra(target, MaxCardsType::Max);
    }

    int getExtra(const Player *target, MaxCardsType::MaxCardsCount type) const override {
        if (!target->hasShownOneGeneral())
            return 0;

        // Virtual count dispatch keeps server Hongfa decisions authoritative.
        const QList<const Player *> targets = target->getAliveSiblings(true);

        const Player *ps_owner = NULL;
        foreach (const Player *p, targets) {
            if (p->hasArmorEffect("PeaceSpell")) {
                ps_owner = p;
                break;
            }
        }

        if (ps_owner == NULL)
            return 0;

        if (target->isFriendWith(ps_owner))
            return ps_owner->getPlayerNumWithSameKingdom("PeaceSpell", QString(), type);

        return 0;
    }
};

HMomentumEquipPackage::HMomentumEquipPackage() : Package("heg_momentum_equip", CardPack){
    HPeaceSpell *dp = new HPeaceSpell;
    dp->setParent(this);

    skills << new HPeaceSpellSkill << new HPeaceSpellSkillMaxCards;
    insertRelatedSkills("heg_PeaceSpell", "#heg_PeaceSpell-max");
}

ADD_PACKAGE(HMomentumEquip)
