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
#include "standard.h"
#include "util.h"
#include <QScopeGuard>

class HXiongyi : public ViewAsSkillV2 {
public:
    HXiongyi() : ViewAsSkillV2("heg_xiongyi") {
        frequency = Limited;
        limit_mark = "@arise";
        m_baseAmount = 3;
    }

    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override {
        return targets.isEmpty();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HXiongyiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override {
        // The activation owner pays even if an interceptor delegates the effect.
        if (!ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0) return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive()) return FinishSkill;
        Room *room = source->getRoom();
        room->broadcastSkillInvoke(objectName(), source);
        room->doSuperLightbox("heg_mateng", objectName());
        QList<ServerPlayer *> friends;
        for (ServerPlayer *p : room->getAlivePlayers()) {
            if (p->isFriendWith(source)) friends << p;
        }
        room->sortByActionOrder(friends);
        // Preserve the dynamically determined friends while using V2 target interception.
        for (ServerPlayer *p : friends) skillEffect(ctx, p);

        if (!source->isAlive() || !source->isWounded()) return ContinueEffects;
        const int count = source->getPlayerNumWithSameKingdom(objectName(), QString(), MaxCardsType::Normal);
        for (const QString &kingdom : Sanguosha->getKingdoms()) {
            if (kingdom == "god" || (source->getRole() == "careerist"
                    ? kingdom == "careerist" : kingdom == source->getKingdom())) continue;
            const int other = source->getPlayerNumWithSameKingdom(objectName(), kingdom, MaxCardsType::Normal);
            if (other > 0 && other < count) return ContinueEffects;
        }
        room->recover(source, RecoverStruct(objectName(), source));
        return ContinueEffects;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
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

class HLirang : public TriggerSkillV2 {
public:
    HLirang() : TriggerSkillV2("heg_lirang") {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    void record(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override {
        // CardsMoveOneTime is delivered once to each seat; update each owner only at its seat.
        if (!ctx.owner || ctx.owner != player || !ctx.original_data) return;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> pending = ListV2I(ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded").toList());
        const bool discard = (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD;
        // Track only card IDs per instance, including direct-to-discard V2 payments.
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const int id = move.card_ids.at(i);
            const bool owned = move.from == ctx.owner && i < move.from_places.size()
                && (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip);
            if (discard && (owned || pending.contains(id))
                && (move.to_place == Player::PlaceTable || move.to_place == Player::DiscardPile)) {
                if (!pending.contains(id)) pending << id;
            } else {
                pending.removeAll(id);
            }
        }
        const QVariantList state = ListI2V(pending);
        if (ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded").toList() != state)
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded", state);
    }

    QList<int> available(Room *room, ServerPlayer *owner, int instanceID,
                         const CardsMoveOneTimeStruct &move) const {
        QList<int> cards;
        if (move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return cards;
        const QList<int> pending = ListV2I(owner->getSkillInstanceStateValue(objectName(), instanceID, "discarded").toList());
        for (int id : move.card_ids) {
            if (pending.contains(id) && room->getCardPlace(id) == Player::DiscardPile) cards << id;
        }
        return cards;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            if (!available(room, player, id, move).isEmpty())
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.original_data) return false;
        const QList<int> cards = available(room, ctx.owner, ctx.instanceID,
            ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (cards.isEmpty()) return false;
        // An accepted concealed activation must distribute at least once after reveal.
        ctx.extra_data = QVariantMap{{"cards", ListI2V(cards)},
            {"optional", ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)}};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *kongrong = ctx.owner;
        if (!kongrong || !kongrong->isAlive()) return false;
        QList<int> cards = ListV2I(ctx.extra_data.toMap().value("cards").toList());
        for (int id : QList<int>(cards)) {
            if (room->getCardPlace(id) != Player::DiscardPile) cards.removeAll(id);
        }
        if (cards.isEmpty()) return false;
        room->broadcastSkillInvoke(objectName(), kongrong);
        const CardMoveReason previewReason(CardMoveReason::S_REASON_PREVIEW, kongrong->objectName(), objectName(), QString());
        const QList<ServerPlayer *> viewers{kongrong};
        QList<CardsMoveStruct> preview{CardsMoveStruct(cards, nullptr, kongrong,
            Player::DiscardPile, Player::PlaceHand, previewReason)};
        room->notifyMoveCards(true, preview, false, viewers);
        room->notifyMoveCards(false, preview, false, viewers);
        // Always retract the private preview, including interrupted distribution.
        const auto clearPreview = qScopeGuard([&] {
            if (cards.isEmpty()) return;
            QList<CardsMoveStruct> cleanup{CardsMoveStruct(cards, kongrong, nullptr,
                Player::PlaceHand, Player::DiscardPile, previewReason)};
            room->notifyMoveCards(true, cleanup, true, viewers);
            room->notifyMoveCards(false, cleanup, false, viewers);
        });
        bool optional = ctx.extra_data.toMap().value("optional").toBool();
        const CardMoveReason reason(CardMoveReason::S_REASON_PREVIEWGIVE, kongrong->objectName());
        while (kongrong->isAlive() && !cards.isEmpty()) {
            const QList<int> before = cards;
            if (!room->askForYiji(kongrong, cards, objectName(), true, true, optional, -1,
                    room->getOtherPlayers(kongrong), reason, "@heg_lirang-distribute", optional)) break;
            optional = true;
            QList<int> moved;
            for (int id : before) {
                if (room->getCardPlace(id) != Player::DiscardPile) {
                    moved << id;
                    cards.removeAll(id);
                }
            }
            if (moved.isEmpty()) break;
            QList<CardsMoveStruct> given{CardsMoveStruct(moved, kongrong, nullptr,
                Player::PlaceHand, Player::PlaceTable, previewReason)};
            room->notifyMoveCards(true, given, true, viewers);
            room->notifyMoveCards(false, given, true, viewers);
        }
        return false;
    }
};

class HShuangren : public TriggerSkillV2 {
public:
    HShuangren() : TriggerSkillV2("heg_shuangren") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
            return TriggerList();
        for (ServerPlayer *p : room->getOtherPlayers(player)) {
            if (player->canPindian(p)) return TriggerList{{player, QStringList{objectName()}}};
        }
        return TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) {
            if (ctx.owner->canPindian(p)) targets << p;
        }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@heg_shuangren", true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !target || !owner->canPindian(target)) return false;
        room->broadcastSkillInvoke(objectName(), 1, owner);
        // Pindian is the effect; never pay/move its cards inside the cancellable cost.
        if (!owner->pindian(target, objectName())) {
            room->broadcastSkillInvoke(objectName(), 3, owner);
            return true;
        }
        if (!owner->isAlive()) return false;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : room->getAlivePlayers()) {
            if (owner->canSlash(p, nullptr, false) && (p == target || p->isFriendWith(target))) targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *to = room->askForPlayerChosen(owner, targets, "heg_shuangren-slash", "@dummy-slash");
        if (!to) return false;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_heg_shuangren");
        room->useCard(CardUseStruct(slash, owner, to), false);
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 2; }
};

class HShuangrenTargetMod : public TargetModSkillV2 {
public:
    HShuangrenTargetMod() : TargetModSkillV2("#heg_shuangren-slash-ndl") { setBaseAmount(999); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card
            && ctx.card->getSkillName() == "heg_shuangren") return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class HSijian : public TriggerSkillV2 {
public:
    HSijian() : TriggerSkillV2("heg_sijian") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == player && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard) {
            for (ServerPlayer *p : room->getOtherPlayers(player)) {
                if (player->canDiscard(p, "he")) return TriggerList{{player, QStringList{objectName()}}};
            }
        }
        return TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) {
            if (ctx.owner->canDiscard(p, "he")) targets << p;
        }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "heg_sijian-invoke", true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        if (ctx.owner && ctx.owner->canDiscard(target, "he")) {
            room->broadcastSkillInvoke(objectName(), ctx.owner);
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0) room->throwCard(id, target, ctx.owner);
        }
        return false;
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
    // Reuse registered native skills by ID; only HEG-specific skills are defined here.
    General *huatuo = new General(this, "heg_huatuo", "qun", 3); // QUN 001
    huatuo->addSkill("jijiu");
    huatuo->addSkill("qingnang");

    General *lvbu = new General(this, "heg_lvbu", "qun", 5); // QUN 002
    lvbu->addCompanion("heg_diaochan");
    lvbu->addSkill("wushuang");

    General *diaochan = new General(this, "heg_diaochan", "qun", 3, false); // QUN 003
    diaochan->addSkill("lijian");
    diaochan->addSkill("biyue");

    General *yuanshao = new General(this, "heg_yuanshao", "qun"); // QUN 004
    yuanshao->addCompanion("heg_yanliangwenchou");
    yuanshao->addSkill("luanji");

    General *yanliangwenchou = new General(this, "heg_yanliangwenchou", "qun"); // QUN 005
    yanliangwenchou->addSkill("shuangxiong");

    General *jiaxu = new General(this, "heg_jiaxu", "qun", 3); // QUN 007
    jiaxu->addSkill("wansha");
    jiaxu->addSkill("luanwu");
    jiaxu->addSkill("weimu");

    General *pangde = new General(this, "heg_pangde", "qun"); // QUN 008
    pangde->addSkill("mashu");
    pangde->addSkill("mengjin");

    General *zhangjiao = new General(this, "heg_zhangjiao", "qun", 3); // QUN 010
    zhangjiao->addSkill("leiji");
    zhangjiao->addSkill("guidao");

    General *caiwenji = new General(this, "heg_caiwenji", "qun", 3, false); // QUN 012
    caiwenji->addSkill("beige");
    caiwenji->addSkill("duanchang");

    General *mateng = new General(this, "heg_mateng", "qun"); // QUN 013
    mateng->addSkill("mashu");
    mateng->addSkill(new HXiongyi);

    General *kongrong = new General(this, "heg_kongrong", "qun", 3); // QUN 014
    kongrong->addSkill(new HMingshi);
    kongrong->addSkill(new HLirang);

    General *jiling = new General(this, "heg_jiling", "qun"); // QUN 015
    jiling->addSkill(new HShuangren);
    jiling->addSkill(new HShuangrenTargetMod);
    insertRelatedSkills("heg_shuangren", "#heg_shuangren-slash-ndl");

    General *tianfeng = new General(this, "heg_tianfeng", "qun", 3); // QUN 016
    tianfeng->addSkill(new HSijian);
    tianfeng->addSkill(new HSuishi);

    General *panfeng = new General(this, "heg_panfeng", "qun"); // QUN 017
    panfeng->addSkill("kuangfu");

    General *zoushi = new General(this, "heg_zoushi", "qun", 3, false); // QUN 018
    zoushi->addSkill(new HHuoshui);
    zoushi->addSkill(new HQingcheng);

}
