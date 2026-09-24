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

#include "h-power.h"
#include "skill.h"
#include <QScopeGuard>
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
#include "h-standard-tricks.h"

#include "h-strategic-advantage.h"
#include "util.h"
#include "skill-declaration.h"
#include "skill-instance-utils.h"




namespace {
// Donor Shouyue applies to every Shu player while a shown provider is alive.
bool hasShownShouyue(const Player *player)
{
    if (!player) return false;
    QList<const Player *> players = player->getAliveSiblings();
    players << player;
    for (const Player *provider : players)
        if (provider->isAlive() && provider->hasShownSkill("heg_shouyue")) return true;
    return false;
}
void invalidateTieqiSlot(Room *room, ServerPlayer *target, ServerPlayer *source, bool head)
{
    QVariantList records = target->getTag("heg_tieqixh_invalidity").toList();
    for (const SkillInstance &instance : target->getSkillInstances()) {
        const Skill *skill = Sanguosha->getSkill(instance.skillName);
        if (instance.source != SourceInnate || instance.bindHead != (head ? 1 : 2)
            || !skill || skill->getFrequency(target) == Skill::Compulsory) continue;
        room->addSkillInvalidity(target, instance.skillName, source->objectName(), "heg_tieqixh", instance.instanceID);
        QVariantMap record;
        record.insert("skill", instance.skillName); record.insert("id", instance.instanceID);
        record.insert("source", source->objectName());
        if (!records.contains(record)) records << record;
    }
    target->setTag("heg_tieqixh_invalidity", records);
}
}
class HTieqiXHClear : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HTieqiXHClear() : TriggerSkillV2("#heg_tieqi_xh-clear") { events << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (ServerPlayer *p : room->getAllPlayers(true)) {
            for (const QVariant &value : p->getTag("heg_tieqixh_invalidity").toList()) {
                const QVariantMap record = value.toMap();
                room->removeSkillInvalidity(p, record.value("skill").toString(), record.value("source").toString(),
                    "heg_tieqixh", record.value("id").toInt());
            }
            p->removeTag("heg_tieqixh_invalidity");
        }
        return true;
    }
};

// Jieyue changes the phase draw base before replacement draw skills resolve.
class HJieyueDraw : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 10; }
    HJieyueDraw() : TriggerSkillV2("#heg_jieyue-draw")
    { events << DrawNCards << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == DrawNCards && player) {
            DrawStruct draw = data.value<DrawStruct>();
            if (draw.reason == "draw_phase") {
                draw.num += 3 * player->getMark("JieyueExtraDraw");
                data = QVariant::fromValue(draw);
            }
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *p : room->getAllPlayers(true)) {
                room->setPlayerMark(p, "JieyueExtraDraw", 0);
                room->setPlayerFlag(p, "-BuyiUsed");
            }
        return true;
    }
};

// Suffixed lord-granted skills retain this donor version independently of shared skills.
class HTuxiEGF : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    explicit HTuxiEGF();

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override;
    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override;

};

HTuxiEGF::HTuxiEGF() : TriggerSkillV2("heg_tuxi_egf")
{
    events << DrawNCards;
}

TriggerList HTuxiEGF::triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->getPhase() != Player::Draw) return TriggerList();
    if (data.value<DrawStruct>().reason != "draw_phase" || data.value<DrawStruct>().num < 1) return TriggerList();
    QList<ServerPlayer *> other_players = room->getOtherPlayers(player);
    foreach (ServerPlayer *p, other_players) {
        if (player->canGet(p, "h")) {
            return TriggerList{{player, {objectName()}}};
        }
    }
    return TriggerList();
}

bool HTuxiEGF::pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *player = ctx.invoker;
    QVariant &data = *ctx.original_data;
    // Payment prompts run after willInvoke without revealing a different source.
    const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
    if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
    const auto clearCostFlag = qScopeGuard([&] {
        if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
    });
    QList<ServerPlayer *> to_choose;
    foreach(ServerPlayer *p, room->getOtherPlayers(player)) {
        if (player->canGet(p, "h"))
            to_choose << p;
    }

    int x = data.value<DrawStruct>().num;
    QList<ServerPlayer *> choosees = room->askForPlayersChosen(player, to_choose, objectName(), 0, x, "@tuxi-card:::" + QString::number(x), true);
    if (choosees.length() > 0) {
        room->sortByActionOrder(choosees);
        player->setTag("tuxi_invoke", QVariant::fromValue(choosees));
        room->broadcastSkillInvoke(objectName(), player);
        return true;
    }

    return false;
}

bool HTuxiEGF::effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *source = ctx.owner;
    DrawStruct draw = ctx.original_data->value<DrawStruct>();
    QList<ServerPlayer *> targets = source->getTag("tuxi_invoke").value<QList<ServerPlayer *> >();
    source->removeTag("tuxi_invoke");

    foreach (ServerPlayer *target, targets) {
        if (!source->canGet(target, "h")) continue;
        int card_id = room->askForCardChosen(source, target, "h", "heg_tuxi_egf", false, Card::MethodGet);
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
        room->obtainCard(source, Sanguosha->getCard(card_id), reason, false);
    }

    draw.num -= targets.length();
    *ctx.original_data = QVariant::fromValue(draw);
    return false;
}

class HQiaobianEGF : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    explicit HQiaobianEGF();

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override;
    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;

};

HQiaobianEGF::HQiaobianEGF() : TriggerSkillV2("heg_qiaobian_egf")
{
    events << EventPhaseChanging;
}

TriggerList HQiaobianEGF::triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    PhaseChangeStruct change = data.value<PhaseChangeStruct>();
    room->setPlayerMark(player, "qiaobianPhase", (int)change.to);
    int index = 0;
    switch (change.to) {
        case Player::RoundStart:
        case Player::Start:
        case Player::Finish:
        case Player::NotActive: return TriggerList();

        case Player::Judge: index = 1; break;
        case Player::Draw: index = 2; break;
        case Player::Play: index = 3; break;
        case Player::Discard: index = 4; break;
        case Player::PhaseNone: Q_ASSERT(false);
    }
    if ((player && player->isAlive() && player->hasSkill(objectName())) && index > 0 && !player->isKongcheng() && !player->isSkipped(change.to))
        return TriggerList{{player, {objectName()}}};
    return TriggerList();
}

bool HQiaobianEGF::pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *zhanghe = ctx.invoker;
    QVariant &data = *ctx.original_data;
    // Payment prompts run after willInvoke without revealing a different source.
    const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
    if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
    const auto clearCostFlag = qScopeGuard([&] {
        if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
    });
    PhaseChangeStruct change = data.value<PhaseChangeStruct>();
    static QStringList phase_strings;
    if (phase_strings.isEmpty())
        phase_strings << "round_start" << "start" << "judge" << "draw"
        << "play" << "discard" << "finish" << "not_active";
    int index = static_cast<int>(change.to);

    QString discard_prompt = QString("#qiaobian:::%1").arg(phase_strings[index]);

    if (room->askForDiscard(zhanghe, objectName(), 1, 1, true, false, discard_prompt)) {
        room->broadcastSkillInvoke(objectName(), zhanghe);
        return true;
    }
    return false;
}

bool HQiaobianEGF::effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *zhanghe = ctx.invoker;
    QVariant &data = *ctx.original_data;
    ctx.manual_effect = true;
    PhaseChangeStruct change = data.value<PhaseChangeStruct>();
    zhanghe->skip(change.to);
    int index = 0;
    switch (change.to) {
        case Player::RoundStart:
        case Player::Start:
        case Player::Finish:
        case Player::NotActive: return false;

        case Player::Judge: index = 1; break;
        case Player::Draw: index = 2; break;
        case Player::Play: index = 3; break;
        case Player::Discard: index = 4; break;
        case Player::PhaseNone: Q_ASSERT(false);
    }
    QString use_prompt = QString("@qiaobian-%1").arg(index);
    if (index == 2) {
        QList<ServerPlayer *> to_choose;
        foreach(ServerPlayer *p, room->getOtherPlayers(zhanghe)) {
            if (zhanghe->canGet(p, "h"))
                to_choose << p;
        }
        if (to_choose.isEmpty()) return false;
        QList<ServerPlayer *> choosees = room->askForPlayersChosen(zhanghe, to_choose, "qiaobian_draw", 0, 2, use_prompt, false);
        if (choosees.length() > 0) {
            room->sortByActionOrder(choosees);
            foreach (ServerPlayer *target, choosees) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, zhanghe->objectName(), target->objectName());
            }
            foreach (ServerPlayer *target, choosees) {
                if (!zhanghe->canGet(target, "h")) continue;
                int card_id = room->askForCardChosen(zhanghe, target, "h", objectName(), false, Card::MethodGet);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, zhanghe->objectName());
                room->obtainCard(zhanghe, Sanguosha->getCard(card_id), reason, false);
            }
        }
    }
    if (index == 3)
        room->askForQiaobian(zhanghe, room->getAlivePlayers(), "qiaobian_play", use_prompt, true, true);

    return false;
}

class HXiaoguoEGF : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    explicit HXiaoguoEGF();

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const;
    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;

};

HXiaoguoEGF::HXiaoguoEGF() : TriggerSkillV2("heg_xiaoguo_egf")
{
    events << EventPhaseStart;
}

TriggerList HXiaoguoEGF::triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const
{
    TriggerList skill_list;
    if (player != NULL && player->isAlive() && player->getPhase() == Player::Finish) {
        QList<ServerPlayer *> yuejins = room->findPlayersBySkillName(objectName());
        foreach (ServerPlayer *yuejin, yuejins) {
            if (yuejin != NULL && player != yuejin && !yuejin->isKongcheng())
                skill_list.insert(yuejin, QStringList(objectName()));
        }
    }
    return skill_list;
}

bool HXiaoguoEGF::pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *player = ctx.invoker;
    ServerPlayer *ask_who = ctx.owner;
    // Payment prompts run after willInvoke without revealing a different source.
    const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
    if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
    const auto clearCostFlag = qScopeGuard([&] {
        if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
    });
    if (room->askForCard(ask_who, ".Basic", "@xiaoguo:"+player->objectName(), QVariant(), objectName())) {
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), player->objectName());
        room->broadcastSkillInvoke(objectName(), ask_who);
        return true;
    }
    return false;
}

