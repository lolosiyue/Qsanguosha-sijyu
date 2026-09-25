/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of QSanguosha-Hegemony.

    This game is free software; you can redistribute it and/or
    modify it under the terms of the MOL General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    See the LICENSE file for more details.

    Mogara
    *********************************************************************/

#include "h-mol.h"
#include "h-formation.h"
#include <QScopeGuard>
#include "standard.h"
#include "h-standard-tricks.h"
#include "h-standard-shu-generals.h"
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
#include "skill-declaration.h"
// New HEG donor: TODO/QSanguosha-For-Hegemony-xxyheaven @ cf61c15.
// Namespace mapping: docs/hegemony-xxy-names.json.

namespace {
QStringList molState(const Player *player, const char *property)
{
    return player->property(property).toString().split('+', Qt::SkipEmptyParts);
}

void setMolState(Room *room, ServerPlayer *player, const char *property,
                 const QStringList &values, const QString &markStem)
{
    const QStringList previous = molState(player, property);
    if (previous == values) return;
    // String properties use the native reconnect transport; composite marks
    // render public values through the existing translated mark display.
    if (!previous.isEmpty())
        room->setPlayerMark(player, markStem + "+" + previous.join("+"), 0);
    room->setPlayerProperty(player, property, values.join("+"));
    if (!values.isEmpty())
        room->setPlayerMark(player, markStem + "+" + values.join("+"), 1);
    if (QString::fromLatin1(property) == "heg_sidi_invalidity") {
        room->filterCards(player, player->getCards("he"), true);
        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
    }
}

void appendMolState(Room *room, ServerPlayer *player, const char *property,
                    const QString &value, const QString &markStem)
{
    QStringList values = molState(player, property);
    if (!values.contains(value)) values << value;
    setMolState(room, player, property, values, markStem);
}

QList<int> jiansuMoney(const Player *player)
{
    QList<int> ids;
    for (const QString &value : molState(player, "heg_jiansu_money"))
        ids << value.toInt();
    return ids;
}

void setJiansuMoney(Room *room, ServerPlayer *player, const QList<int> &ids)
{
    QStringList values;
    for (int id : ids) values << QString::number(id);
    // Eligible card ids are owner-only, including reconnect. PlayerStateService
    // rejects spectator delivery of this private property.
    player->setProperty("heg_jiansu_money", values.join("+"));
    player->addProperty("heg_jiansu_money");
    room->notifyProperty(player, player, "heg_jiansu_money");
    room->setPlayerMark(player, "@money", ids.length());
}
}

class HMOLStateRecord : public TriggerSkillV2
{
public:
    explicit HMOLStateRecord(const QString &name) : TriggerSkillV2(name)
    {
        global = true;
        events << EventPhaseChanging << EventPhaseStart << EventLoseSkill;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *target, QVariant &data) const override
    {
        if (event == EventPhaseStart) {
            if (target && target->getPhase() == Player::NotActive)
                for (ServerPlayer *p : room->getAllPlayers(true)) room->setPlayerMark(p, "##juzhan-turn", 0);
            return true;
        }
        if (event == EventLoseSkill) {
            const SkillChangeStruct change = data.value<SkillChangeStruct>();
            // A removed copy must not erase a pile still owned by another grant.
            if (!target || change.instanceID <= 0 || target->ownsSkill(change.skillName)
                || target->hasSkillInstance(change.skillName, change.instanceID)) return true;
            if (change.skillName == "heg_tunchu") target->clearOnePrivatePile("heg_food");
            else if (change.skillName == "heg_sidi") target->clearOnePrivatePile("heg_drive");
            else if (change.skillName == "heg_yinbingx") target->clearOnePrivatePile("kerchief");
            return true;
        }
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        // Committed effects expire even after the owner dies or loses the skill.
        // Each package registers this idempotent hook for independent loading.
        for (ServerPlayer *player : room->getAllPlayers(true)) {
            setMolState(room, player, "heg_sidi_limit", {}, "&heg_sidi");
            setMolState(room, player, "heg_sidi_invalidity", {}, "&heg_sidi");
            setMolState(room, player, "heg_jilei", {}, "&heg_jilei");
            room->setPlayerProperty(player, "heg_juzhan_prohibited", QString());
            room->removePlayerDisableShow(player, "heg_juzhan");
        }
        return true;
    }
};

class HJuzhanProhibit : public ProhibitSkill
{
public:
    HJuzhanProhibit() : ProhibitSkill("#heg_juzhan-prohibit") { }
    bool isProhibited(const Player *from, const Player *to, const Card *card,
                      const QList<const Player *> &) const override
    {
        // Donor GlobalProhibit applies only to non-skill cards for this turn.
        return from && to && card && card->getTypeId() != Card::TypeSkill
            && molState(from, "heg_juzhan_prohibited").contains(to->objectName());
    }
};

class HWuku : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HWuku() : TriggerSkillV2("heg_wuku")
    {
        events << CardUsed << EventLoseSkill;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventLoseSkill && player && data.value<SkillChangeStruct>().skillName == objectName()) {
            room->setPlayerMark(player, "#heg_wuku", 0);
        }
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardUsed && player != NULL && player->hasShownOneGeneral()) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() == Card::TypeEquip) {
                QList<ServerPlayer *> owners = room->findPlayersBySkillName(objectName());
                TriggerList skill_list;
                foreach (ServerPlayer *owner, owners)
                    if (owner->hasShownOneGeneral() && !player->isFriendWith(owner) && owner->getMark("#heg_wuku") < 2)
                        skill_list.insert(owner, QStringList(objectName()));
                return skill_list;
            }
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        ServerPlayer *player = ctx.owner;
        player->gainMark("#heg_wuku");
        return false;
    }
};

// These declarations produce native cards. V2 owns payment, the exact source,
// and reveal; previews never consume Wuku or the alternating Guishu state.
class HMiewu : public ViewAsSkillV2
{
public:
    HMiewu() : ViewAsSkillV2("heg_miewu", 1) { response_or_use = true; }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::guhuo(objectName(), true, true, false, false, true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || player->getMark("#wuku") < 1 || player->hasFlag("MiewuUsed")) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && !usableNames(request).isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    { return ViewAsSkillV2::canSelectCard(request, candidate) && candidate && !candidate->hasFlag("using"); }
    bool pay(Room *room, SkillContext &context, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)
            || !ViewAsSkillV2::pay(room, context, request)) return false;
        ServerPlayer *player = context.initiator;
        if (!player) return false;
        player->loseMark("#wuku");
        room->setPlayerFlag(player, "MiewuUsed");
        return true;
    }

protected:
    Card *buildCard(const ActiveSkillRequest &request, const QString &name) const override
    {
        Card *card = ViewAsSkillV2::buildCard(request, name);
        if (card) card->setShowSkill(objectName());
        return card;
    }
};

class HMiewuDraw : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HMiewuDraw() : TriggerSkillV2("#heg_miewu-draw")
    { events << EventSkillInvoking; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
        QVariant &data, QList<SkillContext> &contexts) const override
    {
        const SkillContext activation = data.value<SkillContext>();
        const SkillInstanceRef root = room->resolveSkillInstanceRootRef(activation.activationRef);
        if (!player || !root.isValid() || triggerable(event, room, player, data).isEmpty()) return true;
        // The conversion selects one precise source; another same-name grant
        // must not duplicate its post-payment draw.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef ref(player->objectName(), SkillInstanceKey(objectName(), id));
            if (room->resolveSkillInstanceRootRef(ref) != root) continue;
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
            ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
            if (!amountOk) ctx.amount = getBaseAmount();
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const SkillContext activation = data.value<SkillContext>();
        if (!player || activation.skill_name != "heg_miewu" || activation.is_canceled
            || activation.initiator != player || !activation.use_card
            || activation.use_card->getSkillName() != "heg_miewu") return {};
        // Both use and response enter this hook once, only after successful pay.
        return TriggerList{{player, {objectName()}}};
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return true; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker; player->drawCards(1, "heg_miewu"); return false; }
};

class HGuishuViewAsSkill : public ViewAsSkillV2
{
public:
    HGuishuViewAsSkill() : ViewAsSkillV2("heg_guishu", 1) { response_or_use = true; }
    SkillDialogInfo getDialogInfo() const override
    { return SkillDialogInfo::juguan(objectName(), "befriend_attacking,known_both"); }
    SkillDeclarationReason declarationReason(const Player *self, const QString &value, const Card *) const override
    {
        const QStringList names = {"befriend_attacking", "known_both"};
        const int index = names.indexOf(value);
        return self && index >= 0 && self->getMark("GuishuCardState") != index + 1
            ? SkillDeclarationReason::None : SkillDeclarationReason::CandidateUnavailable;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return ViewAsSkillV2::canSelectCard(request, candidate) && candidate
            && candidate->getSuit() == Card::Spade && !candidate->isEquipped();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)
            || declarationReason(request.initiator, request.userString, nullptr) != SkillDeclarationReason::None)
            return nullptr;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        if (!canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()))) return nullptr;
        Card *card = Sanguosha->cloneCard(request.userString);
        if (!card) return nullptr;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        card->setShowSkill(objectName());
        card->setCanRecast(false);
        return card;
    }
    bool pay(Room *room, SkillContext &context, const ActiveSkillRequest &request) const override
    {
        if (declarationReason(request.initiator, request.userString, nullptr) != SkillDeclarationReason::None
            || !ViewAsSkillV2::pay(room, context, request)) return false;
        if (!context.initiator) return false;
        room->setPlayerMark(context.initiator, "GuishuCardState", request.userString == "befriend_attacking" ? 1 : 2);
        return true;
    }
};

class HGuishu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    SkillDialogInfo getDialogInfo() const override
    { return view_as_skill->getDialogInfo(); }
    SkillDeclarationReason declarationReason(const Player *self, const QString &value, const Card *card) const override
    { return view_as_skill->declarationReason(self, value, card); }
    HGuishu() : TriggerSkillV2("heg_guishu")
    {
        view_as_skill = new HGuishuViewAsSkill;
        events << EventPhaseStart;
    }

    bool recordEvent(TriggerEvent , Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player->getPhase() == Player::NotActive)
            room->setPlayerMark(player, "GuishuCardState", 0);
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        return {};
    }
};

class HYuanyu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYuanyu() : TriggerSkillV2("heg_yuanyu")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->isAlive() && !damage.from->inMyAttackRange(player))
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(this)) {
            room->sendCompulsoryTriggerLog(player, objectName());
            invoke = true;
        } else invoke = player->askForSkillInvoke(this, data);
        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        damage.damage--;
        data = QVariant::fromValue(damage);
        if (damage.damage <= 0)
            return true;
        return false;
    }
};

