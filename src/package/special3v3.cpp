#include "special3v3.h"
//#include "skill.h"
//#include "standard.h"
//#include "server.h"
#include "engine.h"
//#include "ai.h"
#include "maneuvering.h"
//#include "clientplayer.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
#if !defined(QSAN_ENGINE_BUILD)
#include "clientstruct.h"
#endif

HongyuanCard::HongyuanCard()
{
    setSkillName("hongyuan");
    mute = true;
}

bool HongyuanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    return targets.length() <= 2 && !targets.contains(Self);
}

bool HongyuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select != Self && targets.length() < 2;
}

void HongyuanCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->setFlags("HongyuanTarget");
}

class HongyuanViewAsSkill : public ViewAsSkillV2
{
public:
    HongyuanViewAsSkill() : ViewAsSkillV2("hongyuan") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@hongyuan";
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "HongyuanCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return target && target != request.initiator && selected.size() < 2;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() <= 2; }
};

class Hongyuan : public TriggerSkillV2
{
public:
    Hongyuan() : TriggerSkillV2("hongyuan") { events << DrawNCards; view_as_skill = new HongyuanViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && data.value<DrawStruct>().reason == "draw_phase"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (room->getMode().startsWith("06_")) {
            if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
            for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
                if (AI::GetRelation3v3(ctx.owner, other) == AI::Friend) ctx.targets << other;
            return true;
        }
        Room::BorrowedSkillScope scope(room, ctx.owner, objectName(), ctx.activationRef);
        const CardUseStruct use = room->askForUseCardStruct(ctx.owner, "@@hongyuan", "@hongyuan", -1, Card::MethodNone);
        if (!use.card) return false;
        ctx.targets = use.to;
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num -= getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Huanshi : public TriggerSkillV2
{
public:
    Huanshi() : TriggerSkillV2("huanshi") { events << AskForRetrial; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (!owner->isNude()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        const Card *card = judge ? onRetrial(ctx.owner, judge) : nullptr;
        if (!card || ctx.owner->isCardLimited(card, Card::MethodResponse)) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (judge && room->getCardOwner(id) == ctx.owner)
            room->retrial(Sanguosha->getCard(id), ctx.owner, judge, objectName());
        return false;
    }
    const Card *onRetrial(ServerPlayer *player, JudgeStruct *judge) const
    {
        const Card *card = nullptr;
        Room *room = player->getRoom();
        if (room->getMode().startsWith("06_") || room->getMode().startsWith("04_")) {
            if (AI::GetRelation3v3(player, judge->who) != AI::Friend) return nullptr;
            QStringList prompt_list;
            prompt_list << "@huanshi-card" << judge->who->objectName()
                << objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
            QString prompt = prompt_list.join(":");

            card = room->askForCard(player, "..", prompt, QVariant::fromValue(judge), Card::MethodNone, judge->who, true);
        } else if (!player->isNude()) {
            QList<int> ids, disabled_ids;
            foreach (const Card *card, player->getCards("he")) {
                if (player->isCardLimited(card, Card::MethodResponse))
                    disabled_ids << card->getEffectiveId();
                else
                    ids << card->getEffectiveId();
            }
            if (!ids.isEmpty() && room->askForSkillInvoke(player, objectName(), QVariant::fromValue(judge))) {
                if (judge->who != player && !player->isKongcheng()) {
                    LogMessage log;
                    log.type = "$ViewAllCards";
                    log.from = judge->who;
                    log.to << player;
                    log.card_str = ListI2S(player->handCards()).join("+");
                    room->sendLog(log, judge->who);
                }
                judge->who->setTag("HuanshiJudge", QVariant::fromValue(judge));
                room->fillAG(ids + disabled_ids, judge->who, disabled_ids);
                const auto clear = qScopeGuard([&] {
                    room->clearAG(judge->who);
                    judge->who->removeTag("HuanshiJudge");
                });
                int card_id = room->askForAG(judge->who, ids, false, objectName());

                if (ids.contains(card_id)) card = Sanguosha->getCard(card_id);
            }
        }
        if (card != nullptr)
            room->broadcastSkillInvoke(objectName());

        return card;
    }
};

class Mingzhe : public TriggerSkillV2
{
public:
    Mingzhe() : TriggerSkillV2("mingzhe") { events << CardsMoveOneTime << CardUsed << CardResponded; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->hasFlag("CurrentPlayer")) return {};
        int count = 0;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return {};
            const QVariantMap current = room->historyParent(room->currentHistoryEventId(), "move_cards", true);
            if (current.isEmpty()) return {};
            QVariantMap filter{{"event_id", current.value("id")}, {"from", player->objectName()}};
            QSet<int> counted;
            for (;;) {
                const QVariantMap page = room->queryHistoryMoves(filter);
                if (!page.value("complete").toBool()) return {};
                if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap().value("data").toMap();
                    const int id = fact.value("card_id", -1).toInt();
                    const int index = move.card_ids.indexOf(id);
                    if (index < 0 || (move.from_places.value(index) != Player::PlaceHand && move.from_places.value(index) != Player::PlaceEquip)) continue;
                    // Loss/gain segments share the event; count only the physical hand/equip loss once.
                    const int fromPlace = fact.value("from_place", -1).toInt();
                    if ((fromPlace != Player::PlaceHand && fromPlace != Player::PlaceEquip) || counted.contains(id)) continue;
                    const QVariantMap card = fact.value("card_before").toMap();
                    if (!card.contains("red")) return {};
                    counted.insert(id);
                    if (card.value("red").toBool()) ++count;
                }
                if (!page.value("has_more").toBool()) break;
                filter["after"] = page.value("next_after");
            }
        } else {
            const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
            if (card && card->isRed()) count = 1;
        }
        return count > 0 ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner->askForSkillInvoke(this, *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class VsGanglie : public TriggerSkillV2
{
public:
    VsGanglie() : TriggerSkillV2("vsganglie") { events << Damaged; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> targets;
        const QString mode = room->getMode();
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if ((!mode.startsWith("06_") && !mode.startsWith("04_")) || AI::GetRelation3v3(ctx.owner, other) == AI::Enemy)
                targets << other;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "vsganglie-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke("nosganglie");
        JudgeStruct judge;
        judge.pattern = ".|heart";
        judge.good = false;
        judge.reason = objectName();
        judge.who = ctx.owner;
        room->judge(judge);
        if (target->isAlive() && judge.isGood()
            && (target->getHandcardNum() < 2 || !room->askForDiscard(target, objectName(), 2, 2, true)))
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};

ZhongyiCard::ZhongyiCard()
{
    setSkillName("zhongyi");
    mute = true;
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void ZhongyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->broadcastSkillInvoke("zhongyi");
    //room->doLightbox("$ZhongyiAnimate");
    room->doSuperLightbox(source, "zhongyi");
    room->removePlayerMark(source, "@loyal");
    source->addToPile("loyal", this);
}

class Zhongyi : public ViewAsSkillV2
{
public:
    Zhongyi() : ViewAsSkillV2("zhongyi", 1) { frequency = Limited; limit_mark = "@loyal"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.selectedCardIds.isEmpty() && card->isRed() && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhongyiCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        if (ctx.invoker->getMark(limit_mark) > 0) room->removePlayerMark(ctx.invoker, limit_mark);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(ctx.invoker, objectName());
        // The card ID is the applied effect receipt; it survives removal of this source.
        QVariantMap receipts = ctx.invoker->property("zhongyi_receipts").toMap();
        for (int id : ctx.use_card->getSubcards()) receipts[QString::number(id)] = QVariantMap{{"instance", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
        room->setPlayerProperty(ctx.invoker, "zhongyi_receipts", receipts);
        ctx.invoker->addToPile("loyal", ctx.use_card);
        return ContinueEffects;
    }
};

class ZhongyiAction : public TriggerSkillV2
{
public:
    ZhongyiAction() : TriggerSkillV2("#zhongyi-action") {
        events << DamageCaused << EventPhaseStart << ActionedReset;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player) return true;
        if ((room->getMode() == "06_3v3" && event == ActionedReset)
            || (room->getMode() != "06_3v3" && event == EventPhaseStart && player->getPhase() == Player::RoundStart)) {
            room->setPlayerProperty(player, "zhongyi_receipts", QVariantMap());
            if (!player->getPile("loyal").isEmpty()) player->clearOnePrivatePile("loyal");
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused || !player) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || damage.chain || damage.transfer || !damage.by_user) return true;
        const bool team = room->getMode().startsWith("06_") || room->getMode().startsWith("04_");
        for (ServerPlayer *owner : room->getAllPlayers()) {
            if (team && AI::GetRelation3v3(player, owner) != AI::Friend) continue;
            const QVariantMap receipts = owner->property("zhongyi_receipts").toMap();
            for (int id : owner->getPile("loyal")) {
                const QVariantMap receipt = receipts.value(QString::number(id)).toMap();
                if (receipt.isEmpty()) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = owner;
                ctx.instanceID = receipt.value("instance").toInt();
                ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey("zhongyi", ctx.instanceID));
                ctx.amount = receipt.value("amount").toInt();
                ctx.extra_data = id;
                ctx.original_data = &data;
                ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->isAlive() && ctx.owner->getPile("loyal").contains(ctx.extra_data.toInt());
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->getMode().startsWith("06_") || room->getMode().startsWith("04_")
            || room->askForSkillInvoke(ctx.owner, "zhongyi", *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#ZhongyiBuff";
        log.from = ctx.owner;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        damage.damage += getEffectiveAmount(ctx);
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

JiuzhuCard::JiuzhuCard()
{
    setSkillName("jiuzhu");
    target_fixed = true;
}

void JiuzhuCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &) const
{
    ServerPlayer *who = room->getCurrentDyingPlayer();
    if (!who) return;

    room->loseHp(HpLostStruct(player, 1, "jiuzhu", player));
    room->recover(who, RecoverStruct("jiuzhu", player));
}

class Jiuzhu : public ViewAsSkillV2
{
public:
    Jiuzhu() : ViewAsSkillV2("jiuzhu", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern != "peach"
            || player->getHp() <= 1 || !player->canDiscard(player, "he")) return false;
        for (const Player *other : player->getAliveSiblings())
            if (other->objectName() == player->property("currentdying").toString())
                return !ServerInfo.GameMode.startsWith("06_") || player->getRole().at(0) == other->getRole().at(0);
        return false;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.selectedCardIds.isEmpty() && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquips().contains(card));
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "JiuzhuCard"; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ServerPlayer *dying = room->getCurrentDyingPlayer();
        if (!dying) return false;
        ctx.extra_data = dying->objectName();
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !ViewAsSkillV2::pay(room, ctx, request)) return false;
        room->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        ServerPlayer *dying = ctx.extra_data.isValid() ? room->findPlayerByObjectName(ctx.extra_data.toString()) : room->getCurrentDyingPlayer();
        if (dying)
            return skillEffect(ctx, dying);
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->getRoom()->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class Zhanshen : public TriggerSkillV2
{
public:
    Zhanshen() : TriggerSkillV2("zhanshen") { events << EventPhaseStart << EventSkillInvoking;
        global = true; frequency = Wake; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment still consumes this exact accepted source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return {};
        TriggerList result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (player->canWake(objectName()) || (player->isWounded()
                && player->getSkillInstanceStateValue(objectName(), id, "fight").toBool()))
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        LogMessage log;
        log.type = "#ZhanshenWake";
        log.from = ctx.owner;
        log.arg = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        room->doSuperLightbox(ctx.owner, objectName());
        room->setPlayerMark(ctx.owner, "zhanshen", 1);
        if (room->changeMaxHpForAwakenSkill(ctx.owner, -getEffectiveAmount(ctx), objectName())) {
            if (ctx.owner->getWeapon() && ctx.owner->canDiscard(ctx.owner, ctx.owner->getWeapon()->getEffectiveId()))
                room->throwCard(ctx.owner->getWeapon(), ctx.owner);
            room->handleAcquireDetachSkills(ctx.owner, "mashu|shenji");
        }
        return false;
    }
};

class ZhanshenDeath : public TriggerSkillV2
{
public:
    ZhanshenDeath() : TriggerSkillV2("#zhanshen") { events << Death; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || data.value<DeathStruct>().who != player) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                const SkillInstance *helper = owner->findSkillInstance(objectName(), id);
                if (helper && !owner->getSkillInstanceStateValue(helper->parent.skillName, helper->parent.instanceID, "fight").toBool())
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (room->getMode().startsWith("06_") || room->getMode().startsWith("04_"))
            return AI::GetRelation3v3(ctx.owner, player) == AI::Friend;
        return room->askForSkillInvoke(player, objectName(), "mark:" + ctx.owner->objectName());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstance *helper = ctx.owner->findSkillInstance(objectName(), ctx.instanceID);
        if (!helper) return false;
        ctx.owner->setSkillInstanceStateValue(helper->parent.skillName, helper->parent.instanceID, "fight", true);
        room->setPlayerMark(ctx.owner, "@fight", 1);
        return false;
    }
};

class ZhenweiDistance : public DistanceSkillV2
{
public:
    explicit ZhenweiDistance(bool receipts = false) : DistanceSkillV2(receipts ? "#zhenwei-receipts" : "#zhenwei"), receipts(receipts)
    {
        setHolderSelector(receipts ? CorrectSkill_System : CorrectSkill_AllHolders);
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.secondary) return CorrectSkillResult::noEffect();
        const bool team = ServerInfo.GameMode.startsWith("06_") || ServerInfo.GameMode.startsWith("04_") || ServerInfo.GameMode == "08_defense";
        if (team) {
            return !receipts && ctx.holder && ctx.holder != ctx.secondary
                && ctx.primary->getRole().at(0) != ctx.secondary->getRole().at(0)
                && ctx.holder->getRole().at(0) == ctx.secondary->getRole().at(0)
                ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
        }
        if (!receipts) return CorrectSkillResult::noEffect();
        const QVariantMap defended = ctx.secondary->property("zhenwei_receipts").toMap();
        const QVariantMap source = ctx.primary->property("zhenwei_receipts").toMap();
        int amount = 0;
        for (auto it = defended.cbegin(); it != defended.cend(); ++it) {
            const QVariantMap receipt = it.value().toMap();
            if (receipt.value("owner").toString() != ctx.primary->objectName() && !source.contains(it.key()))
                amount += receipt.value("amount").toInt();
        }
        return CorrectSkillResult::useAmount(amount);
    }
private:
    bool receipts;
};

ZhenweiCard::ZhenweiCard()
{
    setSkillName("zhenwei");
}

bool ZhenweiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int total = Self->getSiblings().length() + 1;
    return targets.length() < total / 2 - 1 && to_select != Self;
}

void ZhenweiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->setPlayerProperty(effect.to, "zhenwei_from", QVariant::fromValue(effect.from->objectName()));
    room->addPlayerMark(effect.to, "@defense");
}

class ZhenweiViewAsSkill : public ViewAsSkillV2
{
public:
    ZhenweiViewAsSkill() : ViewAsSkillV2("zhenwei") {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zhenwei";
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhenweiCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return target && target != request.initiator && targets.size() < (request.initiator->getSiblings().size() + 1) / 2 - 1;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const QString key = ctx.sourceRef.ownerObjectName + ":" + QString::number(ctx.sourceRef.key.instanceID);
        QVariantMap receipts = target->property("zhenwei_receipts").toMap();
        receipts[key] = QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"instance", ctx.sourceRef.key.instanceID},
            {"amount", getEffectiveAmount(ctx)}};
        Room *room = target->getRoom();
        room->setPlayerProperty(target, "zhenwei_receipts", receipts);
        room->setPlayerProperty(target, "zhenwei_from", ctx.sourceRef.ownerObjectName);
        room->setPlayerMark(target, "@defense", receipts.size());
        return ContinueEffects;
    }
};