bool HXiaoguoEGF::effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *player = ctx.invoker;
    ServerPlayer *ask_who = ctx.owner;
    ctx.manual_effect = true;
    if (player->isDead()) return false;
    if (room->askForCard(player, ".Equip", "@xiaoguo-discard", QVariant()))
        ask_who->drawCards(1, objectName());
    else
        room->damage(DamageStruct("heg_xiaoguo_egf", ask_who, player));

    return false;
}



class HTieqiXH : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    explicit HTieqiXH();

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
                                QVariant &data, QList<SkillContext> &contexts) const override;
    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override;
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override;
};

HTieqiXH::HTieqiXH() : TriggerSkillV2("heg_tieqi_xh")
{
    events << TargetSpecified;
}

bool HTieqiXH::collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
                                QVariant &data, QList<SkillContext> &contexts) const
{
    if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
    QList<ServerPlayer *> targets;
    const CardUseStruct use = data.value<CardUseStruct>();
    if (!use.card || !use.card->isKindOf("Slash")) return true;
    for (ServerPlayer *target : use.to)
        if (target) targets << target;
    // One exact skill source per ordered target; the dispatcher owns activation.
    for (int id : player->getValidSkillInstanceIds(objectName())) {
        const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
        for (int i = 0; i < targets.size(); ++i) {
            ServerPlayer *target = targets.at(i);
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = player;
            ctx.invoker = player;
            ctx.initiator = player;
            ctx.instanceID = id;
            ctx.activationRef = ref;
            ctx.sourceRef = ref;
            ctx.original_data = &data;
            ctx.current_event = event;
            bool amountOk = false;
            ctx.amount = room->getSkillInstanceAmount(ref, &amountOk);
            if (!amountOk) ctx.amount = getBaseAmount();
            if (target) {
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

bool HTieqiXH::pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    ServerPlayer *skill_target = ctx.preferredTarget;
    ServerPlayer *player = ctx.owner;
    // Payment prompts run after willInvoke without revealing a different source.
    const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
    if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
    const auto clearCostFlag = qScopeGuard([&] {
        if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
    });
    if (player->askForSkillInvoke(this, QVariant::fromValue(skill_target))) {
        room->broadcastSkillInvoke(objectName(), player);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), skill_target->objectName());
        return true;
    }
    return false;
}

bool HTieqiXH::effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const
{
    ServerPlayer *player = ctx.owner;
    QVariant &data = *ctx.original_data;
    CardUseStruct use = data.value<CardUseStruct>();
    QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();

    QStringList all_patterns;
    all_patterns << ".|spade" << ".|club" << ".|heart" << ".|diamond";

    JudgeStruct judge;
    judge.pattern = ".";
    judge.good = true;
    judge.reason = "heg_tieqi_xh";
    judge.who = player;
    judge.play_animation = false;

    room->judge(judge);
    judge.pattern = ".|" + judge.card->getSuitString();

    if (hasShownShouyue(player) && player->getSeemingKingdom() == "shu") {

        LogMessage log;
        log.type = "#TieqiAllSkills";
        log.from = player;
        log.to << target;
        log.arg = "heg_tieqi_xh";
        room->sendLog(log);

        if (target->hasShownGeneral1())
            invalidateTieqiSlot(room, target, player, true);
        if (target->getGeneral2() && target->hasShownGeneral2())
            invalidateTieqiSlot(room, target, player, false);

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), true);

        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

    } else if (target->hasShownOneGeneral()) {
        QString choice = "head_general";

        if (player->getAI()) {
            QStringList choices;
            if (target->hasShownGeneral1())
                choices << "head_general";

            if (target->getGeneral2() && target->hasShownGeneral2())
                choices << "deputy_general";

            choice = room->askForChoice(player, "heg_tieqi_xh", choices.join("+"), QVariant::fromValue(target));
        } else {
            QStringList generals;
            if (target->hasShownGeneral1()) {
                QString g = target->getGeneral()->objectName();
                if (g.contains("anjiang"))
                    g.append("_head");
                generals << g;
            }

            if (target->getGeneral2() && target->hasShownGeneral2()) {
                QString g = target->getGeneral2()->objectName();
                if (g.contains("anjiang"))
                    g.append("_deputy");
                generals << g;
            }

            QString general = generals.first();
            if (generals.length() == 2)
                general = room->askForGeneral(player, generals.join("+"), generals.first(), true, "heg_tieqi_xh", QVariant::fromValue(target));

            if (general == target->getGeneral()->objectName() || general == "anjiang_head")
                choice = "head_general";
            else
                choice = "deputy_general";

        }
        LogMessage log;
        log.type = choice == "head_general" ? "#TieqiHeadSkills" : "#TieqiDeputySkills";
        log.from = player;
        log.to << target;
        log.arg = "heg_tieqi_xh";
        room->sendLog(log);


        invalidateTieqiSlot(room, target, player, choice == "head_general");

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), true);

        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

    }

    int index = use.to.indexOf(target);

    if (target->isAlive() && all_patterns.contains(judge.pattern)
            && !room->askForCard(target, judge.pattern, "@tieji-discard:::" + judge.pattern.mid(2), QVariant::fromValue(use))) {
        LogMessage log;
        log.type = "#NoJink";
        log.from = target;
        room->sendLog(log);
        if (index >= 0 && index < jink_list.size()) jink_list[index] = 0;
    }

    player->setTag("Jink_" + use.card->toString(), jink_list);
    return false;
}

HZhengbiCard::HZhengbiCard()
{
    setSkillName("heg_zhengbi");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool HZhengbiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self) return false;
    if (subcardsLength() == 0) return !to_select->hasShownOneGeneral();
    return to_select->hasShownOneGeneral();
}

void HZhengbiCard::extraCost(Room *room, const CardUseStruct &card_use) const
{
    if (subcardsLength() == 0) return;
    ServerPlayer *target = card_use.to.first();
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), target->objectName(), "heg_zhengbi", QString());
    room->obtainCard(target, this, reason, true);
}

void HZhengbiCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->setFlags("ZhengbiTo");
}

class HZhengbiViewAsSkill : public ViewAsSkillV2
{
public:
    HZhengbiViewAsSkill() : ViewAsSkillV2("heg_zhengbi")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        // MethodNone prompts carry UNKNOWN but still require the exact selector.
        return request.initiator && request.pattern == "@@heg_zhengbi"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }

        return selected.isEmpty() && to_select->getTypeId() == Card::TypeBasic;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() > 1) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;

        HZhengbiCard *zhengbi_card = new HZhengbiCard;
        zhengbi_card->addSubcards(request.selectedCardIds);
        return zhengbi_card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HZhengbiCard"; }
};

class HZhengbi : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZhengbi() : TriggerSkillV2("heg_zhengbi")
    {
        events << EventPhaseStart;
        view_as_skill = new HZhengbiViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase()== Player::NotActive) {
            foreach (ServerPlayer *p, room->getAlivePlayers())
                room->setPlayerProperty(p, "zhengbi_targets", QVariant());
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
            if ((player && player->isAlive() && player->hasSkill(objectName())))
                return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        return room->askForUseCard(player, "@@heg_zhengbi", "@heg_zhengbi", -1, Card::MethodNone);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QList<ServerPlayer *> players = room->getOtherPlayers(player);
        foreach (ServerPlayer *p, players) {
            if (p->hasFlag("ZhengbiTo")) {
                p->setFlags("-ZhengbiTo");
                if (p->hasShownOneGeneral()) {
                    if (p->isNude()) return false;
                    QList<int> to_give;

                    QList<const Card *> cards = p->getCards("he");

                    int trickId = -1;
                    foreach (const Card *c, cards) {
                        if (c->getTypeId() != Card::TypeBasic) {
                            trickId = c->getId();
                            break;
                        }
                    }
                    if (trickId != -1)
                        to_give << trickId;
                    else {
                        foreach (const Card *c, cards) {
                            to_give << c->getId();
                            if (to_give.length() > 1) break;
                        }
                    }

                    if (p->getCardCount(true) > 1) {
                        const Card *card = room->askForCard(p, "@@heg_zhengbigive!", "@heg_zhengbi-give:"+player->objectName(), QVariant(), Card::MethodNone);
                        if (card != NULL)
                            to_give = card->getSubcards();
                    }

                    DummyCard *dummy_card = new DummyCard(to_give);
                    dummy_card->deleteLater();

                    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, p->objectName(), player->objectName(), objectName(), QString());
                    room->obtainCard(player, dummy_card, reason, true);
                } else {

                    QStringList assignee_list = player->property("zhengbi_targets").toString().split("+");
                    assignee_list << p->objectName();
                    room->setPlayerProperty(player, "zhengbi_targets", assignee_list.join("+"));

                }
            }
        }
        return false;
    }
};


class HZhengbiGive : public ViewAsSkillV2
{
public:
    HZhengbiGive() : ViewAsSkillV2("heg_zhengbigive")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        // MethodNone prompts carry UNKNOWN but still require the exact selector.
        return request.initiator && request.pattern == "@@heg_zhengbigive!"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }

        if (selected.length() == 0)
            return true;
        else if (selected.length() == 1) {
            return (selected.first()->getTypeId() == Card::TypeBasic && to_select->getTypeId() == Card::TypeBasic);
        }
        return false;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || (request.selectedCardIds.size() != 1 && request.selectedCardIds.size() != 2)) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return request.selectedCardIds.size() == 2
            || Sanguosha->getCard(request.selectedCardIds.first())->getTypeId() != Card::TypeBasic;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        DummyCard *dummy = new DummyCard;
        dummy->addSubcards(request.selectedCardIds);
        return dummy;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "DummyCard"; }
};

class HZhengbiTargetMod : public TargetModSkillV2
{
public:
    HZhengbiTargetMod() : TargetModSkillV2("#heg_zhengbi-target")
    {
        setHolderSelector(CorrectSkill_System);
        pattern = "^SkillCard";
    }

    virtual int getResidueNum(const Player *from, const Card *card, const Player *to) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        QStringList assignee_list = from->property("zhengbi_targets").toString().split("+");
        if (to && assignee_list.contains(to->objectName()) && !to->hasShownOneGeneral())
            return 10000;
        return 0;
    }

    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *to) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        QStringList assignee_list = from->property("zhengbi_targets").toString().split("+");
        if (to && assignee_list.contains(to->objectName()) && !to->hasShownOneGeneral())
            return 10000;
        return 0;
    }


    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        if (!c.primary || !c.card) return CorrectSkillResult::noEffect();
        int n = 0;
        if (c.modType == TargetModSkill::Residue) n = getResidueNum(c.primary, c.card, c.secondary);
        else if (c.modType == TargetModSkill::DistanceLimit) n = getDistanceLimit(c.primary, c.card, c.secondary);
        return n ? CorrectSkillResult::useAmount(n) : CorrectSkillResult::noEffect();
    }
};