class HSidi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HSidi() : TriggerSkillV2("heg_sidi")
    {
        events << Damaged << EventPhaseStart << EventPhaseEnd;
    }

    virtual TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        TriggerList skill_list;
        if (player == NULL || player->isDead()) return skill_list;
        if (triggerEvent == Damaged && !player->isNude()) {
            QList<ServerPlayer *> caozhens = room->findPlayersBySkillName(objectName());
            foreach (ServerPlayer *caozhen, caozhens) {
                if (!caozhen->isFriendWith(player)) continue;
                QString type_name[4] = { QString(), "heg_BasicCard", "TrickCard", "EquipCard" };
                QStringList types;
                types << "heg_BasicCard" << "TrickCard" << "EquipCard";
                foreach (int card_id, caozhen->getPile("heg_drive")) {
                    types.removeOne(type_name[Sanguosha->getCard(card_id)->getTypeId()]);
                }
                if (!types.isEmpty())
                    skill_list.insert(caozhen, QStringList(objectName()));
            }
        } else if (triggerEvent == EventPhaseEnd && player->getPhase() == Player::RoundStart && player->isAlive()) {
            QList<ServerPlayer *> caozhens = room->findPlayersBySkillName(objectName());
            foreach (ServerPlayer *caozhen, caozhens) {
                if (!caozhen->isFriendWith(player) && !caozhen->getPile("heg_drive").isEmpty())
                    skill_list.insert(caozhen, QStringList(objectName()));
            }
        }
        return skill_list;
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (triggerEvent == Damaged) {
            if (ask_who->askForSkillInvoke(this, QVariant::fromValue(player))) {
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2)+1, ask_who);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), player->objectName());
                invoke = true;
            }
        } else if (triggerEvent == EventPhaseEnd) {
            QList<int> ints = room->askForExchangeCards(ask_who, objectName(), 3, 0, "@sidi-remove::"+player->objectName(), "heg_drive");
            if (!ints.isEmpty()) {
                invoke = true;
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = ask_who;
                log.arg = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(ask_who, objectName());
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2)+3, ask_who);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), player->objectName());
                CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), ask_who->objectName(), objectName(), QString());
                DummyCard dummy(ints);
                room->throwCard(&dummy, reason, NULL);
                QString type_name[4] = { QString(), "heg_BasicCard", "TrickCard", "EquipCard" };
                QStringList sidi_types;
                foreach (int id, ints) {
                    sidi_types << type_name[Sanguosha->getCard(id)->getTypeId()];
                }

                ask_who->setTag("sidi_types", sidi_types);
            }

        }

        return invoke;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        if (triggerEvent == Damaged) {
            QString type_name[4] = { QString(), "heg_BasicCard", "TrickCard", "EquipCard" };
            QStringList types;
            types << "heg_BasicCard" << "TrickCard" << "EquipCard";
            foreach (int card_id, ask_who->getPile("heg_drive")) {
                types.removeOne(type_name[Sanguosha->getCard(card_id)->getTypeId()]);
            }
            if (types.isEmpty()) return false;
            QList<int> ints = room->askForExchangeCards(player, "sidi_put", 1, 0, "@sidi-put:"+ask_who->objectName(), QString(), types.join(","));
            if (!ints.isEmpty())
                ask_who->addToPile("heg_drive", ints);

        } else if (triggerEvent == EventPhaseEnd) {
            QStringList sidi_types = ask_who->getTag("sidi_types").toStringList();
            ask_who->removeTag("sidi_types");
            int x = sidi_types.length();

            QStringList choices;
            choices << "cardlimit" << "skilllimit" << "recover";
            QStringList all_choices = choices;

            for (int i = 0; i < x; i++) {
                if (player->isDead() || ask_who->isDead() || choices.isEmpty()) break;
                QString choice = room->askForChoice(ask_who, "sidi_choice", choices.join("+"), QVariant(), all_choices.join("+"),
                                   "@sidi-choice::"+ player->objectName());

                choices.removeOne(choice);

                if (choice == "recover") {
                    QList<ServerPlayer *> players = room->getOtherPlayers(ask_who), weis;
                    foreach (ServerPlayer *p, players) {
                        if (p->isFriendWith(ask_who) && p->canRecover())
                            weis << p;
                    }
                    if (!weis.isEmpty()) {
                        ServerPlayer *to = room->askForPlayerChosen(player, weis, "sidi_recover", "@sidi-recover");
                        RecoverStruct recover;
                        recover.who = player;
                        room->recover(to, recover);
                    }
                }
                if (choice == "cardlimit") {
                    QString cardtype = room->askForChoice(ask_who, "sidi_cardtype", sidi_types.join("+"),
                        QVariant(), "BasicCard+EquipCard+TrickCard", "@sidi-cardtype::"+player->objectName());
                    room->setPlayerCardLimitation(player, "use", cardtype, true);
                    appendMolState(room, player, "heg_sidi_limit", "log_" + cardtype, "&heg_sidi");
                }
                if (choice == "skilllimit") {
                    QStringList skill_names;
                    if (player->hasShownGeneral1()) {
                        foreach (const Skill *skill, player->getActualGeneral1()->getVisibleSkillList()) {
                            skill_names << skill->objectName();
                        }
                    }
                    if (player->getGeneral2() && player->hasShownGeneral2()) {
                        foreach (const Skill *skill, player->getActualGeneral2()->getVisibleSkillList()) {
                            skill_names << skill->objectName();
                        }
                    }
                    if (!skill_names.isEmpty()) {
                        QString skill_name = room->askForChoice(ask_who, "sidi_skill", skill_names.join("+"),
                                                             QVariant(), QString(), "@sidi-skill::"+player->objectName());

                        appendMolState(room, player, "heg_sidi_invalidity", skill_name, "&heg_sidi");
                    }

                }
            }
        }

        return false;
    }
};

class HSidiInvalidity : public InvaliditySkill
{
public:
    HSidiInvalidity() : InvaliditySkill("#heg_sidi-invalidity")
    {

    }

    virtual bool isSkillValid(const Player *target, const Skill *skill) const
    {
        return !molState(target, "heg_sidi_invalidity").contains(skill->objectName());
    }
};

class HDangxian : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDangxian() : TriggerSkillV2("heg_dangxian")
    {
        events << GeneralShowed << EventPhaseEnd;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        if (triggerEvent == GeneralShowed) {
            if (player->cheakSkillLocation(objectName(), data.toStringList()) && player->getMark("dangxianUsed") == 0)
                return TriggerList{{player, {objectName()}}};
        } else if (triggerEvent == EventPhaseEnd && player->getPhase() == Player::RoundStart) {
            return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        ServerPlayer *player = ctx.invoker;
        if (triggerEvent == GeneralShowed) {
            room->addPlayerMark(player, "dangxianUsed");
            room->addPlayerMark(player, "@firstshow");
        } else if (triggerEvent == EventPhaseEnd) {
            player->insertPhase(Player::Play);
        }
        return false;
    }
};

class HHuanshi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HHuanshi() : TriggerSkillV2("heg_huanshi")
    {
        events << AskForRetrial;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player->isFriendWith(judge->who)) return {};
        if (player->isNude() && player->getHandPile().isEmpty()) return {};
        return TriggerList{{player, {objectName()}}};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        JudgeStruct *judge = data.value<JudgeStruct *>();

        QStringList prompt_list;
        prompt_list << "@huanshi-card" << judge->who->objectName()
            << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
        QString prompt = prompt_list.join(":");

        const Card *card = room->askForCard(player, "..", prompt, data, Card::MethodResponse, judge->who, true);

        if (card) {
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);

            LogMessage log2;
            log2.card_str = card->toString();
            log2.from = player;
            log2.type = QString("#%1_Resp").arg(card->getClassName());
            room->sendLog(log2);

            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName(), player);

            int id = card->getEffectiveId();
            bool isHandcard = (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand);

            CardMoveReason reason(CardMoveReason::S_REASON_RESPONSE, player->objectName(), objectName(), QString());

            room->moveCardTo(card, NULL, Player::PlaceTable, reason);

            CardResponseStruct resp(card, judge->who, false);
            resp.m_isHandcard = isHandcard;
            resp.m_isRetrial = true;
            QVariant _data = QVariant::fromValue(resp);
            room->getThread()->trigger(CardResponded, room, player, _data);

            QStringList card_list = player->getTag("huanshi_cards").toStringList();
            card_list.append(card->toString());
            player->setTag("huanshi_cards", card_list);

            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        QStringList card_list = player->getTag("huanshi_cards").toStringList();

        if (card_list.isEmpty()) return false;

        QString card_str = card_list.takeLast();
        player->setTag("huanshi_cards", card_list);

        const Card *card = Card::Parse(card_str);
        if (card) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            room->retrial(card, player, judge, objectName(), false);
            judge->updateResult();
        }
        return false;
    }
};

HHongyuanCard::HHongyuanCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void HHongyuanCard::extraCost(Room *room, const CardUseStruct &card_use) const
{
    room->showCard(card_use.from, subcards);
}

void HHongyuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList hongyuan_ids;
    if (!source->property("view_as_transferable").isNull())
        hongyuan_ids = source->property("view_as_transferable").toString().split("+");
    foreach (int id, subcards) {
        hongyuan_ids << QString::number(id);
    }
    room->setPlayerProperty(source, "view_as_transferable", hongyuan_ids);
}

class HHongyuanViewAsSkill : public ViewAsSkillV2
{
public:
    HHongyuanViewAsSkill() : ViewAsSkillV2("heg_hongyuan", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HHongyuanCard");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
        return request.selectedCardIds.isEmpty() && !candidate->hasFlag("using")
            && Sanguosha->matchExpPattern(".|.|.|hand", request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection order so forged requests obey the same card restrictions.
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
        HHongyuanCard *card = new HHongyuanCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        card->setShowSkill(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HHongyuanCard"; }
};

class HHongyuan : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HHongyuan() : TriggerSkillV2("heg_hongyuan")
    {
        events << BeforeCardsMoveBatch << EventPhaseChanging << PreCardsMoveBatch;
        view_as_skill = new HHongyuanViewAsSkill;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == PreCardsMoveBatch && !player->property("view_as_transferable").isNull()) {
            QStringList hongyuan_ids = player->property("view_as_transferable").toString().split("+");
            QStringList hongyuan_copy = hongyuan_ids;
            foreach (QString card_data, hongyuan_copy) {
                int id = card_data.toInt();
                if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand)
                    hongyuan_ids.removeOne(card_data);
            }
            if (hongyuan_ids.isEmpty())
                room->setPlayerProperty(player, "view_as_transferable", QVariant());
            else
                room->setPlayerProperty(player, "view_as_transferable", hongyuan_ids.join("+"));
        } else if (triggerEvent == EventPhaseChanging)
            room->setPlayerProperty(player, "view_as_transferable", QVariant());
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == BeforeCardsMoveBatch && (player && player->isAlive() && player->hasSkill(objectName()))) {
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.to == player && move.to_place == Player::PlaceHand && move.reason.m_reason == CardMoveReason::S_REASON_DRAW
                        && move.reason.m_skillName == "transfer") {
                    QList<ServerPlayer *> all_players = room->getOtherPlayers(player);
                    foreach (ServerPlayer *p, all_players) {
                        if (player->isFriendWith(p))
                            return TriggerList{{player, {objectName()}}};
                    }
                }
            }
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QList<ServerPlayer *> to_choose, all_players = room->getOtherPlayers(player);
        foreach (ServerPlayer *p, all_players) {
            if (player->isFriendWith(p))
                to_choose << p;
        }
        if (to_choose.isEmpty()) return false;

        ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "hongyuan-invoke", true, true);
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);

            QStringList target_list = player->getTag("hongyuan_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("hongyuan_target", target_list);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        QStringList target_list = player->getTag("hongyuan_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("hongyuan_target", target_list);

        ServerPlayer *to = room->findPlayerByObjectName(target_name);

        if (to) {
            QVariantList move_datas = data.toList();
            QVariantList new_datas;
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.to == player && move.to_place == Player::PlaceHand && move.reason.m_reason == CardMoveReason::S_REASON_DRAW
                        && move.reason.m_skillName == "transfer") {
                    move.to = to;
                }
                new_datas << QVariant::fromValue(move);
            }
            data = QVariant::fromValue(new_datas);
        }
        return false;
    }
};

