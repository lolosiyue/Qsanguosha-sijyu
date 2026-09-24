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
// New HEG donor: TODO/QSanguosha-For-Hegemony-xxyheaven @ cf61c15.
// Namespace mapping: docs/hegemony-xxy-names.json.

class HWanggui : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HWanggui() : TriggerSkillV2("heg_wanggui")
    {
        events << Damage << Damaged;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || !player->hasShownSkill(objectName()) || player->hasFlag("WangguiUsed")) return TriggerList();
        DamageStruct damage = data.value<DamageStruct>();
        if (triggerEvent == Damage && damage.to && damage.to->hasFlag("Global_DFDebut")) return TriggerList();

        if (player->hasShownAllGenerals())
            return TriggerList{{player, {objectName()}}};
        else {
            QList<ServerPlayer *> all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (!player->isFriendWith(p) && p->hasShownOneGeneral())
                    return TriggerList{{player, {objectName()}}};
            }
        }

        return TriggerList();
    }

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->hasShownAllGenerals()) {
            if (player->askForSkillInvoke(this, "prompt")) {
                QStringList target_list = player->getTag("wanggui_target").toStringList();
                target_list.append("self");
                player->setTag("wanggui_target", target_list);
                room->broadcastSkillInvoke(objectName(), player);
                player->setFlags("WangguiUsed");
                return true;
            }
        } else {

            QList<ServerPlayer *> to_choose, all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (!player->isFriendWith(p) && p->hasShownOneGeneral())
                    to_choose << p;
            }
            if (to_choose.isEmpty()) return false;

            ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "wanggui-invoke", true, true);
            if (to != NULL) {
                room->broadcastSkillInvoke(objectName(), player);
                player->setFlags("WangguiUsed");

                QStringList target_list = player->getTag("wanggui_target").toStringList();
                target_list.append(to->objectName());
                player->setTag("wanggui_target", target_list);
                return true;
            }
        }

        return false;
    }

    bool effect(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QStringList target_list = player->getTag("wanggui_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("wanggui_target", target_list);
        if (target_name == "self") {
            QList<ServerPlayer *> to_choose, all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (player->isFriendWith(p))
                    to_choose << p;
            }
            room->sortByActionOrder(to_choose);
            foreach (ServerPlayer *p, to_choose) {
                if (p->isAlive())
                    p->drawCards(1, objectName());
            }
        } else {
            ServerPlayer *to = NULL;
            QList<ServerPlayer *> all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (p->objectName() == target_name) {
                    to = p;
                    break;
                }
            }

            if (to) {
                room->damage(DamageStruct(objectName(), player, to));
            }
        }
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
        if (triggerEvent == EventPhaseStart && player->getPhase() ==  Player::NotActive) {
            QList<ServerPlayer *> alls = room->getAlivePlayers();
            foreach (ServerPlayer *p, alls) {
                room->setPlayerMark(p, "##xibing", 0);
                room->removePlayerDisableShow(p, objectName());
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
            if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return {};
            int blackUses = 0;
            for (const QVariant &entry : history.value("items").toList()) {
                const QVariantMap card = entry.toMap();
                if (card.value("black").toBool() && (card.value("ndtrick").toBool()
                    || card.value("classes").toStringList().contains("Slash"))) ++blackUses;
            }
            if (use.card->isBlack() && (use.card->isKindOf("Slash") || use.card->isNDTrick())
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *huaxin = ctx.owner;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (huaxin->askForSkillInvoke(this, QVariant::fromValue(player))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, huaxin->objectName(), player->objectName());
            room->broadcastSkillInvoke(objectName(), huaxin);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *huaxin = ctx.owner;
        ctx.manual_effect = true;
        int x = player->getHp() - player->getHandcardNum();
        if (x > 0) {
            player->drawCards(x);
            room->setPlayerCardLimitation(player, "use", ".|.|.|hand", true);
            room->addPlayerMark(player, "##xibing");
        }

        if (huaxin->hasShownAllGenerals() && huaxin->getGeneral2() && player->hasShownAllGenerals() && player->getGeneral2()) {
            if (doXiBing(huaxin, huaxin, true))
                doXiBing(huaxin, player, false);
        }
        return false;
    }

private:
    static bool doXiBing(ServerPlayer *huaxin, ServerPlayer *player, bool optional)
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

        player->hideGeneral(head);
        room->setPlayerDisableShow(player, head ? "h":"d", "heg_xibing");

        return true;
    }

};

class HZhente : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhente() : TriggerSkillV2("heg_zhente")
    {
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->hasFlag("ZhenteUsed")) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if ((use.card->getTypeId() == Card::TypeBasic || use.card->isNDTrick()) && use.card->isBlack()
                && (use.from && use.from != player && use.from->isAlive()))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        CardUseStruct use = data.value<CardUseStruct>();
        player->setTag("ZhenteUsedata", data);
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(use.from));
        player->removeTag("ZhenteUsedata");
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
            player->setFlags("ZhenteUsed");
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        CardUseStruct use = data.value<CardUseStruct>();

        if (use.from->isDead()) return false;

        QString prompt = "zhente-ask:" + player->objectName() + "::" + use.card->objectName();

        use.from->setTag("ZhenteUsedata", data);
        QString choice = room->askForChoice(use.from, objectName(), "nullified+cardlimited", data, QString(), prompt);
        use.from->removeTag("ZhenteUsedata");

        if (choice == "nullified") {
            LogMessage log;
            log.type = "#ZhenteChoice1";
            log.from = use.from;
            log.to << player;
            log.arg = use.card->objectName();
            room->sendLog(log);

            use.nullified_list << player->objectName();
            data = QVariant::fromValue(use);
        } else if (choice == "cardlimited")  {

            LogMessage log;
            log.type = "#ZhenteChoice2";
            log.from = use.from;

            room->sendLog(log);

            room->setPlayerCardLimitation(use.from, "use", ".|black|.|.", true);

        }

        return false;
    }
};

class HZhiwei : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhiwei() : TriggerSkillV2("heg_zhiwei")
    {
        events << GeneralShowed << GeneralHidden << GeneralRemoved << Death;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        bool zhiwei_clear = false;
        if (triggerEvent == GeneralHidden) {
            if (player->ownSkill(this) && player->inHeadSkills(objectName()) == data.toBool())
                zhiwei_clear = true;
        } else if (triggerEvent == GeneralRemoved) {
            QString general_name = data.toString().split(":").first();
            const General *general = Sanguosha->getGeneral(general_name);
            if (general && general->hasSkill(objectName()))
                zhiwei_clear = true;
        } else if (triggerEvent == Death) {
            QList<ServerPlayer *> allplayers = room->getAlivePlayers();
            foreach (ServerPlayer *p, allplayers) {
                if (p->getMark("##zhiwei") > 0) {
                    bool clearflag = true;
                    foreach (ServerPlayer *p2, allplayers) {
                        ServerPlayer *AssistTarget = p2->getTag("ZhiweiTarget").value<ServerPlayer *>();
                        if (AssistTarget == p) {
                            clearflag = false;
                        }
                    }
                    if (clearflag)
                        room->setPlayerMark(p, "##zhiwei", 0);
                }
            }
        }

        if (zhiwei_clear) {
            ServerPlayer *AssistTarget = player->getTag("ZhiweiTarget").value<ServerPlayer *>();
            player->removeTag("ZhiweiTarget");
            if (AssistTarget) {
                LogMessage log;
                log.type = "#ZhiweiFinsh";
                log.from = player;
                log.to << AssistTarget;
                log.arg = objectName();
                room->sendLog(log);
                room->removePlayerMark(AssistTarget, "##zhiwei");
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == GeneralShowed)
            return (player->cheakSkillLocation(objectName(), data)) ? TriggerList{{player, {objectName()}}} : TriggerList();

        return TriggerList();
    }

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *to = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "zhiwei-invoke", true, true);
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);

            QStringList target_list = player->getTag("zhiwei_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("zhiwei_target", target_list);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QStringList target_list = player->getTag("zhiwei_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("zhiwei_target", target_list);

        ServerPlayer *to = NULL;
        QList<ServerPlayer *> all_players = room->getAlivePlayers();
        foreach (ServerPlayer *p, all_players) {
            if (p->objectName() == target_name) {
                to = p;
                break;
            }
        }

        if (to) {
            player->setTag("ZhiweiTarget", QVariant::fromValue(to));
            room->addPlayerMark(to, "##zhiwei");
        }

        return false;
    }
};

class HZhiweiEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhiweiEffect() : TriggerSkillV2("#heg_zhiwei-effect")
    {
        events << Damage << Damaged << Death << CardsMoveBatch;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList skill_list;
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            ServerPlayer *dead = death.who;
            ServerPlayer *AssistTarget = player->getTag("ZhiweiTarget").value<ServerPlayer *>();
            if (AssistTarget != NULL && AssistTarget == dead) {
                skill_list.insert(player, QStringList(objectName()));
            }
        }
        if (triggerEvent == CardsMoveBatch && player->getPhase() == Player::Discard) {
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from == player && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                    if (move.to_place == Player::DiscardPile) {
                        QList<int> this_cards;
                        foreach (int id, move.card_ids) {
                            if (room->getCardPlace(id) == Player::DiscardPile)
                                this_cards << id;
                        }
                        if (!this_cards.isEmpty()) {
                            ServerPlayer *AssistTarget = player->getTag("ZhiweiTarget").value<ServerPlayer *>();
                            if (AssistTarget != NULL && AssistTarget->isAlive()) {
                                skill_list.insert(player, QStringList(objectName()));
                            }
                        }
                    }
                }
            }


        }
        if (triggerEvent == Damage || triggerEvent == Damaged) {
            foreach (ServerPlayer *luyusheng, room->getAllPlayers()) {
                ServerPlayer *AssistTarget = luyusheng->getTag("ZhiweiTarget").value<ServerPlayer *>();
                if (AssistTarget == player && (triggerEvent == Damage || !luyusheng->isKongcheng()))
                    skill_list.insert(luyusheng, QStringList(objectName()));
            }
        }
        return skill_list;
    }

    bool pay(TriggerEvent , Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *luyusheng = ctx.owner;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        if (triggerEvent == Damage) {
            LogMessage log;
            log.type = "#ZhiweiEffect1";
            log.from = luyusheng;
            log.to << player;
            log.arg = "heg_zhiwei";
            room->sendLog(log);
            room->notifySkillInvoked(luyusheng, "heg_zhiwei");
            luyusheng->drawCards(1, "heg_zhiwei");
        }
        if (triggerEvent == Damaged) {
            QList<int> all_cards = luyusheng->forceToDiscard(10086, false);
            if (all_cards.isEmpty()) return false;
            LogMessage log;
            log.type = "#ZhiweiEffect2";
            log.from = luyusheng;
            log.to << player;
            log.arg = "heg_zhiwei";
            room->sendLog(log);
            room->notifySkillInvoked(luyusheng, "heg_zhiwei");
            int index = qsanRandomBounded(all_cards.length());
            int id = all_cards.at(index);
            CardMoveReason mreason(CardMoveReason::S_REASON_THROW, luyusheng->objectName(), QString(), "heg_zhiwei", QString());
            room->throwCard(Sanguosha->getCard(id), mreason, luyusheng);

        }
        if (triggerEvent == CardsMoveBatch) {
            QList<int> this_cards;
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from == player && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                    if (move.to_place == Player::DiscardPile) {
                        foreach (int id, move.card_ids) {
                            if (room->getCardPlace(id) == Player::DiscardPile)
                                this_cards << id;
                        }
                    }
                }
            }

            if (!this_cards.isEmpty()) {
                ServerPlayer *AssistTarget = luyusheng->getTag("ZhiweiTarget").value<ServerPlayer *>();
                if (AssistTarget != NULL && AssistTarget->isAlive()) {
                    LogMessage log;
                    log.type = "#ZhiweiEffect3";
                    log.from = luyusheng;
                    log.to << AssistTarget;
                    log.arg = "heg_zhiwei";
                    room->sendLog(log);
                    room->notifySkillInvoked(luyusheng, "heg_zhiwei");
                    DummyCard dummy(this_cards);
                    room->obtainCard(AssistTarget, &dummy);

                }
            }

        }
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            LogMessage log;
            log.type = "#ZhiweiEffect4";
            log.from = luyusheng;
            log.to << death.who;
            log.arg = "heg_zhiwei";
            room->sendLog(log);
            room->notifySkillInvoked(luyusheng, "heg_zhiwei");
            if (luyusheng->getGeneral()->hasSkill("heg_zhiwei") && luyusheng->hasShownAllGenerals() && luyusheng->getGeneral2()) {
                luyusheng->hideGeneral(true);
            }
            if (luyusheng->getGeneral2() && luyusheng->getGeneral2()->hasSkill("heg_zhiwei") && luyusheng->hasShownAllGenerals()) {
                luyusheng->hideGeneral(false);
            }
        }
        return false;
    }
};

class HQiao : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HQiao() : TriggerSkillV2("heg_qiao")
    {
        events << TargetConfirmed << EventPhaseStart;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
         if (triggerEvent == EventPhaseStart && player->getPhase() == Player::NotActive) {
             QList<ServerPlayer *> allplayers = room->getAlivePlayers();
             foreach (ServerPlayer *p, allplayers) {
                 room->setPlayerMark(p, "QiaoUsedTimes", 0);
             }
         }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || triggerEvent != TargetConfirmed || player->getMark("QiaoUsedTimes") > 1)
            return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() == Card::TypeSkill) return TriggerList();
        if (use.from && !player->willBeFriendWith(use.from) && !use.from->isNude())
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        CardUseStruct use = data.value<CardUseStruct>();
        player->setTag("QiaoUsedata", data);
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(use.from));
        player->removeTag("QiaoUsedata");
        if (invoke) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            room->addPlayerMark(player, "QiaoUsedTimes");
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        CardUseStruct use = data.value<CardUseStruct>();
        ServerPlayer *use_from = use.from;
        if (player->canDiscard(use_from, "he")) {
            CardMoveReason reason = CardMoveReason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), use_from->objectName(), objectName(), QString());
            const Card *card = Sanguosha->getCard(room->askForCardChosen(player, use_from, "he", objectName(), false, Card::MethodDiscard));
            room->throwCard(card, reason, use_from, player);
        }
        room->askForDiscard(player, "qiao_discard", 1, 1, false, true, "@qiao-discard");
        return false;
    }
};

class HChengshang : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HChengshang() : TriggerSkillV2("heg_chengshang")
    {
        events << CardFinished << EventPhaseChanging;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
         if (triggerEvent == EventPhaseChanging) {
             QList<ServerPlayer *> allplayers = room->getAlivePlayers();
             foreach (ServerPlayer *p, allplayers) {
                 room->setPlayerFlag(p, "-ChengshangUsed");
             }
         }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || triggerEvent != CardFinished
                || player->getPhase() != Player::Play || player->hasFlag("ChengshangUsed")) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() == Card::TypeSkill || use.card->subcardsLength() != 1) return {};
        const QVariantMap damage = player->getRoom()->queryCardUseDamage();
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()
            || !damage.value("items").toList().isEmpty()) return {};
        foreach (ServerPlayer *to, use.to) {
            if (!to->willBeFriendWith(player))
                return TriggerList{{player, {objectName()}}};
        }

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        CardUseStruct use = data.value<CardUseStruct>();
        QList<int> drawPile = room->getDrawPile(), to_get;

        foreach (int id, drawPile) {
            const Card *card = Sanguosha->getCard(id);
            if (card->getSuit() == use.card->getSuit() && card->getNumber() == use.card->getNumber())
                to_get << id;
        }
        if (!to_get.isEmpty()) {
            DummyCard dummy(to_get);
            room->obtainCard(player, &dummy, true);
            room->setPlayerFlag(player, "ChengshangUsed");
        }

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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
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
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        // Native marks preserve the signed accumulated hand-limit correction.
        int x = player->getMark("@heg_kuangcai");
        const int used = room->countHistoryCards(player);
        if (used < 0) return false;
        if (used == 0) {
            x++;
        } else {
            x--;
        }
        room->setPlayerMark(player, "@heg_kuangcai", x);
        return false;
    }
};

class HKuangcaiMaxCards : public MaxCardsSkillV2
{
public:
    HKuangcaiMaxCards() : MaxCardsSkillV2("#heg_kuangcai-maxcards")
    {
    }

    virtual int getExtra(const Player *target) const
    {
        return target->getMark("@heg_kuangcai");
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        return CorrectSkillResult::useAmount(getExtra(context.primary));
    }
};

class HKuangcaiTarget : public TargetModSkillV2
{
public:
    HKuangcaiTarget() : TargetModSkillV2("#heg_kuangcai-target")
    {
        pattern = "^SkillCard";
    }

    virtual int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        if (from->hasShownSkill("heg_kuangcai") && from->getPhase() != Player::NotActive)
            return 1000;