HFengyingCard::HFengyingCard()
{
    setSkillName("heg_fengying");
    target_fixed = true;
    will_throw = false;
}

const Card *HFengyingCard::validate(CardUseStruct &card_use) const
{
    HThreatenEmperor *te = new HThreatenEmperor(Card::SuitToBeDecided, 0);
    te->addSubcards(card_use.from->getHandcards());
    te->setSkillName("heg_fengying");
    te->setShowSkill("heg_fengying");
    return te;
}

class HFengying : public ViewAsSkillV2
{
public:
    HFengying() : ViewAsSkillV2("heg_fengying")
    {
        frequency = Limited;
        limit_mark = "@honor";
    }

    virtual int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return card->isKindOf("HThreatenEmperor") ? 0 : -1;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        if (player->getMark("@honor") < 1 || player->isKongcheng()) return false;
        HThreatenEmperor emperor(Card::SuitToBeDecided, 0);
        HThreatenEmperor *te = &emperor;
        te->addSubcards(player->getHandcards());
        te->setSkillName(objectName());
        return !player->isProhibited(player, te) && !player->isLocked(te);
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        return new HFengyingCard;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HFengyingCard"; }
};

class HFengyingAfter : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFengyingAfter() : TriggerSkillV2("#heg_fengying-after")
    {
        events << CardUsed << PreCardUsed;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player != NULL && player->isAlive() && triggerEvent == PreCardUsed) {
            const Card *card = data.value<CardUseStruct>().card;
            if (card != NULL && card->getSkillName() == "heg_fengying") {
                room->setPlayerMark(player, "@honor", 0);
                room->broadcastSkillInvoke("heg_fengying", player);
                room->doSuperLightbox("heg_cuiyanmaojie", "heg_fengying");
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != CardUsed || player == NULL || player->isDead()) return TriggerList();

        const Card *card = data.value<CardUseStruct>().card;

        if (card != NULL && card->getSkillName() == "heg_fengying")
            return TriggerList{{player, {objectName()}}};


        return TriggerList();
    }


    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        QList<ServerPlayer *> players, all_players = room->getAlivePlayers();

        foreach (ServerPlayer *p, all_players) {
           if (p->isFriendWith(player))
               players << p;
        }
        if (players.isEmpty()) return false;
        room->sortByActionOrder(players);
        foreach (ServerPlayer *p, players)
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());

        foreach (ServerPlayer *to, players) {
            if (to->isAlive()) {
                int x = to->getMaxHp() - to->getHandcardNum();
                if (x > 0)
                    to->drawCards(x);
            }
        }
        return false;
    }
};

HJieyueCard::HJieyueCard()
{
    setSkillName("heg_jieyue");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool HJieyueCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getSeemingKingdom() != "wei" && to_select != Self;
}

void HJieyueCard::onUse(Room *, CardUseStruct &card_use) const
{
    ServerPlayer *target = card_use.to.first();

    target->setFlags("JieyueTarget");
}

class HJieyueViewAsSkill : public ViewAsSkillV2
{
public:
    HJieyueViewAsSkill() : ViewAsSkillV2("heg_jieyue", 1)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        // MethodNone prompts carry UNKNOWN but still require the exact selector.
        return request.initiator && request.pattern == "@@heg_jieyue"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty() || to_select->hasFlag("using")) return false;
        return Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, to_select);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HJieyueCard *jieyue_card = new HJieyueCard;
        jieyue_card->addSubcard(originalCard);
        return jieyue_card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HJieyueCard"; }
};

class HJieyue : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJieyue() : TriggerSkillV2("heg_jieyue")
    {
        events << EventPhaseStart;
        view_as_skill = new HJieyueViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->isKongcheng()) return TriggerList();
        if (player->getPhase() == Player::Start) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        const Card *card = room->askForUseCard(player, "@@heg_jieyue", "@heg_jieyue", -1, Card::MethodNone);
        if (card) {
            QList<ServerPlayer *> players = player->getRoom()->getOtherPlayers(player);
            foreach (ServerPlayer *target, players) {
                if (target->hasFlag("JieyueTarget")) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                    LogMessage log;
                    log.type = "#ChoosePlayerWithSkill";
                    log.from = player;
                    log.to << target;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->notifySkillInvoked(player, objectName());
                    room->broadcastSkillInvoke(objectName(), player);
                    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "heg_jieyue", QString());
                    room->obtainCard(target, card, reason, false);
                    return true;
                }
            }

        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QList<ServerPlayer *> players = room->getOtherPlayers(player);
        foreach (ServerPlayer *p, players) {
            if (p->hasFlag("JieyueTarget")) {
                p->setFlags("-JieyueTarget");
                if (player->askCommandto("heg_jieyue", p))
                    player->drawCards(1, "heg_jieyue");
                else {
                    room->addPlayerMark(player, "JieyueExtraDraw");      //in gamerule
                }
            }
        }
        return false;
    }
};

HJianglveCard::HJianglveCard()
{
    setSkillName("heg_jianglve");
    mute = true;
    target_fixed = true;
}

void HJianglveCard::onUse(Room *room, CardUseStruct &card_use) const
{
    room->setPlayerMark(card_use.from, "@strategy", 0);
    room->broadcastSkillInvoke("heg_jianglve", card_use.from);
    room->doSuperLightbox("heg_wangping", "heg_jianglve");
    SkillCard::onUse(room, card_use);
}

void HJianglveCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    int index = source->startCommand("heg_jianglve");

    QList<ServerPlayer *> alls = room->getAlivePlayers();
    room->sortByActionOrder(alls);

    /*
    // summon all lieges
    foreach(ServerPlayer *anjiang, alls) {
        if (anjiang->hasShownOneGeneral()) continue;

        QString kingdom = source->getKingdom();
        ServerPlayer *lord = NULL;

        int num = 0;
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            if (p->getKingdom() != kingdom) continue;
            QStringList list = room->getTag(p->objectName()).toStringList();
            if (!list.isEmpty()) {
                const General *general = Sanguosha->getGeneral(list.first());
                if (general->isLord())
                    lord = p;
            }
            if (p->hasShownOneGeneral() && p->getRole() != "careerist")
                num++;
        }

        bool full = (source->getRole() == "careerist" || ((lord == NULL || !lord->hasShownGeneral1()) && num >= room->getPlayers().length() / 2));

        bool can_show = false, can_only_dupty = false;

        if (anjiang->getKingdom() == kingdom) {
            if (full) {
                if (lord == anjiang)
                    can_show = true;
            } else {
                if (anjiang->getActualGeneral1()->getKingdom() != "careerist")
                    can_show = true;
                can_only_dupty = true;
            }
        }

        anjiang->askForGeneralShow("jianglve", can_show, can_only_dupty, can_show, true);
    }

    */

    QList<ServerPlayer *> responsers, all_lieges;

    foreach(ServerPlayer *p, alls) {
        if (p->isFriendWith(source) && p != source)
            all_lieges << p;
    }

    foreach (ServerPlayer *p, all_lieges) {
        if (source->isDead()) break;
        if (p->isAlive() && p->doCommand("heg_jianglve", index, source))
            responsers << p;
    }

    int x = 0;

    responsers.prepend(source);

    foreach(ServerPlayer *p, responsers) {
        if (p->isDead()) continue;

        room->setPlayerProperty(p, "maxhp", p->getMaxHp() + 1);

        LogMessage log;
        log.type = "#GainMaxHp";
        log.from = p;
        log.arg = QString::number(1);
        room->sendLog(log);

        if (p->canRecover()) {
            x++;
            RecoverStruct recover;
            recover.who = source;
            room->recover(p, recover);
        }
    }

    if (x > 0 && source->isAlive())
        source->drawCards(x, "heg_jianglve");

}

class HJianglve : public ViewAsSkillV2
{
public:
    HJianglve() : ViewAsSkillV2("heg_jianglve")
    {
        frequency = Limited;
        limit_mark = "@strategy";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return player->getMark("@strategy") >= 1;
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HJianglveCard *card = new HJianglveCard;
        card->setShowSkill(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HJianglveCard"; }
};

class HEnyuan : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HEnyuan() : TriggerSkillV2("heg_enyuan")
    {
        events << TargetConfirmed << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Peach") && use.from != player && use.from->isAlive())
                return TriggerList{{player, {objectName()}}};
        } else if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from->isAlive()) return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *target = NULL;
        if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            target = damage.from;
        } else if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            target = use.from;
        }
        if (!target || target->isDead()) return false;

        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else {
            invoke = player->askForSkillInvoke(this, QVariant::fromValue(target));
        }

        if (invoke) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
            int x = qsanRandomBounded(2)+1;
            if (triggerEvent == Damaged) x=x+2;
            room->broadcastSkillInvoke(objectName(), x, player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            ServerPlayer *target = use.from;
            if (!target || target->isDead()) return false;
            target->drawCards(1, objectName());
        } else if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *target = damage.from;
            if (!target || target->isDead()) return false;

            if (target == player) {
                room->loseHp(target);
            } else {

                QList<int> result = room->askForExchangeCards(target, "_enyuan", 1, 0, "@heg_enyuan-give:"+ player->objectName(), "", ".|.|.|hand");
                if (result.isEmpty())
                    room->loseHp(target);
                else {
                    DummyCard dummy(result);
                    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), objectName(), QString());
                    reason.m_playerId = player->objectName();
                    room->obtainCard(player, &dummy, reason, false);
                }
            }

        }
        return false;
    }
};

class HXuanhuo : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXuanhuo() : TriggerSkillV2("heg_xuanhuo")
    {
        events << GeneralShown << GeneralHidden << EventAcquireSkill << EventLoseSkill << Death << DFDebut;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        doXuanhuoAttach(room);
        return true;
    }