class HMingzhe : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HMingzhe() : TriggerSkillV2("heg_mingzhe")
    {
        events  << CardsMoveBatch << CardUsed << CardResponded;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->getPhase() != Player::NotActive) return {};
        if (triggerEvent == CardUsed || triggerEvent == CardResponded) {
            const Card *cardstar = NULL;
            if (triggerEvent == CardUsed) {
                CardUseStruct use = data.value<CardUseStruct>();
                cardstar = use.card;
            } else {
                CardResponseStruct resp = data.value<CardResponseStruct>();
                cardstar = resp.m_card;
            }
            if (cardstar && cardstar->getTypeId() != Card::TypeSkill && cardstar->isRed())
                return TriggerList{{player, {objectName()}}};

        } else if (triggerEvent == CardsMoveBatch) {
            QVariantList move_datas = data.toList();
            foreach (QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.from == player && move.from_places.contains(Player::PlaceEquip)) {
                    for (int i = 0; i < move.card_ids.length(); ++i) {
                        const Card *card = Sanguosha->getCard(move.card_ids.at(i));
                        if (card && card->isRed() && move.from_places.at(i) == Player::PlaceEquip) {
                            return TriggerList{{player, {objectName()}}};
                        }
                    }
                }
            }
        }

        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        player->drawCards(1, objectName());
        return false;
    }
};

class HDanlao : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDanlao() : TriggerSkillV2("heg_danlao")
    {
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() == Card::TypeTrick && (player && player->isAlive() && player->hasSkill(objectName()))) {
            bool can_trigger = false;
            foreach (ServerPlayer *p, use.to) {
                if (p->isAlive() && p != player) {
                    can_trigger = true;
                    break;
                }
            }
            if (can_trigger)
                return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        QVariant &data = *ctx.original_data;
        player->drawCards(1, objectName());
        CardUseStruct use = data.value<CardUseStruct>();
        use.nullified_list << player->objectName();
        data = QVariant::fromValue(use);
        return false;
    }
};

class HJilei : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJilei() : TriggerSkillV2("heg_jilei")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            ServerPlayer *from = data.value<DamageStruct>().from;
            return (from && from->isAlive()) ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (from && from->isAlive() && player->askForSkillInvoke(this, QVariant::fromValue(from))) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), from->objectName());
            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *yangxiu = ctx.invoker;
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        QString choice = room->askForChoice(yangxiu, objectName(), "BasicCard+EquipCard+TrickCard",
                                            data, QString(), "@jilei-choose::" + damage.from->objectName());

        LogMessage log;
        log.type = "#Jilei";
        log.from = damage.from;
        log.arg = choice;
        room->sendLog(log);

        QString _type = choice + "|.|.|hand"; // Handcards only
        room->setPlayerCardLimitation(damage.from, "use,response,discard", _type, true);

        QString log_name = "log_" + choice;
        QStringList mark = molState(damage.from, "heg_jilei");
        if (!mark.contains(log_name)) {
            mark.append(log_name);
        }
        setMolState(room, damage.from, "heg_jilei", mark, "&heg_jilei");
        return false;
    }
};

class HWanglie : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HWanglie() : TriggerSkillV2("heg_wanglie")
    {
        events << CardUsed << CardResponded << EventPhaseChanging << EventPhaseStart << GeneralShown << EventAcquireSkill;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        // Only project the distance-availability bit to clients; history owns the count.
        if (triggerEvent == EventPhaseStart) {
            for (ServerPlayer *p : room->getAllPlayers(true))
                room->setPlayerMark(p, "heg_wanglie_first", room->countHistoryCards(p, "phase", QString(), false, true) == 0);
        } else if (player && triggerEvent != EventPhaseChanging) {
            room->setPlayerMark(player, "heg_wanglie_first", room->countHistoryCards(player, "phase", QString(), false, true) == 0);
        }
        if (triggerEvent == EventPhaseChanging) {
            if (player->getMark("##wanglie") > 0) {
                int x = player->getMark("##wanglie");
                for (int i = 1; i <= x; i++)
                    room->removePlayerCardLimitation(player, "use", ".");
                room->setPlayerMark(player, "##wanglie", 0);
            }
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != CardUsed || !(player && player->isAlive() && player->hasSkill(objectName())) || player->getPhase() != Player::Play) return {};
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && (use.card->isKindOf("Slash") || use.card->isNDTrick()))
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        CardUseStruct use = data.value<CardUseStruct>();
        use.no_respond_list << "_ALL_TARGETS";
        data = QVariant::fromValue(use);
        room->setPlayerCardLimitation(player, "use", ".", false);
        room->addPlayerMark(player, "##wanglie");
        return false;
    }
};

class HWanglieTarget : public TargetModSkillV2
{
public:
    HWanglieTarget() : TargetModSkillV2("#heg_wanglie-target")
    {
        frequency = NotFrequent;
        pattern = "^SkillCard";
    }

    virtual int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (!from || !from->hasShownSkill("heg_wanglie")) return 0;
        const ServerPlayer *server = qobject_cast<const ServerPlayer *>(from);
        const bool first = server
            ? server->getRoom()->countHistoryCards(server, "phase", QString(), false, true) == 0
            : from->getMark("heg_wanglie_first") > 0;
        return first ? 1000 : 0;
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

class HYinbingX : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYinbingX() : TriggerSkillV2("heg_yinbingx")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && player->getPhase() == Player::Finish && !player->isNude())
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QList<int> ints = room->askForExchangeCards(player, objectName(), 998, 0, "@yinbing-put", QString(), "^BasicCard");
        if (!ints.isEmpty()) {
            player->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());
            LogMessage log;
            log.from = player;
            log.type = "#InvokeSkill";
            log.arg = objectName();
            room->sendLog(log);
            player->addToPile("kerchief", ints, true);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return false;
    }
};

class HYinbingXCompulsory : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYinbingXCompulsory() : TriggerSkillV2("#heg_yinbingx-compulsory")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player == NULL || player->isDead() || !player->hasShownSkill("heg_yinbingx")
                || player->getPile("kerchief").isEmpty()) return {};
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel")))
            return TriggerList{{player, {objectName()}}};

        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill("heg_yinbingx")) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, "heg_yinbingx");
        } else {

            invoke = player->askForSkillInvoke("heg_yinbingx", data);
        }

        if (invoke) {
            room->broadcastSkillInvoke("heg_yinbingx", player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        Room *room = player->getRoom();
        QList<int> ids = player->getPile("kerchief");
        room->fillAG(ids, player);
        int id = room->askForAG(player, ids, false, "heg_yinbingx");
        room->clearAG(player);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), player->objectName(), "heg_yinbingx", QString());
        room->throwCard(Sanguosha->getCard(id), reason, NULL);
        return false;
    }
};

class HJuedi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJuedi() : TriggerSkillV2("heg_juedi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && player->getPhase() == Player::Start
                && !player->getPile("kerchief").isEmpty())
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        bool invoke = false;
        if (player->hasShownSkill(objectName())) {
            invoke = true;
            room->sendCompulsoryTriggerLog(player, objectName());
        } else {

            invoke = player->askForSkillInvoke(this, data);
        }

        if (invoke) {
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.owner;
        Room *room = target->getRoom();
        QStringList choices;
        choices << "self";
        QList<ServerPlayer *> playerlist;
        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
            if (p->getHp() <= target->getHp())
                playerlist << p;
        }
        if (!playerlist.isEmpty())
            choices << "give";
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant(), "self+give", QString()) == "give") {
            ServerPlayer *to_give = room->askForPlayerChosen(target, playerlist, objectName(), "@juedi");
            int len = target->getPile("kerchief").length();
            DummyCard *dummy = new DummyCard(target->getPile("kerchief"));
            dummy->deleteLater();
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), to_give->objectName(), objectName(), QString());
            room->obtainCard(to_give, dummy, reason);
            RecoverStruct recover;
            recover.who = target;
            room->recover(to_give, recover);
            room->drawCards(to_give, len, objectName());
        } else {
            target->clearOnePrivatePile("kerchief");
            target->fillHandCards(target->getMaxHp(), objectName());
        }
        return false;
    }
};

class HMoukui : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HMoukui() : TriggerSkillV2("heg_moukui")
    {
        events << TargetSpecified;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
        QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !use.card || !use.card->isKindOf("Slash")) return true;
        // Keep each target tied to the exact grant through target interception.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (int i = 0; i < use.to.size(); ++i) {
                ServerPlayer *target = use.to.at(i);
                if (!target) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + target->objectName() + '&' + QString::number(i + 1);
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = ctx.activationRef;
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                ctx.targets << target;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                contexts << ctx;
            }
        }
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *skill_target = ctx.preferredTarget;
        ServerPlayer *player = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, QVariant::fromValue(skill_target))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), skill_target->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *effectTarget) const override
    {
        ServerPlayer *target = effectTarget;
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        QStringList choices;
        choices << "draw";
        if (player->canDiscard(target, "he"))
            choices << "discard";
        QString choice = room->askForChoice(player, objectName(), choices.join("+"),
                QVariant::fromValue(target), "draw+discard", "@moukui-choose::" + target->objectName());
        if (choice == "draw")
            player->drawCards(1, objectName());
        else if (choice == "discard") {
            room->setTag("MoukuiDiscard", data);
            int disc = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
            room->removeTag("MoukuiDiscard");
            room->throwCard(disc, target, player);
        }

        CardUseStruct use = data.value<CardUseStruct>();

        QStringList moukuiRecord = use.card->getTag("moukuiRecord").toStringList();
        moukuiRecord << player->objectName() + ":" + target->objectName();
        use.card->setTag("moukuiRecord", moukuiRecord);

        return false;
    }

};

class HMoukuiEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HMoukuiEffect() : TriggerSkillV2("#heg_moukui-effect")
    {
        events << CardOffset;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card && effect.card->isKindOf("Slash") && effect.to && effect.to->isAlive()) {
            TriggerList skill_list;
            QStringList moukuiRecord = effect.card->getTag("moukuiRecord").toStringList();
            foreach (QString record, moukuiRecord) {
                QStringList names = record.split(":");
                if (names.length() == 2 && names.last() == effect.to->objectName()) {
                    ServerPlayer *fuwan = room->findPlayerByObjectName(names.first());
                    if (fuwan && fuwan->isAlive() && effect.to->canDiscard(fuwan, "he")) {
                        skill_list.insert(fuwan, QStringList(objectName()));
                    }
                }
            }
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        CardEffectStruct effect = data.value<CardEffectStruct>();
        LogMessage log;
        log.type = "#MoukuiDiscard";
        log.from = player;
        log.to << effect.to;
        log.arg = "heg_moukui";
        room->sendLog(log);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), effect.to->objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.to->canDiscard(player, "he")) {
            int disc = room->askForCardChosen(effect.to, player, "he", "heg_moukui", false, Card::MethodDiscard);
            room->throwCard(disc, player, effect.to);
        }
        return false;
    }

};

class HZhenxiTrick : public ViewAsSkillV2
{
public:
    HZhenxiTrick() : ViewAsSkillV2("heg_zhenxi_trick", 1)
    {
        response_or_use = true;
        response_pattern = "@@heg_zhenxi_trick";
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@heg_zhenxi_trick"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
        return request.selectedCardIds.isEmpty() && !candidate->hasFlag("using")
            && Sanguosha->matchExpPattern("BasicCard,EquipCard|diamond,club", request.initiator, candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 1) return false;
        // Replay selection order so forged requests obey the same card restrictions.
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
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Card *card = originalCard->getSuit() == Card::Diamond
            ? static_cast<Card *>(new Indulgence(originalCard->getSuit(), originalCard->getNumber()))
            : static_cast<Card *>(new SupplyShortage(originalCard->getSuit(), originalCard->getNumber()));
        card->addSubcard(originalCard->getId());
        card->setSkillName("_heg_zhenxi");
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return QString();
        return Sanguosha->getCard(request.selectedCardIds.first())->getSuit() == Card::Diamond
            ? "Indulgence" : "SupplyShortage";
    }
};