        return 0;
    }

    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        if (from->hasShownSkill("heg_kuangcai") && from->getPhase() != Player::NotActive)
            return 1000;

        return 0;
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (context.modType == Residue)
            return CorrectSkillResult::useAmount(getResidueNum(context.primary, context.card, context.secondary));
        if (context.modType == DistanceLimit)
            return CorrectSkillResult::useAmount(getDistanceLimit(context.primary, context.card, context.secondary));
        if (context.modType == ExtraTarget)
            return CorrectSkillResult::useAmount(getExtraTargetNum(context.primary, context.card));
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
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->isKongcheng()) return TriggerList();
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

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        CardUseStruct use = data.value<CardUseStruct>();
        player->setTag("ShejianUsedata", data);
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(use.from));
        player->removeTag("ShejianUsedata");
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        QList<int> all_cards = player->forceToDiscard(10086, false);
        player->throwAllHandCards();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.from && use.from->isAlive() && player->isAlive()) {
            int x = all_cards.length();

            QStringList choices;
            choices << "damage";
            if (x > 0 && !use.from->isNude())
                choices << "discard";

            QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(use.from), "discard+damage",
                               "@shejian-choice::"+use.from->objectName()+":"+ QString::number(x));
            if (choice == "damage")
                room->damage(DamageStruct(objectName(), player, use.from));
            else if (choice == "discard" && player->canDiscard(use.from, "he")) {
                QList<int> to_throw = room->askForCardsChosen(player, use.from, "he", objectName(), x, x, false, Card::MethodDiscard);
                if (!to_throw.isEmpty()) {
                    CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, player->objectName(), use.from->objectName(), QString(), QString());
                    room->moveCardsAtomic(CardsMoveStruct(to_throw, NULL, Player::DiscardPile, reason), true);
                }
            }
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
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || !player->hasShownOneGeneral()
                || player->getHp() < 1 || player->hasFlag("yusuiUsed")) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() != Card::TypeSkill && use.card->isBlack()
                && (use.from && use.from->hasShownOneGeneral() && !use.from->isFriendWith(player) && use.from->isAlive()))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        CardUseStruct use = data.value<CardUseStruct>();
        if (player->askForSkillInvoke(this, QVariant::fromValue(use.from))) {
            room->broadcastSkillInvoke(objectName(), player);
            room->setPlayerFlag(player, "yusuiUsed");
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        CardUseStruct use = data.value<CardUseStruct>();
        ServerPlayer *target = use.from;
        room->loseHp(player);
        if (target == NULL || target->isDead() || player->isDead()) return false;
        QStringList choices;
        if (target->getHp() > player->getHp()) choices << "losehp";
        if (!target->isNude()) choices << "discard";
        if (choices.isEmpty()) return false;

        QString choice =room->askForChoice(player, objectName(), choices.join("+"), data, "losehp+discard", "@yusui-choice::"+target->objectName());

        if (choice == "losehp" && target->getHp() > player->getHp())
            room->loseHp(target, target->getHp() - player->getHp());
        else if (choice == "discard")
            room->askForDiscard(target, "yusui_discard", target->getMaxHp(), target->getMaxHp());

        return false;
    }
};

HBoyanCard::HBoyanCard()
{

}

bool HBoyanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void HBoyanCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    target->fillHandCards(target->getMaxHp(), "heg_boyan");
    room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true);
    room->addPlayerMark(target, "##boyan");

    if (source->isAlive() && target->isAlive() &&
            (room->askForChoice(source, "heg_boyan", "yes+no", QVariant::fromValue(target), QString(),
                               "@boyan-zongheng::"+target->objectName()) == "yes")) {

        room->acquireSkillForSlot(target, "heg_boyanzongheng", false, true, false);
    }


}

class HBoyanViewAsSkill : public ViewAsSkillV2
{
public:
    HBoyanViewAsSkill() : ViewAsSkillV2("heg_boyan") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HBoyanCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HBoyanCard *skill_card = new HBoyanCard;
        skill_card->setShowSkill(objectName());
        return skill_card;
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
        events << EventPhaseStart;
        view_as_skill = new HBoyanViewAsSkill;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player->getPhase() == Player::NotActive) {
            room->detachSkillForSlot(player, "heg_boyanzongheng", false);
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                room->setPlayerMark(p, "##boyan", 0);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

HBoyanZonghengCard::HBoyanZonghengCard()
{

}

bool HBoyanZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void HBoyanZonghengCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *target = effect.to;
    Room *room = target->getRoom();
    room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true);
    room->addPlayerMark(target, "##boyan");
}

class HBoyanZongheng : public ViewAsSkillV2
{
public:
    HBoyanZongheng() : ViewAsSkillV2("heg_boyanzongheng") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HBoyanZonghengCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HBoyanZonghengCard *skill_card = new HBoyanZonghengCard;
        skill_card->setShowSkill(objectName());
        return skill_card;
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        Room *room = player->getRoom();
        QList<ServerPlayer *> to_choose, all_players = room->getAlivePlayers();
        foreach (ServerPlayer *p, all_players) {
            if (player->isFriendWith(p))
                to_choose << p;
        }
        room->sortByActionOrder(to_choose);
        foreach (ServerPlayer *p, to_choose) {
            if (p->isAlive())
                p->drawCards(1, objectName());
        }
        return false;
    }
};

HWeimengCard::HWeimengCard()
{

}

bool HWeimengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void HWeimengCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    if (source->isAlive() && target->isAlive() && !target->isKongcheng() && source->getHp() > 0) {
        QList<int> to_get = room->askForCardsChosen(source, target, "h", "heg_weimeng", 1, source->getHp());
        if (!to_get.isEmpty()) {
            CardMoveReason reason1(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
            DummyCard dummy1(to_get);
            room->obtainCard(source, &dummy1, reason1, false);
            if (source->isAlive() && target->isAlive() && !source->isNude()) {
                int num = qMin(to_get.length(), source->getCardCount(true));
                target->setFlags("WeimengTarget");
                QString prompt = QString("@weimeng-give::%1:%2").arg(target->objectName()).arg(num);
                QList<int> ints = room->askForExchangeCards(source, "weimeng_giveback", num, num, prompt);
                target->setFlags("-WeimengTarget");
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "heg_weimeng", QString());
                reason.m_playerId = target->objectName();
                DummyCard dummy2(ints);
                room->moveCardTo(&dummy2, target, Player::PlaceHand, reason);
            }
        }
    }
    if (source->isAlive() && target->isAlive() &&
            (room->askForChoice(source, "heg_weimeng", "yes+no", QVariant::fromValue(target), QString(),
                               "@weimeng-zongheng::"+target->objectName()) == "yes")) {
        room->acquireSkillForSlot(target, "heg_weimengzongheng", false, true, false);
    }
}

class HWeimengViewAsSkill : public ViewAsSkillV2
{
public:
    HWeimengViewAsSkill() : ViewAsSkillV2("heg_weimeng") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HWeimengCard") && request.initiator->getHp() > 0;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HWeimengCard *skill_card = new HWeimengCard;
        skill_card->setShowSkill(objectName());
        return skill_card;
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
        events << EventPhaseStart;
        view_as_skill = new HWeimengViewAsSkill;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player->getPhase() == Player::NotActive) {
            room->detachSkillForSlot(player, "heg_weimengzongheng", false);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

HWeimengZonghengCard::HWeimengZonghengCard()
{

}

bool HWeimengZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void HWeimengZonghengCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    if (source->isDead() || target->isDead() || target->isKongcheng()) return;
    int card_id1 = room->askForCardChosen(source, target, "h", "heg_weimeng", false, Card::MethodGet);
    CardMoveReason reason1(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
    room->obtainCard(source, Sanguosha->getCard(card_id1), reason1, false);

    if (source->isDead() || target->isDead() || source->isNude()) return;

    target->setFlags("WeimengTarget");
    QString prompt = QString("@weimeng-give::%1:%2").arg(target->objectName()).arg(1);
    QList<int> ints = room->askForExchangeCards(source, "weimeng_giveback", 1, 1, prompt);
    target->setFlags("-WeimengTarget");

    int card_id = -1;
    if (ints.isEmpty()) {
        card_id = source->getCards("he").first()->getEffectiveId();
    } else
        card_id = ints.first();

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "heg_weimeng", QString());
    reason.m_playerId = target->objectName();
    room->moveCardTo(Sanguosha->getCard(card_id), target, Player::PlaceHand, reason);

}

class HWeimengZongheng : public ViewAsSkillV2
{
public:
    HWeimengZongheng() : ViewAsSkillV2("heg_weimengzongheng") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HWeimengZonghengCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HWeimengZonghengCard *skill_card = new HWeimengZonghengCard;
        skill_card->setShowSkill(objectName());
        return skill_card;
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        player->drawCards(1, objectName());
        return false;
    }
};

HDaoshuCard::HDaoshuCard()
{

}

bool HDaoshuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canGet(to_select, "h") && to_select != Self;
}

void HDaoshuCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();
    Card::Suit suit = room->askForSuit(source, "heg_daoshu");

    LogMessage log;
    log.type = "#ChooseSuit";
    log.from = source;
    log.arg = Card::Suit2String(suit);
    room->sendLog(log);

    int card_id = room->askForCardChosen(source, target, "h", "heg_daoshu", false, Card::MethodGet);

    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
    CardsMoveStruct daoshu_move(card_id, source, Player::PlaceHand, reason);
    QVariant data = room->moveCardsSub(daoshu_move, true);

    QVariantList move_datas = data.toList();
    QList<int> getcard;
    foreach (QVariant move_data, move_datas) {
        CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
        if (move.from == target && move.reason.m_reason == CardMoveReason::S_REASON_EXTRACTION) {
            for (int i = 0; i < move.card_ids.length(); ++i) {
                int id = move.card_ids.at(i);
                if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip) {
                    getcard << id;
                }
            }
        }
    }

    if (getcard.isEmpty()) return;

    bool cheak_suit = false;

    QStringList card_suits;
    card_suits << "spade" << "heart" << "club" << "diamond";

    foreach (int id, getcard) {
        const Card *c = Sanguosha->getCard(id);
        if (c->getSuit() == suit)
            cheak_suit = true;
        else
            card_suits.removeOne(c->getSuitString());
    }

    if (cheak_suit) {
        room->damage(DamageStruct("heg_daoshu", source, target));
        room->addPlayerHistory(source, getClassName(), -1);
    }

    if (card_suits.length() < 4) {
        const Card *to_give = NULL;
        foreach (const Card *c, source->getHandcards()) {
            if (card_suits.contains(c->getSuitString())) {
                to_give = c;
                break;
            }
        }
        if (to_give == NULL) {
            room->showAllCards(source);
            return;
        }
        const Card *select = room->askForCard(source, ".|" + card_suits.join(",") + "|.|hand!", "@daoshu-give::" + target->objectName(),
                                              QVariant(), Card::MethodNone);
        if (select == NULL)
            select = to_give;

        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "heg_daoshu", QString());
        room->obtainCard(target, select, reason, true);
    }
}

class HDaoshu : public ViewAsSkillV2
{
public:
    HDaoshu() : ViewAsSkillV2("heg_daoshu") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HDaoshuCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HDaoshuCard *card = new HDaoshuCard;
        card->setShowSkill(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HDaoshuCard"; }
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        int x = player->getRoom()->countHistoryCards(player);
        if (x > 0)
            player->drawCards(qMin(x, 5), objectName());

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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        player->throwAllHandCards();
        player->fillHandCards(player->getMaxHp(), objectName());

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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *victim;
        if ((victim = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "heg_@lianyou", true, true)) != NULL) {
            room->broadcastSkillInvoke(objectName(), player);

            QStringList target_list = player->getTag("lianyou_target").toStringList();
            target_list.append(victim->objectName());
            player->setTag("lianyou_target", target_list);

            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QStringList target_list = player->getTag("lianyou_target").toStringList();
        QString target_name = target_list.last();
        target_list.removeLast();
        player->setTag("lianyou_target", target_list);

        ServerPlayer *target = room->findPlayerByObjectName(target_name);
        if (target != NULL) {
            room->addPlayerMark(target, "##xinghuo");
            room->acquireSkillForSlot(target, "heg_xinghuo", false, true, false);
        }
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
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
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        damage.damage++;
        data = QVariant::fromValue(damage);
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        Room *room = player->getRoom();
        int x  = player->getMaxHp();
        QList<ServerPlayer *> all_players = room->getAlivePlayers();

        QList<ServerPlayer *> to_choose;
        foreach(ServerPlayer *p, all_players) {
            if (!p->isNude())
                to_choose << p;
        }

        QStringList choices;
        if (player->getMark("gongxiuchoice") != 1)
            choices << "draw";
        if (player->getMark("gongxiuchoice") != 2 && !to_choose.isEmpty())
            choices << "discard";

        if (!choices.isEmpty()) {

            QString choice = room->askForChoice(player, "gongxiu_choose", choices.join("+"), QVariant(), "draw+discard", "@gongxiu-choose");
            if (choice == "draw") {
                room->setPlayerMark(player, "gongxiuchoice", 1);
                QList<ServerPlayer *> choosees = room->askForPlayersChosen(player, all_players, "gongxiu_draw", 1, x, "@gongxiu-draw:::" + QString::number(x));
                room->sortByActionOrder(choosees);
                foreach (ServerPlayer *target, choosees) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                }
                foreach (ServerPlayer *target, choosees) {
                    target->drawCards(1, objectName());
                }
            }
            if (choice == "discard") {
                room->setPlayerMark(player, "gongxiuchoice", 2);
                QList<ServerPlayer *> choosees = room->askForPlayersChosen(player, to_choose, "gongxiu_discard", 1, x, "@gongxiu-discard:::" + QString::number(x));
                room->sortByActionOrder(choosees);
                foreach (ServerPlayer *target, choosees) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                }
                foreach (ServerPlayer *target, choosees) {
                    room->askForDiscard(target, "gongxiu_throw", 1, 1, false, true, "@gongxiu-throw");
                }
            }
        }
        draw.num -= 1;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

HJingheCard::HJingheCard()
{
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

void HJingheCard::extraCost(Room *room, const CardUseStruct &card_use) const
{
    room->showCard(card_use.from, subcards);
    SkillCard::extraCost(room, card_use);
}

void HJingheCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    QStringList skill_list;
    skill_list << "heg_leiji_tianshu" << "heg_yinbing" << "heg_huoqi" << "heg_guizhu"
               << "heg_xianshou" << "heg_lundao" << "heg_guanyue" << "heg_yanzheng";

    qsanShuffle(skill_list);

    int x = qMin(targets.length(), skill_list.length());

    skill_list = skill_list.mid(0, x);

    skill_list << "cancel";
    QStringList available = skill_list;

    QStringList target_names;
    foreach (ServerPlayer *p, targets) {
        target_names << p->objectName();
    }
    source->setTag("JingheTargets", target_names.join("+"));

    foreach (ServerPlayer *target, targets) {
        if (source->isDead() || available.length() == 1) break;
        if (target->isDead()) continue;

        QString skill_name = room->askForChoice(target, "jinghe_skill", available.join("+"),
                                                QVariant(), skill_list.join("+"), "@jinghe-choose");

        if (skill_name == "cancel") continue;

        available.removeOne(skill_name);

        room->acquireSkillForSlot(target, skill_name, false, true, false);

        QStringList record = target->getTag("JingheSkills:"+source->objectName()).toStringList();
        record << skill_name;
        target->setTag("JingheSkills:"+source->objectName(), QVariant::fromValue(record));

    }

    source->removeTag("JingheTargets");
}

class HJingheViewAsSkill : public ViewAsSkillV2
{
public:
    HJingheViewAsSkill() : ViewAsSkillV2("heg_jinghe") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HJingheCard");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId()) || candidate->isEquipped()
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
        if (!cardSelectionFeasible(request)) return nullptr;
        HJingheCard *card = new HJingheCard;
        card->addSubcards(request.selectedCardIds);
        card->setShowSkill(objectName());
        return card;
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
        events << EventPhaseStart << Death;
        view_as_skill = new HJingheViewAsSkill;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart) return true;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (player != death.who) return true;
        }
        QList<ServerPlayer *> players = room->getAlivePlayers();
        foreach (ServerPlayer *p, players) {
            QStringList skills = p->getTag("JingheSkills:"+player->objectName()).toStringList();
            QStringList detachList;
            foreach(QString skill_name, skills)
                detachList.append("-" + skill_name + "!");
            room->handleAcquireDetachSkills(p, detachList, true);
            p->setTag("JingheSkills:"+player->objectName(), QVariant());
        }
        return true;
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "leiji-invoke", true, true);
        if (target) {
            player->setTag("leiji-target", QVariant::fromValue(target));
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        } else {
            player->removeTag("leiji-target");
            return false;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zhangjiao = ctx.invoker;
        ctx.manual_effect = true;
        ServerPlayer *target = zhangjiao->getTag("leiji-target").value<ServerPlayer *>();
        zhangjiao->removeTag("leiji-target");
        if (target) {

            JudgeStruct judge;
            judge.pattern = ".|spade";
            judge.good = false;
            judge.negative = true;
            judge.reason = objectName();
            judge.who = target;

            room->judge(judge);

            if (judge.isBad())
                room->damage(DamageStruct(objectName(), zhangjiao, target, 2, DamageStruct::Thunder));
        }
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

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
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

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        if (triggerEvent == Predamage) {
            DamageStruct damage = data.value<DamageStruct>();
            room->loseHp(damage.to, damage.damage);
            return true;
        } else if (triggerEvent == HpLost) {
            player->drawCards(1, objectName());
        }
        return false;
    }
};

HHuoqiCard::HHuoqiCard()
{
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

void HHuoqiCard::onEffect(CardEffectStruct &effect) const
{
    RecoverStruct recover;
    recover.card = this;
    recover.who = effect.from;
    effect.to->getRoom()->recover(effect.to, recover);
    effect.to->drawCards(1, "heg_huoqi");
}

class HHuoqi : public ViewAsSkillV2
{
public:
    HHuoqi() : ViewAsSkillV2("heg_huoqi", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HHuoqiCard");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && Sanguosha && request.selectedCardIds.isEmpty()
            && candidate && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
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

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HHuoqiCard *first = new HHuoqiCard;
        first->addSubcard(request.selectedCardIds.first());
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
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
        events << Dying;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && !player->hasFlag("guizhuUsed"))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->setPlayerFlag(player, "guizhuUsed");
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent , Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        player->drawCards(2, objectName());
        return false;
    }
};