private:
    static void doXuanhuoAttach(Room *room)
    {
        QMap<ServerPlayer *, bool> xuanhuo_map;
        QList<ServerPlayer *> players = room->getAlivePlayers(), fazhengs;
        foreach(ServerPlayer *p, players) {
            if (hasShownXuanhuo(p))
                fazhengs << p;
        }
        foreach(ServerPlayer *p, players) {
            bool will_attach = false;
            foreach(ServerPlayer *fazheng, fazhengs) {
                if (fazheng != p && fazheng->isFriendWith(p)) {
                    will_attach = true;
                    break;
                }
            }
            xuanhuo_map.insert(p, will_attach);
        }
        foreach (ServerPlayer *p, xuanhuo_map.keys()) {
            bool will_attach = xuanhuo_map.value(p, false);
            if (will_attach == p->getAcquiredSkills().contains("heg_xuanhuoattach")) continue;

            if (will_attach)
                room->attachSkillToPlayer(p, "heg_xuanhuoattach");
            else
                room->detachSkillFromPlayer(p, "heg_xuanhuoattach");

        }
    }

    static bool hasShownXuanhuo(ServerPlayer *player)
    {
        if (player->hasAcquiredSkill("heg_xuanhuo")) return true;
        if (player->inHeadSkills("heg_xuanhuo") && player->hasShownGeneral1()) return true;
        if (player->getGeneral2() && player->inDeputySkills("heg_xuanhuo") && player->hasShownGeneral2()) return true;
        return false;
    }

};

HXuanhuoAttachCard::HXuanhuoAttachCard()
{
    setSkillName("heg_xuanhuoattach");
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void HXuanhuoAttachCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *shu = card_use.from;

    ServerPlayer *fazheng = room->findPlayerBySkillName("heg_xuanhuo");
    if (!fazheng || fazheng->isDead() || !fazheng->isFriendWith(shu)) return;

    CardUseStruct new_use = card_use;
    new_use.to << fazheng;

    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, shu, data);

    LogMessage log;
    log.type = "#InvokeOthersSkill";
    log.from = shu;
    log.to << fazheng;
    log.arg = "heg_xuanhuo";
    room->sendLog(log);
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, shu->objectName(), fazheng->objectName());
    room->broadcastSkillInvoke("heg_xuanhuo", fazheng);

    //room->notifySkillInvoked(fazheng, "xuanhuo");
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, shu->objectName(), fazheng->objectName(), "heg_xuanhuo", QString());
    room->obtainCard(fazheng, this, reason, false);

    thread->trigger(CardUsed, room, shu, data);
    thread->trigger(CardFinished, room, shu, data);
}

void HXuanhuoAttachCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isNude() || !room->askForDiscard(source, "xuanhuo_discard", 1, 1, false, true, "@heg_xuanhuo-discard")) return;

    QString all_skills = "heg_wusheng+heg_paoxiao+heg_longdan+heg_tieqi+heg_liegong+heg_kuanggu";
    QStringList skill_list;
    QList<const Skill *> skills;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (p->hasShownGeneral1())
            skills << p->getActualGeneral1()->getVisibleSkillList();
        if (p->getGeneral2() && p->hasShownGeneral2())
            skills << p->getActualGeneral2()->getVisibleSkillList();
    }

    foreach (QString skill_name, all_skills.split("+")) {
        bool can_choose = true;
        foreach (const Skill *s, skills) {
            if (s->objectName() == skill_name || s->objectName() == skill_name + "_xh") {
                can_choose = false;
                break;
            }
        }
        if (can_choose)
            skill_list << skill_name;
    }
    if (skill_list.isEmpty()) return;
    QString skill_name = room->askForChoice(source, "heg_xuanhuo", skill_list.join("+"), QVariant(), "@heg_xuanhuo-choose", all_skills);
    skill_name = skill_name + "_xh";
    room->acquireSkillForSlot(source, skill_name, false, true, false);
    QStringList skillnames = source->getTag("XuanhuoSkills").toStringList();
    skillnames << skill_name;
    source->setTag("XuanhuoSkills", QVariant::fromValue(skillnames));
}

class HXuanhuoAttachVS : public ViewAsSkillV2
{
public:
    HXuanhuoAttachVS() : ViewAsSkillV2("heg_xuanhuoattach", 1)
    {
        attached_lord_skill = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
       if (player->hasUsed("HXuanhuoAttachCard")) return false;
       foreach (const Player *fazheng, player->getAliveSiblings()) {
           if (fazheng->hasShownSkill("heg_xuanhuo") && player->isFriendWith(fazheng))
               return true;
        }
       return false;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty() || to_select->hasFlag("using")) return false;
        return Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, to_select);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HXuanhuoAttachCard *rende_card = new HXuanhuoAttachCard;
        rende_card->addSubcard(originalCard);
        return rende_card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HXuanhuoAttachCard"; }
};

class HXuanhuoAttach : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HXuanhuoAttach() : TriggerSkillV2("heg_xuanhuoattach")
    {
        events << EventPhaseStart << GeneralShown << DFDebut;
        view_as_skill = new HXuanhuoAttachVS;
        attached_lord_skill = true;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::NotActive) {
            QStringList skills = player->getTag("XuanhuoSkills").toStringList();
            QStringList detachList;
            foreach(QString skill_name, skills)
                detachList.append("-" + skill_name + "!");
            room->handleAcquireDetachSkills(player, detachList, true);
            player->setTag("XuanhuoSkills", QVariant());
        } else if (triggerEvent == GeneralShown || triggerEvent == DFDebut) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                QList<const Skill *> skills;
                if (p->hasShownGeneral1())
                    skills << p->getActualGeneral1()->getVisibleSkillList();
                if (p->getGeneral2() && p->hasShownGeneral2())
                    skills << p->getActualGeneral2()->getVisibleSkillList();
                QStringList xuanhuoskills = p->getTag("XuanhuoSkills").toStringList();
                QStringList detachList;
                foreach (const Skill *skill, skills) {
                    QString skill_name = skill->objectName()+"_xh";
                    if (xuanhuoskills.contains(skill_name)) {
                        xuanhuoskills.removeOne(skill_name);
                        detachList.append("-" + skill_name + "!");
                    }
                }
                room->handleAcquireDetachSkills(p, detachList, true);
                p->setTag("XuanhuoSkills", QVariant::fromValue(xuanhuoskills));
            }
        }
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *, QVariant &) const
    {
        return TriggerList();
    }
};


class HWushengXH : public ViewAsSkillV2
{
public:
    HWushengXH() : ViewAsSkillV2("heg_wusheng_xh", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            return request.pattern == "slash";
        }
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return Slash::IsAvailable(player);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty() || to_select->hasFlag("using")) return false;

        if (!to_select->isRed() && (request.initiator->getSeemingKingdom() != "shu" || !hasShownShouyue(request.initiator)))
            return false;

        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            Slash slash(Card::SuitToBeDecided, -1);
            slash.addSubcard(to_select->getEffectiveId());
            return slash.isAvailable(request.initiator);
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Card *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard->getId());
        slash->setSkillName(objectName());
        slash->setShowSkill(objectName());
        return slash;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};


class HPaoxiaoXH : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HPaoxiaoXH() : TriggerSkillV2("heg_paoxiao_xh")
    {
        events << TargetSpecified << CardUsed;
        frequency = Compulsory;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
                                QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
        QList<ServerPlayer *> targets;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash")) return true;
        if (event == CardUsed) {
            if (room->countHistoryCards(player, "turn", "Slash") == 2) targets << nullptr;
        } else if (event == TargetSpecified && hasShownShouyue(player)
                   && player->getSeemingKingdom() == "shu") {
            for (ServerPlayer *target : use.to)
                if (target) targets << target;
        }
        // One exact skill source per ordered target; the dispatcher owns activation.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            for (int i = 0; i < targets.size(); ++i) {
                ServerPlayer *target = targets.at(i);
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = ref;
                ctx.sourceRef = ref;
                ctx.original_data = &data;
                ctx.current_event = event;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ref, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                if (target) {
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


    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardUsed) {
            ctx.manual_effect = true;
            ctx.owner->drawCards(1, objectName());
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->addQinggangTag(ctx.original_data->value<CardUseStruct>().card);
        return false;
    }
};

class HLongdanXH : public ViewAsSkillV2
{
public:
    HLongdanXH() : ViewAsSkillV2("heg_longdan_xh", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            return request.pattern == "jink" || request.pattern == "slash";
        }
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return Slash::IsAvailable(player);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty() || to_select->hasFlag("using")) return false;

        switch (request.reason) {
            case CardUseStruct::CARD_USE_REASON_PLAY: {
                return to_select->isKindOf("Jink");
            }
            case CardUseStruct::CARD_USE_REASON_RESPONSE:
            case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
                QString pattern = request.pattern;
                if (pattern == "slash")
                    return to_select->isKindOf("Jink");
                else if (pattern == "jink")
                    return to_select->isKindOf("Slash");
            }
            default:
                return false;
        }
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        if (originalCard->isKindOf("Slash")) {
            Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
            jink->addSubcard(originalCard);
            jink->setSkillName(objectName());
            return jink;
        } else if (originalCard->isKindOf("Jink")) {
            Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
            slash->addSubcard(originalCard);
            slash->setSkillName(objectName());
            return slash;
        } else
            return NULL;
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return QString();
        return Sanguosha->getCard(request.selectedCardIds.first())->isKindOf("Slash")
            ? "Jink" : "Slash";
    }
};

class HLiegongXH : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLiegongXH() : TriggerSkillV2("heg_liegong_xh")
    {
        events << TargetSpecified;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
                                QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
        QList<ServerPlayer *> targets;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash")) return true;
        for (ServerPlayer *target : use.to)
            if (target && target->getHp() >= player->getHp()) targets << target;
        // One exact skill source per ordered target; the dispatcher owns activation.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            for (int i = 0; i < targets.size(); ++i) {
                ServerPlayer *target = targets.at(i);
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = ref;
                ctx.sourceRef = ref;
                ctx.original_data = &data;
                ctx.current_event = event;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ref, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                if (target) {
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
        ServerPlayer *skill_target = ctx.preferredTarget;
        ServerPlayer *player = ctx.owner;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, QVariant::fromValue(skill_target))) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), skill_target->objectName());
            return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *huangzhong = ctx.owner;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        QString choice = room->askForChoice(huangzhong, objectName(), "nojink+adddamage", data, QString(), "@liegong-choice::"+ target->objectName());
        if (choice == "nojink") {
            QVariantList jink_list = huangzhong->getTag("Jink_" + use.card->toString()).toList();
            doLiegong(target, use, jink_list);
            huangzhong->setTag("Jink_" + use.card->toString(), jink_list);
        } else if (choice == "adddamage") {
            QStringList AddDamage_List = use.card->getTag("heg_liegong_xh_damage").toStringList();
            AddDamage_List << target->objectName();
            use.card->setTag("heg_liegong_xh_damage", AddDamage_List);
        }
        return false;
    }