class HZhenxi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HZhenxi() : TriggerSkillV2("heg_zhenxi")
    {
        events << TargetSpecified;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player,
        QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !use.card || !use.card->isKindOf("Slash")) return true;
        // Keep each target tied to the exact grant through target interception.
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (int i = 0; i < use.to.size(); ++i) {
                ServerPlayer *target = use.to.at(i);
                if (!target) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + target->objectName() + '&' + QString::number(i + 1);
                ctx.owner = player;
                ctx.invoker = player;
                ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = ctx.activationRef;
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                ctx.targets << target;
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                contexts << ctx;
            }
        }
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *skill_target = ctx.preferredTarget;
        ServerPlayer *player = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this, QVariant::fromValue(skill_target))) {
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), skill_target->objectName());
            room->broadcastSkillInvoke(objectName(), player);
            return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *effectTarget) const override
    {
        ServerPlayer *target = effectTarget;
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        QStringList choices;
        choices << "usecard";
        if (player->canDiscard(target, "he"))
            choices << "discard";
        QString choice = room->askForChoice(player, objectName(), choices.join("+"),
                QVariant::fromValue(target), "usecard+discard", "@zhenxi-choose::" + target->objectName());
        if (choice == "usecard") {
            room->setPlayerProperty(player, "zhenxi_target", target->objectName());
            if (room->askForUseCard(player, "@@heg_zhenxi_trick", "@zhenxi-trick::" + target->objectName())) {
                if (player->hasShownAllGenerals() && !target->hasShownAllGenerals() && player->canDiscard(target, "he") &&
                        room->askForChoice(player, "zhenxi_discard", "yes+no", data, QString(), "@zhenxi-discard::" + target->objectName()) == "yes") {
                    room->setTag("ZhenxiDiscard", data);
                    int disc = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
                    room->removeTag("ZhenxiDiscard");
                    room->throwCard(disc, target, player);
                }
            }
            room->setPlayerProperty(player, "zhenxi_target", QVariant());
        } else if (choice == "discard") {
            room->setTag("ZhenxiDiscard", data);
            int disc = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
            room->removeTag("ZhenxiDiscard");
            room->throwCard(disc, target, player);
            if (player->hasShownAllGenerals() && !target->hasShownAllGenerals()) {
                room->setPlayerProperty(player, "zhenxi_target", target->objectName());
                room->askForUseCard(player, "@@heg_zhenxi_trick", "@zhenxi-trick::" + target->objectName());
                room->setPlayerProperty(player, "zhenxi_target", QVariant());
            }
        }
        return false;
    }

    virtual int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        return (card->isKindOf("Indulgence") || card->isKindOf("SupplyShortage")) ? -2 : -1;
    }
};

class HZhenxiProhibit : public ProhibitSkill
{
public:
    HZhenxiProhibit() : ProhibitSkill("#heg_zhenxi-prohibit")
    {
    }

    virtual bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (from && to && card->getSkillName(true) == "heg_zhenxi")
            return from->property("zhenxi_target").toString() != to->objectName();
        return false;
    }
};

class HZhenxiTargetMod : public TargetModSkillV2
{
public:
    HZhenxiTargetMod() : TargetModSkillV2("#heg_zhenxi-target")
    {
        pattern = "^SkillCard";
    }

    virtual int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (!Sanguosha->matchExpPattern(pattern, from, card))
            return 0;

        if (card->getSkillName(true) == "heg_zhenxi")
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

HJiansuCard::HJiansuCard()
{
    will_throw = true;
}

bool HJiansuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->canRecover() && to_select->getHp() <= subcardsLength();
}

void HJiansuCard::onEffect(CardEffectStruct &effect) const
{
    QVariantList effect_list = effect.from->getTag("jianshuTag").toList();
    effect_list << QVariant::fromValue(effect);
    effect.from->setTag("jianshuTag", effect_list);
}

class HJiansuViewAsSkill : public ViewAsSkillV2
{
public:
    HJiansuViewAsSkill() : ViewAsSkillV2("heg_jiansu")
    { response_pattern = "@@heg_jiansu"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@heg_jiansu"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
        QList<int> selected;
        for (int id : request.selectedCardIds) {
            if (id < 0 || selected.contains(id) || !Sanguosha->getCard(id)) return false;
            selected.append(id);
        }
        return !request.initiator->isJilei(candidate)
            && jiansuMoney(request.initiator).contains(candidate->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.isEmpty()) return false;
        // Replay selection order so forged requests obey the same card restrictions.
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
        HJiansuCard *card = new HJiansuCard;
        card->addSubcards(request.selectedCardIds);
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HJiansuCard"; }
};

class HJiansu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJiansu() : TriggerSkillV2("heg_jiansu")
    {
        events << CardsMoveBatch << PreCardsMoveBatch << EventPhaseStart;
        relate_to_place = "deputy";
        view_as_skill = new HJiansuViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == PreCardsMoveBatch) {
            QList<int> mark = jiansuMoney(player);
            QList<int> mark_copy = mark;
            foreach (int id, mark) {
                if (room->getCardOwner(id) != player || room->getCardPlace(id) != Player::PlaceHand)
                    mark_copy.removeOne(id);
            }
            setJiansuMoney(room, player, mark_copy);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        if (triggerEvent == CardsMoveBatch && player->getPhase() == Player::NotActive) {
            QVariantList move_datas = data.toList();
            foreach(QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.to == player && move.to_place == Player::PlaceHand) {
                    for (int i = 0; i < move.card_ids.length(); ++i) {
                        int id = move.card_ids.at(i);
                        if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand) {
                            return TriggerList{{player, {objectName()}}};
                        }
                    }
                }
            }
        } else if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play && !jiansuMoney(player).isEmpty()) {
            return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (triggerEvent == CardsMoveBatch) {
            if (player->askForSkillInvoke(this, data)) {
                room->broadcastSkillInvoke(objectName(), player);
                return true;
            }
        } else if (triggerEvent == EventPhaseStart) {
            return room->askForUseCard(player, "@@heg_jiansu", "@jiansu-card", -1, Card::MethodDiscard);
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        if (triggerEvent == CardsMoveBatch) {
            QVariantList move_datas = data.toList();
            QList<int> card_ids;
            foreach(QVariant move_data, move_datas) {
                CardsMoveOneTimeStruct move = move_data.value<CardsMoveOneTimeStruct>();
                if (move.to == player && move.to_place == Player::PlaceHand) {
                    for (int i = 0; i < move.card_ids.length(); ++i) {
                        int id = move.card_ids.at(i);
                        if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand) {
                            card_ids << id;
                        }
                    }
                }
            }
            room->showCard(player, card_ids);
            QList<int> mark = jiansuMoney(player);
            mark << card_ids;
            setJiansuMoney(room, player, mark);
        } else if (triggerEvent == EventPhaseStart) {
            QVariantList data_list = player->getTag("jianshuTag").toList();
            if (data_list.isEmpty()) return false;
            QVariant jianshu_data = data_list.takeLast();
            player->setTag("jianshuTag", data_list);
            CardEffectStruct effect = jianshu_data.value<CardEffectStruct>();
            ServerPlayer *target = effect.to;
            if (target->isAlive()) {
                RecoverStruct recover;
                recover.who = effect.from;
                room->recover(target, recover);
            }
        }
        return false;
    }
};

class HYaowu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYaowu() : TriggerSkillV2("heg_yaowu")
    {
        events << Damage;
        frequency = Limited;
        limit_mark = "@showoff";
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && !player->hasShownSkill(objectName()) && player->getMark(limit_mark) > 0)
            return TriggerList{{player, {objectName()}}};

        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doSuperLightbox("heg_huaxiong", objectName());
            room->removePlayerMark(player, limit_mark);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        room->setPlayerProperty(player, "maxhp", player->getMaxHp() + 2);

        LogMessage log;
        log.type = "#GainMaxHp";
        log.from = player;
        log.arg = QString::number(2);
        room->sendLog(log);

        RecoverStruct recover;
        recover.recover = 2;
        recover.who = player;
        room->recover(player, recover);

        room->addPlayerMark(player, "##yaowu");

        return false;
    }
};

class HYaowuDeath : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent event) const override { return event == BuryVictim ? -2 : 3; }
    bool usesEventPriority() const override { return true; }
    HYaowuDeath() : TriggerSkillV2("#heg_yaowu-death")
    {
        events << Death << BuryVictim;
        frequency = Compulsory;
        global = true;
        // GameRule buries at -1; the donor consequence happens afterwards.
    }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        // Death still has the transformation mark; burial clears it before -2.
        if (event != Death) return true;
        const DeathStruct death = data.value<DeathStruct>();
        if (death.who)
            death.who->setTag("heg_yaowu_death", death.who->getMark("##yaowu") > 0);
        return true;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *target,
        QVariant &data, QList<SkillContext> &contexts) const override
    {
        // The death consequence survives burial and does not borrow an ally's skill.
        if (event != BuryVictim) return true;
        const DeathStruct death = data.value<DeathStruct>();
        if (!death.who || !death.who->isDead() || !death.who->getTag("heg_yaowu_death").toBool()) return true;
        for (ServerPlayer *ally : room->getAlivePlayers()) {
            if (!ally->isFriendWith(death.who)) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ally;
            ctx.invoker = target;
            ctx.initiator = target;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.amount = getBaseAmount();
            contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        return death.who && death.who->isDead() && death.who->getTag("heg_yaowu_death").toBool()
            && ctx.owner->isFriendWith(death.who);
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *askWho = ctx.owner;
        if (askWho && askWho->isAlive()) room->loseHp(askWho);
        return false;
    }
};

class HShiyong : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HShiyong() : TriggerSkillV2("heg_shiyong")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card) {
            if (player->getMark("##yaowu") > 0) {
                if (!damage.card->isBlack() && damage.from && damage.from->isAlive())
                    return TriggerList{{player, {objectName()}}};
            } else if (!damage.card->isRed())
                return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        if (player->getMark("##yaowu") > 0) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from->isAlive())
                damage.from->drawCards(1, objectName());
        } else
            player->drawCards(1, objectName());
        return false;
    }
};

class HZhuidu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HZhuidu() : TriggerSkillV2("heg_zhuidu")
    {
        events << DamageCaused << EventPhaseChanging;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play) {
            room->setPlayerFlag(player, "-zhuiduUsed");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == DamageCaused && (player && player->isAlive() && player->hasSkill(objectName()))
                && player->getPhase() == Player::Play && !player->hasFlag("zhuiduUsed")) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to && damage.to->isAlive())
                return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        DamageStruct damage = data.value<DamageStruct>();
        bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(damage.to));
        if (invoke) {
            room->setPlayerFlag(player, "zhuiduUsed");
            room->broadcastSkillInvoke(objectName(), player);

            return true;
        }

        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target = damage.to;

        if (target->isDead()) return false;
        bool adddamage = false, throwallequips = false;


        if (target->isFemale() && room->askForDiscard(player, "zhuidu_discard", 1, 1, true, true, "@zhuidu-both::" + target->objectName())) {
            adddamage = true;
            throwallequips = true;
        } else {
            if (!target->getEquips().isEmpty()
                && room->askForChoice(target, "zhuidu_choice", "throw+damage", data) == "throw")
                throwallequips = true;
            else
                adddamage = true;
        }

        if (throwallequips)
            target->throwAllEquips();

        if (adddamage) {
            damage.damage++;
            data = QVariant::fromValue(damage);
        }

        return false;
    }
};