class Zhenwei : public TriggerSkillV2
{
public:
    Zhenwei() : TriggerSkillV2("zhenwei") {
        events << EventPhaseChanging << Death;
        view_as_skill = new ZhenweiViewAsSkill;
        frequency = Compulsory;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        const bool clear = event == Death ? data.value<DeathStruct>().who == player
            : event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
        if (!clear) return true;
        // An applied defense expires by its owner's turn/death even after skill removal.
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            QVariantMap receipts = target->property("zhenwei_receipts").toMap();
            bool changed = false;
            for (auto it = receipts.begin(); it != receipts.end();) {
                if (it.value().toMap().value("owner").toString() == player->objectName()) {
                    it = receipts.erase(it);
                    changed = true;
                } else ++it;
            }
            if (!changed) continue;
            room->setPlayerProperty(target, "zhenwei_receipts", receipts);
            room->setPlayerMark(target, "@defense", receipts.size());
            room->setPlayerProperty(target, "zhenwei_from", receipts.isEmpty() ? QString()
                : receipts.cbegin().value().toMap().value("owner").toString());
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const QString mode = room->getMode();
        return event == EventPhaseChanging && player && player->isAlive() && player->hasSkill(objectName())
            && !mode.startsWith("06_") && !mode.startsWith("04_") && mode != "08_defense"
            && Sanguosha->getPlayerCount(mode) > 3 && data.value<PhaseChangeStruct>().to == Player::NotActive
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Room::AcceptedViewAsEffectScope scope(room, ctx.owner, objectName(), ctx);
        if (!scope.isValid()) return false;
        room->askForUseCard(ctx.owner, "@@zhenwei", "@zhenwei");
        return false;
    }
};