private:
    static void doLiegong(ServerPlayer *target, CardUseStruct use, QVariantList &jink_list)
    {
        int index = use.to.indexOf(target);
        LogMessage log;
        log.type = "#NoJink";
        log.from = target;
        target->getRoom()->sendLog(log);
        if (index >= 0 && index < jink_list.size()) jink_list[index] = 0;
    }
};

class HKuangguXH : public TriggerSkillV2 {
public:
    HKuangguXH() : TriggerSkillV2("heg_kuanggu_xh")
    {
        events << PreDamageDone << Damage << DamageComplete;
        frequency = NotFrequent;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if ((event != PreDamageDone && event != DamageComplete) || !ctx.original_data) return;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || !damage.to || ctx.owner != damage.from) return;
        QVariantList ranges = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "damage_ranges").toList();
        // Each damage frame and skill instance keeps its own distance snapshot;
        // nested damage must not overwrite the outer frame's recovery eligibility.
        if (event == PreDamageDone) {
            const int distance = damage.from->distanceTo(damage.to);
            ranges << QVariant(distance >= 0 && distance <= 1);
        } else if (!ranges.isEmpty()) {
            ranges.removeLast();
        }
        if (ranges.isEmpty()) ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "damage_ranges");
        else ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "damage_ranges", ranges);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage || (!player || !player->isAlive() || !player->hasSkill(objectName()))) return {};
        const int count = data.value<DamageStruct>().damage;
        if (count <= 0) return {};
        QStringList choices;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const QVariantList ranges = player->getSkillInstanceStateValue(objectName(), id, "damage_ranges").toList();
            if (!ranges.isEmpty() && ranges.last().toBool())
                choices << SkillInstanceUtils::formatName(objectName(), id) + "*" + QString::number(count);
        }
        return choices.isEmpty() ? TriggerList() : TriggerList{{player, choices}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        if (ctx.owner->isWounded() && room->askForChoice(ctx.owner, objectName(), "recover+draw") == "recover")
            room->recover(ctx.owner, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
        else ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

HGanluCard::HGanluCard()
{
    setSkillName("heg_ganlu");
}

bool HGanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

bool HGanluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    switch (targets.length()) {
    case 0: return !to_select->getEquips().isEmpty();
    case 1: {
        int n1 = targets.first()->getEquips().length();
        int n2 = to_select->getEquips().length();
        return qAbs(n1 - n2) <= Self->getLostHp();
    }
    default:
        return false;
    }
}

void HGanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    LogMessage log;
    log.type = "#GanluSwap";
    log.from = source;
    log.to = targets;
    room->sendLog(log);

    ServerPlayer *first = targets.at(0), *second = targets.at(1);

    QList<int> equips1, equips2;
    foreach(const Card *equip, first->getEquips())
        equips1.append(equip->getId());
    foreach(const Card *equip, second->getEquips())
        equips2.append(equip->getId());

    CardMoveReason reason1(CardMoveReason::S_REASON_SWAP, source->objectName(), second->objectName(), "heg_ganlu", QString());
    CardMoveReason reason2(CardMoveReason::S_REASON_SWAP, source->objectName(), first->objectName(), "heg_ganlu", QString());
    CardMoveReason reason3(CardMoveReason::S_REASON_NATURAL_ENTER, QString());

    QList<CardsMoveStruct> move_to_table;
    CardsMoveStruct move1(equips1, NULL, Player::PlaceTable, reason1);
    CardsMoveStruct move2(equips2, NULL, Player::PlaceTable, reason2);
    move_to_table.push_back(move2);
    move_to_table.push_back(move1);
    if (!move_to_table.isEmpty()) {
        room->moveCardsAtomic(move_to_table, false);

        QList<CardsMoveStruct> back_move;

        if (first->isAlive()) {
            CardsMoveStruct move3(room->getCardIdsOnTable(equips2), first, Player::PlaceEquip, reason2);
            back_move.push_back(move3);
        } else {
            CardsMoveStruct move3(room->getCardIdsOnTable(equips2), NULL, Player::DiscardPile, reason3);
            back_move.push_back(move3);
        }
        if (second->isAlive()) {
            CardsMoveStruct move3(room->getCardIdsOnTable(equips1), second, Player::PlaceEquip, reason1);
            back_move.push_back(move3);
        } else {
            CardsMoveStruct move3(room->getCardIdsOnTable(equips1), NULL, Player::DiscardPile, reason3);
            back_move.push_back(move3);
        }

        if (!back_move.isEmpty())
            room->moveCardsAtomic(back_move, false);
    }
}

class HGanlu : public ViewAsSkillV2
{
public:
    HGanlu() : ViewAsSkillV2("heg_ganlu")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HGanluCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HGanluCard *card = new HGanluCard;
        card->setShowSkill(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HGanluCard"; }
};

class HBuyi : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HBuyi() : TriggerSkillV2("heg_buyi")
    {
        events << QuitDying;

    }

    virtual TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (player->isAlive() && player->getHp() > 0 && dying.damage && dying.damage->from && dying.damage->from->isAlive()) {
            TriggerList skill_list;
            QList<ServerPlayer *> wuguotais = room->findPlayersBySkillName(objectName());
            foreach (ServerPlayer *wuguotai, wuguotais) {
                if (wuguotai != NULL && wuguotai->isFriendWith(player) && !wuguotai->hasFlag("BuyiUsed"))
                    skill_list.insert(wuguotai, QStringList(objectName()));
            }
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *wuguotai = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.damage && dying.damage->from && dying.damage->from->isAlive() && wuguotai->askForSkillInvoke(this, QVariant::fromValue(dying.damage->from))) {
            room->broadcastSkillInvoke(objectName(), wuguotai);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, wuguotai->objectName(), dying.damage->from->objectName());
            room->setPlayerFlag(wuguotai, "BuyiUsed");
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *wuguotai = ctx.owner;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.damage && dying.damage->from && dying.damage->from->isAlive()) {
            if (!wuguotai->askCommandto(objectName(), dying.damage->from)) {
                RecoverStruct recover;
                recover.who = wuguotai;
                room->recover(player, recover);
            }
        }
        return false;
    }
};


class HKeshouViewAsSkill : public ViewAsSkillV2
{
public:
    HKeshouViewAsSkill() : ViewAsSkillV2("heg_keshou")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        // MethodNone prompts carry UNKNOWN but still require the exact selector.
        return request.initiator && request.pattern == "@@heg_keshou"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        QList<const Card *> selected;
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!card) return false;
            selected << card;
        }

        if (request.initiator->isJilei(to_select)) return false;
        if (selected.isEmpty())
            return true;
        else if (selected.length() == 1)
            return to_select->sameColorWith(selected.first());
        return false;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 2) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;

        DummyCard *discard = new DummyCard;
        discard->addSubcards(request.selectedCardIds);
        return discard;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "DummyCard"; }
};

class HKeshou : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HKeshou() : TriggerSkillV2("heg_keshou")
    {
        events << DamageInflicted;
        view_as_skill = new HKeshouViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && player->getCardCount(true) > 1)
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        const Card *card = room->askForCard(player, "@@heg_keshou", "@heg_keshou", data, Card::MethodNone);
        if (card) {
            room->broadcastSkillInvoke(objectName(), player);
            room->throwCard(card, objectName(), player, NULL);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();
        damage.damage--;

        bool no_friend = true;

        QList<ServerPlayer *> alls = room->getOtherPlayers(player);
        foreach (ServerPlayer *p, alls) {
            if (p->isFriendWith(player)) {
                no_friend = false;
                break;
            }
        }

        if (no_friend) {
            JudgeStruct judge;
            judge.pattern = ".|red";
            judge.good = true;
            judge.reason = objectName();
            judge.who = player;
            room->judge(judge);

            if (judge.isGood())
                player->drawCards(1, objectName());
        }

        data = QVariant::fromValue(damage);

        if (damage.damage <= 0)
            return true;

        return false;
    }
};

class HZhuwei : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZhuwei() : TriggerSkillV2("heg_zhuwei")
    {
        events << FinishJudge << EventPhaseStart;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() ==  Player::NotActive) {
            QList<ServerPlayer *> alls = room->getAlivePlayers();
            foreach (ServerPlayer *p, alls) {
                room->setPlayerMark(p, "#heg_zhuwei", 0);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != FinishJudge || !(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge && isDamageCard(judge->card))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        JudgeStruct *judge = data.value<JudgeStruct *>();
        player->obtainCard(judge->card);

        ServerPlayer *current = room->getCurrent();
        if (current != NULL && current->isAlive() && current->getPhase() != Player::NotActive) {
            if (room->askForChoice(player, objectName(), "yes+no", data, QString(), "@heg_zhuwei-choose:" + current->objectName()) == "yes") {
                room->addPlayerMark(current, "#heg_zhuwei");

                LogMessage log;
                log.type = "#ZhuweiBuff";
                log.from = player;
                log.to << current;
                room->sendLog(log);

            }
        }

        return false;
    }

private:
    static bool isDamageCard(const Card *card)
    {
        return card->isKindOf("Slash") || card->isKindOf("SavageAssault") || card->isKindOf("ArcheryAttack")
                || card->isKindOf("Duel") || card->isKindOf("FireAttack") || card->isKindOf("HBurningCamps")
                || card->isKindOf("Drowning");
    }
};

class HZhuweiTargetMod : public TargetModSkillV2
{
public:
    HZhuweiTargetMod() : TargetModSkillV2("#heg_zhuwei-target")
    {
        setHolderSelector(CorrectSkill_System);
    }

    virtual int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;
        return from->getMark("#heg_zhuwei");
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        if (!c.primary || !c.card) return CorrectSkillResult::noEffect();
        int n = 0;
        if (c.modType == TargetModSkill::Residue) n = getResidueNum(c.primary, c.card, c.secondary);
        else if (c.modType == TargetModSkill::DistanceLimit) n = getDistanceLimit(c.primary, c.card, c.secondary);
        return n ? CorrectSkillResult::useAmount(n) : CorrectSkillResult::noEffect();
    }
};