class HShigong : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HShigong() : TriggerSkillV2("heg_shigong")
    {
        events << Dying;
        frequency = Limited;
        limit_mark = "@handover";
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &data) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName())) && player->getPhase() == Player::NotActive) {
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who == player && player->getHp() < 1 && player->getGeneral2()
                    && !player->getActualGeneral2Name().contains("sujiang"))
                return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (player->askForSkillInvoke(this)) {
            room->broadcastSkillInvoke(objectName(), player);
            room->doSuperLightbox("heg_liufuren", objectName());
            room->setPlayerMark(player, limit_mark, 0);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        QString general_name = player->getActualGeneral2Name();
        player->removeGeneral(false);
        ServerPlayer *current = room->getCurrent();
        if (current && current->isAlive()) {
            int x = 1;
            QStringList skill_names;
            QList<const Skill *> skills = Sanguosha->getGeneral(general_name)->getVisibleSkillList();
            foreach (const Skill *skill, skills) {
                if (isNormalSkill(skill))
                    skill_names << skill->objectName();

            }

            if (!skill_names.isEmpty()) {
                skill_names << "cancel";

                QString skill_name = room->askForChoice(current, "shigong_skill", skill_names.join("+"), data, QString(), "@shigong-choose:::"+general_name);

                if (skill_name != "cancel") {
                    room->acquireSkillForSlot(current, skill_name, false, true, false);
                    x = player->getMaxHp();
                }

            }

            if (player->isAlive()) {
                RecoverStruct recover;
                recover.recover = x - player->getHp();
                room->recover(player, recover);
            }
        }

        return false;
    }

private:
    static bool isNormalSkill(const Skill *skill)
    {
        if (skill->isAttachedLordSkill() || skill->isLordSkill()) return false;
        if (skill->relateToPlace(true) || skill->relateToPlace(false)
            || dynamic_cast<const HArraySummon *>(ViewAsSkill::parseViewAsSkill(skill))) return false;
        return (skill->getFrequency() == Skill::Frequent || skill->getFrequency() == Skill::NotFrequent);
    }
};

class HTanfeng : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HTanfeng() : TriggerSkillV2("heg_tanfeng")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        if (player->getPhase() == Player::Start) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (!player->willBeFriendWith(p) && player->canDiscard(p, "hej")) {
                    return TriggerList{{player, {objectName()}}};
                }
            }
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QList<ServerPlayer *> to_choose, all_players = room->getAlivePlayers();
        foreach (ServerPlayer *p, all_players) {
            if (!player->willBeFriendWith(p) && player->canDiscard(p, "hej"))
                to_choose << p;
        }
        if (to_choose.isEmpty()) return false;

        ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "@tanfeng-target", true, true);
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);
            QStringList target_list = player->getTag("tanfeng_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("tanfeng_target", target_list);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        Room *room = player->getRoom();
        QStringList target_list = player->getTag("tanfeng_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("tanfeng_target", target_list);
        ServerPlayer *to = room->findPlayerByObjectName(target_name);

        if (to && player->canDiscard(to, "hej")) {
            room->throwCard(room->askForCardChosen(player, to, "hej", objectName(), false, Card::MethodDiscard), to, player);

            if (player->isAlive() && to->isAlive()) {

                QStringList phase_strings;
                phase_strings << "judge" << "draw" << "play" << "discard" << "finish" << "cancel";

                QString choice = room->askForChoice(to, objectName(), phase_strings.join("+"),
                                                    QVariant(), QString(), "@tanfeng-choose:" + player->objectName());

                if (choice != "cancel") {
                    room->damage(DamageStruct(objectName(), player, to, 1, DamageStruct::Fire));
                    player->skip((Player::Phase)(phase_strings.indexOf(choice)+2));
                }
            }
        }
        return false;
    }
};

class HJinwu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJinwu() : TriggerSkillV2("heg_jinwu")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        if (player->getPhase() == Player::Play) return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        Room *room = player->getRoom();
        if (player->askCommandto(objectName(), player) && player->isAlive()) {

            QList<ServerPlayer *> targets, allplayers = room->getAlivePlayers();
            foreach (ServerPlayer *p, allplayers) {
                if (player->canSlash(p))
                    targets << p;
            }
            if (!targets.isEmpty()) {
                ServerPlayer *target = room->askForPlayerChosen(player, targets, "jinwu-slash", "@jinwu-slash");
                if (target) {
                    Slash *slash = new Slash(Card::NoSuit, 0);
                    slash->setSkillName("_heg_jinwu");
                    CardUseStruct slashUse(slash, player, target);
                    room->useCard(slashUse, false);
                }
            }
        }
        else
            room->setPlayerFlag(player, "Global_PlayPhaseTerminated");
        return false;
    }

    virtual int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return 0;
    }
};


class HZhuke : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HZhuke() : TriggerSkillV2("heg_zhuke")
    {
        events << CommandVerifying << TurnedOver << ChainStateChanged;
        relate_to_place = "head";
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if ((player && player->isAlive() && player->hasSkill(objectName()))) {
            if ((triggerEvent == TurnedOver && !player->faceUp()) || (triggerEvent == ChainStateChanged && player->isChained())) {
                return TriggerList{{player, {objectName()}}};
            } else if (triggerEvent == CommandVerifying) {
                return TriggerList{{player, {objectName()}}};
            }
        }
        return {};
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (triggerEvent == CommandVerifying) {

            player->setTag("ZhukeCommanddata", data);
            bool invoke = player->askForSkillInvoke(this);
            player->removeTag("ZhukeCommanddata");

            if (invoke) {
                room->broadcastSkillInvoke(objectName(), player);
                return true;
            }
        } else if (triggerEvent == TurnedOver || triggerEvent == ChainStateChanged) {
            QList<ServerPlayer *> to_choose, all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (player->isFriendWith(p) && p->canRecover())
                    to_choose << p;
            }
            if (to_choose.isEmpty()) return false;

            ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "zhuke-invoke", true, true);
            if (to != NULL) {
                room->broadcastSkillInvoke(objectName(), player);

                QStringList target_list = player->getTag("zhuke_target").toStringList();
                target_list.append(to->objectName());
                player->setTag("zhuke_target", target_list);
                return true;
            }
        }
        return false;
    }


    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        QVariant &data = *ctx.original_data;
        if (triggerEvent == CommandVerifying) {
            QStringList list = data.toString().split(":");

            source->setTag("ZhukeCommanddata", data);
            int index = source->startCommand(objectName());
            source->removeTag("ZhukeCommanddata");

            QStringList allcommands;
            allcommands << "command1" << "command2" << "command3" << "command4" << "command5" << "command6";
            list[1] = allcommands.at(index);
            data = list.join(":");

        } else if (triggerEvent == TurnedOver || triggerEvent == ChainStateChanged) {
            QStringList target_list = source->getTag("zhuke_target").toStringList();
            QString target_name = target_list.takeLast();
            source->setTag("zhuke_target", target_list);

            ServerPlayer *to = room->findPlayerByObjectName(target_name);

            if (to) {
                RecoverStruct rec;
                rec.who = source;
                room->recover(to, rec);
            }
        }

        return false;
    }
};

class HQuanjia : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HQuanjia() : TriggerSkillV2("heg_quanjia")
    {
        relate_to_place = "deputy";
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *, QVariant &) const override
    {
        return {};
    }
};

class HQuanjiaCompulsory : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HQuanjiaCompulsory() : TriggerSkillV2("#heg_quanjia-compulsory")
    {
        events << GeneralShowed;
        frequency = Compulsory;
        relate_to_place = "deputy";
    }

    TriggerList triggerable(TriggerEvent , Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player->cheakSkillLocation("heg_quanjia", data) && player->getMark("quanjiaUsed") == 0)
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        room->sendCompulsoryTriggerLog(player, "heg_quanjia");
        room->broadcastSkillInvoke("heg_quanjia", player);
        room->addPlayerMark(player, "quanjiaUsed");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        QList<ServerPlayer *> alls = room->getAlivePlayers();
        room->sortByActionOrder(alls);
        foreach(ServerPlayer *anjiang, alls) {
            if (source->getRole().startsWith("careerist")) break;
            if (anjiang->hasShownOneGeneral()) continue;

            QString kingdom = source->getKingdom();

            bool can_show = false, can_only_dupty = false;

            if (anjiang->getKingdom() == kingdom) {
                if (anjiang->getActualGeneral1()->getKingdom() != "careerist")
                    can_show = true;
                can_only_dupty = true;
            }

            room->setTag("GlobalQuanjiaShow", true);
            QStringList showChoices;
            if (can_show && anjiang->disableShow(true).isEmpty() && !anjiang->hasShownGeneral1())
                showChoices << "show_head_general";
            if (can_only_dupty && anjiang->disableShow(false).isEmpty() && !anjiang->hasShownGeneral2())
                showChoices << "show_deputy_general";
            if (can_show && can_only_dupty && anjiang->disableShow(true).isEmpty()
                && anjiang->disableShow(false).isEmpty())
                showChoices << "show_both_generals";
            showChoices << "cancel";
            const QString showChoice = room->askForChoice(anjiang, "heg_quanjia",
                showChoices.join("+"), QVariant(), QString(), "@generalshow-choose");
            // Equivalent to donor askForGeneralShow(head, deputy, all, refusable, change=false):
            // suppress per-slot logs, then emit one combined reveal log.
            if (showChoices.contains(showChoice) && showChoice != "cancel") {
                if (showChoice == "show_head_general" || showChoice == "show_both_generals")
                    anjiang->showGeneral(true, true, false);
                if (showChoice == "show_deputy_general" || showChoice == "show_both_generals")
                    anjiang->showGeneral(false, true, false);
                LogMessage log;
                log.type = "#BasaraReveal";
                log.from = anjiang;
                log.arg = anjiang->getGeneralName();
                log.arg2 = anjiang->getGeneral2Name();
                room->sendLog(log);
            }
            room->setTag("GlobalQuanjiaShow", false);
        }
        QList<ServerPlayer *> to_draw, allplayers = room->getAlivePlayers();
        foreach (ServerPlayer *p, allplayers) {
            if (p->isFriendWith(source))
                to_draw << p;
        }
        room->sortByActionOrder(to_draw);
        foreach (ServerPlayer *p, to_draw) {
            if (p->isAlive())
                p->drawCards(1, "heg_quanjia");
        }
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            bool head = (p->hasShownGeneral1() && p->getGeneral()->hasSkill("rende"));
            if (head || (p->hasShownGeneral2() && p->getGeneral2()->hasSkill("rende"))) {
                room->addPlayerMark(p, "##quanjia");
                room->acquireSkillForSlot(p, "heg_zhangwu", head, true, false);
                room->acquireSkillForSlot(p, "heg_shouyue", head, true, false);
                room->sendCompulsoryTriggerLog(p, "heg_shouyue");
                room->broadcastSkillInvoke("heg_shouyue", p);
            }
        }
        return false;
    }
};

class HTunchu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HTunchu() : TriggerSkillV2("heg_tunchu")
    {
        events << DrawNCards;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!data.canConvert<DrawStruct>() || data.value<DrawStruct>().reason != QLatin1String("draw_phase")) return {};
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        return TriggerList{{player, {objectName()}}};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        Room *room = player->getRoom();
        room->setPlayerCardLimitation(player, "use", "Slash", true);
        room->addPlayerMark(player, "##tunchu");
        draw.num += 2;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class HTunchuEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HTunchuEffect() : TriggerSkillV2("#heg_tunchu-effect")
    {
        events << EventPhaseEnd << EventPhaseStart;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::NotActive)
            room->setPlayerMark(player, "##tunchu", 0);
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseEnd && player->getPhase() == Player::Draw
                && player->getMark("##tunchu") > 0 && !player->isKongcheng())
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (player->isAlive() && !player->isKongcheng()) {
            QList<int> ints = room->askForExchangeCards(player, "tunchu_push", 2, 1, "@tunchu-push", "", ".|.|.|hand");
            player->addToPile("heg_food", ints);
        }
        return false;
    }
};