class VSCrossbowSkill : public TargetModSkillV2
{
public:
    VSCrossbowSkill() : TargetModSkillV2("vscrossbow") {
        frequency = Compulsory;
        setHolderSelector(CorrectSkill_System);
        m_baseAmount = 2;
    }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.primary && ctx.primary->hasWeapon("vscrossbow") && ctx.modType == TargetModSkill::Residue
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

VSCrossbow::VSCrossbow(Suit suit, int number)
    : Crossbow(suit, number)
{
    setObjectName("vscrossbow");
}

bool VSCrossbow::match(const QString &pattern) const
{
    QStringList patterns = pattern.split("+");
    if (patterns.contains("crossbow"))
        return true;
    else
        return Crossbow::match(pattern);
}

New3v3CardPackage::New3v3CardPackage()
    : Package("~New3v3Card")
{
    QList<Card *> cards;
    cards << new SupplyShortage(Card::Spade, 1)
        << new SupplyShortage(Card::Club, 12)
        << new Nullification(Card::Heart, 12);

    foreach(Card *card, cards)
        card->setParent(this);

    type = CardPack;
}

ADD_PACKAGE(New3v3Card)

New3v3_2013CardPackage::New3v3_2013CardPackage()
: Package("~New3v3_2013Card")
{
    QList<Card *> cards;
    cards << new VSCrossbow(Card::Club)
        << new VSCrossbow(Card::Diamond);
    skills << new VSCrossbowSkill;

    foreach(Card *card, cards)
        card->setParent(this);

    type = CardPack;
}

ADD_PACKAGE(New3v3_2013Card)

Special3v3Package::Special3v3Package()
: Package("~Special3v3")
{
    General *vs_nos_xiahoudun = new General(this, "vs_nos_xiahoudun", "wei");
    vs_nos_xiahoudun->addSkill(new VsGanglie);

    General *vs_nos_guanyu = new General(this, "vs_nos_guanyu", "shu");
    vs_nos_guanyu->addSkill("wusheng");
    vs_nos_guanyu->addSkill(new Zhongyi);
    vs_nos_guanyu->addSkill(new ZhongyiAction);
    related_skills.insert("zhongyi", "#zhongyi-action");

    General *vs_nos_zhaoyun = new General(this, "vs_nos_zhaoyun", "shu");
    vs_nos_zhaoyun->addSkill("longdan");
    vs_nos_zhaoyun->addSkill(new Jiuzhu);

    General *vs_nos_lvbu = new General(this, "vs_nos_lvbu", "qun");
    vs_nos_lvbu->addSkill("wushuang");
    vs_nos_lvbu->addSkill(new Zhanshen);
    vs_nos_lvbu->addSkill(new ZhanshenDeath);
    related_skills.insert("zhanshen", "#zhanshen");

    addMetaObject<ZhongyiCard>();
    addMetaObject<JiuzhuCard>();
}

ADD_PACKAGE(Special3v3)

Special3v3ExtPackage::Special3v3ExtPackage()
: Package("Special3v3Ext")
{
    General *wenpin = new General(this, "wenpin", "wei"); // WEI 019
    wenpin->addSkill(new Zhenwei);
    wenpin->addSkill(new ZhenweiDistance);
    skills << new ZhenweiDistance(true);
    related_skills.insert("zhenwei", "#zhenwei");

    General *zhugejin = new General(this, "zhugejin", "wu", 3, true); // WU 018
    zhugejin->addSkill(new Hongyuan);
    zhugejin->addSkill(new Huanshi);
    zhugejin->addSkill(new Mingzhe);

    addMetaObject<ZhenweiCard>();
    addMetaObject<HongyuanCard>();
}

ADD_PACKAGE(Special3v3Ext)