class HZhuweiMaxCards : public MaxCardsSkillV2
{
public:
    HZhuweiMaxCards() : MaxCardsSkillV2("#heg_zhuwei-maxcard")
    {
        setHolderSelector(CorrectSkill_System);
    }

    virtual int getExtra(const Player *target) const
    {
        return target->getMark("#heg_zhuwei");
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        return c.primary ? CorrectSkillResult::useAmount(getExtra(c.primary)) : CorrectSkillResult::noEffect();
    }
};

class HFudi : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HFudi() : TriggerSkillV2("heg_fudi")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *zhangxiu, QVariant &data) const override
    {
        if ((zhangxiu && zhangxiu->isAlive() && zhangxiu->hasSkill(objectName())) && !zhangxiu->isKongcheng()) {
            ServerPlayer *from = data.value<DamageStruct>().from;
            return (from && zhangxiu != from && from->isAlive()) ? TriggerList{{zhangxiu, {objectName()}}} : TriggerList();
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *from = data.value<DamageStruct>().from;
        player->setTag("FudiTarget", QVariant::fromValue(from)); // for AI
        QList<int> result = room->askForExchangeCards(player, objectName(), 1, 0, "@heg_fudi-give:"+ from->objectName(), "", ".|.|.|hand");
        player->removeTag("FudiTarget");
        if (!result.isEmpty()) {
            LogMessage l;
            l.type = "#InvokeSkill";
            l.from = player;
            l.arg = objectName();
            room->sendLog(l);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), from->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            room->notifySkillInvoked(player, objectName());

            DummyCard dummy(result);
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), from->objectName(), objectName(), QString());
            room->obtainCard(from, &dummy, reason, false);

            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zhangxiu = ctx.owner;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *from = damage.from;
        Room *room = zhangxiu->getRoom();

        if (!from || from->isDead()) return false;
        QList<ServerPlayer *> targets;
        int x = zhangxiu->getHp();
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->isFriendWith(from)) {
                if (p->getHp() < x) continue;
                if (p->getHp() > x)
                    targets.clear();
                x = p->getHp();
                targets << p;
            }
        }
        if (targets.isEmpty()) return false;

        ServerPlayer *target = room->askForPlayerChosen(zhangxiu, targets, "fudi_damage", "@heg_fudi-damage");
        room->damage(DamageStruct(objectName(), zhangxiu, target, 1));
        return false;
    }
};

class HCongjian : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HCongjian() : TriggerSkillV2("heg_congjian")
    {
        events << DamageInflicted << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return TriggerList();
        bool not_active = (player->getPhase() == Player::NotActive);

        if ((triggerEvent == DamageCaused && not_active) || (triggerEvent == DamageInflicted && !not_active))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.reason != "heg_fudi") {
                int n = qsanRandomBounded(2)+1;
                if (triggerEvent == DamageCaused)
                    n+=2;
                room->broadcastSkillInvoke(objectName(), n, player);
            }


            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        DamageStruct damage = data.value<DamageStruct>();

        damage.damage ++;
        data = QVariant::fromValue(damage);

        return false;
    }
};

HWeidiCard::HWeidiCard()
{
    setSkillName("heg_weidi");

}

bool HWeidiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->hasFlag("WeidiHadDrawCards");
}

void HWeidiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *player = effect.from, *to = effect.to;
    Room *room = player->getRoom();

    if (player->askCommandto("heg_weidi", to) || player == to || to->isKongcheng()) return;

    DummyCard *cards = to->wholeHandCards();
    cards->deleteLater();
    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
    room->moveCardTo(cards, player, Player::PlaceHand, reason);

    int x = qMin(cards->subcardsLength(), player->getCardCount(true));

    if (x > 0 && player->isAlive() && to->isAlive()) {
        to->setFlags("WeidiTarget");
        QList<int> result = room->askForExchangeCards(player, "weidi_give", x, x, QString("@heg_weidi-return:%1::%2").arg(to->objectName()).arg(x), "", ".");
        to->setFlags("-WeidiTarget");
        DummyCard dummy(result);
        CardMoveReason return_reason = CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName());
        room->moveCardTo(&dummy, to, Player::PlaceHand, return_reason);
    }

}


class HWeidi : public ViewAsSkillV2
{
public:
    HWeidi() : ViewAsSkillV2("heg_weidi")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HWeidiCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HWeidiCard *card = new HWeidiCard;
        card->setShowSkill(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HWeidiCard"; }
};


class HWeidiRecord : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HWeidiRecord() : TriggerSkillV2("heg_weidi_record")
    {
        events << EventPhaseStart << CardsMoveBatch;
        global = true;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseStart && (player->getPhase() == Player::RoundStart || player->getPhase() == Player::NotActive)) {
            QList<ServerPlayer *> players = room->getAlivePlayers();
            foreach (ServerPlayer *p, players) {
                room->setPlayerFlag(p, "-WeidiHadDrawCards");
            }
        } else if (triggerEvent == CardsMoveBatch) {
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from == NULL && (move.from_places.contains(Player::DrawPileBottom) || move.from_places.contains(Player::DrawPile))
                    && (move.to == player && move.to_place == Player::PlaceHand)) {
                    room->setPlayerFlag(player, "WeidiHadDrawCards");
                }
            }

        }
        return true;
    }

};



class HYongsi : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HYongsi() : TriggerSkillV2("heg_yongsi")
    {
        frequency = Compulsory;
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if ((player && player->isAlive() && player->hasSkill(objectName())) && use.card->isKindOf("HKnownBoth") && !player->isKongcheng())
            return TriggerList{{player, {objectName()}}};

        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else
            invoke = player->askForSkillInvoke(this, data);

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), 2, player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        room->showAllCards(player);

        return false;
    }
};

class HYongsiViewHas : public ViewAsEquipSkill
{
public:
    HYongsiViewHas() : ViewAsEquipSkill("#heg_yongsi-viewhas") {}
    QString viewAsEquip(const Player *player) const override
    {
        if (!player || !player->isAlive() || !player->hasShownSkill("heg_yongsi")) return QString();
        QList<const Player *> players = player->getAliveSiblings();
        players << player;
        for (const Player *p : players)
            if (p->getTreasure() && p->getTreasure()->isKindOf("HJadeSeal")) return QString();
        // Native virtual-equipment sources retain the exact Yongsi instance and validity.
        return "JadeSeal";
    }
};

class HJianan : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJianan() : TriggerSkillV2("heg_jianan$")
    {
        frequency = Compulsory;
        events << GeneralShown;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != GeneralShown) return true;
        if (player && player->isAlive() && player->hasLordSkill(objectName()) && data.toBool()) {
            room->sendCompulsoryTriggerLog(player, objectName());
            room->broadcastSkillInvoke(objectName(), player);
        }
        return true;
    }

};


class HEliteGeneralFlag : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HEliteGeneralFlag() : TriggerSkillV2("heg_elitegeneralflag")
    {
        events << EventPhaseStart << Death;
        global = true;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player != room->getLord("wei", true)) return true;

        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart) return true;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (player != death.who) return true;
        }
        QList<ServerPlayer *> players = room->getAlivePlayers();
        foreach (ServerPlayer *p, players) {
            room->removePlayerDisableShow(p, objectName());
            QStringList skills = p->getTag("JiananSkills").toStringList();
            QStringList detachList;
            foreach(QString skill_name, skills)
                detachList.append("-" + skill_name + "!");
            room->handleAcquireDetachSkills(p, detachList, true);
            p->setTag("JiananSkills", QVariant());
        }
        return true;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
                                QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive()
            || player->getPhase() != Player::Start || player->getSeemingKingdom() != "wei"
            || !player->hasShownOneGeneral() || player->isNude()
            || getAvailableGenerals(player).isEmpty()) return true;
        ServerPlayer *lord = room->getLord("wei");
        if (!lord || !lord->hasLordSkill("heg_jianan") || !lord->hasShownGeneral1()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = player;
        ctx.invoker = player;
        ctx.initiator = player;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.amount = getBaseAmount();
        contexts << ctx;
        return true;
    }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        // This global rule is available only while the shown Jianan lord remains.
        ServerPlayer *lord = room->getLord("wei");
        return ctx.owner && ctx.owner->isAlive() && ctx.owner == ctx.invoker
            && lord && lord->hasLordSkill("heg_jianan") && lord->hasShownGeneral1();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (room->askForCard(player, "..", "@heg_elitegeneralflag", QVariant(), objectName())) {
            ServerPlayer *lord = room->getLord("wei");
            if (lord)
                room->broadcastSkillInvoke(objectName(), lord);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        ctx.manual_effect = true;
        ServerPlayer *lord = room->getLord("wei");
        if (lord == NULL) return false;
        QStringList generals = getAvailableGenerals(player);
        bool head;
        if (generals.isEmpty()) return false;
        if (generals.length() == 1) head = (generals.first() == "head");
        else head = (room->askForChoice(player, "jianan_hide", generals.join("+"), data, QString(), "@jianan-hide") == "head");

        player->hideGeneral(head);
        room->setPlayerDisableShow(player, head?"h":"d", objectName());

        QStringList all_skills, skills = getAvailableSkills(room);
        all_skills << "tenyeartuxi" << "qiaobian" << "xiaoguo" << "heg_jieyue" << "heg_duanliang";

        if (skills.isEmpty()) return false;
        QString skill_name = room->askForChoice(player, "jianan_skill", skills.join("+"), data, "@jianan-skill", all_skills.join("+"));
        // Shared base skills still grant the distinct donor EGF variants.
        if (skill_name == "tenyeartuxi") skill_name = "heg_tuxi";
        else if (!skill_name.startsWith("heg_")) skill_name.prepend("heg_");
        skill_name += "_egf";

        room->acquireSkillForSlot(player, skill_name, false, true, false);
        QStringList skill_list = player->getTag("JiananSkills").toStringList();
        skill_list << skill_name;
        player->setTag("JiananSkills", QVariant::fromValue(skill_list));

        return false;
    }

    static QStringList getAvailableSkills(Room *room)
    {
        QStringList skills;
        skills << "tenyeartuxi" << "qiaobian" << "xiaoguo" << "heg_jieyue" << "heg_duanliang";
        QStringList _skills = skills;
        QList<ServerPlayer *> players = room->getAlivePlayers();
        foreach (ServerPlayer *p, players) {
            foreach (QString skill, _skills) {
                const QString native = skill == "heg_duanliang" ? "duanliang" : skill;
                const QString egf = skill == "tenyeartuxi" ? "heg_tuxi_egf"
                    : (skill.startsWith("heg_") ? skill : "heg_" + skill) + "_egf";
                if ((p->hasSkill(native, true) && p->hasShownSkill(native))
                    || (p->hasSkill(egf, true) && p->hasShownSkill(egf))) {
                    skills.removeOne(skill);
                }
            }
        }
        return skills;
    }

    static QStringList getAvailableGenerals(ServerPlayer *wei)
    {
        QStringList generals;
        if (!wei->hasShownGeneral1() || (wei->hasShownAllGenerals() && !wei->getActualGeneral1Name().contains("sujiang") && !wei->isLord()))
            generals << "head";

        if (!wei->hasShownGeneral2() || (wei->hasShownAllGenerals() && !wei->getActualGeneral2Name().contains("sujiang")))
            generals << "deputy";

        return generals;
    }
};