class HShuliang : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HShuliang() : TriggerSkillV2("heg_shuliang")
    {
        events << EventPhaseStart;
    }

    virtual TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        TriggerList skill_list;
        if (player == NULL || player->isDead() || player->getPhase() != Player::Finish) return skill_list;
        QList<ServerPlayer *> owners = room->findPlayersBySkillName(objectName());
        foreach (ServerPlayer *ask_who, owners) {
            if (!ask_who->getPile("heg_food").isEmpty() && ask_who->isFriendWith(player) && ask_who != player
                    && !ask_who->distanceTo(player) != -1 && ask_who->distanceTo(player) <= ask_who->getPile("heg_food").length())
                skill_list.insert(ask_who, QStringList(objectName()));
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        QList<int> ints = room->askForExchangeCards(ask_who, objectName(), 1, 0, "@shuliang:" + player->objectName(), "heg_food");
        if (!ints.isEmpty()) {
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = ask_who;
            log.arg = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(ask_who, objectName());
            room->broadcastSkillInvoke(objectName(), ask_who);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), player->objectName());
            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), ask_who->objectName(), objectName(), QString());
            room->throwCard(Sanguosha->getCard(ints.first()), reason, NULL);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        player->drawCards(2, objectName());
        return false;
    }
};

class HDujin : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDujin() : TriggerSkillV2("heg_dujin")
    {
        events << DrawNCards;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!data.canConvert<DrawStruct>() || data.value<DrawStruct>().reason != QLatin1String("draw_phase")) return {};
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || !player->hasEquip()) return {};
        return TriggerList{{player, {objectName()}}};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += player->getEquips().length()/2 + player->getEquips().length()%2;
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class HDujinCompulsory : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDujinCompulsory() : TriggerSkillV2("#heg_dujin-compulsory")
    {
        events << GeneralShowed;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player->cheakSkillLocation("heg_dujin", data) && player->getMark("dujinUsed") == 0) {
            QList<ServerPlayer *> all_players = room->getAllPlayers(true);

            foreach (ServerPlayer *p, all_players) {
                if (p != player && p->isFriendWith(player))
                    return {};
            }

            return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        room->sendCompulsoryTriggerLog(player, "heg_dujin");
        room->broadcastSkillInvoke("heg_dujin", player);
        room->addPlayerMark(player, "dujinUsed");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        room->addPlayerMark(player, "@firstshow");
        return false;
    }
};

class HKenshang : public ViewAsSkillV2
{
public:
    HKenshang() : ViewAsSkillV2("heg_kenshang") { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator
            && ((request.reason == CardUseStruct::CARD_USE_REASON_PLAY
                    && Slash::IsAvailable(request.initiator))
                || (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                    && request.pattern == "slash"));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
        QList<int> selected;
        for (int id : request.selectedCardIds) {
            if (id < 0 || selected.contains(id) || !Sanguosha->getCard(id)) return false;
            selected.append(id);
        }
        // The candidate and already selected materials affect Slash legality;
        // keep this query local and side-effect free.
        Slash slash(Card::SuitToBeDecided, -1);
        slash.addSubcards(request.selectedCardIds);
        slash.addSubcard(candidate);
        return slash.isAvailable(request.initiator);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.isEmpty()) return false;
        // Replay selection order so forged requests obey the same card restrictions.
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
        Card *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcards(request.selectedCardIds);
        slash->setSkillName(objectName());
        slash->setShowSkill(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class HKenshangEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HKenshangEffect() : TriggerSkillV2("#heg_kenshang-effect")
    {
        events << TargetSpecifying << CardFinished;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card != NULL && use.card->isKindOf("Slash") && use.card->getSkillName() == "heg_kenshang") {
            if (triggerEvent == TargetSpecifying) {
                QList<ServerPlayer *> targets = room->getUseExtraTargets(use, false);
                foreach (ServerPlayer *p, targets) {
                    if (!player->inMyAttackRange(p))
                        return TriggerList{{player, {objectName()}}};
                }

                bool cheak1 = false, cheak2 = false;
                foreach (ServerPlayer *p, use.to) {
                    if (player->inMyAttackRange(p))
                        cheak1 = true;
                    else
                        cheak2 = true;
                }
                if (cheak1 && cheak2)
                    return TriggerList{{player, {objectName()}}};

            } else if (triggerEvent == CardFinished)
                return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        room->sendCompulsoryTriggerLog(player, "heg_kenshang");
        return true;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == TargetSpecifying && player->askForSkillInvoke("_heg_kenshang", "prompt")) {
            QList<ServerPlayer *> targets = room->getUseExtraTargets(use, false);
            foreach (ServerPlayer *p, targets) {
                if (!player->inMyAttackRange(p))
                    use.to.append(p);
            }
            QList<ServerPlayer *> all_players = room->getAlivePlayers();
            foreach (ServerPlayer *p, all_players) {
                if (use.to.contains(p) && player->inMyAttackRange(p))
                    room->cancelTarget(use, p);
            }
            LogMessage log;
            log.type = "#KenshangTarget";
            log.from = player;
            log.to = use.to;
            room->sendLog(log);
            room->sortByActionOrder(use.to);
            foreach (ServerPlayer *p, use.to)
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
            data = QVariant::fromValue(use);

        } if (triggerEvent == CardFinished) {
            const QVariantMap history = room->queryCardUseDamage();
            if (!history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return false;
            int damage_point = 0;
            for (const QVariant &entry : history.value("items").toList())
                damage_point += entry.toMap().value("data").toMap().value("amount").toInt();
            if (damage_point < use.card->subcardsLength())
                player->drawCards(damage_point, "heg_kenshang");
            else {
                QStringList limited_skills;
                QList<const Skill *> skills = player->getVisibleSkillList();
                foreach (const Skill *skill, skills) {
                    if ((skill->objectName() == "heg_kenshang" || skill->objectName().startsWith("mashu"))) {
                        if (player->inHeadSkills(skill->objectName()) && !player->hasShownGeneral1())
                            continue;
                        if (player->inDeputySkills(skill->objectName()) && !player->hasShownGeneral2())
                            continue;
                        limited_skills.append(skill->objectName());
                    }
                }
                if (!limited_skills.isEmpty()) {
                    QString skill_name = room->askForChoice(player, "heg_kenshang", limited_skills.join("+"), QVariant(), QString(), "@kenshang-choose");
                    room->detachSkillForSlot(player, skill_name, player->inHeadSkills(skill_name));
                }
            }
        }
        return false;
    }
};

class HQizhi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HQizhi() : TriggerSkillV2("heg_qizhi")
    {
        events << TargetSpecified << EventPhaseStart;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::NotActive)
            for (ServerPlayer *p : room->getAllPlayers(true)) room->setPlayerMark(p, "#qizhi-turn", 0);
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified || !(player && player->isAlive() && player->hasSkill(objectName())) || player->getPhase() == Player::NotActive || player->getMark("#qizhi-turn") > 2)
            return {};
        CardUseStruct use = data.value<CardUseStruct>();
        if ((use.card->getTypeId() == Card::TypeBasic || use.card->getTypeId() == Card::TypeTrick)) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (!use.to.contains(p) and (!p->isKongcheng() or (!p->isNude() && p->isFriendWith(player))))
                    return TriggerList{{player, {objectName()}}};
            }
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        CardUseStruct use = data.value<CardUseStruct>();
        QList<ServerPlayer *> to_choose;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (!use.to.contains(p) and (!p->isKongcheng() or (!p->isNude() && p->isFriendWith(player))))
                to_choose << p;
        }
        if (to_choose.isEmpty()) return false;
        ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "qizhi-invoke", true, true);
        if (to != NULL) {
            room->broadcastSkillInvoke(objectName(), player);
            room->addPlayerMark(player, "#qizhi-turn");
            QStringList target_list = player->getTag("qizhi_target").toStringList();
            target_list.append(to->objectName());
            player->setTag("qizhi_target", target_list);
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QStringList target_list = player->getTag("qizhi_target").toStringList();
        QString target_name = target_list.takeLast();
        player->setTag("qizhi_target", target_list);
        ServerPlayer *to = room->findPlayerByObjectName(target_name);
        if (to == NULL) return false;
        QString flag = player->isFriendWith(to) ? "he" : "h";

        if (to && player->canDiscard(to, flag)) {
            int to_throw = room->askForCardChosen(player, to, flag, objectName(), false, Card::MethodDiscard);
            room->throwCard(to_throw, to, player);
            if (to->isAlive())
                to->drawCards(1, objectName());
        }
        return false;
    }
};

class HJinqu : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJinqu() : TriggerSkillV2("heg_jinqu")
    {
        events << EventPhaseStart;

    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        if (player->getPhase() == Player::Finish) return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        player->drawCards(2, objectName());
        if (player->isDead()) return false;
        int x = player->getHandcardNum() - player->getMark("#qizhi-turn");
        if (x > 0)
            player->getRoom()->askForDiscard(player, objectName(), x, x);
        return false;
    }
};

class HJuzhan : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HJuzhan() : TriggerSkillV2("heg_juzhan")
    {
        events << TargetSpecified << TargetConfirmed << EventPhaseStart << GeneralShown << EventLoseSkill;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() ==  Player::NotActive) {
            QList<ServerPlayer *> alls = room->getAlivePlayers();
            foreach (ServerPlayer *p, alls) {
                room->removePlayerDisableShow(p, objectName());
            }
        } else if (triggerEvent == GeneralShown && player->hasShownSkill(objectName())) {
            if (player->getMark("juzhan_usedtimes")%2 == 0)
                setMolState(room, player, "heg_juzhan_state", { "SwitchYang" }, "&heg_juzhan");
            else
                setMolState(room, player, "heg_juzhan_state", { "SwitchYin" }, "&heg_juzhan");
        } else if (triggerEvent == EventLoseSkill && data.value<SkillChangeStruct>().skillName == objectName()) {
            setMolState(room, player, "heg_juzhan_state", {}, "&heg_juzhan");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName()))) return {};
        if (triggerEvent == TargetConfirmed && player->getMark("juzhan_usedtimes")%2 == 0) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && use.from->isAlive())
                return TriggerList{{player, {objectName()}}};
        } else if (triggerEvent == TargetSpecified && player->getMark("juzhan_usedtimes")%2 == 1) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash")) {
                foreach (ServerPlayer *p, use.to) {
                    if (!p->isNude())
                        return TriggerList{{player, {objectName()}}};
                }
            }
        }
        return {};
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == TargetConfirmed) {
            if (room->askForSkillInvoke(player, objectName(), QVariant::fromValue(use.from))) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), use.from->objectName());
                room->broadcastSkillInvoke(objectName(), player);
                return true;
            }
        } else if (triggerEvent == TargetSpecified) {
            QList<ServerPlayer *> to_choose;
            foreach (ServerPlayer *p, use.to) {
                if (!p->isNude())
                    to_choose << p;
            }
            if (to_choose.isEmpty()) return false;
            ServerPlayer *to = room->askForPlayerChosen(player, to_choose, objectName(), "juzhan-invoke", true, true);
            if (to != NULL) {
                room->broadcastSkillInvoke(objectName(), player);
                QStringList target_list = player->getTag("juzhan_target").toStringList();
                target_list.append(to->objectName());
                player->setTag("juzhan_target", target_list);
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        room->addPlayerMark(player, "juzhan_usedtimes");
        if (player->getMark("juzhan_usedtimes") % 2 == 0)
            setMolState(room, player, "heg_juzhan_state", { "SwitchYang" }, "&heg_juzhan");
        else
            setMolState(room, player, "heg_juzhan_state", { "SwitchYin" }, "&heg_juzhan");
        CardUseStruct use = data.value<CardUseStruct>();
        if (triggerEvent == TargetConfirmed) {
            player->drawCards(1, objectName());
            if (use.from->isDead()) return false;
            use.from->drawCards(1, objectName());
            if (use.from->isDead() || player->isDead() || !use.from->hasShownAllGenerals()) return false;
            QStringList generals, allchoices;
            allchoices << "head" << "deputy" << "cancel";
            if (!use.from->getActualGeneral1Name().contains("sujiang") && !use.from->isLord())
                generals << "head";
            if (use.from->getGeneral2() != NULL && !use.from->getGeneral2Name().contains("sujiang"))
                generals << "deputy";
            if (generals.isEmpty()) return false;
            generals << "cancel";
            QString choice = room->askForChoice(player, objectName(), generals.join("+"), QVariant(), allchoices.join("+"),
                                                "@juzhan-hide::" + use.from->objectName());
            if (choice == "cancel") return false;
            bool head = (choice == "head");
            use.from->hideGeneral(head);
            room->setPlayerDisableShow(use.from, head ? "h":"d", objectName());
        } else if (triggerEvent == TargetSpecified) {
            QStringList target_list = player->getTag("juzhan_target").toStringList();
            QString target_name = target_list.takeLast();
            player->setTag("juzhan_target", target_list);
            ServerPlayer *to = room->findPlayerByObjectName(target_name);
            if (to && player->canGet(to, "he")) {
                int card_id = room->askForCardChosen(player, to, "he", objectName(), false, Card::MethodGet);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName(), to->objectName(), QString(), QString());
                room->obtainCard(player, Sanguosha->getCard(card_id), reason, false);
                if (to->isAlive()) {
                    QStringList mark = molState(player, "heg_juzhan_prohibited");
                    mark << target_name;
                    room->setPlayerProperty(player, "heg_juzhan_prohibited", mark.join("+"));
                    room->setPlayerMark(to, "##juzhan-turn", 1);
                }
            }
        }
        return false;
    }
};