HXianshouCard::HXianshouCard()
{
}

bool HXianshouCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void HXianshouCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isAlive())
        effect.to->drawCards(effect.to->isWounded() ? 1 : 2, "heg_xianshou");
}

class HXianshou : public ViewAsSkillV2
{
public:
    HXianshou() : ViewAsSkillV2("heg_xianshou") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HXianshouCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HXianshouCard *first = new HXianshouCard;
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
    }

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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
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
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *from = damage.from;
        Room *room = player->getRoom();
        if (player->getHandcardNum() < from->getHandcardNum() && player->canDiscard(from, "he")) {
            int card_id = room->askForCardChosen(player, from, "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(Sanguosha->getCard(card_id), from, player);
        } else if (player->getHandcardNum() > from->getHandcardNum())
            player->drawCards(1, objectName());
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        Room *room = player->getRoom();
        QList<int> ids = room->getNCards(2);
        room->fillAG(ids, player);
        int card_id = room->askForAG(player, ids, false, objectName());
        room->clearAG(player);
        room->returnToTopDrawPile(ids);
        room->obtainCard(player, card_id, false);
        return false;
    }
};

class HYanzheng : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HYanzheng() : TriggerSkillV2("heg_yanzheng")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (player->getPhase() == Player::Start && player->getHandcardNum() > 1) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QList<int> ints = room->askForExchangeCards(player, objectName(), 1, 0, "@yanzheng", "", ".|.|.|hand");

        if (!ints.isEmpty()) {
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            QList<const Card *> cards = player->getHandcards();
            QList<int> to_throw;
            foreach (const Card *c, cards) {
                int id = c->getId();
                if (!ints.contains(id) && player->canDiscard(player, id))
                    to_throw << id;
            }
            CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), QString(), objectName(), QString());
            CardsMoveStruct dis_move(to_throw, NULL, Player::DiscardPile, reason);
            QVariant data = room->moveCardsSub(dis_move, true);

            QVariantList move_datas = data.toList();

            int x = 0;
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from == player && move.reason.m_reason == CardMoveReason::S_REASON_THROW) {
                    for (int i = 0; i < move.card_ids.length(); ++i) {
                        const Card *card = Sanguosha->getCard(move.card_ids.at(i));
                        if (card && (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip)) {
                            x++;
                        }
                    }
                }
            }
            room->setPlayerMark(player, "yanzhengCount", x);

            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ctx.manual_effect = true;
        Room *room = player->getRoom();
        int x = player->getMark("yanzhengCount");
        room->setPlayerMark(player, "yanzhengCount", 0);
        if (x == 0) return false;

        QList<ServerPlayer *> choosees = room->askForPlayersChosen(player, room->getAlivePlayers(),
                             "yanzheng_damage", 1, x, "@yanzheng-damage:::" + QString::number(x));
        if (choosees.length() > 0) {
            room->sortByActionOrder(choosees);
            foreach (ServerPlayer *target, choosees) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
            }
            foreach (ServerPlayer *target, choosees) {
                room->damage(DamageStruct(objectName(), player, target));
            }

        }
        return false;
    }
};

HFenglveCard::HFenglveCard()
{
}

bool HFenglveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void HFenglveCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    if (source->canPindian(target)) {

        std::unique_ptr<PindianStruct> pd(source->PinDian(target, "heg_fenglve", NULL));
        if (!pd) return;

        if (source->isDead() || target->isDead()) return;

        int x1 = pd->from_number,x2 = pd->to_number;

        if (x1 > x2 && !target->isAllNude()) {
            QList<int> to_get;
            QList<const Card *> cards = target->getCards("hej");

            if (cards.length() > 2)
                to_get = room->askForCardsChosen(target, target, "hej", "heg_fenglve", 2, 2);
            else {
                foreach (const Card *c, cards) {
                    to_get << c->getEffectiveId();
                }
            }
            if (!to_get.isEmpty()) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), source->objectName(), "heg_fenglve", QString());
                reason.m_playerId = source->objectName();
                DummyCard dummy1(to_get);
                room->obtainCard(source, &dummy1, reason, false);
            }

        } else if (x1 < x2 && !source->isNude()) {

            target->setFlags("FenglveTarget");
            QString prompt = QString("@fenglve-give1::%1").arg(target->objectName());
            QList<int> ints = room->askForExchangeCards(source, "fenglve_give", 1, 1, prompt);
            target->setFlags("-FenglveTarget");

            int card_id = -1;
            if (ints.isEmpty()) {
                card_id = source->getCards("he").first()->getEffectiveId();
            } else
                card_id = ints.first();

            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "heg_fenglve", QString());
            room->moveCardTo(Sanguosha->getCard(card_id), target, Player::PlaceHand, reason);

        }
    }

    if (source->isAlive() && target->isAlive() &&
            (room->askForChoice(source, "heg_fenglve", "yes+no", QVariant::fromValue(target), QString(),
                               "@fenglve-zongheng::"+target->objectName()) == "yes")) {

        room->acquireSkillForSlot(target, "heg_fenglvezongheng", false, true, false);
    }
}

class HFenglveViewAsSkill : public ViewAsSkillV2
{
public:
    HFenglveViewAsSkill() : ViewAsSkillV2("heg_fenglve") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HFenglveCard") && !request.initiator->isKongcheng();
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HFenglveCard *first = new HFenglveCard;
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
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
        events << EventPhaseStart;
        view_as_skill = new HFenglveViewAsSkill;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player->getPhase() == Player::NotActive) {
            room->detachSkillForSlot(player, "heg_fenglvezongheng", false);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

HFenglveZonghengCard::HFenglveZonghengCard()
{
}

bool HFenglveZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void HFenglveZonghengCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    if (source->canPindian(target)) {

        std::unique_ptr<PindianStruct> pd(source->PinDian(target, "heg_fenglvezongheng", NULL));
        if (!pd) return;

        if (source->isDead() || target->isDead()) return;

        int x1 = pd->from_number,x2 = pd->to_number;

        if (x1 > x2 && !target->isAllNude()) {
            int card_id = room->askForCardChosen(target, target, "hej", "heg_fenglve");
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), source->objectName(), "heg_fenglve", QString());
            reason.m_playerId = source->objectName();
            room->moveCardTo(Sanguosha->getCard(card_id), source, Player::PlaceHand, reason);

        } else if (x1 < x2 && !source->isNude()) {
            QList<int> ints;
            if (source->getCardCount(true) < 3) {
                ints = source->forceToDiscard(2, true, false);
            } else {
                target->setFlags("FenglveTarget");
                QString prompt = QString("@fenglve-give2::%1").arg(target->objectName());
                ints = room->askForExchangeCards(source, "fenglve_give", 2, 2, prompt);
                target->setFlags("-FenglveTarget");
            }

            if (!ints.isEmpty()) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), "heg_fenglve", QString());
                DummyCard dummy2(ints);
                room->obtainCard(target, &dummy2, reason, false);
            }
        }
    }
}

class HFenglveZongheng : public ViewAsSkillV2
{
public:
    HFenglveZongheng() : ViewAsSkillV2("heg_fenglvezongheng") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HFenglveZonghengCard") && !request.initiator->isKongcheng();
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HFenglveZonghengCard *first = new HFenglveZonghengCard;
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
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
        events << DamageCaused;
    }

    TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to && player != damage.to) {
            QList<ServerPlayer *> owners = room->findPlayersBySkillName(objectName());
            TriggerList skill_list;
            foreach (ServerPlayer *owner, owners)
                if (owner != damage.to && owner->isFriendWith(player) && !owner->hasFlag("heg_anyongUsed"))
                    skill_list.insert(owner, QStringList(objectName()));
            return skill_list;
        }

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.invoker;
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        player->setTag("AnyongDamagedata", data);
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(target));
        player->removeTag("AnyongDamagedata");
        if (invoke) {
            room->setPlayerFlag(player, "heg_anyongUsed");
            room->broadcastSkillInvoke(objectName(), player);

            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->hasShownAllGenerals()) {
            room->loseHp(player);
            if (player->hasSkill("heg_anyong", true))
                room->detachSkillForSlot(player, "heg_anyong", player->inHeadSkills("heg_anyong"));
        } else if (damage.to->hasShownOneGeneral())  {
            room->askForDiscard(player, "anyong_discard", 2, 2, false, false, "@anyong-discard");
        }
        damage.damage+=damage.damage;

        data = QVariant::fromValue(damage);
        return false;
    }
};