HHuibianCard::HHuibianCard()
{
    setSkillName("heg_huibian");
}

bool HHuibianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

bool HHuibianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    switch (targets.length()) {
    case 0: return to_select->getSeemingKingdom() == "wei";
    case 1: {
        return to_select->getSeemingKingdom() == "wei" && to_select->isWounded();
    }
    default:
        return false;
    }
}

void HHuibianCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *source = card_use.from;

    LogMessage log;
    log.from = source;
    log.to << card_use.to;
    log.type = "#UseCard";
    log.card_str = toString();
    room->sendLog(log);

    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, source, data);

    if (source->ownSkill("heg_huibian") && !source->hasShownSkill("heg_huibian"))
        source->showGeneral(source->inHeadSkills("heg_huibian"));

    thread->trigger(CardUsed, room, source, data);
    thread->trigger(CardFinished, room, source, data);
}

void HHuibianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *first = targets.at(0), *second = targets.at(1);

    room->damage(DamageStruct("heg_huibian", source, first));

    first->drawCards(2, "heg_huibian");

    if (second->isAlive() && second->canRecover()) {
        RecoverStruct recover;
        recover.who = source;
        room->recover(second, recover);
    }
}

class HHuibian : public ViewAsSkillV2
{
public:
    HHuibian() : ViewAsSkillV2("heg_huibian")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasUsed("HHuibianCard");
    }

    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HHuibianCard *card = new HHuibianCard;
        card->setShowSkill(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HHuibianCard"; }
};

class HZongyu : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZongyu() : TriggerSkillV2("heg_zongyu")
    {
        events << CardsMoveBatch;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
                                QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
        QList<ServerPlayer *> targets;
        if (!player->getOffensiveHorse() && !player->getDefensiveHorse()) return true;
        for (const QVariant &moveData : data.toList()) {
            const CardsMoveOneTimeStruct move = moveData.value<CardsMoveOneTimeStruct>();
            if (move.to_place != Player::PlaceEquip || !move.to || move.to == player) continue;
            for (int id : move.card_ids) {
                ServerPlayer *recipient = room->findPlayerByObjectName(move.to->objectName());
                if (recipient && Sanguosha->getCard(id)->isKindOf("HSixDragons")
                    && room->getCardOwner(id) == recipient && room->getCardPlace(id) == Player::PlaceEquip) {
                    targets << recipient;
                    break;
                }
            }
            if (!targets.isEmpty()) break;
        }
        // One exact skill source per ordered target; the dispatcher owns activation.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            for (int i = 0; i < targets.size(); ++i) {
                ServerPlayer *target = targets.at(i);
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = ref;
                ctx.sourceRef = ref;
                ctx.original_data = &data;
                ctx.current_event = event;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ref, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                if (target) {
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
        ServerPlayer *target = ctx.preferredTarget;
        ServerPlayer *player = ctx.owner;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            if (target)
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
            room->broadcastSkillInvoke(objectName());
            return true;
        }

        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *second) const override
    {
        ServerPlayer *first = ctx.owner;
        if (first->isDead() || second->isDead()) return false;
        QList<int> equips1, equips2;
        foreach(const Card *equip, first->getEquips())
            if (equip->isKindOf("Horse"))
                equips1.append(equip->getId());

        foreach(const Card *equip, second->getEquips())
            if (equip->isKindOf("Horse"))
                equips2.append(equip->getId());

        if (equips1.isEmpty() && equips2.isEmpty()) return false;

        LogMessage log;
        log.type = "#ZongyuSwap";
        log.from = first;
        log.to << second;
        room->sendLog(log);

        CardMoveReason reason1(CardMoveReason::S_REASON_SWAP, first->objectName(), second->objectName(), "heg_zongyu", QString());
        CardMoveReason reason2(CardMoveReason::S_REASON_SWAP, first->objectName(), first->objectName(), "heg_zongyu", QString());
        CardMoveReason reason3(CardMoveReason::S_REASON_NATURAL_ENTER, QString());

        QList<CardsMoveStruct> move_to_table;
        CardsMoveStruct move1(equips1, NULL, Player::PlaceTable, reason1);
        CardsMoveStruct move2(equips2, NULL, Player::PlaceTable, reason2);
        move_to_table.push_back(move2);
        move_to_table.push_back(move1);
        if (!move_to_table.isEmpty()) {
            room->moveCardsAtomic(move_to_table, false);

            QList<CardsMoveStruct> back_move;

            if (first->isAlive()) {
                CardsMoveStruct move3(room->getCardIdsOnTable(equips2), first, Player::PlaceEquip, reason2);
                back_move.push_back(move3);
            } else {
                CardsMoveStruct move3(room->getCardIdsOnTable(equips2), NULL, Player::DiscardPile, reason3);
                back_move.push_back(move3);
            }
            if (second->isAlive()) {
                CardsMoveStruct move3(room->getCardIdsOnTable(equips1), second, Player::PlaceEquip, reason1);
                back_move.push_back(move3);
            } else {
                CardsMoveStruct move3(room->getCardIdsOnTable(equips1), NULL, Player::DiscardPile, reason3);
                back_move.push_back(move3);
            }

            if (!back_move.isEmpty())
                room->moveCardsAtomic(back_move, false);
        }

        return false;
    }
};



class HZongyuCompulsory : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HZongyuCompulsory() : TriggerSkillV2("#heg_zongyu-compulsory")
    {
        frequency = Compulsory;
        events << CardUsed;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player == NULL || player->isDead() || !player->hasSkill("heg_zongyu")) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card != NULL && use.card->isKindOf("Horse") && room->isAllOnPlace(use.card, Player::PlaceTable)) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                foreach (const Card *card, p->getEquips()) {
                    if (Sanguosha->getCard(card->getEffectiveId())->isKindOf("HSixDragons"))
                        return TriggerList{{player, {objectName()}}};
                }
            }

            foreach (int id, room->getDiscardPile()) {
                if (Sanguosha->getCard(id)->isKindOf("HSixDragons"))
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill("heg_zongyu")) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, "heg_zongyu");
        } else
            invoke = player->askForSkillInvoke("heg_zongyu", data);

        if (invoke) {
            room->broadcastSkillInvoke("heg_zongyu", player);

            CardUseStruct use = data.value<CardUseStruct>();

            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "heg_zongyu", QString());
            room->throwCard(use.card, reason, NULL);


            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        if (player->isDead()) return false;

        const Card *six_dragons = NULL;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            foreach (const Card *card, p->getEquips()) {
                if (Sanguosha->getCard(card->getEffectiveId())->isKindOf("HSixDragons")) {
                    six_dragons = Sanguosha->getCard(card->getEffectiveId());
                    break;
                }
            }
            if (six_dragons != NULL)
                break;
        }
        if (six_dragons == NULL)
            foreach (int id, room->getDiscardPile()) {
            if (Sanguosha->getCard(id)->isKindOf("HSixDragons")) {
                six_dragons = Sanguosha->getCard(id);
                break;
            }
        }

        if (six_dragons == NULL) return false;
        room->moveCardTo(six_dragons, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), "heg_zongyu", QString()));
        return false;
    }
};

class HJieyueEGFViewAsSkill : public ViewAsSkillV2
{
public:
    HJieyueEGFViewAsSkill() : ViewAsSkillV2("heg_jieyue_egf", 1)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        // MethodNone prompts carry UNKNOWN but still require the exact selector.
        return request.initiator && request.pattern == "@@heg_jieyue_egf"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty() || to_select->hasFlag("using")) return false;
        return Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, to_select);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        HJieyueCard *jieyue_card = new HJieyueCard;
        jieyue_card->addSubcard(originalCard);
        return jieyue_card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "HJieyueCard"; }
};

class HJieyueEGF : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HJieyueEGF() : TriggerSkillV2("heg_jieyue_egf")
    {
        events << EventPhaseStart;
        view_as_skill = new HJieyueEGFViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->isKongcheng()) return TriggerList();
        if (player->getPhase() == Player::Start) return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Payment prompts run after willInvoke without revealing a different source.
        const bool addCostFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (addCostFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&] {
            if (addCostFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        const Card *card = room->askForUseCard(player, "@@heg_jieyue_egf", "@heg_jieyue", -1, Card::MethodNone);
        if (card) {
            QList<ServerPlayer *> players = player->getRoom()->getOtherPlayers(player);
            foreach (ServerPlayer *target, players) {
                if (target->hasFlag("JieyueTarget")) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                    LogMessage log;
                    log.type = "#ChoosePlayerWithSkill";
                    log.from = player;
                    log.to << target;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->notifySkillInvoked(player, objectName());
                    room->broadcastSkillInvoke(objectName(), player);
                    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "heg_jieyue", QString());
                    room->obtainCard(target, card, reason, false);
                    return true;
                }
            }

        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QList<ServerPlayer *> players = player->getRoom()->getOtherPlayers(player);
        foreach (ServerPlayer *p, players) {
            if (p->hasFlag("JieyueTarget")) {
                p->setFlags("-JieyueTarget");
                if (player->askCommandto("heg_jieyue", p))
                    player->drawCards(1, "heg_jieyue");
                else {
                    player->getRoom()->addPlayerMark(player, "JieyueExtraDraw");
                }
            }
        }
        return false;
    }
};