class HDanshou : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDanshou() : TriggerSkillV2("heg_danshou")
    {
        events << EventPhaseStart;
    }

    virtual TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        TriggerList skill_list;
        if (player == NULL || player->isDead() || player->getPhase() != Player::Start) return skill_list;
        QList<ServerPlayer *> zhurans = room->findPlayersBySkillName(objectName());
        foreach (ServerPlayer *zhuran, zhurans) {
            if (!zhuran->isAllNude() && zhuran->getMark("heg_danshou_used-round") == 0)
                skill_list.insert(zhuran, QStringList(objectName()));
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        if (ask_who->askForSkillInvoke(this, QVariant::fromValue(player))) {
            room->broadcastSkillInvoke(objectName(), ask_who);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ask_who->objectName(), player->objectName());
            room->addPlayerMark(ask_who, "heg_danshou_used-round");
            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *ask_who = ctx.owner;
        DummyCard *card = new DummyCard;
        QList<const Card *> all_cards = ask_who->getCards("hej");
        foreach (const Card *c, all_cards) {
            if (!ask_who->isJilei(c))
                card->addSubcard(c);
        }
        int x = card->subcardsLength();
        if (x > 0) {
            room->throwCard(card, ask_who);
            room->addPlayerMark(ask_who, "danshou-turn", x);
            room->addPlayerMark(ask_who, "#danshou-turn");
        }

        return false;
    }
};

class HDanshouEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HDanshouEffect() : TriggerSkillV2("#heg_danshou-effect")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->getPhase() == Player::NotActive) {
            for (ServerPlayer *p : room->getAllPlayers(true)) {
                room->setPlayerMark(p, "danshou-turn", 0);
                room->setPlayerMark(p, "#danshou-turn", 0);
            }
        }
        return true;
    }

    virtual TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        TriggerList skill_list;
        if (player && player->getPhase() > Player::Start && player->getPhase() < Player::Finish) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->getMark("#danshou-turn") > 0 && p->getHandcardNum() <= p->getMark("danshou-turn"))
                    skill_list.insert(p, QStringList(objectName()));
            }
        }
        return skill_list;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        ServerPlayer *ask_who = ctx.owner;
        int x = ask_who->getMark("#danshou-turn");
        QString choice = room->askForChoice(ask_who, "heg_danshou", "draw+exdraw", QVariant::fromValue(player), QString(),
                                            "#danshou-choose:::"+QString::number(x));
        if (choice == "draw") {
            ask_who->drawCards(x, "heg_danshou");
            if (x == 4 && ask_who->isAlive() && player->isAlive() &&
                    room->askForChoice(ask_who, "heg_danshou", "yes+no", QVariant::fromValue(player), QString(),
                                       "#danshou-damage::"+player->objectName()) == "yes")
                room->damage(DamageStruct("heg_danshou", ask_who, player));
        } else if (choice == "exdraw")
            room->addPlayerMark(ask_who, "#danshou-turn");
        return false;
    }
};

HBiaozhaoCard::HBiaozhaoCard()
{
    mute = true;
}

bool HBiaozhaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self) return false;
    HKnownBoth *known_both = new HKnownBoth(Card::NoSuit, 0);
    known_both->deleteLater();
    if (targets.isEmpty()) {
        return known_both->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, known_both);
    } else if (targets.length() == 1) {
        return !to_select->isFriendWith(targets.first());
    }
    return false;
}

bool HBiaozhaoCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void HBiaozhaoCard::onUse(Room *room, CardUseStruct &card_use) const
{
    // Shared SkillCard payment/reveal dispatch owns the exact V2 source.
    SkillCard::onUse(room, card_use);
}

void HBiaozhaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    HKnownBoth *known_both = new HKnownBoth(Card::NoSuit, 0);
    known_both->deleteLater();
    known_both->setSkillName("_heg_biaozhao");
    room->useCard(CardUseStruct(known_both, source, targets.first()));

    ServerPlayer *to = targets.last();

    if (source->isDead() || to->isDead() || source->isNude()) return;

    QList<int> cards = room->askForExchangeCards(source, "biaozhao_give", 1, 1, "@biaozhao-give::" + to->objectName());
    if (cards.isEmpty()) return;

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), to->objectName(), "heg_biaozhao", QString());
    room->moveCardsAtomic(CardsMoveStruct(cards, to, Player::PlaceHand, reason), false);

    if (source->isAlive())
        source->drawCards(1, "heg_biaozhao");

}

class HBiaozhao : public ViewAsSkillV2
{
public:
    HBiaozhao() : ViewAsSkillV2("heg_biaozhao") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HBiaozhaoCard"));
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HBiaozhaoCard *card = new HBiaozhaoCard;
        card->setShowSkill(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HBiaozhaoCard"; }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 0; }
};

class HYechou : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYechou() : TriggerSkillV2("heg_yechou")
    {
        events << Death;
        frequency = Compulsory;
    }

    virtual bool canPreshow() const
    {
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player == NULL || !player->hasSkill(objectName())) return {};
        DeathStruct death = data.value<DeathStruct>();
        if (death.who == player && death.damage && death.damage->from && death.damage->from->isAlive()) {
            return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        room->broadcastSkillInvoke(objectName(), player);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *target = death.damage->from;
        for (int i = 0; i < 3; i++) {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_heg_yechou");
            slash->deleteLater();
            if (target->isDead() || player->isProhibited(target, slash)) break;
            CardUseStruct slashUse(slash, player, target);
            if (i == 0)
                slashUse.no_respond_list << "_ALL_TARGETS";
            if (i == 1)
                target->addQinggangTag(slash);
            if (i == 2) {
                slash->setFlags("heg_yechou_damage");
            }
            room->useCard(slashUse, false);
        }
        return false;
    }
};

class HYechouEffect : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HYechouEffect() : TriggerSkillV2("#heg_yechou-effect")
    {
        events << Dying << QuitDying << ConfirmDamage;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // This is a committed generated-card bonus, independent of the dead owner.
        if (triggerEvent == ConfirmDamage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->hasFlag("heg_yechou_damage")
                && !damage.chain && !damage.transfer) {
                ++damage.damage;
                data = QVariant::fromValue(damage);
            }
        }
        if (triggerEvent == QuitDying && player->getMark("##yechou") > 0)
            room->setPlayerMark(player, "##yechou", 0);
        if (triggerEvent == Dying) {
            const DyingStruct dying = data.value<DyingStruct>();
            // This restriction belongs to the generated Slash, not the victim's grants.
            if (player && dying.who == player && dying.damage && dying.damage->card
                && dying.damage->card->getSkillName() == "heg_yechou")
                room->addPlayerMark(player, "##yechou");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class HYechouProhibit : public ProhibitSkill
{
public:
    HYechouProhibit() : ProhibitSkill("#heg_yechou-prohibit")
    {
    }

    virtual bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("Peach") && from && to && from != to && from->isFriendWith(to) && to->getMark("##yechou") > 0;
    }
};

class HXiaoqiViewAsSkill : public ViewAsSkillV2
{
public:
    HXiaoqiViewAsSkill() : ViewAsSkillV2("heg_xiaoqi") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && ((request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && request.pattern == "slash" && request.initiator->getMark("#xiaoqi") > 0);
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Card *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class HXiaoqi : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HXiaoqi() : TriggerSkillV2("heg_xiaoqi")
    {
        events << CardUsed << CardResponded << CardFinished;
        view_as_skill = new HXiaoqiViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent == CardResponded) {
            CardResponseStruct resp = data.value<CardResponseStruct>();
            const Card *cardstar = resp.m_card;
            if (cardstar->getSkillName() == objectName())
                room->removePlayerMark(player, "#xiaoqi");
        }
        if (triggerEvent == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->hasFlag("xiaoqieffect"))
                room->setPlayerMark(player, "#xiaoqi", 0);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != CardUsed) return {};
        CardUseStruct use = data.value<CardUseStruct>();
        if ((player && player->isAlive() && player->hasSkill(objectName())) && use.card != NULL && use.card->isKindOf("Slash")) {
            return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
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
        CardUseStruct use = data.value<CardUseStruct>();
        Duel *duel = new Duel(use.card->getSuit(), use.card->getNumber());
        duel->addSubcards(use.card->getSubcards());
        if (!use.card->isVirtualCard()) duel->addSubcard(use.card->getEffectiveId());
        duel->setSkillName(use.card->getSkillName());
        duel->setShowSkill(use.card->showSkill());
        duel->setFlags("xiaoqieffect");
        use.changeCard(duel);
        data = QVariant::fromValue(use);

        int x = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->hasShownGeneral1()) {
                QList<const Skill *> skills = p->getGeneral()->getVisibleSkillList();
                foreach (const Skill *skill, skills) {
                    if (skill->objectName().startsWith("mashu")) {
                        x++;
                        break;
                    }
                }
            }
            if (p->getGeneral2() != NULL && p->hasShownGeneral2()) {
                QList<const Skill *> skills = p->getGeneral2()->getVisibleSkillList();
                foreach (const Skill *skill, skills) {
                    if (skill->objectName().startsWith("mashu")) {
                        x++;
                        break;
                    }
                }
            }
        }
        room->setPlayerMark(player, "#xiaoqi", x);
        return false;
    }
};

HKanjiCard::HKanjiCard()
{
    target_fixed = true;
}

void HKanjiCard::extraCost(Room *room, const CardUseStruct &card_use) const
{
    room->showAllCards(card_use.from);
}

void HKanjiCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList suits;
    foreach (int id, source->handCards()) {
        const Card *card = Sanguosha->getCard(id);
        QString suit = card->getSuitString();
        if (suits.contains(suit))
            return;
        else
            suits << suit;
    }
    source->drawCards(2, "heg_kanji");
    if (source->isDead() || suits.length() == 4) return;
    suits.clear();
    foreach (int id, source->handCards()) {
        const Card *card = Sanguosha->getCard(id);
        QString suit = card->getSuitString();
        if (!suits.contains(suit))
            suits << suit;
    }
    if (suits.length() == 4)
        source->skip(Player::Discard);
}

class HKanji : public ViewAsSkillV2
{
public:
    HKanji() : ViewAsSkillV2("heg_kanji") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasUsed("HKanjiCard"));
    }
    bool canSelectCard(const ActiveSkillRequest &, const Card *) const override { return false; }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && request.selectedCardIds.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        HKanjiCard *card = new HKanjiCard;
        card->setShowSkill(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HKanjiCard"; }
};

class HQianzhengViewAsSkill : public ViewAsSkillV2
{
public:
    HQianzhengViewAsSkill() : ViewAsSkillV2("heg_qianzheng", 2)
    { response_pattern = "@@heg_qianzheng"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@heg_qianzheng"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !Sanguosha || !candidate || candidate->getEffectiveId() < 0
            || request.selectedCardIds.contains(candidate->getEffectiveId())) return false;
        QList<int> selected;
        for (int id : request.selectedCardIds) {
            if (id < 0 || selected.contains(id) || !Sanguosha->getCard(id)) return false;
            selected.append(id);
        }
        return request.selectedCardIds.size() < 2;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !Sanguosha || request.selectedCardIds.size() != 2) return false;
        // Replay selection order so forged requests obey the same card restrictions.
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
        DummyCard *card = new DummyCard;
        card->addSubcards(request.selectedCardIds);
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "DummyCard"; }
};

class HQianzheng : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HQianzheng() : TriggerSkillV2("heg_qianzheng")
    {
        events << TargetConfirmed;
        view_as_skill = new HQianzhengViewAsSkill;
    }

    virtual bool canPreshow() const
    {
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!(player && player->isAlive() && player->hasSkill(objectName())) || player->hasFlag("qianzhengUsed")) return {};
        CardUseStruct use = data.value<CardUseStruct>();
        if ((use.card->isKindOf("Slash") || use.card->isNDTrick()) && use.from != player)
            return TriggerList{{player, {objectName()}}};
        return {};
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        // Prompt payment must not reveal the source before V2 commits invocation.
        const bool costFlag = ctx.owner && !ctx.owner->hasFlag("Global_askForSkillCost");
        if (costFlag) ctx.owner->setFlags("Global_askForSkillCost");
        const auto clearCostFlag = qScopeGuard([&ctx, costFlag] {
            if (costFlag) ctx.owner->setFlags("-Global_askForSkillCost");
        });
        const Card *card = room->askForCard(player, "@@heg_qianzheng", "@qianzheng-cost", data, Card::MethodRecast);
        if (card) {
            room->setPlayerFlag(player, "qianzhengUsed");
            QVariantList cost_list = player->getTag("qianzheng_cost").toList();
            cost_list.append(QVariant::fromValue(card));
            player->setTag("qianzheng_cost", cost_list);

            return true;
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariant &data = *ctx.original_data;
        QVariantList cost_list = player->getTag("qianzheng_cost").toList();
        const Card *card = cost_list.takeLast().value<const Card *>();
        player->setTag("qianzheng_cost", cost_list);
        CardUseStruct use = data.value<CardUseStruct>();
        bool extra_effect = true;
        foreach (int id, card->getSubcards()) {
            if (use.card->getTypeId() == Sanguosha->getCard(id)->getTypeId()) {
                extra_effect = false;
                break;
            }
        }
        if (extra_effect) {
            QStringList qianzhengRecord = use.card->getTag("qianzhengRecord").toStringList();
            qianzhengRecord << player->objectName();
            use.card->setTag("qianzhengRecord", qianzhengRecord);
        }
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, player->objectName());
        reason.m_skillName = objectName();
        room->moveCardTo(card, player, NULL, Player::DiscardPile, reason, true);

        player->drawCards(2, "recast");
        return false;
    }
};

class HQianzhengDelay : public TriggerSkillV2
{
public:
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    HQianzhengDelay() : TriggerSkillV2("#heg_qianzheng-delay")
    {
        events << CardFinished;
        frequency = Compulsory;
    }

    virtual TriggerList triggerable(TriggerEvent , Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() != Card::TypeSkill
            && room->isAllOnPlace(use.card, Player::PlaceTable)) {
            QStringList qianzhengRecord = use.card->getTag("qianzhengRecord").toStringList();
            QList<ServerPlayer *> players = room->getAlivePlayers();
            TriggerList skill_list;
            foreach (ServerPlayer *player, players) {
                if (qianzhengRecord.contains(player->objectName()))
                    skill_list.insert(player, QStringList(objectName()));
            }
            return skill_list;
        }
        return TriggerList();
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QVariant &data = *ctx.original_data;
        player->obtainCard(data.value<CardUseStruct>().card);
        return false;
    }
};

HMOLPackage::HMOLPackage()
    : Package("heg_mol")
{
    General *duyu = new General(this, "heg_duyu", "qun", 4, true, true, true);
    duyu->addSkill(new HWuku);
    duyu->addSkill(new HMiewu);
    duyu->addSkill(new HMiewuDraw);
    related_skills.insert("heg_miewu", "#heg_miewu-draw");

    General *lifeng = new General(this, "heg_lifeng", "shu", 3);
    lifeng->addSkill(new HTunchu);
    lifeng->addSkill(new HTunchuEffect);
    related_skills.insert("heg_tunchu", "#heg_tunchu-effect");
    lifeng->addSkill(new HShuliang);

    General *lingcao = new General(this, "heg_lingcao", "wu");
    lingcao->addSkill(new HDujin);
    lingcao->addSkill(new HDujinCompulsory);
    related_skills.insert("heg_dujin", "#heg_dujin-compulsory");

    General *wangji = new General(this, "heg_wangji", "wei", 3);
    wangji->addSkill(new HQizhi);
    wangji->addSkill(new HJinqu);

    General *yanyan = new General(this, "heg_yanyan", "shu");
    yanyan->addSkill(new HJuzhan);

    General *zhuran = new General(this, "heg_zhuran", "wu");
    zhuran->addSkill(new HDanshou);
    zhuran->addSkill(new HDanshouEffect);
    related_skills.insert("heg_danshou", "#heg_danshou-effect");

    General *xugong = new General(this, "heg_xugong", "wu", 3);
    xugong->setSubordinateKingdom("qun");
    xugong->addCompanion("yanbaihu");
    xugong->addSkill(new HBiaozhao);
    xugong->addSkill(new HYechou);
    xugong->addSkill(new HYechouEffect);
    xugong->addSkill(new HYechouProhibit);
    related_skills.insert("heg_yechou", "#heg_yechou-effect");
    related_skills.insert("heg_yechou", "#heg_yechou-prohibit");

    skills << new HMOLStateRecord("#heg_mol-state") << new HJuzhanProhibit;
    addMetaObject<HBiaozhaoCard>();
}

HOverseasPackage::HOverseasPackage()
    : Package("heg_overseas")
{
    General *caozhen = new General(this, "heg_caozhen", "wei");
    caozhen->addCompanion("heg_caopi");
    caozhen->addSkill(new HSidi);
    caozhen->addSkill(new HSidiInvalidity);
    related_skills.insert("heg_sidi", "#heg_sidi-invalidity");

    General *liaohua = new General(this, "heg_liaohua", "shu");
    liaohua->addSkill(new HDangxian);
    liaohua->addCompanion("heg_guanyu");

    General *zhugejin = new General(this, "heg_zhugejin", "wu", 3);
    zhugejin->addSkill(new HHuanshi);
    zhugejin->addSkill(new HHongyuan);
    zhugejin->addSkill(new HMingzhe);
    zhugejin->addCompanion("heg_sunquan");

    General *beimihu = new General(this, "heg_beimihu", "qun", 3, false);
    beimihu->addSkill(new HGuishu);
    beimihu->addSkill(new HYuanyu);

    General *tianyu = new General(this, "heg_tianyu", "wei");
    tianyu->setDeputyMaxHpAdjustedValue();
    tianyu->addSkill(new HZhenxi);
    tianyu->addSkill(new HZhenxiProhibit);
    tianyu->addSkill(new HZhenxiTargetMod);
    related_skills.insert("heg_zhenxi", "#heg_zhenxi-prohibit");
    related_skills.insert("heg_zhenxi", "#heg_zhenxi-target");
    tianyu->addSkill(new HJiansu);

    General *xianglang = new General(this, "heg_xianglang", "shu", 3);
    xianglang->addCompanion("masu");
    xianglang->addSkill(new HKanji);
    xianglang->addSkill(new HQianzheng);
    xianglang->addSkill(new HQianzhengDelay);
    related_skills.insert("heg_qianzheng", "#heg_qianzheng-delay");

    General *maxiumatie = new General(this, "heg_maxiumatie", "qun");
    maxiumatie->addSkill("mashu");
    maxiumatie->addSkill(new HXiaoqi);

    General *xiahoushang = new General(this, "heg_xiahoushang", "wei");
    xiahoushang->addCompanion("heg_caopi");
    xiahoushang->addSkill(new HTanfeng);

    General *liyan = new General(this, "heg_liyan", "shu");
    liyan->addCompanion("heg_chendao");
    liyan->setHeadMaxHpAdjustedValue();
    liyan->addSkill(new HJinwu);
    liyan->addSkill(new HZhuke);
    liyan->addSkill(new HQuanjia);
    liyan->addSkill(new HQuanjiaCompulsory);
    related_skills.insert("heg_quanjia", "#heg_quanjia-compulsory");

    General *huaxiong = new General(this, "heg_huaxiong", "qun");
    huaxiong->addSkill(new HYaowu);
    huaxiong->addSkill(new HYaowuDeath);
    huaxiong->addSkill(new HShiyong);
    related_skills.insert("heg_yaowu", "#heg_yaowu-death");

    General *liufuren = new General(this, "heg_liufuren", "qun", 3, false);
    liufuren->addCompanion("heg_yuanshao");
    liufuren->addSkill(new HZhuidu);
    liufuren->addSkill(new HShigong);

    General *yangxiu = new General(this, "heg_yangxiu", "wei", 3);
    yangxiu->addSkill(new HDanlao);
    yangxiu->addSkill(new HJilei);

    General *chendao = new General(this, "heg_chendao", "shu");
    chendao->addCompanion("heg_zhaoyun");
    chendao->addSkill(new HWanglie);
    chendao->addSkill(new HWanglieTarget);
    related_skills.insert("heg_wanglie", "#heg_wanglie-target");

    General *zumao = new General(this, "heg_zumao", "wu");
    zumao->addCompanion("heg_sunjian");
    zumao->addSkill(new HYinbingX);
    zumao->addSkill(new HYinbingXCompulsory);
    related_skills.insert("heg_yinbingx", "#heg_yinbingx-compulsory");
    zumao->addSkill(new HJuedi);

    General *fuwan = new General(this, "heg_fuwan", "qun");
    fuwan->addSkill(new HMoukui);
    fuwan->addSkill(new HMoukuiEffect);
    related_skills.insert("heg_moukui", "#heg_moukui-effect");

    addMetaObject<HHongyuanCard>();
    addMetaObject<HJiansuCard>();
    addMetaObject<HKanjiCard>();

    skills << new HZhenxiTrick << new HMOLStateRecord("#heg_overseas-state");
}


ADD_PACKAGE(HMOL)
ADD_PACKAGE(HOverseas)