class HGuowu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HGuowu() : TriggerSkillV2("heg_guowu")
    {
        events << EventPhaseStart << EventPhaseChanging;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseChanging)
            room->setPlayerMark(player, "#guowu", 0);
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && (player && player->isAlive() && player->hasSkill(objectName()))) {
            if (player->getPhase() == Player::Play && !player->isKongcheng())
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        room->showAllCards(player);
        QList<const Card *> handcards = player->getHandcards();
        QStringList types;
        foreach (const Card *card, handcards) {
            QString type_name = card->getType();
            if (!types.contains(type_name))
                types << type_name;
        }
        int x = types.length();
        room->setPlayerMark(player, "#guowu", x);
        if (x > 0) {
            int id = room->getRandomCardInPile("Slash", false);
            if (id > -1)
                player->obtainCard(Sanguosha->getCard(id));
        }
        return false;
    }
};

class HGuowuEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HGuowuEffect() : TriggerSkillV2("#heg_guowu-effect")
    {
        events << TargetSpecifying;
    }

    TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player->getMark("#guowu") > 2) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash")) {
                QList<ServerPlayer *> targets = room->getUseExtraTargets(use);
                if (!targets.isEmpty())
                    return TriggerList{{player, {objectName()}}};

            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        CardUseStruct use = data.value<CardUseStruct>();
        QList<ServerPlayer *> targets = room->getUseExtraTargets(use);
        if (!targets.isEmpty()) {

            player->setTag("GuowuUsedata", data);        //for AI

            QList<ServerPlayer *> choosees = room->askForPlayersChosen(player, targets, objectName(),
                    0, 2, "@guowu-add:::" + use.card->objectName());

            player->removeTag("GuowuUsedata");        //for AI

            if (choosees.length() > 0) {

                LogMessage log;
                log.type = "$AddCardTarget";
                log.from = player;
                log.to = choosees;
                log.card_str = use.card->toString();
                log.arg = "heg_guowu";
                room->sendLog(log);

                QStringList target_list = player->getTag("guowu_target").toStringList();

                QStringList names;
                foreach (ServerPlayer *p, choosees) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
                    names << p->objectName();
                }

                target_list << names.join("+");

                player->setTag("guowu_target", target_list);

                room->removePlayerMark(player, "#guowu");

                return true;
            }


        }
        return false;
    }

    bool effect(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        QStringList target_list = player->getTag("guowu_target").toStringList();
        if (target_list.isEmpty()) return false;
        QStringList target_names = target_list.takeLast().split("+");
        player->setTag("guowu_target", target_list);

        QList<ServerPlayer *> targets;
        foreach (QString name, target_names) {
            ServerPlayer *target = room->findPlayerByObjectName(name);
            if (target)
                targets << target;
        }
        CardUseStruct use = data.value<CardUseStruct>();
        use.to << targets;
        room->sortByActionOrder(use.to);
        data = QVariant::fromValue(use);

        return false;
    }
};

class HGuowuTargetMod : public TargetModSkillV2
{
public:
    HGuowuTargetMod() : TargetModSkillV2("#heg_guowu-targetmod")
    {
        pattern = "^SkillCard";
    }

    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        if (from->getMark("#guowu") > 1)
            return 1000;
        else
            return 0;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (context.modType == Residue)
            return CorrectSkillResult::useAmount(getResidueNum(context.primary, context.card, context.secondary));
        if (context.modType == DistanceLimit)
            return CorrectSkillResult::useAmount(getDistanceLimit(context.primary, context.card, context.secondary));
        if (context.modType == ExtraTarget)
            return CorrectSkillResult::useAmount(getExtraTargetNum(context.primary, context.card));
        return CorrectSkillResult::noEffect();
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
        // Native Wushuang initializes its shared Duel tag at priority 2.
        // Append this variant's selected targets after that initialization.
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
                ctx.sourceRef = ctx.activationRef;
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.preferredTarget ? ctx.preferredTarget : ctx.owner;
        ServerPlayer *ask_who = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ask_who->setTag("WushuangData", data); // for AI
        ask_who->setTag("WushuangTarget", QVariant::fromValue(target)); // for AI
        bool invoke = false;
        if (ask_who->hasShownSkill(this)) {
            room->sendCompulsoryTriggerLog(ask_who, objectName());
            invoke = true;
        } else invoke = ask_who->askForSkillInvoke(this, QVariant::fromValue(target));

        ask_who->removeTag("WushuangData");
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), ask_who);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), target->objectName());
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *ask_who = ctx.owner;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = !ctx.preferredTarget;
        if (ctx.preferredTarget) return false;
        CardUseStruct use = data.value<CardUseStruct>();

        if (triggerEvent == TargetSpecifying) {
            QList<ServerPlayer *> targets = room->getUseExtraTargets(use);
            if (targets.isEmpty()) return false;

        ask_who->setTag("WushuangUsedata", data);        //for AI

            QList<ServerPlayer *> choosees = room->askForPlayersChosen(ask_who, targets, "wushuang_extra", 0, 2, "@wushuang-add");

            ask_who->removeTag("WushuangUsedata");        //for AI

            if (choosees.length() > 0) {

                LogMessage log;
                log.type = "$AddCardTarget";
                log.from = ask_who;
                log.to = choosees;
                log.card_str = use.card->toString();
                log.arg = objectName();
                room->sendLog(log);

                foreach (ServerPlayer *p, choosees) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), p->objectName());
                }

                use.to << choosees;
                room->sortByActionOrder(use.to);
                data = QVariant::fromValue(use);

            }
            return false;
        }
        return false;
    }

    bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *,
        SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *ask_who = ctx.owner;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (triggerEvent == TargetSpecified) {
            if (use.card->isKindOf("Slash")) {
                int x = use.to.indexOf(target);
                QVariantList jink_list = ask_who->getTag("Jink_" + use.card->toString()).toList();
                if (x >= 0 && x < jink_list.size() && jink_list.at(x).toInt() == 1)
                    jink_list[x] = 2;
                ask_who->setTag("Jink_" + use.card->toString(), jink_list);
            } else if (use.card->isKindOf("Duel")) {
                // Native Wushuang handles the second Duel response globally;
                // its engine-owned tag is shared with the ordinary skill.
                const QString key = "Wushuang_" + use.card->toString();
                QStringList wushuang_list = room->getTag(key).toStringList();
                if (!wushuang_list.contains(target->objectName())) wushuang_list << target->objectName();
                room->setTag(key, wushuang_list);
            }
        } else if (triggerEvent == TargetConfirmed) {
            const QString key = "Wushuang_" + use.card->toString();
            QStringList wushuang_list = room->getTag(key).toStringList();
            if (!wushuang_list.contains(use.from->objectName())) wushuang_list << use.from->objectName();
            room->setTag(key, wushuang_list);
        }

        return false;
    }
};

HZhuangrongCard::HZhuangrongCard()
{
    target_fixed = true;
}

void HZhuangrongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->acquireSkillForSlot(source, "heg_wushuang_lvlingqi", false, true, false);
}