class HDuanliangEGF : public ViewAsSkillV2
{
public:
    HDuanliangEGF() : ViewAsSkillV2("heg_duanliang_egf", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        return !player->hasFlag("DuanliangEGFCannot");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->getEffectiveId() < 0
            || request.selectedCardIds.contains(to_select->getEffectiveId())) return false;
        if (!request.selectedCardIds.isEmpty() || to_select->hasFlag("using")) return false;
        return Sanguosha->matchExpPattern("BasicCard,EquipCard|black", request.initiator, to_select);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        // Replay the selection in order without allocating a preview card.
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (!canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        SupplyShortage *shortage = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
        shortage->setSkillName(objectName());
        shortage->setShowSkill(objectName());
        shortage->addSubcard(originalCard);
        shortage->setFlags("Global_NoDistanceChecking");
        return shortage;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "SupplyShortage"; }
};

class HSixDragonsSkill : public DistanceSkillV2
{
public:
    HSixDragonsSkill() :DistanceSkillV2("heg_SixDragons")
    {
        setHolderSelector(CorrectSkill_System);
    }

    virtual int getCorrect(const Player *from, const Player *to) const
    {
        int corrent = 0;
        {
            foreach (const Card *card, from->getEquips()) {
                if (card->isKindOf("HSixDragons") && !from->isEquipsNullified(card, to)) {
                    corrent = corrent -1;
                }
            }
        }
        {
            foreach (const Card *card, to->getEquips()) {
                if (card->isKindOf("HSixDragons") && !to->isEquipsNullified(card, from)) {
                    corrent = corrent +1;
                }
            }
        }
        return corrent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        if (!c.primary || !c.secondary) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(getCorrect(c.primary, c.secondary));
    }
};

// These effects belonged to donor standard-shu helpers; each is bound only to its granted variant.
class HPaoxiaoXHTarget : public TargetModSkillV2
{
public:
    HPaoxiaoXHTarget() : TargetModSkillV2("#heg_paoxiao_xh-target") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        return c.modType == TargetModSkill::Residue ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};
class HLiegongXHTarget : public TargetModSkillV2
{
public:
    HLiegongXHTarget() : TargetModSkillV2("#heg_liegong_xh-target") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        return c.modType == TargetModSkill::DistanceLimit && c.primary && c.secondary
            && c.primary->getHandcardNum() >= c.secondary->getHandcardNum()
            ? CorrectSkillResult::useAmount(10000) : CorrectSkillResult::noEffect();
    }
};
class HLiegongXHRange : public AttackRangeSkillV2
{
public:
    HLiegongXHRange() : AttackRangeSkillV2("#heg_liegong_xh-for-lord") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        return c.primary && hasShownShouyue(c.primary) && c.primary->getSeemingKingdom() == "shu"
            ? CorrectSkillResult::useAmount(1) : CorrectSkillResult::noEffect();
    }
};
class HDuanliangEGFTarget : public TargetModSkillV2
{
public:
    HDuanliangEGFTarget() : TargetModSkillV2("#heg_duanliang_egf-target", "SupplyShortage") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &c) const override
    {
        return c.modType == TargetModSkill::DistanceLimit && c.card && c.card->getSkillName() == "heg_duanliang_egf"
            ? CorrectSkillResult::useAmount(10000) : CorrectSkillResult::noEffect();
    }
};
class HLiegongXHDamage : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLiegongXHDamage() : TriggerSkillV2("#heg_liegong_xh-damage")
    { events << DamageCaused << CardFinished; global = true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card;
            if (card) card->removeTag("heg_liegong_xh_damage");
            return true;
        }
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to || damage.chain || damage.transfer || !damage.by_user) return true;
        const QStringList targets = damage.card->getTag("heg_liegong_xh_damage").toStringList();
        damage.damage += targets.count(damage.to->objectName());
        data = QVariant::fromValue(damage);
        return true;
    }
};
class HLongdanXHDraw : public TriggerSkillV2
{
public:
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    HLongdanXHDraw() : TriggerSkillV2("#heg_longdan_xh-draw")
    { events << CardUsed << CardResponded; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        return player && player->isAlive() && card && card->getSkillName() == "heg_longdan_xh"
            && player->hasSkill("heg_longdan_xh") && hasShownShouyue(player)
            && player->getSeemingKingdom() == "shu" ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ctx.manual_effect = true;
        room->notifySkillInvoked(room->getLord("shu"), "heg_shouyue");
        player->drawCards(1, "heg_longdan_xh");
        return false;
    }
};

HPowerPackage::HPowerPackage()
    : Package("heg_power")
{
    General *cuiyanmaojie = new General(this, "heg_cuiyanmaojie", "wei", 3);
    cuiyanmaojie->addSkill(new HZhengbi);
    cuiyanmaojie->addSkill(new HZhengbiTargetMod);
    cuiyanmaojie->addSkill(new HFengying);
    cuiyanmaojie->addSkill(new HFengyingAfter);
    insertRelatedSkills("heg_zhengbi", "#heg_zhengbi-target");
    insertRelatedSkills("heg_fengying", "#heg_fengying-after");
    cuiyanmaojie->addCompanion("heg_caopi");

    General *yujin = new General(this, "heg_yujin", "wei");
    yujin->addSkill(new HJieyue);
    yujin->addCompanion("heg_xiahoudun");

    General *wangping = new General(this, "heg_wangping", "shu");
    wangping->addSkill(new HJianglve);
    wangping->addCompanion("heg_jiangwanfeiyi");

    General *fazheng = new General(this, "heg_fazheng", "shu", 3);
    fazheng->addSkill(new HEnyuan);
    fazheng->addSkill(new HXuanhuo);
    fazheng->addRelateSkill("heg_wusheng_xh");
    fazheng->addRelateSkill("heg_paoxiao_xh");
    fazheng->addRelateSkill("heg_longdan_xh");
    fazheng->addRelateSkill("heg_tieqi_xh");
    fazheng->addRelateSkill("heg_liegong_xh");
    fazheng->addRelateSkill("heg_kuanggu_xh");
    fazheng->addCompanion("heg_liubei");

    General *wuguotai = new General(this, "heg_wuguotai", "wu", 3, false);
    wuguotai->addSkill(new HGanlu);
    wuguotai->addSkill(new HBuyi);
    wuguotai->addCompanion("heg_sunjian");

    General *lukang = new General(this, "heg_lukang", "wu", 3);
    lukang->addSkill(new HKeshou);
    lukang->addSkill(new HZhuwei);
    lukang->addSkill(new HZhuweiTargetMod);
    lukang->addSkill(new HZhuweiMaxCards);
    insertRelatedSkills("heg_zhuwei", "#heg_zhuwei-target");
    insertRelatedSkills("heg_zhuwei", "#heg_zhuwei-maxcard");
    lukang->addCompanion("heg_luxun");

    General *zhangxiu = new General(this, "heg_zhangxiu", "qun");
    zhangxiu->addSkill(new HFudi);
    zhangxiu->addSkill(new HCongjian);
    zhangxiu->addCompanion("heg_jiaxu");

    General *yuanshu = new General(this, "heg_yuanshu", "qun");
    yuanshu->addSkill(new HWeidi);
    yuanshu->addSkill(new HYongsi);
    yuanshu->addSkill(new HYongsiViewHas);
    insertRelatedSkills("heg_yongsi", "#heg_yongsi-viewhas");
    yuanshu->addCompanion("heg_jiling");

    General *caocao = new General(this, "heg_lord_caocao$", "wei", 4, true, true);
    caocao->addSkill(new HJianan);
    caocao->addSkill(new HHuibian);
    caocao->addSkill(new HZongyu);
    caocao->addSkill(new HZongyuCompulsory);
    caocao->addRelateSkill("heg_elitegeneralflag");
    caocao->addRelateSkill("heg_tuxi_egf");
    caocao->addRelateSkill("heg_qiaobian_egf");
    caocao->addRelateSkill("heg_xiaoguo_egf");
    caocao->addRelateSkill("heg_jieyue_egf");
    caocao->addRelateSkill("heg_duanliang_egf");
    insertRelatedSkills("heg_zongyu", "#heg_zongyu-compulsory");

    addMetaObject<HZhengbiCard>();
    addMetaObject<HFengyingCard>();
    addMetaObject<HJieyueCard>();
    addMetaObject<HJianglveCard>();
    addMetaObject<HXuanhuoAttachCard>();
    addMetaObject<HGanluCard>();
    addMetaObject<HWeidiCard>();
    addMetaObject<HHuibianCard>();

    insertRelatedSkills("heg_tieqi_xh", "#heg_tieqi_xh-clear");
    insertRelatedSkills("heg_paoxiao_xh", "#heg_paoxiao_xh-target");
    insertRelatedSkills("heg_liegong_xh", "#heg_liegong_xh-target");
    insertRelatedSkills("heg_liegong_xh", "#heg_liegong_xh-for-lord");
    insertRelatedSkills("heg_liegong_xh", "#heg_liegong_xh-damage");
    insertRelatedSkills("heg_longdan_xh", "#heg_longdan_xh-draw");
    insertRelatedSkills("heg_duanliang_egf", "#heg_duanliang_egf-target");
    skills << new HPaoxiaoXHTarget << new HLiegongXHTarget << new HLiegongXHRange << new HLiegongXHDamage
           << new HLongdanXHDraw << new HDuanliangEGFTarget;
    skills << new HJieyueDraw << new HTieqiXHClear << new HZhengbiGive << new HXuanhuoAttach << new HWeidiRecord << new HEliteGeneralFlag
           << new HWushengXH << new HPaoxiaoXH << new HLongdanXH << new HTieqiXH << new HLiegongXH << new HKuangguXH
           << new HTuxiEGF << new HQiaobianEGF << new HXiaoguoEGF << new HJieyueEGF << new HDuanliangEGF;
}

ADD_PACKAGE(HPower)

HSixDragons::HSixDragons(Card::Suit suit, int number, int correct) : Horse(suit, number, correct)
{
    setObjectName("SixDragons");
}

HPowerEquipPackage::HPowerEquipPackage() : Package("heg_power_equip", CardPack)
{
    Horse *horse = new HSixDragons(Card::Heart, 13, 0);
    horse->setObjectName("SixDragons");
    horse->setParent(this);

    skills << new HSixDragonsSkill;
}

ADD_PACKAGE(HPowerEquip)