class HZhuangrongViewAsSkill : public ViewAsSkillV2
{
public:
    HZhuangrongViewAsSkill() : ViewAsSkillV2("heg_zhuangrong", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HZhuangrongCard");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && Sanguosha && request.selectedCardIds.isEmpty()
            && candidate && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
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

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HZhuangrongCard *first = new HZhuangrongCard;
        first->addSubcard(request.selectedCardIds.first());
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
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
        events << EventPhaseChanging;
        view_as_skill = new HZhuangrongViewAsSkill;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
        room->detachSkillForSlot(player, "heg_wushuang_lvlingqi", false);
        return true;
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
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
    {
        ctx.manual_effect = true;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += 2;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class HShenweiMaxCards : public MaxCardsSkillV2
{
public:
    HShenweiMaxCards() : MaxCardsSkillV2("#heg_shenwei-maxcards")
    {
    }

    virtual int getExtra(const Player *target) const
    {
        if (target->hasShownSkill("heg_shenwei"))
            return 2;
        return 0;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        return CorrectSkillResult::useAmount(getExtra(context.primary));
    }
};

class HDeshao : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HDeshao() : TriggerSkillV2("heg_deshao")
    {
        events << TargetSpecified << EventPhaseStart;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() ==  Player::NotActive) {
            QList<ServerPlayer *> allplayers = room->getAlivePlayers();
            foreach (ServerPlayer *p, allplayers) {
                room->setPlayerMark(p, "#deshao", 0);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList skill_list;
        if (triggerEvent != TargetSpecified) return skill_list;
        CardUseStruct use = data.value<CardUseStruct>();
        if (player && player->isAlive() && use.card->getTypeId() != Card::TypeSkill && use.card->isBlack()) {
            ServerPlayer *yanghu = use.to.isEmpty() ? nullptr : use.to.first();
            foreach (ServerPlayer *to, use.to) {
                if (to != yanghu) return skill_list;
            }

            if (yanghu && yanghu != player && (yanghu && yanghu->isAlive() && yanghu->hasSkill(objectName()))
                    && yanghu->getMark("#deshao") < yanghu->getHp() && yanghu->canDiscard(player, "he")) {
                int x = 0, y = 0;
                if (yanghu->hasShownGeneral1()) x++;
                if (yanghu->hasShownGeneral2()) x++;
                if (player->hasShownGeneral1()) y++;
                if (player->hasShownGeneral2()) y++;
                if (x >= y)
                    skill_list.insert(yanghu, QStringList(objectName()));
            }
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *yanghu = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        yanghu->setTag("DeshaoUsedata", data);
        bool invoke = yanghu->askForSkillInvoke(this, QVariant::fromValue(player));
        yanghu->removeTag("DeshaoUsedata");

        if (invoke) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, yanghu->objectName(), player->objectName());
            room->broadcastSkillInvoke(objectName(), yanghu);
            room->addPlayerMark(yanghu, "#deshao");
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *yanghu = ctx.owner;
        ctx.manual_effect = true;
        if (yanghu->canDiscard(player, "he")) {
            int id = room->askForCardChosen(yanghu, player, "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(id, player, yanghu);
        }
        return false;
    }
};

HMingfaCard::HMingfaCard()
{
}

bool HMingfaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->willBeFriendWith(to_select);
}

void HMingfaCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    QStringList target_list = source->getTag("MingfaTarget").toStringList();
    target_list.append(target->objectName());
    source->setTag("MingfaTarget", target_list);
    room->addPlayerMark(target, "##mingfa");

//    if (source->isAlive() && target->isAlive() &&
//            (room->askForChoice(source, "mingfa", "yes+no", QVariant::fromValue(target), QString(),
//                               "@mingfa-zongheng::"+target->objectName()) == "yes")) {
//        room->acquireSkillForSlot(target, "mingfazongheng", false, true, false);
//    }
}

class HMingfaViewAsSkill : public ViewAsSkillV2
{
public:
    HMingfaViewAsSkill() : ViewAsSkillV2("heg_mingfa") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HMingfaCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HMingfaCard *first = new HMingfaCard;
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
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

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player->getPhase() == Player::NotActive) {
            room->detachSkillForSlot(player, "heg_mingfazongheng", false);
            room->setPlayerMark(player, "##mingfa", 0);
            QList<ServerPlayer *> players = room->getAlivePlayers();
            foreach (ServerPlayer *p, players) {
                QStringList target_list = p->getTag("MingfaTarget").toStringList();
                target_list.removeAll(player->objectName());
                p->setTag("MingfaTarget", target_list);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return TriggerList();
    }
};

class HMingfaEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HMingfaEffect() : TriggerSkillV2("#heg_mingfa-effect")
    {
        events << EventPhaseChanging;
    }

    TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList skill_list;
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::NotActive && player->isAlive()) {
            QList<ServerPlayer *> players = room->getAlivePlayers();
            foreach (ServerPlayer *p, players) {
                QStringList target_list = p->getTag("MingfaTarget").toStringList();
                if (target_list.contains(player->objectName()) && player->getHandcardNum() != p->getHandcardNum()) {
                    skill_list.insert(p, QStringList(objectName()));
                }
            }
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *yanghu = ctx.owner;
        ctx.manual_effect = true;
        LogMessage log;
        log.type = "#MingfaEffect";
        log.from = yanghu;
        log.to << player;
        log.arg = "heg_mingfa";
        room->sendLog(log);
        room->notifySkillInvoked(yanghu, "heg_mingfa");
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, yanghu->objectName(), player->objectName());

        int x = player->getHandcardNum() - yanghu->getHandcardNum();
        if (x > 0) {
            yanghu->drawCards(qMin(x, 5), "heg_mingfa");
        } else if (x < 0)  {
            room->damage(DamageStruct("heg_mingfa", yanghu, player));
            if (yanghu->isAlive() && player->isAlive() && yanghu->canGet(player, "h")) {
                int card_id = room->askForCardChosen(yanghu, player, "h", "heg_mingfa", false, Card::MethodGet);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, yanghu->objectName());
                room->obtainCard(yanghu, Sanguosha->getCard(card_id), reason, false);
            }
        }
        return false;
    }
};

HMingfaZonghengCard::HMingfaZonghengCard()
{
}

bool HMingfaZonghengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !Self->isFriendWith(to_select);
}

void HMingfaZonghengCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();

    QStringList target_list = source->getTag("MingfaTarget").toStringList();
    target_list.append(target->objectName());
    source->setTag("MingfaTarget", target_list);
    room->addPlayerMark(target, "##mingfa");
}

class HMingfaZongheng : public ViewAsSkillV2
{
public:
    HMingfaZongheng() : ViewAsSkillV2("heg_mingfazongheng", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HMingfaZonghengCard");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && Sanguosha && request.selectedCardIds.isEmpty()
            && candidate && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
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

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HMingfaZonghengCard *first = new HMingfaZonghengCard;
        first->addSubcard(request.selectedCardIds.first());
        return first;
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
        events << CardsMoveBatch;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && !player->hasFlag("YouyanUsed") && player->getPhase() != Player::NotActive) {
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
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName(), player);
            player->setFlags("YouyanUsed");
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        QList<int> guanxing = room->getNCards(4);

        CardsMoveStruct move(guanxing, player, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString()));
        room->moveCardsAtomic(move, true);

        QList<Card::Suit> suits;
        QVariantList move_datas = data.toList();
        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.to_place == Player::DiscardPile
                    && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                for (int i = 0; i < move.card_ids.length(); ++i) {
                    if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip) {
                        const Card *card = Sanguosha->getCard(move.card_ids.at(i));
                        if (card) {
                            suits << card->getSuit();
                        }
                    }
                }
            }
        }
        QList<int> to_get, to_throw;
        foreach (int id, guanxing) {
            if (suits.contains(Sanguosha->getCard(id)->getSuit()))
                to_throw << id;
            else
                to_get << id;
        }

        if (!to_get.isEmpty()) {
            DummyCard *dummy = new DummyCard(to_get);
            room->obtainCard(player, dummy, true);
            dummy->deleteLater();
        }

        if (!to_throw.isEmpty()) {
            DummyCard *dummy = new DummyCard(to_throw);
            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
            room->throwCard(dummy, reason, NULL);
            dummy->deleteLater();
        }

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

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart)
                return true;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (player != death.who)
                return true;
        } else
            return true;

        ServerPlayer *target1 = player->getTag("ZhuihuanDamage").value<ServerPlayer *>();
        if (target1) {
            player->removeTag("ZhuihuanDamage");
            room->removePlayerMark(target1, "##zhuihuan");
        }
        ServerPlayer *target2 = player->getTag("ZhuihuanDiscard").value<ServerPlayer *>();
        if (target2) {
            player->removeTag("ZhuihuanDiscard");
            room->removePlayerMark(target2, "##zhuihuan");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging && (player && player->isAlive() && player->hasSkill(objectName()))) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QList<ServerPlayer *> choosees = room->askForPlayersChosen(player, room->getAlivePlayers(), objectName(), 0, 2, "@zhuihuan-invoke", true);
        if (choosees.length() > 0) {
            room->sortByActionOrder(choosees);
            player->setTag("zhuihuan_invoke", QVariant::fromValue(choosees));
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent , Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QList<ServerPlayer *> targets = player->getTag("zhuihuan_invoke").value<QList<ServerPlayer *> >();
        player->removeTag("zhuihuan_invoke");

        if (targets.isEmpty()) return false;

        ServerPlayer *target1 = targets.takeFirst();

        QStringList choices, tag_names;
        choices << "damage" << "discard";
        tag_names << "ZhuihuanDamage" << "ZhuihuanDiscard";


        QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target1), QString(),
                           "@zhuihuan-choose::" + target1->objectName());

        int x = choices.indexOf(choice);
        player->setTag(tag_names[x], QVariant::fromValue(target1));
        room->addPlayerMark(target1, "##zhuihuan");

        if (!targets.isEmpty()) {
            ServerPlayer *target2 = targets.takeFirst();
            player->setTag(tag_names[1-x], QVariant::fromValue(target2));
            room->addPlayerMark(target2, "##zhuihuan");
        }



        return false;
    }

};

class HZhuihuanEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HZhuihuanEffect() : TriggerSkillV2("#heg_zhuihuan-effect")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList skill_list;
        if (player == NULL || player->isDead()) return skill_list;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            ServerPlayer *target1 = p->getTag("ZhuihuanDamage").value<ServerPlayer *>();
            ServerPlayer *target2 = p->getTag("ZhuihuanDiscard").value<ServerPlayer *>();
            if (target1 == player || target2 == player) {
                skill_list.insert(p, QStringList(objectName()));
            }
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *yangwan = ctx.owner;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        room->notifySkillInvoked(yangwan, "heg_zhuihuan");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *yangwan = ctx.owner;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target1 = yangwan->getTag("ZhuihuanDamage").value<ServerPlayer *>();
        if (target1 == player && player->isAlive()) {
            yangwan->removeTag("ZhuihuanDamage");
            room->removePlayerMark(player, "##zhuihuan");
            LogMessage log;
            log.type = "#ZhuihuanEffect";
            log.from = yangwan;
            log.to << player;
            log.arg = "zhuihuan:damage";
            room->sendLog(log);
            if (damage.from && damage.from->isAlive()) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), damage.from->objectName());
                room->damage(DamageStruct("heg_zhuihuan", player, damage.from));
            }
        }
        ServerPlayer *target2 = yangwan->getTag("ZhuihuanDiscard").value<ServerPlayer *>();
        if (target2 == player && player->isAlive()) {
            yangwan->removeTag("ZhuihuanDiscard");
            room->removePlayerMark(player, "##zhuihuan");
            LogMessage log;
            log.type = "#ZhuihuanEffect";
            log.from = yangwan;
            log.to << player;
            log.arg = "zhuihuan:discard";
            room->sendLog(log);
            if (damage.from && damage.from->isAlive()) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), damage.from->objectName());
                room->askForDiscard(damage.from, "zhuihuan_discard", 2, 2);
            }
        }

        return false;
    }

};

HJianguoCard::HJianguoCard()
{
}

bool HJianguoCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void HJianguoCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();
    QStringList choices;
    choices << "d1tx";
    if (!target->isNude())
        choices << "t1dx";
    QString choice = room->askForChoice(source, "heg_jianguo", choices.join("+"), QVariant(), "d1tx+t1dx", "#jianguo-choice::" + target->objectName());
    if (choice == "d1tx") {
        target->drawCards(1, "heg_jianguo");
        if (target->isAlive() && target->getHandcardNum() > 1) {
            int x = qMin(target->getHandcardNum()/2, 5);
            room->askForDiscard(target, "heg_jianguo", x, x);
        }
    } else if (choice == "t1dx") {
        room->askForDiscard(target, "heg_jianguo", 1, 1, false, true);
        if (target->isAlive() && target->getHandcardNum() > 1)
            target->drawCards(qMin(target->getHandcardNum()/2, 5), "heg_jianguo");
    }
}

class HJianguo : public ViewAsSkillV2
{
public:
    HJianguo() : ViewAsSkillV2("heg_jianguo") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HJianguoCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HJianguoCard *first = new HJianguoCard;
        first->setSkillName(objectName());
        first->setShowSkill(objectName());
        return first;
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
        events << TargetSpecified << CardUsed << CardResponded << GeneralShown << EventPhaseStart;
    }

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
        if (player->getMark("qingshi-turn") > 1 || player->getPhase() == Player::NotActive) return TriggerList();
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
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
            room->addPlayerMark(player, "qingshi-turn");
            QStringList target_list = player->getTag("qingshi_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("qingshi_target", target_list);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QStringList target_list = player->getTag("qingshi_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("qingshi_target", target_list);
        ServerPlayer *to = room->findPlayerByObjectName(target_name);
        if (to ) {
            room->damage(DamageStruct(objectName(), player, to));
        }
        return false;
    }
};

HQuanjianCard::HQuanjianCard()
{

}

bool HQuanjianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->isFriendWith(to_select);
}

void HQuanjianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *player = effect.from, *to = effect.to;
    if (!player->askCommandto("heg_quanjian", to))
        player->getRoom()->addPlayerMark(to, "#quanjian-turn");
}

class HQuanjian : public ViewAsSkillV2
{
public:
    HQuanjian() : ViewAsSkillV2("heg_quanjian") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HQuanjianCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HQuanjianCard *card = new HQuanjianCard;
        card->setShowSkill(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HQuanjianCard"; }
};

class HQuanjianEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HQuanjianEffect() : TriggerSkillV2("#heg_quanjian-effect")
    {
        events << DamageInflicted << EventPhaseStart;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::NotActive)
            for (ServerPlayer *p : room->getAllPlayers(true)) room->setPlayerMark(p, "#quanjian-turn", 0);
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == DamageInflicted && player->getMark("#quanjian-turn") > 0)
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        damage.damage+= player->getMark("#quanjian-turn");
        data = QVariant::fromValue(damage);
        room->setPlayerMark(player, "#quanjian-turn", 0);
        return false;
    }
};

class HTujue : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }

    HTujue() : TriggerSkillV2("heg_tujue")
    {
        events << AskForPeaches;
        frequency = Limited;
        limit_mark = "@impasse";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *target, QVariant &data) const override
    {
        if ((target && target->isAlive() && target->hasSkill(objectName())) && target->getMark(limit_mark) > 0 && !target->isNude()) {
            DyingStruct dying_data = data.value<DyingStruct>();
            if (target->getHp() < 1 && dying_data.who == target)
                return TriggerList{{target, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *to = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "tujue-invoke", true, true);
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);
            QStringList target_list = player->getTag("tujue_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("tujue_target", target_list);
            room->setPlayerMark(player, limit_mark, 0);
            room->doSuperLightbox("heg_huangquan", objectName());

            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QStringList target_list = player->getTag("tujue_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("tujue_target", target_list);
        ServerPlayer *to = room->findPlayerByObjectName(target_name);
        if (to) {
            DummyCard *card = player->isKongcheng() ? new DummyCard : player->wholeHandCards();
            foreach(const Card *equip, player->getEquips())
                card->addSubcard(equip);
            card->deleteLater();
            int x = qMin(card->subcardsLength(), 3);
            if (x > 0) {
                room->obtainCard(to, card, CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName(), to->objectName(),
                        objectName(), QString()), false);
                if (player->isAlive()) {
                    RecoverStruct recover;
                    recover.recover = x;
                    room->recover(player, recover);
                    if (player->isAlive())
                        player->drawCards(x, objectName());
                }
            }
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
            if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return {};
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
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
            QStringList cost_list = player->getTag("zhiren_cost").toStringList();
            cost_list.append("busuan");
            player->setTag("zhiren_cost", cost_list);
            return true;
        } else if (choice == "discard") {
            ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "@zhiren-target", true, true);
            if (to != NULL) {
                room->broadcastSkillInvoke(objectName(), player);
                QStringList target_list = player->getTag("zhiren_cost").toStringList();
                target_list.append(to->objectName());
                player->setTag("zhiren_cost", target_list);
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        QStringList target_list = player->getTag("zhiren_cost").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("zhiren_cost", target_list);
        if (target_name == "busuan") {
            const Card *card = data.value<CardUseStruct>().card;
            int x = 0;
            if (card->isKindOf("Slash"))
                x = 1;
            else if (card->isKindOf("Nullification"))
                x = 4;
            else
                x = GetHanNumFromString(Sanguosha->translate(card->objectName()));
            QList<int> guanxing = room->getNCards(x);
            LogMessage log;
            log.type = "$ViewDrawPile";
            log.from = player;
            QStringList card_names;
            for (int id : guanxing)
                card_names << QString::number(id);
            log.card_str = card_names.join("+");
            room->doNotify(player, QSanProtocol::S_COMMAND_LOG_SKILL, log.toVariant());

            room->askForGuanxing(player, guanxing, Room::GuanxingBothSides);

        } else {
            ServerPlayer *to = room->findPlayerByObjectName(target_name);
            if (to && player->canDiscard(to, "e")) {
                int to_throw = room->askForCardChosen(player, to, "e", objectName(), false, Card::MethodDiscard);
                room->throwCard(to_throw, to, player);
            }
        }
        return false;
    }

private:
    static int GetHanNumFromString(QString str)     //获取汉字个数
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
        events << CardsMoveBatch;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->hasFlag("yanerUsed")) return TriggerList();
        QVariantList move_datas = data.toList();
        foreach (QVariant move_data, move_datas) {
            CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from->isAlive() && move.from->getPhase() == Player::Play && move.from != player
                    && move.from->isFriendWith(player) && move.from_places.contains(Player::PlaceHand) && move.from->isKongcheng())
                return TriggerList{{player, {objectName()}}};

        }

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Keep payment after interception and reveal tied to the exact source.
        const bool addedCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addedCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto costFlag = qScopeGuard([&] {
            if (addedCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *target = room->getCurrent();
        if (player->askForSkillInvoke(this, QVariant::fromValue(target))) {
            room->broadcastSkillInvoke(objectName(), player);
            room->setPlayerFlag(player, "yanerUsed");
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        ServerPlayer *target = room->getCurrent();
        if (target && target->isAlive())
            target->drawCards(1, objectName());

        if (player->isAlive())
            player->drawCards(1, objectName());

        return false;
    }
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
